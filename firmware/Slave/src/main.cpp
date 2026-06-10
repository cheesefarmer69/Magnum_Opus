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
#define BUTTON_PIN       3   // Drukknop tussen 3V3 en GPIO3 (INPUT_PULLDOWN -> HIGH = ingedrukt)
#define BATTERY_ADC_PIN  4   // Spanningsdeler 2x 100k (ADC1)
#define BUZZER_PIN       5   // Passieve buzzer via 100Ohm (digitaal)
#define WARNING_LED_PIN  6   // Rode LED via 150Ohm (batterij-waarschuwing + drukknop-puls)
#define BUILTIN_LED_PIN  8   // Ingebouwde LED ESP32-C3 SuperMini (active-LOW) - knippert bij zenden

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
// piezo is het luidst rond zijn resonantiefrequentie (typisch 2-4 kHz). De
// buzzer-piep (ACTIE_BUZZER_PIEP) gebruikt 1500 Hz; pas dat desnoods aan in
// MELODIE_PIEP voor maximaal volume.

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
// DRUKKNOP (GPIO3 - tussen 3V3 en GPIO3, INPUT_PULLDOWN)
// ====================================================================
// Het framework is altijd actief, met of zonder fysieke knop: zonder knop
// houdt de pulldown de pin LOW -> geen valse triggers. Bij indrukken (HIGH)
// geven we een korte puls op de rode LED (GPIO6) en sturen we een serieele
// hook-regel naar de master (voor latere spellogica).
const unsigned long KNOP_DEBOUNCE_MS = 30;     // ontdender-tijd
const unsigned long KNOP_PULS_MS     = 150;    // duur rode-LED puls bij druk
bool          vorigeKnopStatus  = false;       // voor rising-edge detectie
unsigned long laatsteKnopWissel = 0;           // debounce-timer
unsigned long rodeLedPulsTot    = 0;           // millis() tot wanneer puls actief

// ====================================================================
// INGEBOUWDE LED (GPIO8, active-LOW) - knippert bij succesvolle zend
// ====================================================================
const unsigned long ZEND_KNIPPER_MS = 40;      // duur knipper bij geslaagde zend
volatile unsigned long ingebouwdeLedTot = 0;   // millis() tot wanneer LED aan

// ====================================================================
// ACTION IDs
// ====================================================================
// Minimale set: enkel acties die aan een spel-event hangen. De LED-toestanden
// (portaal, happy hour) worden centraal door Node-RED aangestuurd op basis van
// de actieve effecten; bij het verlopen van een effect stuurt Node-RED ACTIE_NIETS.
//
// ID | Constante           | Type   | Beschrijving
// ---|---------------------|--------|------------------------------------------
//  0 | ACTIE_NIETS         | stop   | Alles uit, MOSFET laag
//  1 | ACTIE_PORTAAL       | kleur  | Alle 7 LEDs continu paars (portaal-toestand)
//  2 | ACTIE_HAPPY_HOUR    | kleur  | Alle 7 LEDs continu goud (happy-hour-toestand)
//  3 | ACTIE_BUZZER_PIEP   | buzzer | 1x 1500 Hz, 600 ms (uur-afroep / zoemer-test)
//  4 | ACTIE_MEDICIJN      | kleur  | Alle 7 LEDs continu felroze (medicijn, ziekte-event)
//  5 | ACTIE_ZIEK_W3       | buzzer | Ziekenhuis-monitor-piep + 3 hartslagen (zieke: nog 3 events)
//  6 | ACTIE_ZIEK_W2       | buzzer | Ziekenhuis-monitor-piep + 2 hartslagen (nog 2 events)
//  7 | ACTIE_ZIEK_W1       | buzzer | Ziekenhuis-monitor-piep + 1 hartslag (nog 1 event)
//  8 | ACTIE_NUKE          | anim   | Pulserend radioactief geel<->groen (NUKE-ring)
//  9 | ACTIE_MN_OPEN       | kleur  | Zacht wit continu (middernacht-poort open)
// 10 | ACTIE_MN_DICHT      | kleur  | Rood continu (middernacht-poort dicht)
// 11 | ACTIE_OOGST         | anim   | Dramatische wit/rood-strobe (middernacht-oogst)
//
const uint8_t ACTIE_NIETS        = 0;
const uint8_t ACTIE_PORTAAL      = 1;
const uint8_t ACTIE_HAPPY_HOUR   = 2;
const uint8_t ACTIE_BUZZER_PIEP  = 3;
const uint8_t ACTIE_MEDICIJN     = 4;
const uint8_t ACTIE_ZIEK_W3      = 5;
const uint8_t ACTIE_ZIEK_W2      = 6;
const uint8_t ACTIE_ZIEK_W1      = 7;
const uint8_t ACTIE_NUKE         = 8;
const uint8_t ACTIE_MN_OPEN      = 9;
const uint8_t ACTIE_MN_DICHT     = 10;
const uint8_t ACTIE_OOGST        = 11;

// ====================================================================
// MELODIE STATE + NOTEN TABEL
// ====================================================================
struct Noot {
    uint16_t freq;   // Hz; 0 = pauze (noTone)
    uint16_t duur;   // ms; freq=0 EN duur=0 markeren het einde
};

// Buzzer-piep: één duidelijke toon bij het afroepen van een uur. Einde = {0,0}.
static const Noot MELODIE_PIEP[] = {
    {1500, 600},
    {   0,   0}
};

// Ziekte-waarschuwing: een ziekenhuis-monitor-piep (3 korte hoge piepjes) gevolgd door
// een "bonzend hart" (lub-dub) dat zo vaak klinkt als de speler nog events rest (3/2/1).
// De passieve buzzer is het luidst rond 2-4 kHz; de monitor zit op 2200 Hz, het hart wat lager.
// MONITOR = {2200,120},{0,160} x3, dan een korte rust voor het hart begint.
// HART (lub-dub) = {1200,90},{0,60},{900,90},{0,500} per slag.
static const Noot MELODIE_ZIEK_W3[] = {
    {2200,120},{0,160},{2200,120},{0,160},{2200,120},{0,400},
    {1200,90},{0,60},{900,90},{0,500},
    {1200,90},{0,60},{900,90},{0,500},
    {1200,90},{0,60},{900,90},{0,500},
    {   0,   0}
};
static const Noot MELODIE_ZIEK_W2[] = {
    {2200,120},{0,160},{2200,120},{0,160},{2200,120},{0,400},
    {1200,90},{0,60},{900,90},{0,500},
    {1200,90},{0,60},{900,90},{0,500},
    {   0,   0}
};
static const Noot MELODIE_ZIEK_W1[] = {
    {2200,120},{0,160},{2200,120},{0,160},{2200,120},{0,400},
    {1200,90},{0,60},{900,90},{0,500},
    {   0,   0}
};

struct MelodieState {
    uint8_t       type;     // 0 = inactief
    uint8_t       noot;     // huidige noot-index
    unsigned long startMs;  // millis() bij start huidige noot
};
MelodieState melodie = {0, 0, 0};

// Huidige LED-actie + starttijd, voor de geanimeerde acties (8 = nuke, 11 = oogst).
// updateAnimatie() leest deze en blijft tekenen tot een nieuwe actie binnenkomt.
volatile uint8_t huidigeActie = ACTIE_NIETS;
unsigned long actieStartMs = 0;

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
    // Visuele zend-indicator: knipper de ingebouwde LED kort (niet-blokkerend).
    ingebouwdeLedTot = millis() + ZEND_KNIPPER_MS;
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

// Batterij-knippermodus: 0 = uit, 1 = langzaam (1Hz), 2 = snel (2Hz).
// checkBatterij() bepaalt de modus periodiek; updateRodeLed() stuurt de LED
// elke loop zodat de drukknop-puls voorrang kan krijgen.
int battLedModus = 0;

void checkBatterij() {
  if (millis() - laatsteBattCheck < BATT_CHECK_INTERVAL) return;
  laatsteBattCheck = millis();

  float v_batt = leesBatterijSpanning();
  Serial.printf("[BATT] %.2fV\n", v_batt);

  if (v_batt < BATT_KRITIEK) {
    battLedModus = 2;        // snel knipperen
  } else if (v_batt < BATT_WAARSCHUWING) {
    battLedModus = 1;        // langzaam knipperen
  } else {
    battLedModus = 0;        // uit
  }
}

// ====================================================================
// DRUKKNOP CHECK (GPIO3, rising-edge met debounce)
// ====================================================================
// Detecteert een druk (LOW->HIGH). Zonder fysieke knop blijft de pin LOW via
// de interne pulldown -> nooit een valse trigger. Bij een druk: rode-LED puls
// + serieele hook-regel naar de master (voor latere spellogica).
void checkKnop() {
  bool knopNu = (digitalRead(BUTTON_PIN) == HIGH);
  if (knopNu != vorigeKnopStatus &&
      (millis() - laatsteKnopWissel) >= KNOP_DEBOUNCE_MS) {
    laatsteKnopWissel = millis();
    vorigeKnopStatus = knopNu;
    if (knopNu) {  // rising edge = ingedrukt
      rodeLedPulsTot = millis() + KNOP_PULS_MS;
      Serial.printf("{\"paal\":%d,\"knop\":1}\n", PAAL_ID);
    }
  }
}

// ====================================================================
// LED-AANSTURING (niet-blokkerend, elke loop)
// ====================================================================
// Rode LED (GPIO6): drukknop-puls heeft voorrang; anders batterij-status.
void updateRodeLed() {
  if (millis() < rodeLedPulsTot) {
    digitalWrite(WARNING_LED_PIN, HIGH);     // puls: vol aan
  } else if (battLedModus == 2) {
    digitalWrite(WARNING_LED_PIN, (millis() / 250) % 2);   // snel (2Hz)
  } else if (battLedModus == 1) {
    digitalWrite(WARNING_LED_PIN, (millis() / 500) % 2);   // langzaam (1Hz)
  } else {
    digitalWrite(WARNING_LED_PIN, LOW);
  }
}

// Ingebouwde LED (GPIO8, active-LOW): kort aan na een geslaagde zend.
void updateIngebouwdeLed() {
  digitalWrite(BUILTIN_LED_PIN, (millis() < ingebouwdeLedTot) ? LOW : HIGH);
}

// ====================================================================
// MELODIE SPELER (aanroepen vanuit wacht-loop, niet ISR-veilig)
// ====================================================================
// tone() is interrupt-gestuurd: de toon speelt door ook tijdens BLE-scan.
// updateMelodie() hoeft alleen de nootovergangen bij te houden.
// Roep het aan vanuit de wacht-loop (elke ~1 ms) en op andere geschikte punten.
static const Noot* getMelodieSequentie(uint8_t type) {
  switch (type) {
    case ACTIE_BUZZER_PIEP:  return MELODIE_PIEP;
    case ACTIE_ZIEK_W3:      return MELODIE_ZIEK_W3;
    case ACTIE_ZIEK_W2:      return MELODIE_ZIEK_W2;
    case ACTIE_ZIEK_W1:      return MELODIE_ZIEK_W1;
    default:                 return nullptr;
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
// LED-ANIMATIES (millis-gebaseerd, geen aparte task)
// ====================================================================
// Rendert frames voor de geanimeerde acties (8 = nuke-ring, 11 = oogst). Wordt
// vaak aangeroepen vanuit de wacht-loop; solid acties (0/1/2/4/9/10) doen hier niets.
void updateAnimatie() {
  if (huidigeActie != ACTIE_NUKE && huidigeActie != ACTIE_OOGST) return;
  const unsigned long t = millis() - actieStartMs;

  if (huidigeActie == ACTIE_NUKE) {
    // Pulserend radioactief: geel<->groen, helderheid ademt via een sinus.
    uint8_t hue = 64 + (uint8_t)(beatsin8(20, 0, 32));   // 64=groen .. 96 richting geel-groen
    uint8_t val = beatsin8(40, 60, 255);                 // ademende helderheid
    if (xSemaphoreTake(xLedMutex, pdMS_TO_TICKS(20))) {
      fill_solid(leds, NUM_LEDS, CHSV(hue, 255, val));
      FastLED.show();
      xSemaphoreGive(xLedMutex);
    }
  } else {  // ACTIE_OOGST
    // Eerste ~3 s felle wit/rood-strobe, daarna een rustige rode gloed.
    CRGB kleur;
    if (t < 3000) {
      bool wit = ((t / 90) % 2) == 0;                    // ~11 Hz strobe
      kleur = wit ? CRGB(255, 255, 255) : CRGB(200, 0, 0);
    } else {
      uint8_t val = beatsin8(15, 20, 120);               // trage rode gloed
      kleur = CHSV(0, 255, val);
    }
    if (xSemaphoreTake(xLedMutex, pdMS_TO_TICKS(20))) {
      fill_solid(leds, NUM_LEDS, kleur);
      FastLED.show();
      xSemaphoreGive(xLedMutex);
    }
  }
}

// ====================================================================
// ACTIE UITVOEREN
// ====================================================================
// Kleuren/ACTIE_NIETS: neem mutex, teken direct, geef mutex.
// Buzzer-melodie (piep / ziekte-waarschuwingen): start eerste noot direct; updateMelodie() doet de rest.
void voerActieUit(uint8_t actie) {

  // --- Buzzer-melodieën (3 = piep, 5/6/7 = ziekte-waarschuwing) --------
  const Noot* seq = getMelodieSequentie(actie);
  if (seq) {
    Serial.printf("[ACTIE] Buzzer-melodie %d\n", actie);
    melodie.type    = actie;
    melodie.noot    = 0;
    melodie.startMs = millis();
    if (seq[0].freq > 0) tone(BUZZER_PIN, seq[0].freq);
    return;   // buzzer-acties wijzigen huidigeActie (LED-staat) NIET
  }

  // Vanaf hier is het een LED-actie: onthoud ze voor updateAnimatie() en de geanimeerde acties.
  huidigeActie = actie;
  actieStartMs = millis();

  // --- Geanimeerde acties (8 = nuke-ring, 11 = oogst): MOSFET aan, updateAnimatie() tekent ---
  if (actie == ACTIE_NUKE || actie == ACTIE_OOGST) {
    digitalWrite(MOSFET_PIN, HIGH);
    delay(5);
    Serial.printf("[ACTIE] Animatie %d\n", actie);
    updateAnimatie();   // teken meteen het eerste frame
    return;
  }

  // --- Solid LED-toestanden (0 uit, 1 paars, 2 goud, 4 felroze, 9 wit, 10 rood) ---
  CRGB kleur;
  switch (actie) {
    case ACTIE_PORTAAL:     kleur = CRGB(128,   0, 255); break;   // paars
    case ACTIE_HAPPY_HOUR:  kleur = CRGB(255, 180,   0); break;   // goud
    case ACTIE_MEDICIJN:    kleur = CRGB(255,  20, 147); break;   // felroze (deep pink)
    case ACTIE_MN_OPEN:     kleur = CRGB(180, 200, 255); break;   // zacht wit (poort open)
    case ACTIE_MN_DICHT:    kleur = CRGB(220,   0,   0); break;   // rood (poort dicht)
    case ACTIE_NIETS:
    default:                kleur = CRGB::Black;          break;
  }

  Serial.printf("[ACTIE] LED %d\n", actie);

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

  // Waarschuwings-LED (gedeeld: batterij-waarschuwing + drukknop-puls)
  pinMode(WARNING_LED_PIN, OUTPUT);
  digitalWrite(WARNING_LED_PIN, LOW);

  // Ingebouwde LED (active-LOW): uit bij start (HIGH = uit)
  pinMode(BUILTIN_LED_PIN, OUTPUT);
  digitalWrite(BUILTIN_LED_PIN, HIGH);

  // Drukknop (tussen 3V3 en GPIO3): interne pulldown zodat de pin zonder
  // aangesloten knop LOW blijft en er geen valse triggers ontstaan.
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);

  // ADC pin (input mode is standaard)
  pinMode(BATTERY_ADC_PIN, INPUT);

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

  // FreeRTOS mutex aanmaken (beschermt FastLED.show() aanroepen)
  xLedMutex = xSemaphoreCreateMutex();

  Serial.println("=== Slave klaar, Paal ID: " + String(PAAL_ID) + " ===");
}

// ====================================================================
// LOOP
// ====================================================================
void loop() {
  updateMelodie();   // nootovergangen bijhouden voor BLE-scan start
  updateAnimatie();

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
  updateAnimatie();

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
    checkKnop();
    updateRodeLed();
    updateIngebouwdeLed();
    updateMelodie();
    updateAnimatie();
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
