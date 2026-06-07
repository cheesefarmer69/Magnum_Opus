#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

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

// ---- COMMANDO-QUEUE MET RETRIES ----
// Elk commando blijft in de queue tot OnDataSent met SUCCESS terugkomt
// (= MAC-laag ACK van slave-radio). Bij FAIL of timeout retries automatisch
// tot MAX_POGINGEN, daarna opgegeven met een log-regel.
struct PendingCmd {
  uint8_t  doelMac[6];
  commando_message cmd;
  uint8_t  pogingen;
  uint32_t laatstVerstuurd;
  bool     wachtOpAck;
};

static const uint8_t  MAX_POGINGEN    = 5;
static const uint32_t RETRY_INTERVAL  = 250;   // ms
static const uint8_t  QUEUE_SIZE      = 16;

PendingCmd cmdQueue[QUEUE_SIZE];
uint8_t    queueHead = 0;
uint8_t    queueTail = 0;
uint8_t    queueAantal = 0;

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

// Voeg commando toe aan FIFO-queue. Wordt async verstuurd door verwerkQueue().
static bool enqueueCommando(uint8_t paal, uint8_t actie) {
  if (queueAantal >= QUEUE_SIZE) {
    Serial.printf("{\"status\":\"queue_vol\",\"paal\":%d}\n", paal);
    return false;
  }
  PendingCmd &pc = cmdQueue[queueTail];
  memcpy(pc.doelMac, slaveAdressen[paal - 1], 6);
  pc.cmd.paal_id  = paal;
  pc.cmd.actie_id = actie;
  pc.pogingen     = 0;
  pc.laatstVerstuurd = 0;
  pc.wachtOpAck   = false;
  queueTail = (queueTail + 1) % QUEUE_SIZE;
  queueAantal++;
  return true;
}

// Drijft de queue: verstuurt het actieve commando, retried bij timeout,
// geeft op na MAX_POGINGEN. Wordt elke loop()-tick aangeroepen.
void verwerkQueue() {
  if (queueAantal == 0) return;
  PendingCmd &actief = cmdQueue[queueHead];
  uint32_t nu = millis();

  if (actief.wachtOpAck) return;  // wacht op OnDataSent

  if (actief.pogingen > 0 && (nu - actief.laatstVerstuurd) < RETRY_INTERVAL) return;

  if (actief.pogingen >= MAX_POGINGEN) {
    Serial.printf("{\"status\":\"opgegeven\",\"paal\":%d,\"actie\":%d,\"pogingen\":%d}\n",
                  actief.cmd.paal_id, actief.cmd.actie_id, actief.pogingen);
    queueHead = (queueHead + 1) % QUEUE_SIZE;
    queueAantal--;
    return;
  }

  actief.pogingen++;
  actief.laatstVerstuurd = nu;
  actief.wachtOpAck = true;
  esp_err_t r = esp_now_send(actief.doelMac,
                             (uint8_t *)&actief.cmd,
                             sizeof(actief.cmd));
  if (r != ESP_OK) {
    actief.wachtOpAck = false;
    Serial.printf("{\"status\":\"send_err\",\"paal\":%d,\"poging\":%d}\n",
                  actief.cmd.paal_id, actief.pogingen);
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
    Serial.printf("[GATE] Genegeerd: %02X:%02X:%02X:%02X:%02X:%02X (niet in slaveAdressen[])\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return;
  }

  // Visuele ontvangst-indicator: pulst de ingebouwde LED kort (niet-blokkerend).
  ingebouwdeLedTot = millis() + RECV_KNIPPER_MS;

  Serial.printf("[RECV] %d bytes van paal %d (%02X:%02X:%02X:%02X:%02X:%02X)\n",
    len, paalIndex + 1, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  if (len < (int)sizeof(batch_message)) {
    Serial.printf("[RECV] Te kort: %d < %d, genegeerd\n", len, (int)sizeof(batch_message));
    return;
  }

  memcpy(&inkomendeData, incomingData, sizeof(inkomendeData));

  Serial.printf("[RECV] Paal %d, %d spelers, batt %.2fV\n",
    inkomendeData.paal_id, inkomendeData.aantalGevonden, inkomendeData.batterij_v);

  for (int i = 0; i < inkomendeData.aantalGevonden; i++) {
    Serial.printf("{\"paal\":%d,\"mac\":\"%s\",\"rssi\":%d}\n",
      inkomendeData.paal_id,
      inkomendeData.spelers[i].speler_mac,
      inkomendeData.spelers[i].rssi);
  }

  // Batterij-regel per batch, óók bij 0 spelers — zo blijft de batterij-status
  // in Node-RED actueel zelfs in een leeg vak. 0.0V = "niet gemeten", overslaan.
  if (inkomendeData.batterij_v > 0.0f) {
    Serial.printf("{\"paal\":%d,\"batt\":%.2f}\n",
      inkomendeData.paal_id, inkomendeData.batterij_v);
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (queueAantal == 0) return;
  PendingCmd &actief = cmdQueue[queueHead];
  if (memcmp(actief.doelMac, mac_addr, 6) != 0) return;

  actief.wachtOpAck = false;
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.printf("{\"status\":\"ack\",\"paal\":%d,\"actie\":%d,\"pogingen\":%d}\n",
                  actief.cmd.paal_id, actief.cmd.actie_id, actief.pogingen);
    queueHead = (queueHead + 1) % QUEUE_SIZE;
    queueAantal--;
  }
  // Bij FAIL: blijft actief, verwerkQueue() retried na RETRY_INTERVAL.
}

// ---- SERIEEL COMMANDO VAN RASPBERRY PI ----
void verwerkSerieel() {
  if (!Serial.available()) return;

  String lijn = Serial.readStringUntil('\n');
  lijn.trim();

  int paalIndex = lijn.indexOf("\"paal\":");
  int actieIndex = lijn.indexOf("\"actie\":");

  if (paalIndex == -1 || actieIndex == -1) return;

  int paal = lijn.substring(paalIndex + 7).toInt();
  uint8_t actie = lijn.substring(actieIndex + 8).toInt();

  // In queue zetten — verwerkQueue() drijft hem af tot ack of opgegeven.
  if (paal >= 1 && paal <= AANTAL_SLAVES) {
    if (enqueueCommando(paal, actie)) {
      Serial.printf("{\"status\":\"queued\",\"paal\":%d,\"actie\":%d}\n", paal, actie);
    }
  } else {
    Serial.printf("{\"status\":\"onbekende paal\",\"paal\":%d}\n", paal);
  }
}

// ---- SETUP ----
void setup() {
  Serial.begin(115200);
  delay(2000);

  // Ingebouwde LED (active-HIGH): uit bij start
  pinMode(BUILTIN_LED_PIN, OUTPUT);
  digitalWrite(BUILTIN_LED_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("Master MAC: " + WiFi.macAddress());
  Serial.println("Master kanaal: " + String(WiFi.channel()));

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init MISLUKT");
    return;
  }

  // Kanaal vastzetten NA init
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WIFI_KANAAL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  esp_wifi_set_ps(WIFI_PS_NONE);

  Serial.println("Kanaal na fix: " + String(WiFi.channel()));

  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  // Alle slaves toevoegen als peer
  for (int i = 0; i < AANTAL_SLAVES; i++) {
    // Skip slaves met placeholder MAC (allemaal nullen)
    bool isPlaceholder = true;
    for (int j = 0; j < 6; j++) {
      if (slaveAdressen[i][j] != 0x00) {
        isPlaceholder = false;
        break;
      }
    }
    if (isPlaceholder) {
      Serial.printf("[PEER] Paal %d overgeslagen (geen MAC ingevuld)\n", i + 1);
      continue;
    }

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, slaveAdressen[i], 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      Serial.printf("[PEER] Paal %d toegevoegd: %02X:%02X:%02X:%02X:%02X:%02X\n",
        i + 1, slaveAdressen[i][0], slaveAdressen[i][1], slaveAdressen[i][2],
        slaveAdressen[i][3], slaveAdressen[i][4], slaveAdressen[i][5]);
    } else {
      Serial.printf("[PEER] Paal %d toevoegen MISLUKT!\n", i + 1);
    }
  }

  Serial.println("=== Master klaar ===");
}

// ---- LOOP ----
void loop() {
  verwerkSerieel();
  verwerkQueue();
  // Ingebouwde LED (active-HIGH): aan zolang de ontvangst-puls loopt.
  digitalWrite(BUILTIN_LED_PIN, (millis() < ingebouwdeLedTot) ? HIGH : LOW);
}