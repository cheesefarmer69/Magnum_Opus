#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

const int WIFI_KANAAL = 1;

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
  Serial.print("[SEND] Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "MISLUKT");
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

  commando_message commando;
  commando.paal_id = paal;
  commando.actie_id = actie;

  // Stuur commando naar de juiste slave
  if (paal >= 1 && paal <= AANTAL_SLAVES) {
    esp_err_t result = esp_now_send(slaveAdressen[paal - 1],
                                     (uint8_t *)&commando,
                                     sizeof(commando));
    Serial.printf("{\"status\":\"%s\",\"paal\":%d}\n",
      (result == ESP_OK) ? "verstuurd" : "mislukt", paal);
  } else {
    Serial.printf("{\"status\":\"onbekende paal\",\"paal\":%d}\n", paal);
  }
}

// ---- SETUP ----
void setup() {
  Serial.begin(115200);
  delay(2000);

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
}