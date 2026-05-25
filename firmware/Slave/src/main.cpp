#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <FastLED.h>
#include "esp_random.h"

// ====================================================================
// PARAMETERS
// ====================================================================
const int SCAN_DUUR_S    = 1;
const int WACHT_TIMEOUT  = 200;
const int PAAL_ID        = 1;
const int WIFI_KANAAL    = 1;
const int MAX_BACKOFF_MS = 150;   // willekeurige zendvertraging (0..150ms)

// ====================================================================
// PIN MAPPING (komt overeen met PCB schema)
// ====================================================================
#define LED_DATA_PIN     0   // WS2812B data via 330Ohm
#define MOSFET_PIN       1   // IRLZ44N gate via 220Ohm (10k pull-down)
#define LIGHT_SENSOR_PIN 3   // TEMT6000 / LDR (ADC1 - werkt met WiFi)
#define BATTERY_ADC_PIN  4   // Spanningsdeler 2x 100k (ADC1)
#define BUZZER_PIN       5   // Passieve buzzer via 100Ohm (digitaal)
#define WARNING_LED_PIN  6   // Rode LED via 150Ohm (batterij waarschuwing)

// ====================================================================
// LED STRIP
// ====================================================================
#define NUM_LEDS    7        // 7 LEDs op de PCB
#define BRIGHTNESS  150
CRGB leds[NUM_LEDS];

// ====================================================================
// BUZZER
// ====================================================================
// Passieve buzzer: het volume hangt sterk af van de frequentie. Een passieve
// piezo is het luidst rond zijn resonantiefrequentie (typisch 2-4 kHz). 1000 Hz
// is ver onder resonantie -> stil. Zet dit op de resonantiefrequentie uit het
// datasheet van jouw buzzer voor maximaal volume.
const int BUZZER_FREQ = 2060;   // Hz
volatile bool buzzerActief = false;

// ====================================================================
// BATTERIJ METING
// ====================================================================
// Spanningsdeler: R1 = R2 = 100k, dus V_adc = V_batterij / 2
// ESP32-C3 ADC: 12 bit (0-4095), referentie 3.3V
const float ADC_REF_VOLT       = 3.3;
const float ADC_MAX_VALUE      = 4095.0;
const float DIVIDER_FACTOR     = 2.0;   // 1 / (R2/(R1+R2)) = 1/0.5 = 2
const float BATT_WAARSCHUWING  = 3.4;   // V, onder deze waarde LED aan
const float BATT_KRITIEK       = 3.2;   // V, onder deze waarde snel knipperen
const unsigned long BATT_CHECK_INTERVAL = 5000;  // ms
unsigned long laatsteBattCheck = 0;

// ====================================================================
// LICHT SENSOR (voor laser detectie)
// ====================================================================
// Drempel: onder deze ADC-spanning is er GEEN laser (straal verbroken)
// Kalibreer met jouw opstelling: meet licht_volt bij laser aan en uit
const float LICHT_DREMPEL      = 1.5;   // V, pas aan na kalibratie
const unsigned long LICHT_CHECK_INTERVAL = 100;  // ms
unsigned long laatsteLichtCheck = 0;
bool laserGedetecteerd = false;
bool vorigeLaserStatus = false;

// ====================================================================
// ACTION IDs
// ====================================================================
const uint8_t ACTIE_NIETS       = 0;
const uint8_t ACTIE_ROOD        = 1;
const uint8_t ACTIE_GROEN       = 2;
const uint8_t ACTIE_BUZZER_AAN  = 3;
const uint8_t ACTIE_BUZZER_UIT  = 4;

// ====================================================================
// WHITELIST
// ====================================================================
const char *toegelatenBeacons[] = {
  "48:87:2d:9d:bb:7d",
  "48:87:2d:9d:ba:5c",
  "48:87:2d:9d:ba:cc",
  "48:87:2d:9d:ba:5f",
  "48:87:2d:9d:bb:0b",
  "48:87:2d:9d:ba:a5",
};
const int aantalBeacons = 6;

// ====================================================================
// MAC ADRES MASTER
// ====================================================================
uint8_t masterAddress[] = { 0xF0, 0x24, 0xF9, 0x5A, 0x01, 0x90 };

// ====================================================================
// DATASTRUCTS
// ====================================================================
typedef struct __attribute__((packed)) batch_message {
  int32_t paal_id;
  int32_t aantalGevonden;
  float   batterij_v;       // gemeten batterijspanning (0.0 = niet gemeten)
  struct {
    char speler_mac[18];
    int32_t rssi;
  } spelers[9];
} batch_message;
batch_message batchData;

typedef struct __attribute__((packed)) commando_message {
  int32_t paal_id;
  uint8_t actie_id;
} commando_message;

// ====================================================================
// TOESTANDSVARIABELEN
// ====================================================================
volatile bool commandoOntvangen = false;
volatile uint8_t ontvangenActie = ACTIE_NIETS;

NimBLEScan *pBLEScan = nullptr;

// ====================================================================
// ESP-NOW CALLBACKS
// ====================================================================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("[ESP-NOW] Batch verzonden OK");
  } else {
    Serial.println("[ESP-NOW] Verzending MISLUKT");
  }
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len < (int)sizeof(commando_message)) {
    Serial.println("[ESP-NOW] Commando te kort");
    return;
  }
  commando_message ontvangen;
  memcpy(&ontvangen, incomingData, sizeof(ontvangen));

  Serial.printf("[ESP-NOW] Commando voor paal %d, actie %d\n",
                ontvangen.paal_id, ontvangen.actie_id);

  if (ontvangen.paal_id == PAAL_ID) {
    ontvangenActie = ontvangen.actie_id;
    commandoOntvangen = true;
  }
}

// ====================================================================
// BLE RESULTATEN
// ====================================================================
class BeaconZoeker : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *apparaat) override {
    std::string gevondenMac = apparaat->getAddress().toString();
    int rssi = apparaat->getRSSI();

    // Alleen whitelisted beacons doorlaten
    bool whitelisted = false;
    for (int i = 0; i < aantalBeacons; i++) {
      if (gevondenMac == toegelatenBeacons[i]) {
        whitelisted = true;
        break;
      }
    }
    if (!whitelisted) return;

    // Dedup binnen deze batch: dezelfde MAC mag maar één keer voorkomen.
    // Een beacon adverteert meerdere keren per seconde — zonder dedup zou
    // batchData[] volstromen met duplicaten van dezelfde speler.
    // Bestaat de MAC al? Behoud de sterkste RSSI van deze scan.
    for (int i = 0; i < batchData.aantalGevonden; i++) {
      if (gevondenMac == batchData.spelers[i].speler_mac) {
        if (rssi > batchData.spelers[i].rssi) {
          batchData.spelers[i].rssi = rssi;
        }
        return;
      }
    }

    // Nieuwe MAC: toevoegen als er nog plek is.
    if (batchData.aantalGevonden < 9) {
      strncpy(
        batchData.spelers[batchData.aantalGevonden].speler_mac,
        gevondenMac.c_str(), 17);
      batchData.spelers[batchData.aantalGevonden].speler_mac[17] = '\0';
      batchData.spelers[batchData.aantalGevonden].rssi = rssi;
      batchData.aantalGevonden++;
      Serial.printf("[BLE] Whitelisted: %s RSSI: %d\n",
                    gevondenMac.c_str(), rssi);
    }
  }
};

// ====================================================================
// BATTERIJ CHECK
// ====================================================================
float leesBatterijSpanning() {
  // analogReadMilliVolts() gebruikt de fabriekskalibratie (eFuse) van de ADC.
  // Dat is veel nauwkeuriger dan analogRead() met een vaste 3.3V-referentie,
  // die op de ESP32-C3 structureel enkele procenten afwijkt (meet te laag).
  // Gemiddelde van 8 metingen tegen ruis.
  uint32_t som_mv = 0;
  for (int i = 0; i < 8; i++) {
    som_mv += analogReadMilliVolts(BATTERY_ADC_PIN);
  }
  float v_adc = (som_mv / 8.0f) / 1000.0f;   // mV -> V
  float v_batterij = v_adc * DIVIDER_FACTOR;
  return v_batterij;
}

void checkBatterij() {
  if (millis() - laatsteBattCheck < BATT_CHECK_INTERVAL) return;
  laatsteBattCheck = millis();

  float v_batt = leesBatterijSpanning();
  Serial.printf("[BATT] %.2fV\n", v_batt);

  if (v_batt < BATT_KRITIEK) {
    // Snel knipperen (2Hz)
    digitalWrite(WARNING_LED_PIN, (millis() / 250) % 2);
  } else if (v_batt < BATT_WAARSCHUWING) {
    // Langzaam knipperen (1Hz)
    digitalWrite(WARNING_LED_PIN, (millis() / 500) % 2);
  } else {
    digitalWrite(WARNING_LED_PIN, LOW);
  }
}

// ====================================================================
// LICHT SENSOR CHECK (laser detectie)
// ====================================================================
float leesLichtSpanning() {
  int adc_raw = analogRead(LIGHT_SENSOR_PIN);
  float v_licht = (adc_raw / ADC_MAX_VALUE) * ADC_REF_VOLT;
  return v_licht;
}

void checkLichtSensor() {
  if (millis() - laatsteLichtCheck < LICHT_CHECK_INTERVAL) return;
  laatsteLichtCheck = millis();

  float v_licht = leesLichtSpanning();
  laserGedetecteerd = (v_licht > LICHT_DREMPEL);

  // Detecteer verandering
  if (laserGedetecteerd != vorigeLaserStatus) {
    if (laserGedetecteerd) {
      Serial.printf("[LASER] Gedetecteerd (%.2fV)\n", v_licht);
    } else {
      Serial.printf("[LASER] Straal verbroken (%.2fV)\n", v_licht);
    }
    vorigeLaserStatus = laserGedetecteerd;
  }
}

// ====================================================================
// ACTIE UITVOEREN
// ====================================================================
void voerActieUit(uint8_t actie) {
  switch (actie) {
    case ACTIE_ROOD:
      Serial.println("[ACTIE] LED strip ROOD");
      digitalWrite(MOSFET_PIN, HIGH);
      delay(5);
      fill_solid(leds, NUM_LEDS, CRGB::Red);
      FastLED.show();
      break;

    case ACTIE_GROEN:
      Serial.println("[ACTIE] LED strip GROEN");
      digitalWrite(MOSFET_PIN, HIGH);
      delay(5);
      fill_solid(leds, NUM_LEDS, CRGB::Green);
      FastLED.show();
      break;

    case ACTIE_NIETS:
      Serial.println("[ACTIE] LEDs uit");
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
      delay(5);
      digitalWrite(MOSFET_PIN, LOW);
      break;

    case ACTIE_BUZZER_AAN:
      Serial.println("[ACTIE] Buzzer AAN");
      buzzerActief = true;
      tone(BUZZER_PIN, BUZZER_FREQ);
      break;

    case ACTIE_BUZZER_UIT:
      Serial.println("[ACTIE] Buzzer UIT");
      buzzerActief = false;
      noTone(BUZZER_PIN);
      break;

    default:
      break;
  }
}

// ====================================================================
// SETUP
// ====================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // ADC configuratie voor ESP32-C3
  analogReadResolution(12);  // 12-bit (0-4095)

  // LED strip init
  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  // MOSFET pin
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW);

  // Buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  // Waarschuwings-LED
  pinMode(WARNING_LED_PIN, OUTPUT);
  digitalWrite(WARNING_LED_PIN, LOW);

  // ADC pinnen (input mode is standaard)
  pinMode(BATTERY_ADC_PIN, INPUT);
  pinMode(LIGHT_SENSOR_PIN, INPUT);

  // Startup batterij check
  float v_start = leesBatterijSpanning();
  Serial.printf("[BATT] Start spanning: %.2fV\n", v_start);

  // NimBLE init
  Serial.println("[SETUP] NimBLE init...");
  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new BeaconZoeker(), true);
  pBLEScan->setActiveScan(false);
  pBLEScan->setInterval(80);
  pBLEScan->setWindow(40);
  Serial.println("[SETUP] NimBLE OK");

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  // Eenmalig het MAC-adres van deze slave (ESP32-C3 mini) tonen.
  // Noteer dit adres en zet het in de master's slaveAdressen[] array.
  Serial.println();
  Serial.println("============================================");
  Serial.print("  SLAVE MAC-ADRES : ");
  Serial.println(WiFi.macAddress());
  Serial.println("============================================");
  Serial.println();

  // ESP-NOW init
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init MISLUKT!");
    return;
  }

  // Kanaal instellen NA esp_now_init
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WIFI_KANAAL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  Serial.println("[SETUP] WiFi kanaal: " + String(WiFi.channel()));

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, masterAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ESP-NOW] Master toevoegen MISLUKT!");
    return;
  }

  Serial.printf("[ESP-NOW] Master: %02X:%02X:%02X:%02X:%02X:%02X\n",
    masterAddress[0], masterAddress[1], masterAddress[2],
    masterAddress[3], masterAddress[4], masterAddress[5]);

  Serial.println("=== Slave klaar, Paal ID: " + String(PAAL_ID) + " ===");
}

// ====================================================================
// LOOP
// ====================================================================
void loop() {
  // Monitor sensoren (niet-blokkerend)
 

  batchData.paal_id = PAAL_ID;
  batchData.aantalGevonden = 0;
  batchData.batterij_v = leesBatterijSpanning();
  commandoOntvangen = false;
  ontvangenActie = ACTIE_NIETS;

  Serial.println("\n[SCAN] Start...");

  BLEScanResults results = pBLEScan->start(SCAN_DUUR_S, false);
  pBLEScan->clearResults();
  delay(20);

  Serial.printf("[SCAN] Klaar, %d whitelisted gevonden (batt %.2fV)\n",
                batchData.aantalGevonden, batchData.batterij_v);

  // Random backoff: ontkoppelt de zendmomenten van meerdere slaves zodat ze
  // niet elke cyclus in fase blijven en elkaar wegdrukken. esp_random() is
  // een hardware-RNG, dus per bordje verschillend — geen randomSeed() nodig.
  uint32_t backoff = esp_random() % (MAX_BACKOFF_MS + 1);
  Serial.printf("[BACKOFF] %u ms\n", backoff);
  delay(backoff);

  // Altijd versturen, ook bij 0 spelers: zo weet de master (en het dashboard)
  // dat een leeg vak ook echt leeg is. Bij overslaan blijft de oude stand staan.
  Serial.printf("[SEND] Versturen naar master (%d spelers)...\n",
                batchData.aantalGevonden);

  esp_err_t result = esp_now_send(masterAddress,
                                   (uint8_t *)&batchData,
                                   sizeof(batchData));

  if (result != ESP_OK) {
    Serial.printf("[SEND] esp_now_send fout: %d\n", result);
  }

  unsigned long startWacht = millis();
  while (!commandoOntvangen && (millis() - startWacht < WACHT_TIMEOUT)) {
    checkBatterij();
    checkLichtSensor();
    delay(1);
  }

  if (commandoOntvangen) {
    Serial.printf("[CMD] Actie ontvangen: %d\n", ontvangenActie);
    voerActieUit(ontvangenActie);
  } else {
    Serial.println("[CMD] Timeout, geen commando");
  }

  delay(50);
}