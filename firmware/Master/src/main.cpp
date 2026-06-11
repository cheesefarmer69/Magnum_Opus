#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

const int WIFI_KANAAL = 1;

// ---- INGEBOUWDE LED ----
// ESP32 WROOM-32 onboard LED op GPIO2 (active-HIGH). Pulst kort bij elke
// ontvangen slave-batch als visuele ontvangst-indicator (niet-blokkerend).
#define BUILTIN_LED_PIN 2
const unsigned long RECV_KNIPPER_MS = 30;       // duur puls bij batch-ontvangst
unsigned long ingebouwdeLedTot = 0;             // millis() tot wanneer LED aan

// ---- DATASTRUCTS ----
typedef struct __attribute__((packed)) batch_message {
  int32_t paal_id;
  int32_t aantalGevonden;
  float   batterij_v;       // gemeten batterijspanning slave (0.0 = niet gemeten)
  struct {
    char speler_mac[18];
    int32_t rssi;
  } spelers[9];
} batch_message;
batch_message inkomendeData;

typedef struct __attribute__((packed)) commando_message {
  int32_t paal_id;
  uint8_t actie_id;
} commando_message;

// ---- SLAVES REGISTREREN ----
const int AANTAL_SLAVES = 3;

uint8_t slaveAdressen[AANTAL_SLAVES][6] = {
  {0xAC, 0xA7, 0x04, 0xBD, 0x3A, 0x48},
  {0xAC, 0xA7, 0x04, 0xC0, 0xC6, 0x14},
  {0x8C, 0xFD, 0x49, 0x54, 0xC4, 0x38}

};

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
struct SlaveCmdSlot {
  bool             actief;           // staat er een pending commando in dit slot?
  commando_message cmd;
  uint8_t          pogingen;
  uint32_t         laatstVerstuurd;
  bool             wachtOpAck;       // wacht op OnDataSent()
};
SlaveCmdSlot cmdPerSlave[AANTAL_SLAVES] = {};   // index = paal - 1, zero-init

static const uint8_t  MAX_POGINGEN    = 5;
static const uint32_t RETRY_INTERVAL  = 250;   // ms

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

// Zet een commando in het pending-slot van zijn paal (overschrijft = laatste wint).
// Een placeholder-paal (all-zero MAC) wordt direct geweigerd, zonder retry-cyclus.
static bool enqueueCommando(uint8_t paal, uint8_t actie) {
  int i = paal - 1;
  if (i < 0 || i >= AANTAL_SLAVES) {
    logRegel("{\"status\":\"onbekende paal\",\"paal\":%d}\n", paal);
    return false;
  }
  if (isPlaceholderMac(slaveAdressen[i])) {
    logRegel("{\"status\":\"geen_slave\",\"paal\":%d}\n", paal);
    return false;
  }
  SlaveCmdSlot &s = cmdPerSlave[i];
  s.cmd.paal_id     = paal;
  s.cmd.actie_id    = actie;
  s.pogingen        = 0;
  s.laatstVerstuurd = 0;
  s.wachtOpAck      = false;
  s.actief          = true;   // overschrijven: laatste commando wint
  return true;
}

// Drijft alle slots af: verstuurt due commando's, retried bij timeout, geeft op
// na MAX_POGINGEN. Parallel — elk slot is onafhankelijk. Elke loop()-tick.
void verwerkQueue() {
  uint32_t nu = millis();
  for (int i = 0; i < AANTAL_SLAVES; i++) {
    SlaveCmdSlot &s = cmdPerSlave[i];
    if (!s.actief) continue;
    if (s.wachtOpAck) continue;  // wacht op OnDataSent
    if (s.pogingen > 0 && (nu - s.laatstVerstuurd) < RETRY_INTERVAL) continue;

    if (s.pogingen >= MAX_POGINGEN) {
      logRegel("{\"status\":\"opgegeven\",\"paal\":%d,\"actie\":%d,\"pogingen\":%d}\n",
               s.cmd.paal_id, s.cmd.actie_id, s.pogingen);
      s.actief = false;
      continue;
    }

    s.pogingen++;
    s.laatstVerstuurd = nu;
    s.wachtOpAck = true;
    esp_err_t r = esp_now_send(slaveAdressen[i], (uint8_t *)&s.cmd, sizeof(s.cmd));
    if (r != ESP_OK) {
      s.wachtOpAck = false;
      logRegel("{\"status\":\"send_err\",\"paal\":%d,\"poging\":%d}\n",
               s.cmd.paal_id, s.pogingen);
    }
  }
}

// ---- CALLBACKS ----
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  // Sender-MAC gate: ESP-NOW levert pakketten van ÉLKE afzender aan deze
  // callback. esp_now_add_peer() filtert alleen voor zenden, niet ontvangst.
  // Drop dus pakketten van slaves die niet in slaveAdressen[] staan — zo
  // negeert deze master de slaves die bij een andere master horen.
  int paalIndex = vindSlaveIndex(mac);
  if (paalIndex < 0) {
    logRegel("[GATE] Genegeerd: %02X:%02X:%02X:%02X:%02X:%02X (niet in slaveAdressen[])\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return;
  }

  // Visuele ontvangst-indicator: pulst de ingebouwde LED kort (niet-blokkerend).
  ingebouwdeLedTot = millis() + RECV_KNIPPER_MS;

  logRegel("[RECV] %d bytes van paal %d (%02X:%02X:%02X:%02X:%02X:%02X)\n",
    len, paalIndex + 1, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  if (len < (int)sizeof(batch_message)) {
    logRegel("[RECV] Te kort: %d < %d, genegeerd\n", len, (int)sizeof(batch_message));
    return;
  }

  memcpy(&inkomendeData, incomingData, sizeof(inkomendeData));

  logRegel("[RECV] Paal %d, %d spelers, batt %.2fV\n",
    inkomendeData.paal_id, inkomendeData.aantalGevonden, inkomendeData.batterij_v);

  for (int i = 0; i < inkomendeData.aantalGevonden; i++) {
    logRegel("{\"paal\":%d,\"mac\":\"%s\",\"rssi\":%d}\n",
      inkomendeData.paal_id,
      inkomendeData.spelers[i].speler_mac,
      inkomendeData.spelers[i].rssi);
  }

  // Batterij-regel per batch, óók bij 0 spelers — zo blijft de batterij-status
  // in Node-RED actueel zelfs in een leeg vak. 0.0V = "niet gemeten", overslaan.
  if (inkomendeData.batterij_v > 0.0f) {
    logRegel("{\"paal\":%d,\"batt\":%.2f}\n",
      inkomendeData.paal_id, inkomendeData.batterij_v);
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Vind het slot van de bestemmeling via zijn MAC — werkt met meerdere
  // in-flight sends (één per slave) tegelijk.
  int i = vindSlaveIndex(mac_addr);
  if (i < 0) return;
  SlaveCmdSlot &s = cmdPerSlave[i];
  if (!s.actief) return;

  s.wachtOpAck = false;
  if (status == ESP_NOW_SEND_SUCCESS) {
    logRegel("{\"status\":\"ack\",\"paal\":%d,\"actie\":%d,\"pogingen\":%d}\n",
             s.cmd.paal_id, s.cmd.actie_id, s.pogingen);
    s.actief = false;   // slot vrij
  }
  // Bij FAIL: blijft actief, verwerkQueue() retried na RETRY_INTERVAL.
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

  if (paal >= 1 && paal <= AANTAL_SLAVES) {
    if (enqueueCommando(paal, actie)) {
      logRegel("{\"status\":\"queued\",\"paal\":%d,\"actie\":%d}\n", paal, actie);
    }
  } else {
    logRegel("{\"status\":\"onbekende paal\",\"paal\":%d}\n", paal);
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

  // Alle slaves toevoegen als peer
  for (int i = 0; i < AANTAL_SLAVES; i++) {
    // Skip slaves met placeholder MAC (allemaal nullen)
    if (isPlaceholderMac(slaveAdressen[i])) {
      logRegel("[PEER] Paal %d overgeslagen (geen MAC ingevuld)\n", i + 1);
      continue;
    }

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, slaveAdressen[i], 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      logRegel("[PEER] Paal %d toegevoegd: %02X:%02X:%02X:%02X:%02X:%02X\n",
        i + 1, slaveAdressen[i][0], slaveAdressen[i][1], slaveAdressen[i][2],
        slaveAdressen[i][3], slaveAdressen[i][4], slaveAdressen[i][5]);
    } else {
      logRegel("[PEER] Paal %d toevoegen MISLUKT!\n", i + 1);
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
