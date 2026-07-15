#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

const int WIFI_KANAAL = 1;

// ---- MULTI-MASTER CONFIG (via PlatformIO build_flags, zie platformio.ini) ----
// Elke master bedient een paalbereik: master1 = 1..8, master2 = 9..16, master3 = 17..24.
// Defaults (master1) zodat het bestand los compileert in de editor (de echte master1/2/3-envs
// zetten PAAL_MIN/PAAL_MAX/MASTER_NR via build_flags in platformio.ini).
#ifndef PAAL_MIN
#define PAAL_MIN 1
#endif
#ifndef PAAL_MAX
#define PAAL_MAX 8
#endif
#ifndef MASTER_NR
#define MASTER_NR 1
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

// ---- IDENTITEITS-AANKONDIGING ----
// De master stuurt periodiek wie hij is (master-nr + paalbereik) op serial, zodat de bridge meteen weet
// welke poort welke master is en commando/masterN kan routeren -- ZONDER te moeten wachten op een
// rapporterende slave (anders blijft de route ongeleerd en worden alle commando's genegeerd).
const unsigned long ANNOUNCE_INTERVAL_MS = 3000;

// ---- PROTOCOL v2 ----
#define MSG_BATCH        0x01   // slave -> master
#define MSG_COMMANDO     0x02   // master -> slave
#define MSG_CMD_ACK      0x03   // slave -> master, NA uitvoering
#define MSG_HEARTBEAT    0x04   // slave -> master, periodiek
#define MSG_FOUT         0x05   // slave -> master
#define MSG_KNOP         0x06   // slave -> master, bij druk
#define MSG_BUZZER_TOON  0x07   // master -> slave, buzzer-tuning (continue toon)
#define MSG_KLOKSLAG     0x08   // master -> slave, Klokslag-LED (teamkleur + helderheid + modus)
#define MSG_SCAN_CONFIG  0x09   // master -> slave, BLE-scan-vensterduur (ms) instellen
#define MSG_LED_CONFIG   0x0A   // master -> slave, LED-helderheid (0..255) instellen
#define MSG_BOM          0x0B   // master -> slave, bom-animatie (minigame "Bommen vermijden")

// JSON-actie 12 = buzzer-toon (tuning): geen commando_message_v2, maar een directe
// MSG_BUZZER_TOON met de frequentie uit het extra JSON-veld "toon" (zie docs/protocol.md).
#define ACTIE_BUZZER_TOON  12
// JSON-actie 16 = Klokslag-LED: directe MSG_KLOKSLAG met r/g/b/helderheid/modus uit het JSON
// (geen FIFO/ACK, fire-and-forget zoals buzzer-tuning). Zie docs/protocol.md.
#define ACTIE_KLOKSLAG     16
// JSON-actie 20 = BLE-scan-config: directe MSG_SCAN_CONFIG met de vensterduur uit het extra
// JSON-veld "scan_ms" (ms). Geen FIFO/ACK, fire-and-forget zoals buzzer/klokslag. Zie docs/protocol.md.
#define ACTIE_SCAN_CONFIG  20
#define SCAN_MS_DEFAULT  1000   // fallback wanneer "scan_ms" ontbreekt in de JSON
// JSON-actie 21 = LED-helderheid: directe MSG_LED_CONFIG met de helderheid (0..255) uit het extra
// JSON-veld "helderheid". Geen FIFO/ACK, fire-and-forget. Zie docs/protocol.md.
#define ACTIE_LED_CONFIG   21
#define LED_HELDER_DEFAULT 150  // fallback wanneer "helderheid" ontbreekt in de JSON
// JSON-actie 25 = bom-animatie (minigame): directe MSG_BOM met laad_ms/hold_ms/pink_ms/pink_hz uit
// de extra JSON-velden. Geen FIFO/ACK, fire-and-forget zoals klokslag. Zie docs/protocol.md.
#define ACTIE_BOM          25

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
  uint16_t teller;          // cumulatieve druk-teller (kogelvrij: laatste waarde telt)
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

typedef struct __attribute__((packed)) scan_config_message {
  uint8_t  msg_type;        // = MSG_SCAN_CONFIG
  uint8_t  paal_id;         // doel-slave (1..24)
  uint16_t scan_ms;         // gewenste BLE-scan-vensterduur in ms (slave clamp't 300..2000)
} scan_config_message;

typedef struct __attribute__((packed)) led_config_message {
  uint8_t msg_type;         // = MSG_LED_CONFIG
  uint8_t paal_id;          // doel-slave (1..24)
  uint8_t helderheid;       // gewenste FastLED-helderheid 0..255 (slave clamp't 5..255)
} led_config_message;

typedef struct __attribute__((packed)) bom_message {
  uint8_t  msg_type;        // = MSG_BOM
  uint8_t  paal_id;         // doel-slave (1..24)
  uint16_t laad_ms;         // oplaad-ramp 0 -> max
  uint16_t hold_ms;         // vasthouden op max vóór het knipperen
  uint16_t pink_ms;         // knipperduur; daarna dooft de LED (= ontploft)
  uint16_t pink_hz;         // knipperfrequentie
  int32_t  wacht_ms;        // v2: ms tot vuren op de slave (SIGNED; per zendpoging VERS herberekend
                            //     als uitvoerOp - nu, mag negatief = "al zoveel ms geleden"); 0 = direct
  uint8_t  seq;             // v2: per-slave dedupe-teller (slaat 0 over; 0 = v1/geen dedupe)
} bom_message;              // 15 B; oude slaves (10 B-check) accepteren dit frame ook

// ---- SLAVES REGISTREREN ----
// De MAC->PAAL_ID-tabel staat gedeeld in firmware/shared/paal_macs.h (ÉÉN bron van waarheid
// voor slave én master). Deze master vult daaruit slaveAdressen[] voor zijn eigen paalbereik
// (PAAL_MIN..PAAL_MAX); index 0 = paal PAAL_MIN. Palen die (nog) niet in de tabel staan blijven
// placeholder (all-zero) en worden overgeslagen bij peer-registratie en de ontvangst-gate.
#include "paal_macs.h"
uint8_t slaveAdressen[AANTAL_SLAVES][6] = {0};

// Vul slaveAdressen[] uit de gedeelde tabel voor het bereik van deze master (in setup()).
static void vulSlaveAdressen() {
  for (int i = 0; i < PAAL_MACS_N; i++) {
    int paal = PAAL_MACS[i].paal;
    if (paal >= PAAL_MIN && paal <= PAAL_MAX) {
      memcpy(slaveAdressen[paal - PAAL_MIN], PAAL_MACS[i].mac, 6);
    }
  }
}

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
// BUDGET-BEGRENSD: schrijf alleen wat in de TX-ringbuffer past (setTxBufferSize in
// setup) en breek anders af tot de volgende tick. Een ongelimiteerde while blokkeerde
// de loop ~150-200 ms op UART-backpressure onder batch-druk — precies wanneer een
// phase-locked resend (M1) sub-ms moet vertrekken. Peek-dan-receive is race-vrij:
// loop() is de enige consument.
void verwerkLogQueue() {
  if (logQueue == nullptr) return;
  char buf[LOGREGEL_MAX];
  while (xQueuePeek(logQueue, buf, 0) == pdTRUE) {
    size_t len = strlen(buf);
    if ((size_t)Serial.availableForWrite() < len) break;   // geen ruimte -> volgende tick
    xQueueReceive(logQueue, buf, 0);                        // nu pas echt consumeren
    Serial.write((const uint8_t *)buf, len);
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
// (MSG_CMD_ACK ná uitvoering), niet op de MAC-laag-ACK.
//
// TIMING (niet-blokkerende slave-scan): de slave voert een ontvangen commando binnen ~5 ms
// uit, maar zijn radio is tijdens de BLE-scan ~80% bezet — het betrouwbare RX-venster is de
// ~250-400 ms ná de scan. Retries zijn daarom PHASE-LOCKED: elke MSG_BATCH/MSG_HEARTBEAT
// markeert het begin van dat vrije venster (slaveVensterVlag) en triggert een directe resend.
// APP_ACK_TIMEOUT (600 ms ≈ halve slave-cyclus) is enkel nog de blinde fallback; hem
// "terugtunen" naar ~1500 ms is gebaseerd op de oude blokkerende-scan-aanname en maakt
// commando's alleen maar trager.
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

// De FIFO's worden vanuit TWEE taken op TWEE cores gemuteerd: OnDataRecv/OnDataSent
// (WiFi-task, core 0) poppen/rewinden, enqueueCommando/verwerkQueue (loop, core 1)
// enqueuen/zenden. head/count zijn niet-atomaire read-modify-writes -> zonder lock
// kan een verloren update een paal-queue permanent wedgen (count-underflow). Alle
// index-mutaties lopen daarom onder deze spinlock; esp_now_send/logRegel blijven
// erbuiten (FreeRTOS-calls mogen niet binnen een critical section).
static portMUX_TYPE cmdMux = portMUX_INITIALIZER_UNLOCKED;

static const uint8_t  MAX_POGINGEN     = 6;     // blinde + phase-pogingen over >=2 slave-cycli
static const uint32_t APP_ACK_TIMEOUT  = 600;   // ms; fallback — phase-lock is de hoofdroute
static uint16_t       volgendeCmdSeq   = 1;     // monotone teller (0 = "geen")

// Phase-lock: 1 zodra een MSG_BATCH/MSG_HEARTBEAT van deze slave binnenkwam (WiFi-task
// schrijft, verwerkQueue consumeert per loop-tick). Direct na zo'n bericht is de slave-radio
// ~250 ms vrij (post-scan luistervenster) -> hét moment voor een pending resend.
volatile uint8_t slaveVensterVlag[AANTAL_SLAVES] = {0};
static const uint32_t VENSTER_HERZEND_GUARD_MS = 50;   // dempt dubbeltrigger batch+heartbeat

// De vlag heeft sinds de geplande bommen TWEE consumenten (verwerkQueue + verwerkBomQueue).
// loop() leest+wist hem daarom één keer per tick naar vensterTick[] (FIFO: byte-identieke
// one-shot-semantiek) en zet tegelijk bomVensterTot[] (bommen: venster blijft ~200 ms "open",
// zodat meerdere pending bommen binnen hetzelfde vrije radio-venster de lucht in kunnen).
static uint8_t  vensterTick[AANTAL_SLAVES]  = {0};
static uint32_t bomVensterTot[AANTAL_SLAVES] = {0};
static const uint32_t BOM_VENSTER_OPEN_MS = 200;

// ---- GEPLANDE BOMMEN (minigame "Bommen vermijden", beat-sync) ----
// Per-slave wachtrij voor actie 25-cues met wacht_ms > 0. Node-RED stuurt elke cue LEAD
// (~1,2 s) vooraf; wij herzenden hem phase-locked (vrij radio-venster) + blind (150 ms-cadans)
// met per poging een VERS herberekende signed rest-wacht, tot het verval op uitvoerOp + de
// animatieduur. Bewust GEEN ACK-machinerie: duplicaat-bezorging is onschadelijk (de slave
// dedupet op seq) en zo blijft deze hele administratie LOOP-TASK-PRIVÉ — enqueue gebeurt via
// verwerkSerieel->verwerkRegel en verzenden via verwerkBomQueue, allebei op de loop-task,
// dus (anders dan de cmdMux-FIFO's hierboven) is hier géén lock nodig.
#define BOM_QUEUE_DIEPTE 4
struct BomItem {
  uint16_t laad, hold, pink, hz;
  uint32_t uitvoerOpMs;      // millis() waarop de slave moet vuren
  uint32_t vervaltOpMs;      // uitvoerOp + laad+hold+pink (+marge): daarna niet meer zenden
  uint32_t laatstePogingMs;  // 0 = nog nooit verzonden
  uint8_t  seq;
  uint8_t  pogingen;
};
struct BomQueue { BomItem items[BOM_QUEUE_DIEPTE]; uint8_t head; uint8_t count; };
static BomQueue bomPerSlave[AANTAL_SLAVES] = {};
static uint8_t  bomSeqTeller[AANTAL_SLAVES] = {0};
static const uint8_t  BOM_MAX_POGINGEN       = 30;
static const uint32_t BOM_RETRY_MS           = 150;  // blinde cadans (venster is de hoofdroute)
static const uint32_t BOM_VENSTER_SPACING_MS = 40;   // min. afstand tussen pogingen ín het venster
static const uint32_t BOM_VERVAL_MARGE_MS    = 200;

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
  bool     gedropt    = false;
  uint16_t gedroptSeq = 0;
  taskENTER_CRITICAL(&cmdMux);
  if (q.count == CMD_FIFO_DIEPTE) {   // FIFO vol -> oudste droppen
    gedropt    = true;
    gedroptSeq = q.items[q.head].cmd_seq;
    q.head = (q.head + 1) % CMD_FIFO_DIEPTE;
    q.count--;
  }
  uint8_t tail = (q.head + q.count) % CMD_FIFO_DIEPTE;
  q.items[tail].actie_id        = actie;
  q.items[tail].cmd_seq         = volgendeCmdSeq++;
  // 0 en 0xFFFF overslaan: 0 = "geen", 0xFFFF = slave-boot-sentinel (laatsteUitgevoerdeSeq).
  // Zonder de 0xFFFF-skip kan het eerste commando na een slave-reboot stil geskipt worden (L5).
  if (volgendeCmdSeq == 0 || volgendeCmdSeq == 0xFFFF) volgendeCmdSeq = 1;
  q.items[tail].pogingen        = 0;
  q.items[tail].laatstVerstuurd = 0;
  q.count++;
  taskEXIT_CRITICAL(&cmdMux);
  if (gedropt) {
    logRegel("{\"status\":\"fifo_vol\",\"paal\":%d,\"gedropt_seq\":%u}\n", paal, gedroptSeq);
  }
  return true;
}

// Drijft per slave het HEAD-item van zijn FIFO af: verstuurt het, retried phase-locked
// (direct na een batch/heartbeat van die slave = vrij radio-venster) of blind na
// APP_ACK_TIMEOUT, geeft op na MAX_POGINGEN (pop + volgende item).
// Retries hergebruiken hetzelfde cmd_seq (idempotent). Parallel per slave. Elke loop()-tick.
void verwerkQueue() {
  uint32_t nu = millis();
  for (int i = 0; i < AANTAL_SLAVES; i++) {
    SlaveCmdQueue &q = cmdPerSlave[i];
    // Venster-vlag: sinds de geplande bommen ge-hoist naar loop() (twee consumenten) —
    // vensterTick[] draagt dezelfde consume-once-per-tick-semantiek, ook zonder pending item.
    bool venster = (vensterTick[i] != 0);

    // Besluit + index-mutatie onder de lock; zenden/loggen erbuiten (zie cmdMux).
    bool     doeSend = false, doeOpgegeven = false;
    uint8_t  sendActie = 0, sendPoging = 0, opgActie = 0, opgPogingen = 0;
    uint16_t sendSeq = 0, opgSeq = 0;

    taskENTER_CRITICAL(&cmdMux);
    if (q.count > 0) {
      SlaveCmd &h = q.items[q.head];   // in-flight = head
      bool inFlight  = (h.pogingen > 0);
      bool timeoutOm = inFlight && (nu - h.laatstVerstuurd) >= APP_ACK_TIMEOUT;
      bool phaseNu   = inFlight && venster && (nu - h.laatstVerstuurd) >= VENSTER_HERZEND_GUARD_MS;
      if (!inFlight || timeoutOm || phaseNu) {
        if (h.pogingen >= MAX_POGINGEN) {
          doeOpgegeven = true;
          opgActie = h.actie_id; opgSeq = h.cmd_seq; opgPogingen = h.pogingen;
          q.head = (q.head + 1) % CMD_FIFO_DIEPTE;
          q.count--;
        } else {
          h.pogingen++;
          h.laatstVerstuurd = nu;
          doeSend = true;
          sendActie = h.actie_id; sendSeq = h.cmd_seq; sendPoging = h.pogingen;
        }
      }
    }
    taskEXIT_CRITICAL(&cmdMux);

    if (doeOpgegeven) {
      logRegel("{\"status\":\"opgegeven\",\"paal\":%d,\"actie\":%d,\"seq\":%u,\"pogingen\":%d}\n",
               PAAL_MIN + i, opgActie, opgSeq, opgPogingen);
      continue;
    }
    if (doeSend) {
      // Het commando draagt de GLOBALE paal_id (PAAL_MIN + i) zodat de slave op zijn PAAL_ID matcht.
      commando_message_v2 cmd = { MSG_COMMANDO, (uint8_t)(PAAL_MIN + i), sendActie, sendSeq };
      esp_err_t r = esp_now_send(slaveAdressen[i], (uint8_t *)&cmd, sizeof(cmd));
      if (r != ESP_OK) {
        logRegel("{\"status\":\"send_err\",\"paal\":%d,\"poging\":%d}\n", PAAL_MIN + i, sendPoging);
        // Snel opnieuw proberen — met seq-guard: als een ACK het item intussen popte,
        // niet de timer van het VOLGENDE item verpesten.
        taskENTER_CRITICAL(&cmdMux);
        if (q.count > 0 && q.items[q.head].cmd_seq == sendSeq) {
          q.items[q.head].laatstVerstuurd = nu - (APP_ACK_TIMEOUT - 150);
        }
        taskEXIT_CRITICAL(&cmdMux);
      }
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
      // Phase-lock: batch = begin van het vrije radio-venster van deze slave (M1).
      slaveVensterVlag[paalIndex] = 1;
      memcpy(&inkomendeData, incomingData, (size_t)verwacht);   // accepteert ook volle 215-B frames
#ifdef LOG_RECV_DEBUG
      logRegel("[RECV] Paal %d, %d spelers, batt %u mV\n",
        inkomendeData.paal_id, n, inkomendeData.batt_mv);
#endif
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
      // Alleen het HEAD-item is in-flight; match op cmd_seq -> pop (onder de cmdMux-lock,
      // want de loop-task muteert head/count tegelijk op de andere core).
      bool gepopt = false;
      taskENTER_CRITICAL(&cmdMux);
      if (q.count > 0 && q.items[q.head].cmd_seq == a.cmd_seq) {
        q.head = (q.head + 1) % CMD_FIFO_DIEPTE;
        q.count--;
        gepopt = true;
      }
      taskEXIT_CRITICAL(&cmdMux);
      if (gepopt) {
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
      // Phase-lock: heartbeat valt in hetzelfde vrije venster als de batch (M1).
      slaveVensterVlag[paalIndex] = 1;
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
      logRegel("{\"paal\":%d,\"knop\":1,\"teller\":%u}\n", k.paal_id, k.teller);
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
  bool    had    = false;
  uint8_t poging = 0;
  taskENTER_CRITICAL(&cmdMux);
  if (q.count > 0) {
    poging = q.items[q.head].pogingen;
    q.items[q.head].laatstVerstuurd = millis() - (APP_ACK_TIMEOUT - 150);
    had = true;
  }
  taskEXIT_CRITICAL(&cmdMux);
  if (had) logRegel("{\"status\":\"send_err\",\"paal\":%d,\"poging\":%d}\n", PAAL_MIN + i, poging);
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

// BLE-scan-config: stuur direct een MSG_SCAN_CONFIG (geen FIFO/ACK, fire-and-forget) met de
// gewenste scan-vensterduur in ms. De slave clamp't de waarde zelf (300..2000 ms).
static void stuurScanConfig(int paal, uint16_t scan_ms) {
  int i = paalNaarIndex(paal);
  if (i < 0 || i >= AANTAL_SLAVES) {
    logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    return;
  }
  if (isPlaceholderMac(slaveAdressen[i])) {
    logRegel("{\"status\":\"geen_slave\",\"paal\":%d}\n", paal);
    return;
  }
  scan_config_message sc = { MSG_SCAN_CONFIG, (uint8_t)paal, scan_ms };
  esp_err_t r = esp_now_send(slaveAdressen[i], (uint8_t *)&sc, sizeof(sc));
  logRegel("{\"status\":\"%s\",\"paal\":%d,\"scan_ms\":%u}\n",
           (r == ESP_OK) ? "scan" : "send_err", paal, scan_ms);
}

// LED-helderheid: stuur direct een MSG_LED_CONFIG (geen FIFO/ACK, fire-and-forget) met de
// gewenste FastLED-helderheid (0..255). De slave clamp't zelf (min 5).
static void stuurLedConfig(int paal, uint8_t helderheid) {
  int i = paalNaarIndex(paal);
  if (i < 0 || i >= AANTAL_SLAVES) {
    logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    return;
  }
  if (isPlaceholderMac(slaveAdressen[i])) {
    logRegel("{\"status\":\"geen_slave\",\"paal\":%d}\n", paal);
    return;
  }
  led_config_message lc = { MSG_LED_CONFIG, (uint8_t)paal, helderheid };
  esp_err_t r = esp_now_send(slaveAdressen[i], (uint8_t *)&lc, sizeof(lc));
  logRegel("{\"status\":\"%s\",\"paal\":%d,\"helderheid\":%u}\n",
           (r == ESP_OK) ? "led" : "send_err", paal, helderheid);
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

// Bom-animatie (minigame): stuur direct een MSG_BOM (geen FIFO/ACK, fire-and-forget). De slave rendert
// de oplaad-ramp -> hold -> pinken -> uit lokaal met de meegegeven tijden.
static void stuurBom(int paal, uint16_t laad_ms, uint16_t hold_ms, uint16_t pink_ms, uint16_t pink_hz) {
  int i = paalNaarIndex(paal);
  if (i < 0 || i >= AANTAL_SLAVES) {
    logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    return;
  }
  if (isPlaceholderMac(slaveAdressen[i])) {
    logRegel("{\"status\":\"geen_slave\",\"paal\":%d}\n", paal);
    return;
  }
  // v2-frame met wacht 0 (= direct vuren) + verse seq: gedrag identiek aan v1, maar een
  // dubbel bezorgde herhaling zou op een nieuwe slave netjes gededuped worden.
  uint8_t s = ++bomSeqTeller[i]; if (s == 0) s = ++bomSeqTeller[i];   // seq 0 = gereserveerd (v1)
  bom_message bm = { MSG_BOM, (uint8_t)paal, laad_ms, hold_ms, pink_ms, pink_hz, 0, s };
  esp_err_t res = esp_now_send(slaveAdressen[i], (uint8_t *)&bm, sizeof(bm));
  logRegel("{\"status\":\"%s\",\"paal\":%d,\"actie\":25}\n",
           (res == ESP_OK) ? "bom" : "send_err", paal);
}

// ---- GEPLANDE BOMMEN (wacht_ms > 0): wachtrij + phase-locked herzendingen ----
// Alles hier draait op de loop-task (enqueue via verwerkRegel, zenden via verwerkBomQueue),
// dus zonder lock — zie de toelichting bij BomQueue.

// Wis de bom-wachtrij van één slave (bij actie 0 = stop/einde minigame): er mogen daarna
// géén herzendingen meer vertrekken, anders vuurt een net-gewiste bom alsnog op de slave.
static void bomWisSlave(int i) {
  if (i < 0 || i >= AANTAL_SLAVES) return;
  if (bomPerSlave[i].count > 0) {
    logRegel("{\"status\":\"bom_gewist\",\"paal\":%d,\"n\":%d}\n", PAAL_MIN + i, bomPerSlave[i].count);
  }
  bomPerSlave[i].head = 0;
  bomPerSlave[i].count = 0;
}

// Plan een bom: uitvoerOp = nu + wacht. Vol (hoort niet: tijdlijn-max is 2 pending/paal) ->
// drop-NEWEST + log (afwijking van enqueueCommando's drop-oldest: het oudste item is hier
// het eerst due en dus het belangrijkst).
static void enqueueBom(int paal, uint16_t laad, uint16_t hold, uint16_t pink, uint16_t hz, uint32_t wacht) {
  int i = paalNaarIndex(paal);
  if (i < 0 || i >= AANTAL_SLAVES) {
    logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    return;
  }
  if (isPlaceholderMac(slaveAdressen[i])) {
    logRegel("{\"status\":\"geen_slave\",\"paal\":%d}\n", paal);
    return;
  }
  BomQueue &q = bomPerSlave[i];
  if (q.count >= BOM_QUEUE_DIEPTE) {
    logRegel("{\"status\":\"bom_vol\",\"paal\":%d}\n", paal);
    return;
  }
  uint8_t s = ++bomSeqTeller[i]; if (s == 0) s = ++bomSeqTeller[i];
  uint32_t nu = millis();
  BomItem &it = q.items[(q.head + q.count) % BOM_QUEUE_DIEPTE];
  it.laad = laad; it.hold = hold; it.pink = pink; it.hz = hz;
  it.uitvoerOpMs     = nu + wacht;
  it.vervaltOpMs     = it.uitvoerOpMs + (uint32_t)laad + hold + pink + BOM_VERVAL_MARGE_MS;
  it.laatstePogingMs = 0;
  it.seq             = s;
  it.pogingen        = 0;
  q.count++;
  logRegel("{\"status\":\"bom_gepland\",\"paal\":%d,\"wacht\":%lu,\"seq\":%u}\n",
           paal, (unsigned long)wacht, s);
}

// Zend pending bommen: éérste gelegenheid direct, daarna phase-locked in het vrije
// radio-venster (spacing 40 ms) en blind op de 150 ms-cadans. Elke poging draagt een VERS
// herberekende signed rest-wacht, zodat de slave altijd op hetzelfde absolute moment vuurt,
// hoe laat het frame ook landt. Max één send per slave per loop-tick (airtime-vriendelijk).
static void verwerkBomQueue() {
  uint32_t nu = millis();
  for (int i = 0; i < AANTAL_SLAVES; i++) {
    BomQueue &q = bomPerSlave[i];
    if (q.count == 0) continue;

    // 1) Verval: head-items die voorbij hun animatie-einde zijn (of uitgeput) poppen.
    while (q.count > 0) {
      BomItem &h = q.items[q.head];
      bool voorbij  = ((int32_t)(nu - h.vervaltOpMs) >= 0);
      bool uitgeput = (h.pogingen >= BOM_MAX_POGINGEN);
      if (!voorbij && !uitgeput) break;
      logRegel("{\"status\":\"bom_verlopen\",\"paal\":%d,\"seq\":%u,\"pogingen\":%u}\n",
               PAAL_MIN + i, h.seq, h.pogingen);
      q.head = (uint8_t)((q.head + 1) % BOM_QUEUE_DIEPTE);
      q.count--;
    }
    if (q.count == 0) continue;

    // 2) Zend-kandidaat: het eerste item (in enqueue-volgorde) dat aan de beurt is.
    bool vensterOpen = ((int32_t)(bomVensterTot[i] - nu) > 0);
    for (uint8_t k = 0; k < q.count; k++) {
      BomItem &it = q.items[(q.head + k) % BOM_QUEUE_DIEPTE];
      bool nooitVerstuurd = (it.laatstePogingMs == 0);
      uint32_t sindsPoging = nu - it.laatstePogingMs;
      bool mag = nooitVerstuurd ||
                 (vensterOpen && sindsPoging >= BOM_VENSTER_SPACING_MS) ||
                 (sindsPoging >= BOM_RETRY_MS);
      if (!mag) continue;

      bom_message bm = { MSG_BOM, (uint8_t)(PAAL_MIN + i), it.laad, it.hold, it.pink, it.hz,
                         (int32_t)(it.uitvoerOpMs - nu), it.seq };
      esp_err_t res = esp_now_send(slaveAdressen[i], (uint8_t *)&bm, sizeof(bm));
      it.laatstePogingMs = nu;
      it.pogingen++;
      if (it.pogingen == 1) {   // enkel de eerste poging loggen (logRegel-budget)
        logRegel("{\"status\":\"%s\",\"paal\":%d,\"actie\":25,\"seq\":%u}\n",
                 (res == ESP_OK) ? "bom" : "send_err", PAAL_MIN + i, it.seq);
      }
      break;   // max één send per slave per tick
    }
  }
}

// ---- SERIEEL COMMANDO VAN RASPBERRY PI ----
// Heap-vrije JSON-veld-parser: zoekt "sleutel": in de regel en parset het getal erna.
// Vervangt de oude Arduino-String/substring-parsing die per commandoregel meerdere
// wisselende-grootte heap-allocaties deed -> fragmentatie-churn over uren.
static long jsonVeld(const char *regel, const char *sleutel, long fallback) {
  const char *p = strstr(regel, sleutel);
  if (p == nullptr) return fallback;
  return atol(p + strlen(sleutel));
}

// Parse één complete regel en zet het commando in het juiste paal-slot.
void verwerkRegel(const char *regel) {
  if (strstr(regel, "\"paal\":") == nullptr || strstr(regel, "\"actie\":") == nullptr) return;

  int     paal  = (int)jsonVeld(regel, "\"paal\":", 0);
  uint8_t actie = (uint8_t)jsonVeld(regel, "\"actie\":", 0);

  // Bom-animatie (actie 25): niet via de FIFO, maar direct als MSG_BOM met de tijden uit de extra
  // velden laad_ms/hold_ms/pink_ms/pink_hz. Fire-and-forget zoals klokslag; de bridge blijft ongewijzigd.
  if (actie == ACTIE_BOM) {
    uint16_t laad = (uint16_t)jsonVeld(regel, "\"laad_ms\":", 0);
    uint16_t hold = (uint16_t)jsonVeld(regel, "\"hold_ms\":", 0);
    uint16_t pink = (uint16_t)jsonVeld(regel, "\"pink_ms\":", 0);
    uint16_t hz   = (uint16_t)jsonVeld(regel, "\"pink_hz\":", 2);
    if (paal >= PAAL_MIN && paal <= PAAL_MAX) {
      stuurBom(paal, laad, hold, pink, hz);
    } else {
      logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    }
    return;
  }

  // Buzzer-tuning (actie 12): niet via de FIFO, maar direct als MSG_BUZZER_TOON met de
  // frequentie uit het extra veld "toon" (Hz; 0 = stop). Zo blijft de bridge ongewijzigd.
  if (actie == ACTIE_BUZZER_TOON) {
    uint16_t toon = (uint16_t)jsonVeld(regel, "\"toon\":", 0);
    if (paal >= PAAL_MIN && paal <= PAAL_MAX) {
      stuurBuzzerToon(paal, toon);
    } else {
      logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    }
    return;
  }

  // Klokslag-LED (actie 16): niet via de FIFO, maar direct als MSG_KLOKSLAG met r/g/b/helderheid/modus.
  if (actie == ACTIE_KLOKSLAG) {
    uint8_t r          = (uint8_t)jsonVeld(regel, "\"r\":", 0);
    uint8_t g          = (uint8_t)jsonVeld(regel, "\"g\":", 0);
    uint8_t b          = (uint8_t)jsonVeld(regel, "\"b\":", 0);
    uint8_t helderheid = (uint8_t)jsonVeld(regel, "\"helderheid\":", 255);
    uint8_t modus      = (uint8_t)jsonVeld(regel, "\"modus\":", 0);
    if (paal >= PAAL_MIN && paal <= PAAL_MAX) {
      stuurKlokslag(paal, r, g, b, helderheid, modus);
    } else {
      logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    }
    return;
  }

  // BLE-scan-config (actie 20): niet via de FIFO, maar direct als MSG_SCAN_CONFIG met de
  // vensterduur uit het extra veld "scan_ms" (ms). Zo blijft de bridge ongewijzigd.
  if (actie == ACTIE_SCAN_CONFIG) {
    uint16_t ms = (uint16_t)jsonVeld(regel, "\"scan_ms\":", SCAN_MS_DEFAULT);
    if (paal >= PAAL_MIN && paal <= PAAL_MAX) {
      stuurScanConfig(paal, ms);
    } else {
      logRegel("{\"status\":\"buiten_bereik\",\"paal\":%d,\"master\":%d}\n", paal, MASTER_NR);
    }
    return;
  }

  // LED-helderheid (actie 21): direct als MSG_LED_CONFIG met de helderheid uit het extra veld
  // "helderheid" (0..255). Zo blijft de bridge ongewijzigd.
  if (actie == ACTIE_LED_CONFIG) {
    uint8_t h = (uint8_t)jsonVeld(regel, "\"helderheid\":", LED_HELDER_DEFAULT);
    if (paal >= PAAL_MIN && paal <= PAAL_MAX) {
      stuurLedConfig(paal, h);
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
  // TX-ringbuffer MOET vóór begin() (core 2.0.17 weigert hem erna). Maakt Serial.write
  // niet-blokkerend zolang de ring vrij is en availableForWrite() = vrije ring-ruimte
  // (i.p.v. enkel de 128-B HW-FIFO) — vereist voor de budget-drain in verwerkLogQueue.
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);

  // Zelfherstel bij een loop-hang: schrijf de Arduino-loop in bij de Task-WDT (5 s,
  // panic=reset in sdkconfig). Zonder deze regel bewaakt de WDT niets en blijft een
  // vastgelopen master dood tot een handmatige power-cycle. De loop is non-blocking
  // (budget-gedrainde log-queue), dus een pass blijft ver onder de 5 s.
  enableLoopWDT();

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

  vulSlaveAdressen();   // slaveAdressen[] uit de gedeelde paal_macs.h-tabel vullen

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
  // Venster-vlaggen één keer per tick lezen+wissen (twee consumenten: FIFO + bommen).
  // vensterTick[] = one-shot voor verwerkQueue (byte-identiek aan het oude gedrag);
  // bomVensterTot[] laat verwerkBomQueue het vrije radio-venster ~200 ms benutten.
  {
    uint32_t nu0 = millis();
    for (int i = 0; i < AANTAL_SLAVES; i++) {
      if (slaveVensterVlag[i]) {
        slaveVensterVlag[i] = 0;
        vensterTick[i] = 1;
        bomVensterTot[i] = nu0 + BOM_VENSTER_OPEN_MS;
      } else {
        vensterTick[i] = 0;
      }
    }
  }
  verwerkSerieel();
  verwerkQueue();
  verwerkBomQueue();

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

  // Identiteit aankondigen (meteen na boot + elke ANNOUNCE_INTERVAL_MS). De bridge leert hieruit welke
  // poort = welke master en kan zo commando/masterN routeren, ook als er nog geen slave detecteert.
  // GEEN "status"-veld: de bridge negeert status-echo's bij het leren van routes.
  static uint32_t laatsteAnnounce = 0;
  if (laatsteAnnounce == 0 || nu - laatsteAnnounce > ANNOUNCE_INTERVAL_MS) {
    laatsteAnnounce = nu;
    logRegel("{\"announce\":1,\"master\":%d,\"paal_min\":%d,\"paal_max\":%d}\n",
             MASTER_NR, PAAL_MIN, PAAL_MAX);
  }

  verwerkLogQueue();   // enige Serial-schrijver

  // Ingebouwde LED (active-HIGH): aan zolang de ontvangst-puls loopt.
  digitalWrite(BUILTIN_LED_PIN, (millis() < ingebouwdeLedTot) ? HIGH : LOW);
}
