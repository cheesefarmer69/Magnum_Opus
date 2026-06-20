#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

const int WIFI_KANAAL = 1;

// ---- MULTI-MASTER CONFIG (via PlatformIO build_flags, zie platformio.ini) ----
// Elke master bedient een paalbereik: master1 = 1..7, master2 = 8..16, master3 = 17..24.
// Defaults (master1) zodat het bestand los compileert in de editor.
#ifndef PAAL_MIN
#define PAAL_MIN 9
#endif
#ifndef PAAL_MAX
#define PAAL_MAX 16
#endif
#ifndef MASTER_NR
#define MASTER_NR 2
#endif
// Aantal slaves dat deze master bedient (afgeleid uit het bereik).
#define AANTAL_SLAVES (PAAL_MAX - PAAL_MIN + 1)
// Globale paal-ID -> 0-based index in slaveAdressen[]/cmdPerSlave[].
static inline int paalNaarIndex(int paal) { return paal - PAAL_MIN; }

// ---- INGEBOUWDE LED ----
// ESP32 WROOM-32 onboard LED op GPIO2 (active-HIGH). Pulst kort bij elke
// ontvangen slave-batch als visuele ontvangst-indicator (niet-blokkerend).
#define BUILTIN_LED_PIN 2
const unsigned long RECV_KNIPPER_MS = 30;       // duur puls bij batch-ontvangst
unsigned long ingebouwdeLedTot = 0;             // millis() tot wanneer LED aan

// ---- PROTOCOL v2 ----
#define MSG_BATCH        0x01   // slave -> master
#define MSG_COMMANDO     0x02   // master -> slave
#define MSG_CMD_ACK      0x03   // slave -> master, NA uitvoering
#define MSG_HEARTBEAT    0x04   // slave -> master, periodiek
#define MSG_FOUT         0x05   // slave -> master
#define MSG_KNOP         0x06   // slave -> master, bij druk
#define MSG_BUZZER_TOON  0x07   // master -> slave, buzzer-tuning (continue toon)
#define MSG_KLOKSLAG     0x08   // master -> slave, Klokslag-LED (teamkleur + helderheid + modus)

// JSON-actie 12 = buzzer-toon (tuning): geen commando_message_v2, maar een directe
// MSG_BUZZER_TOON met de frequentie uit het extra JSON-veld "toon" (zie docs/protocol.md).
#define ACTIE_BUZZER_TOON  12
// JSON-actie 16 = Klokslag-LED: directe MSG_KLOKSLAG met r/g/b/helderheid/modus uit het JSON
// (geen FIFO/ACK, fire-and-forget zoals buzzer-tuning). Zie docs/protocol.md.
#define ACTIE_KLOKSLAG     16

#define MAX_SPELERS 30

// ---- DATASTRUCTS (v2: dispatch op msg_type, binaire MAC's) ----
typedef struct __attribute__((packed)) batch_message_v2 {
  uint8_t  msg_type;        // = MSG_BATCH
  uint8_t  paal_id;         // 1..24
  uint8_t  aantal;          // aantal spelers in deze batch (0..30)
  uint16_t batt_mv;         // batterijspanning in mV (0 = niet gemeten)
  struct {
    uint8_t mac[6];         // binair MAC-adres (big-endian)
    int8_t  rssi;           // dBm
  } spelers[MAX_SPELERS];
} batch_message_v2;
static_assert(sizeof(batch_message_v2) <= 250, "batch_message_v2 te groot voor ESP-NOW");
batch_message_v2 inkomendeData;

typedef struct __attribute__((packed)) commando_message_v2 {
  uint8_t  msg_type;        // = MSG_COMMANDO
  uint8_t  paal_id;
  uint8_t  actie_id;
  uint16_t cmd_seq;
} commando_message_v2;

typedef struct __attribute__((packed)) cmd_ack_message {
  uint8_t  msg_type;        // = MSG_CMD_ACK
  uint8_t  paal_id;
  uint16_t cmd_seq;
  uint8_t  status;          // 0 = uitgevoerd, 1 = geweigerd/onbekende actie
} cmd_ack_message;

typedef struct __attribute__((packed)) heartbeat_message {
  uint8_t  msg_type;        // = MSG_HEARTBEAT
  uint8_t  paal_id;
  uint16_t batt_mv;
  uint32_t uptime_s;
  uint8_t  fw_versie;
} heartbeat_message;

typedef struct __attribute__((packed)) fout_message {
  uint8_t  msg_type;        // = MSG_FOUT
  uint8_t  paal_id;
  uint8_t  ernst;
  uint8_t  foutcode;
  uint32_t detail;
} fout_message;

typedef struct __attribute__((packed)) knop_message {
  uint8_t  msg_type;        // = MSG_KNOP
  uint8_t  paal_id;
} knop_message;

typedef struct __attribute__((packed)) buzzer_toon_message {
  uint8_t  msg_type;        // = MSG_BUZZER_TOON
  uint8_t  paal_id;
  uint16_t freq_hz;         // 0 = stop (noTone), anders continue toon
} buzzer_toon_message;

typedef struct __attribute__((packed)) klokslag_message {
  uint8_t  msg_type;        // = MSG_KLOKSLAG
  uint8_t  paal_id;         // doel-slave (1..24)
  uint8_t  r, g, b;         // teamkleur (controller/eigenaar)
  uint8_t  helderheid;      // 0..255 (engine schaalt al met voortgang P/H)
  uint8_t  modus;           // 0=owned/solid, 1=capturing/flikker, 2=frozen, 3=rust-ademend
} klokslag_message;

// ---- SLAVES REGISTREREN ----
// De slave-MAC's per master staan in include/slave_macs.h (geselecteerd op MASTER_NR).
// Index 0 = paal PAAL_MIN. Definieert: uint8_t slaveAdressen[AANTAL_SLAVES][6].
#include "slave_macs.h"

// ---- SERIAL-LOG-QUEUE (één schrijver) ----
// Serial-regels komen uit twee taken: OnDataRecv() draait op de WiFi-task, de
// queue-/serieel-verwerking op de loop-task. Serial.print is niet atomair over
// taken, dus konden regels door elkaar geweven raken (bridge.py dropt zo'n
// half-gemengde regel stil). Oplossing: elke producent bouwt zijn regel volledig
// op via logRegel() en zet die in een FreeRTOS-queue; ALLEEN loop() schrijft naar
// Serial (verwerkLogQueue). Raakt de queue vol onder pieklast, dan tellen we de
// verloren regels in logDrops en melden dat als {"status":"log_drop",...}.
#define LOGREGEL_MAX      192
#define LOG_QUEUE_DIEPTE  48
static QueueHandle_t     logQueue = nullptr;
static volatile uint32_t logDrops = 0;

// Stelt één serial-regel samen en zet hem in de log-queue (niet-blokkerend).
// Vóór queue-creatie (vroege setup-fase) valt hij terug op directe Serial-output.
void logRegel(const char *fmt, ...) {
  char buf[LOGREGEL_MAX];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (logQueue == nullptr) { Serial.print(buf); return; }
  if (xQueueSend(logQueue, buf, 0) != pdTRUE) logDrops++;
}

// Drijft de log-queue af: enige plek waar naar Serial geschreven wordt. Elke
// regel gaat in één stuk naar de lijn → geen interleaving.
void verwerkLogQueue() {
  if (logQueue == nullptr) return;
  char buf[LOGREGEL_MAX];
  while (xQueueReceive(logQueue, buf, 0) == pdTRUE) {
    Serial.print(buf);
  }
}

// ---- COMMANDO-SLOTS MET RETRIES (één pending-slot per slave) ----
// In plaats van één gedeelde FIFO houdt elke slave zijn eigen pending-slot bij.
// De slots worden elke loop-tick PARALLEL verwerkt: een onbereikbare paal
// doorloopt zijn eigen retry-cyclus zonder andere palen te blokkeren. Een nieuw
// commando voor een paal die nog een pending commando heeft, overschrijft het
// (laatste-wint — matcht de Node-RED-eindtoestandssemantiek). 24 palen = 24
// slots: een veld-breed commando wordt nooit geweigerd.
// Per-slave FIFO i.p.v. één slot: twee snel opeenvolgende, verschillende commando's naar
// dezelfde paal (bv. buzzer-piep + portaal) worden NIET meer samengevoegd (geen laatste-wint),
// maar in volgorde afgeleverd. Het HEAD-item is in-flight; afronding op de APPLICATIE-ACK
// (MSG_CMD_ACK ná uitvoering), niet op de MAC-laag-ACK. Omdat de slave een commando pas aan
// het einde van zijn ~1 s scan-cyclus verwerkt, is de ACK-round-trip ~1-1,3 s; de retry-timeout
// staat daarom op ~1500 ms (niet de 250 ms MAC-interval).
#define CMD_FIFO_DIEPTE 4
struct SlaveCmd {
  uint8_t  actie_id;
  uint16_t cmd_seq;
  uint8_t  pogingen;
  uint32_t laatstVerstuurd;
};
struct SlaveCmdQueue {
  SlaveCmd items[CMD_FIFO_DIEPTE];
  uint8_t  head;
  uint8_t  count;
};
SlaveCmdQueue cmdPerSlave[AANTAL_SLAVES] = {};   // index = paal - 1, zero-init

static const uint8_t  MAX_POGINGEN     = 4;
static const uint32_t APP_ACK_TIMEOUT  = 1500;  // ms wachten op MSG_CMD_ACK voor resend
static uint16_t       volgendeCmdSeq   = 1;     // monotone teller (0 = "geen")

// ---- HULPFUNCTIES ----
// True als deze MAC-rij alleen uit nullen bestaat (placeholder-slot).
static bool isPlaceholderMac(const uint8_t *mac) {
  for (int j = 0; j < 6; j++) {
    if (mac[j] != 0x00) return false;
  }
  return true;
}

// Zoek de afzender-MAC in slaveAdressen[]. Geeft 0-based index of -1 bij geen
// match. Placeholder-rijen (all-zero) worden overgeslagen zodat een lege
// slot nooit per ongeluk matcht.
static int vindSlaveIndex(const uint8_t *mac) {
  for (int i = 0; i < AANTAL_SLAVES; i++) {
    if (isPlaceholderMac(slaveAdressen[i])) continue;
    if (memcmp(mac, slaveAdressen[i], 6) == 0) return i;
  }
  return -1;
}

// Zet een commando achteraan de per-slave FIFO van zijn paal (in volgorde).
// Een placeholder-paal (all-zero MAC) wordt direct geweigerd, zonder retry-cyclus.
static bool enqueueCommando(uint8_t paal, uint8_t actie) {
  int i = paalNaarIndex(paal);
  if (i < 0 || i >= AANTAL_SLAVES) {
    logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    return false;
  }
  if (isPlaceholderMac(slaveAdressen[i])) {
    logRegel("{\"status\":\"geen_slave\",\"paal\":%d}\n", paal);
    return false;
  }
  SlaveCmdQueue &q = cmdPerSlave[i];
  if (q.count == CMD_FIFO_DIEPTE) {   // FIFO vol -> oudste droppen
    logRegel("{\"status\":\"fifo_vol\",\"paal\":%d,\"gedropt_seq\":%u}\n",
             paal, q.items[q.head].cmd_seq);
    q.head = (q.head + 1) % CMD_FIFO_DIEPTE;
    q.count--;
  }
  uint8_t tail = (q.head + q.count) % CMD_FIFO_DIEPTE;
  q.items[tail].actie_id        = actie;
  q.items[tail].cmd_seq         = volgendeCmdSeq++;
  if (volgendeCmdSeq == 0) volgendeCmdSeq = 1;   // 0 overslaan (sentinel)
  q.items[tail].pogingen        = 0;
  q.items[tail].laatstVerstuurd = 0;
  q.count++;
  return true;
}

// Drijft per slave het HEAD-item van zijn FIFO af: verstuurt het, retried als er binnen
// APP_ACK_TIMEOUT geen MSG_CMD_ACK kwam, geeft op na MAX_POGINGEN (pop + volgende item).
// Retries hergebruiken hetzelfde cmd_seq (idempotent). Parallel per slave. Elke loop()-tick.
void verwerkQueue() {
  uint32_t nu = millis();
  for (int i = 0; i < AANTAL_SLAVES; i++) {
    SlaveCmdQueue &q = cmdPerSlave[i];
    if (q.count == 0) continue;
    SlaveCmd &h = q.items[q.head];   // in-flight = head
    if (h.pogingen > 0 && (nu - h.laatstVerstuurd) < APP_ACK_TIMEOUT) continue;

    if (h.pogingen >= MAX_POGINGEN) {
      logRegel("{\"status\":\"opgegeven\",\"paal\":%d,\"actie\":%d,\"seq\":%u,\"pogingen\":%d}\n",
               PAAL_MIN + i, h.actie_id, h.cmd_seq, h.pogingen);
      q.head = (q.head + 1) % CMD_FIFO_DIEPTE;
      q.count--;
      continue;
    }

    h.pogingen++;
    h.laatstVerstuurd = nu;
    // Het commando draagt de GLOBALE paal_id (PAAL_MIN + i) zodat de slave op zijn PAAL_ID matcht.
    commando_message_v2 cmd = { MSG_COMMANDO, (uint8_t)(PAAL_MIN + i), h.actie_id, h.cmd_seq };
    esp_err_t r = esp_now_send(slaveAdressen[i], (uint8_t *)&cmd, sizeof(cmd));
    if (r != ESP_OK) {
      logRegel("{\"status\":\"send_err\",\"paal\":%d,\"poging\":%d}\n", PAAL_MIN + i, h.pogingen);
      h.laatstVerstuurd = nu - (APP_ACK_TIMEOUT - 150);   // snel opnieuw proberen
    }
  }
}

// ---- CALLBACKS ----
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  // Sender-MAC gate: ESP-NOW levert pakketten van ÉLKE afzender aan deze callback.
  // esp_now_add_peer() filtert alleen voor zenden, niet ontvangst. Drop pakketten van
  // slaves die niet in slaveAdressen[] staan (segmentatie tussen 3 masters).
  int paalIndex = vindSlaveIndex(mac);
  if (paalIndex < 0) {
    logRegel("[GATE] Genegeerd: %02X:%02X:%02X:%02X:%02X:%02X (niet in slaveAdressen[])\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return;
  }
  if (len < 1) return;
  uint8_t type = incomingData[0];

  switch (type) {
    case MSG_BATCH: {
      // Variabele lengte: de slave stuurt enkel het gebruikte deel (header + aantal*7).
      // Valideer in twee stappen: eerst de header, dan de lengte tegen het aantal.
      const int HEADER = (int)offsetof(batch_message_v2, spelers);   // 5 bytes
      if (len < HEADER) {
        logRegel("[RECV] Batch te kort: %d < %d (header)\n", len, HEADER);
        return;
      }
      uint8_t n = incomingData[2];   // aantal (byte 2 van de header)
      if (n > MAX_SPELERS) {
        logRegel("[RECV] Batch ongeldig: aantal %d > %d\n", n, MAX_SPELERS);
        return;
      }
      int verwacht = HEADER + (int)n * (int)sizeof(inkomendeData.spelers[0]);
      if (len < verwacht) {
        logRegel("[RECV] Batch te kort: %d < %d (aantal %d)\n", len, verwacht, n);
        return;
      }
      // Visuele ontvangst-indicator: alleen op een batch (HW4).
      ingebouwdeLedTot = millis() + RECV_KNIPPER_MS;
      memcpy(&inkomendeData, incomingData, (size_t)verwacht);   // accepteert ook volle 215-B frames
      logRegel("[RECV] Paal %d, %d spelers, batt %u mV\n",
        inkomendeData.paal_id, n, inkomendeData.batt_mv);
      for (int i = 0; i < n; i++) {
        const uint8_t *m = inkomendeData.spelers[i].mac;
        logRegel("{\"paal\":%d,\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"rssi\":%d}\n",
          inkomendeData.paal_id, m[0], m[1], m[2], m[3], m[4], m[5],
          inkomendeData.spelers[i].rssi);
      }
      // Batterij-regel per batch (mV -> V), 0 = niet gemeten -> overslaan.
      if (inkomendeData.batt_mv > 0) {
        logRegel("{\"paal\":%d,\"batt\":%.2f}\n",
          inkomendeData.paal_id, inkomendeData.batt_mv / 1000.0);
      }
      break;
    }
    case MSG_CMD_ACK: {
      if (len < (int)sizeof(cmd_ack_message)) return;
      cmd_ack_message a;
      memcpy(&a, incomingData, sizeof(a));
      SlaveCmdQueue &q = cmdPerSlave[paalIndex];
      // Alleen het HEAD-item is in-flight; match op cmd_seq -> pop.
      if (q.count > 0 && q.items[q.head].cmd_seq == a.cmd_seq) {
        q.head = (q.head + 1) % CMD_FIFO_DIEPTE;
        q.count--;
        logRegel("{\"status\":\"%s\",\"paal\":%d,\"seq\":%u}\n",
                 a.status == 0 ? "uitgevoerd" : "geweigerd", a.paal_id, a.cmd_seq);
      } else {
        logRegel("[ACK] Paal %d seq %u zonder matchend head (stale)\n", a.paal_id, a.cmd_seq);
      }
      break;
    }
    case MSG_HEARTBEAT: {
      if (len < (int)sizeof(heartbeat_message)) return;
      heartbeat_message h;
      memcpy(&h, incomingData, sizeof(h));
      logRegel("{\"paal\":%d,\"hb\":1,\"batt\":%.2f,\"uptime\":%u,\"fw\":%d}\n",
        h.paal_id, h.batt_mv / 1000.0, h.uptime_s, h.fw_versie);
      break;
    }
    case MSG_FOUT: {
      if (len < (int)sizeof(fout_message)) return;
      fout_message f;
      memcpy(&f, incomingData, sizeof(f));
      logRegel("{\"paal\":%d,\"fout\":%d,\"ernst\":%d,\"detail\":%u}\n",
        f.paal_id, f.foutcode, f.ernst, f.detail);
      break;
    }
    case MSG_KNOP: {
      if (len < (int)sizeof(knop_message)) return;
      knop_message k;
      memcpy(&k, incomingData, sizeof(k));
      logRegel("{\"paal\":%d,\"knop\":1}\n", k.paal_id);
      break;
    }
    default:
      logRegel("[RECV] Onbekend msg_type 0x%02X van paal %d, genegeerd\n", type, PAAL_MIN + paalIndex);
      break;
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // In v2 rondt de MAC-laag-ACK het commando NIET af (dat doet MSG_CMD_ACK ná
  // uitvoering). Bij een radio-FAIL loggen we en zetten we de slot-timer terug zodat
  // verwerkQueue meteen opnieuw probeert (sneller herstel).
  if (status == ESP_NOW_SEND_SUCCESS) return;
  int i = vindSlaveIndex(mac_addr);
  if (i < 0) return;
  SlaveCmdQueue &q = cmdPerSlave[i];
  if (q.count == 0) return;
  logRegel("{\"status\":\"send_err\",\"paal\":%d,\"poging\":%d}\n", PAAL_MIN + i, q.items[q.head].pogingen);
  q.items[q.head].laatstVerstuurd = millis() - (APP_ACK_TIMEOUT - 150);
}

// Buzzer-tuning: stuur direct een MSG_BUZZER_TOON (geen FIFO/ACK, fire-and-forget).
// Een continue toon op de slave waarmee per bordje de luidste resonantie te zoeken is.
static void stuurBuzzerToon(int paal, uint16_t freq) {
  int i = paalNaarIndex(paal);
  if (i < 0 || i >= AANTAL_SLAVES) {
    logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    return;
  }
  if (isPlaceholderMac(slaveAdressen[i])) {
    logRegel("{\"status\":\"geen_slave\",\"paal\":%d}\n", paal);
    return;
  }
  buzzer_toon_message bt = { MSG_BUZZER_TOON, (uint8_t)paal, freq };
  esp_err_t r = esp_now_send(slaveAdressen[i], (uint8_t *)&bt, sizeof(bt));
  logRegel("{\"status\":\"%s\",\"paal\":%d,\"toon\":%u}\n",
           (r == ESP_OK) ? "toon" : "send_err", paal, freq);
}

// Klokslag-LED: stuur direct een MSG_KLOKSLAG (geen FIFO/ACK, fire-and-forget zoals buzzer-tuning).
// De slave rendert continu (solid/flikker/ademend) tot het volgende bericht binnenkomt.
static void stuurKlokslag(int paal, uint8_t r, uint8_t g, uint8_t b, uint8_t helderheid, uint8_t modus) {
  int i = paalNaarIndex(paal);
  if (i < 0 || i >= AANTAL_SLAVES) {
    logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    return;
  }
  if (isPlaceholderMac(slaveAdressen[i])) {
    logRegel("{\"status\":\"geen_slave\",\"paal\":%d}\n", paal);
    return;
  }
  klokslag_message km = { MSG_KLOKSLAG, (uint8_t)paal, r, g, b, helderheid, modus };
  esp_err_t res = esp_now_send(slaveAdressen[i], (uint8_t *)&km, sizeof(km));
  logRegel("{\"status\":\"%s\",\"paal\":%d,\"actie\":16}\n",
           (res == ESP_OK) ? "klokslag" : "send_err", paal);
}

// ---- SERIEEL COMMANDO VAN RASPBERRY PI ----
// Parse één complete regel en zet het commando in het juiste paal-slot.
void verwerkRegel(const char *regel) {
  String lijn(regel);
  lijn.trim();

  int paalIndex  = lijn.indexOf("\"paal\":");
  int actieIndex = lijn.indexOf("\"actie\":");
  if (paalIndex == -1 || actieIndex == -1) return;

  int     paal  = lijn.substring(paalIndex + 7).toInt();
  uint8_t actie = lijn.substring(actieIndex + 8).toInt();

  // Buzzer-tuning (actie 12): niet via de FIFO, maar direct als MSG_BUZZER_TOON met de
  // frequentie uit het extra veld "toon" (Hz; 0 = stop). Zo blijft de bridge ongewijzigd.
  if (actie == ACTIE_BUZZER_TOON) {
    int toonIndex = lijn.indexOf("\"toon\":");
    uint16_t toon = (toonIndex == -1) ? 0 : (uint16_t)lijn.substring(toonIndex + 7).toInt();
    if (paal >= PAAL_MIN && paal <= PAAL_MAX) {
      stuurBuzzerToon(paal, toon);
    } else {
      logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    }
    return;
  }

  // Klokslag-LED (actie 16): niet via de FIFO, maar direct als MSG_KLOKSLAG met r/g/b/helderheid/modus.
  if (actie == ACTIE_KLOKSLAG) {
    int rIdx = lijn.indexOf("\"r\":"), gIdx = lijn.indexOf("\"g\":"), bIdx = lijn.indexOf("\"b\":");
    int hIdx = lijn.indexOf("\"helderheid\":"), mIdx = lijn.indexOf("\"modus\":");
    uint8_t r = (rIdx == -1) ? 0 : (uint8_t)lijn.substring(rIdx + 4).toInt();
    uint8_t g = (gIdx == -1) ? 0 : (uint8_t)lijn.substring(gIdx + 4).toInt();
    uint8_t b = (bIdx == -1) ? 0 : (uint8_t)lijn.substring(bIdx + 4).toInt();
    uint8_t helderheid = (hIdx == -1) ? 255 : (uint8_t)lijn.substring(hIdx + 13).toInt();
    uint8_t modus = (mIdx == -1) ? 0 : (uint8_t)lijn.substring(mIdx + 8).toInt();
    if (paal >= PAAL_MIN && paal <= PAAL_MAX) {
      stuurKlokslag(paal, r, g, b, helderheid, modus);
    } else {
      logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    }
    return;
  }

  if (paal >= PAAL_MIN && paal <= PAAL_MAX) {
    if (enqueueCommando(paal, actie)) {
      logRegel("{\"status\":\"queued\",\"paal\":%d,\"actie\":%d}\n", paal, actie);
    }
  } else {
    // Buiten het bereik van deze master — hoort nooit te gebeuren (de bridge routeert
    // op paal_id). Zo'n regel wijst dus op een routeringsfout.
    logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
  }
}

// Niet-blokkerend serieel lezen: byte per byte tot '\n'. Geen readStringUntil
// (die blokkeert tot de default Stream-timeout van 1000 ms) → een trage of
// partiële regel van de Pi bevriest de loop niet. Overflow = regel weggooien.
void verwerkSerieel() {
  static char   rxBuf[128];
  static size_t rxLen = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      rxBuf[rxLen] = '\0';
      verwerkRegel(rxBuf);
      rxLen = 0;
    } else if (rxLen < sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = c;
    } else {
      rxLen = 0;   // overflow: gooi de regel weg
    }
  }
}

// ---- SETUP ----
void setup() {
  Serial.begin(115200);

  // Log-queue vroeg aanmaken zodat alle output via één schrijver loopt.
  logQueue = xQueueCreate(LOG_QUEUE_DIEPTE, LOGREGEL_MAX);

  delay(2000);

  // Ingebouwde LED (active-HIGH): uit bij start
  pinMode(BUILTIN_LED_PIN, OUTPUT);
  digitalWrite(BUILTIN_LED_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  logRegel("[SETUP] Master %d, palen %d-%d (%d slaves)\n", MASTER_NR, PAAL_MIN, PAAL_MAX, AANTAL_SLAVES);
  logRegel("Master MAC: %s\n", WiFi.macAddress().c_str());
  logRegel("Master kanaal: %d\n", WiFi.channel());

  if (esp_now_init() != ESP_OK) {
    logRegel("ESP-NOW init MISLUKT\n");
    return;
  }

  // Kanaal vastzetten NA init
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WIFI_KANAAL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  esp_wifi_set_ps(WIFI_PS_NONE);

  logRegel("Kanaal na fix: %d\n", WiFi.channel());

  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  // Alle slaves van deze master toevoegen als peer (index 0 = paal PAAL_MIN)
  for (int i = 0; i < AANTAL_SLAVES; i++) {
    // Skip slaves met placeholder MAC (allemaal nullen)
    if (isPlaceholderMac(slaveAdressen[i])) {
      logRegel("[PEER] Paal %d overgeslagen (geen MAC ingevuld)\n", PAAL_MIN + i);
      continue;
    }

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, slaveAdressen[i], 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      logRegel("[PEER] Paal %d toegevoegd: %02X:%02X:%02X:%02X:%02X:%02X\n",
        PAAL_MIN + i, slaveAdressen[i][0], slaveAdressen[i][1], slaveAdressen[i][2],
        slaveAdressen[i][3], slaveAdressen[i][4], slaveAdressen[i][5]);
    } else {
      logRegel("[PEER] Paal %d toevoegen MISLUKT!\n", PAAL_MIN + i);
    }
  }

  logRegel("=== Master klaar ===\n");
}

// ---- LOOP ----
void loop() {
  verwerkSerieel();
  verwerkQueue();

  // Verlies in de log-queue (pieklast) hooguit ~1×/s melden.
  static uint32_t laatsteDropCheck = 0;
  static uint32_t gemeldeDrops = 0;
  uint32_t nu = millis();
  if (nu - laatsteDropCheck > 1000) {
    laatsteDropCheck = nu;
    uint32_t d = logDrops;
    if (d != gemeldeDrops) {
      gemeldeDrops = d;
      logRegel("{\"status\":\"log_drop\",\"aantal\":%u}\n", d);
    }
  }

  verwerkLogQueue();   // enige Serial-schrijver

  // Ingebouwde LED (active-HIGH): aan zolang de ontvangst-puls loopt.
  digitalWrite(BUILTIN_LED_PIN, (millis() < ingebouwdeLedTot) ? HIGH : LOW);
}
