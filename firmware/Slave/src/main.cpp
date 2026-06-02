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
const int BUZZER_FREQ = 2060;   // Hz — continue toon (ACTIE_BUZZER_AAN)
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
//
// ID  | Constante                  | Type      | Beschrijving
// ----|----------------------------|-----------|-----------------------------------
//  0  | ACTIE_NIETS               | stop      | Alles uit, MOSFET laag
//  1  | ACTIE_ROOD                | kleur     | Alle 7 LEDs rood
//  2  | ACTIE_GROEN               | kleur     | Alle 7 LEDs groen
//  3  | ACTIE_BUZZER_AAN          | buzzer    | Continue toon 2060 Hz
//  4  | ACTIE_BUZZER_UIT          | buzzer    | Buzzer uit
//  5  | ACTIE_BLAUW               | kleur     | Alle 7 LEDs blauw
//  6  | ACTIE_WIT                 | kleur     | Alle 7 LEDs wit
//  7  | ACTIE_GEEL                | kleur     | Alle 7 LEDs geel
//  8  | ACTIE_PAARS               | kleur     | Alle 7 LEDs paars
//  9  | ACTIE_CYAAN               | kleur     | Alle 7 LEDs cyaan
// 10  | ACTIE_ORANJE              | kleur     | Alle 7 LEDs oranje
// 11  | ACTIE_KNIPPEREN_SNEL      | animatie  | Wit aan/uit, 4 Hz
// 12  | ACTIE_KNIPPEREN_TRAAG     | animatie  | Wit aan/uit, 1 Hz
// 13  | ACTIE_PULSEREN_ROOD       | animatie  | Rood breathing, 0-255-0, ~2s cyclus
// 14  | ACTIE_PULSEREN_BLAUW      | animatie  | Blauw breathing, 0-255-0, ~2s cyclus
// 15  | ACTIE_REGENBOOG           | animatie  | Hue rotatie, alle LEDs zelfde kleur, 3s
// 16  | ACTIE_POLITIE             | animatie  | Links rood / rechts blauw, afwisselend 8 Hz
// 17  | ACTIE_MELODIE_EEN_PIEP    | melodie   | 1x 1000 Hz, 200 ms
// 18  | ACTIE_MELODIE_TWEE_PIEP   | melodie   | 2x 1000 Hz, 100 ms aan / 100 ms pauze
// 19  | ACTIE_MELODIE_OPLOPEND    | melodie   | 3 noten oplopend: 500-750-1000 Hz, 150 ms elk
// 20  | ACTIE_MELODIE_AFLOPEND    | melodie   | 3 noten aflopend: 1000-750-500 Hz, 150 ms elk
// 21  | ACTIE_MELODIE_ALARM       | melodie   | 5x afwisselend 400/800 Hz, 100 ms per noot
// 22  | ACTIE_MELODIE_FANFARE     | melodie   | C4-E4-G4-E4-C4, 120 ms per noot
// 23  | ACTIE_BUZZER_PIEP         | buzzer    | 1x 1500 Hz, 600 ms (uur-afroep)
//
const uint8_t ACTIE_NIETS             =  0;
const uint8_t ACTIE_ROOD              =  1;
const uint8_t ACTIE_GROEN             =  2;
const uint8_t ACTIE_BUZZER_AAN        =  3;
const uint8_t ACTIE_BUZZER_UIT        =  4;
const uint8_t ACTIE_BLAUW             =  5;
const uint8_t ACTIE_WIT               =  6;
const uint8_t ACTIE_GEEL              =  7;
const uint8_t ACTIE_PAARS             =  8;
const uint8_t ACTIE_CYAAN             =  9;
const uint8_t ACTIE_ORANJE            = 10;
const uint8_t ACTIE_KNIPPEREN_SNEL    = 11;
const uint8_t ACTIE_KNIPPEREN_TRAAG   = 12;
const uint8_t ACTIE_PULSEREN_ROOD     = 13;
const uint8_t ACTIE_PULSEREN_BLAUW    = 14;
const uint8_t ACTIE_REGENBOOG         = 15;
const uint8_t ACTIE_POLITIE           = 16;
const uint8_t ACTIE_MELODIE_EEN_PIEP  = 17;
const uint8_t ACTIE_MELODIE_TWEE_PIEP = 18;
const uint8_t ACTIE_MELODIE_OPLOPEND  = 19;
const uint8_t ACTIE_MELODIE_AFLOPEND  = 20;
const uint8_t ACTIE_MELODIE_ALARM     = 21;
const uint8_t ACTIE_MELODIE_FANFARE   = 22;
const uint8_t ACTIE_BUZZER_PIEP       = 23;

// ====================================================================
// ANIMATIE STATE
// ====================================================================
// type = 0 betekent geen actieve animatie. De animatietaak doet niets.
// Tijdsberekening op basis van millis() — geen stap-teller nodig.
struct AnimatieState {
    uint8_t type;   // 0 = inactief, anders het actie-ID
};
AnimatieState animatie = {0};

// ====================================================================
// MELODIE STATE + NOTEN TABELLEN
// ====================================================================
struct Noot {
    uint16_t freq;   // Hz; 0 = pauze (noTone)
    uint16_t duur;   // ms; freq=0 EN duur=0 markeren het einde
};

// Einde-markering: {0, 0}
static const Noot MELODIE_EEN_PIEP[] = {
    {1000, 200},
    {   0,   0}
};
static const Noot MELODIE_TWEE_PIEP[] = {
    {1000, 100},
    {   0, 100},
    {1000, 100},
    {   0,   0}
};
static const Noot MELODIE_OPLOPEND[] = {
    { 500, 150},
    { 750, 150},
    {1000, 150},
    {   0,   0}
};
static const Noot MELODIE_AFLOPEND[] = {
    {1000, 150},
    { 750, 150},
    { 500, 150},
    {   0,   0}
};
static const Noot MELODIE_ALARM[] = {
    { 400, 100}, {800, 100},
    { 400, 100}, {800, 100},
    { 400, 100}, {800, 100},
    { 400, 100}, {800, 100},
    { 400, 100}, {800, 100},
    {   0,   0}
};
// C4=523 E4=659 G4=784
static const Noot MELODIE_FANFARE[] = {
    {523, 120},
    {659, 120},
    {784, 120},
    {659, 120},
    {523, 120},
    {  0,   0}
};
// Buzzer-piep: één duidelijke toon bij het afroepen van een uur
static const Noot MELODIE_PIEP[] = {
    {1500, 600},
    {   0,   0}
};

struct MelodieState {
    uint8_t       type;     // 0 = inactief
    uint8_t       noot;     // huidige noot-index
    unsigned long startMs;  // millis() bij start huidige noot
};
MelodieState melodie = {0, 0, 0};

// ====================================================================
// FREERTOS MUTEX — beschermt FastLED.show() aanroepen
// ====================================================================
// Animatietaak en voerActieUit() schrijven allebei naar de LED-strip.
// De mutex zorgt dat ze niet tegelijk FastLED.show() aanroepen.
SemaphoreHandle_t xLedMutex = NULL;

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
// LED ANIMATIES (aanroepen vanuit animatieTask met mutex genomen)
// ====================================================================
// Alle berekeningen zijn tijdsgebaseerd via millis(). Er is geen stap-teller
// nodig — de juiste frame volgt direct uit de absolute tijdspositie in de cyclus.
// FastLED.show() mag ALLEEN worden aangeroepen vanuit code die de xLedMutex
// houdt of vanuit setup() (voor de taak gestart is).
void updateAnimatie() {
  if (animatie.type == 0) return;

  switch (animatie.type) {

    case ACTIE_KNIPPEREN_SNEL: {
      // 4 Hz = 250 ms periode, 125 ms halve periode
      bool aan = (millis() % 250) < 125;
      fill_solid(leds, NUM_LEDS, aan ? CRGB::White : CRGB::Black);
      FastLED.show();
      break;
    }

    case ACTIE_KNIPPEREN_TRAAG: {
      // 1 Hz = 1000 ms periode, 500 ms halve periode
      bool aan = (millis() % 1000) < 500;
      fill_solid(leds, NUM_LEDS, aan ? CRGB::White : CRGB::Black);
      FastLED.show();
      break;
    }

    case ACTIE_PULSEREN_ROOD: {
      // Lineaire breathing: helderheid 0->255->0 in 2 s
      const uint16_t PERIODE_MS = 2000;
      uint16_t pos = (uint16_t)(millis() % PERIODE_MS);
      uint8_t bri = (pos < PERIODE_MS / 2)
                    ? (uint8_t)((pos * 255UL) / (PERIODE_MS / 2))
                    : (uint8_t)(((PERIODE_MS - pos) * 255UL) / (PERIODE_MS / 2));
      fill_solid(leds, NUM_LEDS, CRGB(bri, 0, 0));
      FastLED.show();
      break;
    }

    case ACTIE_PULSEREN_BLAUW: {
      const uint16_t PERIODE_MS = 2000;
      uint16_t pos = (uint16_t)(millis() % PERIODE_MS);
      uint8_t bri = (pos < PERIODE_MS / 2)
                    ? (uint8_t)((pos * 255UL) / (PERIODE_MS / 2))
                    : (uint8_t)(((PERIODE_MS - pos) * 255UL) / (PERIODE_MS / 2));
      fill_solid(leds, NUM_LEDS, CRGB(0, 0, bri));
      FastLED.show();
      break;
    }

    case ACTIE_REGENBOOG: {
      // Hue roteert 0-255 in 3 s, alle LEDs zelfde kleur
      const uint16_t PERIODE_MS = 3000;
      uint8_t hue = (uint8_t)((millis() % PERIODE_MS) * 256UL / PERIODE_MS);
      fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 255));
      FastLED.show();
      break;
    }

    case ACTIE_POLITIE: {
      // 8 Hz = 125 ms periode, 62 ms halve periode
      // Fase 0: LEDs 0-2 rood, LED 3 uit, LEDs 4-6 blauw
      // Fase 1: LEDs 0-2 blauw, LED 3 uit, LEDs 4-6 rood
      bool fase = (millis() % 125) < 62;
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i < 3)      leds[i] = fase ? CRGB::Red  : CRGB::Blue;
        else if (i > 3) leds[i] = fase ? CRGB::Blue : CRGB::Red;
        else            leds[i] = CRGB::Black;
      }
      FastLED.show();
      break;
    }

    default:
      break;
  }
}

// ====================================================================
// ANIMATIE TAAK (FreeRTOS, ~33 FPS)
// ====================================================================
void animatieTask(void *pvParameters) {
  while (true) {
    if (xSemaphoreTake(xLedMutex, portMAX_DELAY)) {
      updateAnimatie();
      xSemaphoreGive(xLedMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

// ====================================================================
// MELODIE SPELER (aanroepen vanuit wacht-loop, niet ISR-veilig)
// ====================================================================
// tone() is interrupt-gestuurd: de toon speelt door ook tijdens BLE-scan.
// updateMelodie() hoeft alleen de nootovergangen bij te houden.
// Roep het aan vanuit de wacht-loop (elke ~1 ms) en op andere geschikte punten.
static const Noot* getMelodieSequentie(uint8_t type) {
  switch (type) {
    case ACTIE_MELODIE_EEN_PIEP:   return MELODIE_EEN_PIEP;
    case ACTIE_MELODIE_TWEE_PIEP:  return MELODIE_TWEE_PIEP;
    case ACTIE_MELODIE_OPLOPEND:   return MELODIE_OPLOPEND;
    case ACTIE_MELODIE_AFLOPEND:   return MELODIE_AFLOPEND;
    case ACTIE_MELODIE_ALARM:      return MELODIE_ALARM;
    case ACTIE_MELODIE_FANFARE:    return MELODIE_FANFARE;
    case ACTIE_BUZZER_PIEP:        return MELODIE_PIEP;
    default:                        return nullptr;
  }
}

void updateMelodie() {
  if (melodie.type == 0) return;

  const Noot* seq = getMelodieSequentie(melodie.type);
  if (!seq) { melodie.type = 0; noTone(BUZZER_PIN); return; }

  // Huidige noot klaar?
  if (millis() - melodie.startMs < seq[melodie.noot].duur) return;

  // Volgende noot
  melodie.noot++;
  const Noot& n = seq[melodie.noot];

  // Einde-markering: freq=0 EN duur=0
  if (n.freq == 0 && n.duur == 0) {
    melodie.type = 0;
    noTone(BUZZER_PIN);
    return;
  }

  melodie.startMs = millis();
  if (n.freq == 0) {
    noTone(BUZZER_PIN);   // pauze-noot
  } else {
    tone(BUZZER_PIN, n.freq);
  }
}

// ====================================================================
// ACTIE UITVOEREN
// ====================================================================
// Vaste kleuren en ACTIE_NIETS: neem mutex, teken direct, geef mutex.
// Animaties: sla type op; updateAnimatie() in de taak doet de rest.
// Melodieën: start eerste noot direct; updateMelodie() doet de rest.
void voerActieUit(uint8_t actie) {

  // --- Vaste kleuren (0, 1, 2, 5-10) -----------------------------------
  if (actie == ACTIE_NIETS ||
      actie == ACTIE_ROOD || actie == ACTIE_GROEN ||
      (actie >= ACTIE_BLAUW && actie <= ACTIE_ORANJE)) {

    animatie.type = 0;   // stop lopende animatie

    CRGB kleur;
    switch (actie) {
      case ACTIE_NIETS:   kleur = CRGB::Black;                 break;
      case ACTIE_ROOD:    kleur = CRGB::Red;                   break;
      case ACTIE_GROEN:   kleur = CRGB::Green;                 break;
      case ACTIE_BLAUW:   kleur = CRGB::Blue;                  break;
      case ACTIE_WIT:     kleur = CRGB::White;                 break;
      case ACTIE_GEEL:    kleur = CRGB(255, 200, 0);           break;
      case ACTIE_PAARS:   kleur = CRGB(128, 0, 255);           break;
      case ACTIE_CYAAN:   kleur = CRGB::Cyan;                  break;
      case ACTIE_ORANJE:  kleur = CRGB(255, 80, 0);            break;
      default:            kleur = CRGB::Black;                 break;
    }

    Serial.printf("[ACTIE] LED kleur %d\n", actie);

    if (actie == ACTIE_NIETS) {
      if (xSemaphoreTake(xLedMutex, pdMS_TO_TICKS(100))) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        xSemaphoreGive(xLedMutex);
      }
      delay(5);
      digitalWrite(MOSFET_PIN, LOW);
    } else {
      digitalWrite(MOSFET_PIN, HIGH);
      delay(5);   // wacht op stabiele voedingsspanning voor WS2812B
      if (xSemaphoreTake(xLedMutex, pdMS_TO_TICKS(100))) {
        fill_solid(leds, NUM_LEDS, kleur);
        FastLED.show();
        xSemaphoreGive(xLedMutex);
      }
    }
    return;
  }

  // --- Animaties (11-16) -----------------------------------------------
  if (actie >= ACTIE_KNIPPEREN_SNEL && actie <= ACTIE_POLITIE) {
    Serial.printf("[ACTIE] Animatie %d gestart\n", actie);
    digitalWrite(MOSFET_PIN, HIGH);
    delay(5);
    animatie.type = actie;   // animatieTask pakt dit op
    return;
  }

  // --- Buzzer (3-4) ----------------------------------------------------
  if (actie == ACTIE_BUZZER_AAN) {
    Serial.println("[ACTIE] Buzzer AAN");
    buzzerActief = true;
    tone(BUZZER_PIN, BUZZER_FREQ);
    return;
  }
  if (actie == ACTIE_BUZZER_UIT) {
    Serial.println("[ACTIE] Buzzer UIT");
    buzzerActief = false;
    noTone(BUZZER_PIN);
    return;
  }

  // --- Melodieën (17-22) + buzzer-piep (23) ----------------------------
  if (actie >= ACTIE_MELODIE_EEN_PIEP && actie <= ACTIE_BUZZER_PIEP) {
    const Noot* seq = getMelodieSequentie(actie);
    if (!seq) return;

    Serial.printf("[ACTIE] Melodie %d gestart\n", actie);
    melodie.type    = actie;
    melodie.noot    = 0;
    melodie.startMs = millis();

    // Eerste noot direct starten
    if (seq[0].freq > 0) {
      tone(BUZZER_PIN, seq[0].freq);
    }
    return;
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

  // FreeRTOS mutex aanmaken en animatietaak starten
  xLedMutex = xSemaphoreCreateMutex();
  xTaskCreate(animatieTask, "anim", 2048, NULL, 1, NULL);
  Serial.println("[SETUP] Animatietaak gestart");

  Serial.println("=== Slave klaar, Paal ID: " + String(PAAL_ID) + " ===");
}

// ====================================================================
// LOOP
// ====================================================================
void loop() {
  updateMelodie();   // nootovergangen bijhouden voor BLE-scan start

  batchData.paal_id = PAAL_ID;
  batchData.aantalGevonden = 0;
  batchData.batterij_v = leesBatterijSpanning();
  commandoOntvangen = false;
  ontvangenActie = ACTIE_NIETS;

  Serial.println("\n[SCAN] Start...");

  BLEScanResults results = pBLEScan->start(SCAN_DUUR_S, false);
  pBLEScan->clearResults();
  delay(20);

  updateMelodie();   // inhaal na BLE-scan (max ~1 s vertraging op nootovergang)

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
    updateMelodie();
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
