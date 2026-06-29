#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <FastLED.h>
#include "esp_random.h"
#include "driver/gpio.h"   // IRAM-veilige gpio_get_level/gpio_set_level voor de knop-ISR
#include "esp_timer.h"     // IRAM-veilige esp_timer_get_time() voor knop-debounce

// ====================================================================
// PARAMETERS
// ====================================================================
const int SCAN_DUUR_S    = 1;
const int WACHT_TIMEOUT  = 200;
const int PAAL_ID        = 9;
const int WIFI_KANAAL    = 1;
const int MAX_BACKOFF_MS = 150;   // willekeurige zendvertraging (0..150ms)

// Per-paal buzzer-resonantiefrequentie (productiespreiding op de passieve piezo).
// Eén universele build: enkel PAAL_ID hierboven aanpassen, de buzzer-piep gebruikt
// automatisch de gekalibreerde frequentie voor díe paal uit onderstaande tabel.
// Vind de luidste waarde per bordje met het Node-RED dashboard "Buzzer-tuning"
// (stuurt actie 12 / MSG_BUZZER_TOON). Index = paal_id (1..24); index 0 ongebruikt.
const uint16_t BUZZER_FREQ_TABEL[25] = {
    0,
    2450, 1780, 2230, 1780, 1920, 2150, 1870, 1830,   // palen 1..8
    2080, 2100, 2030, 1940, 1950, 2240, 1910, 2280,   // palen 9..16
    1950, 2230, 2270, 1970, 2290, 2100, 1670, 2080    // palen 17..24
};

// ---- PROTOCOL v2 ----
#define MSG_BATCH        0x01   // slave -> master
#define MSG_COMMANDO     0x02   // master -> slave
#define MSG_CMD_ACK      0x03   // slave -> master, NA uitvoering
#define MSG_HEARTBEAT    0x04   // slave -> master, periodiek
#define MSG_FOUT         0x05   // slave -> master
#define MSG_KNOP         0x06   // slave -> master, bij druk
#define MSG_BUZZER_TOON  0x07   // master -> slave, buzzer-tuning (continue toon)
#define MSG_KLOKSLAG     0x08   // master -> slave, Klokslag-LED (teamkleur + helderheid + modus)

const uint8_t       FW_VERSIE            = 2;
const unsigned long HEARTBEAT_INTERVAL_S = 10;   // "ik leef"-interval

// Foutcodes (zie docs/protocol.md §3)
#define FOUT_BATT_KRITIEK   1
#define FOUT_ESPNOW_ZEND    2
#define FOUT_BLE_OVERFLOW   3

// ====================================================================
// PIN MAPPING (komt overeen met PCB schema)
// ====================================================================
#define LED_DATA_PIN     0   // WS2812B data via 330Ohm
#define MOSFET_PIN       1   // IRLZ44N gate via 220Ohm (10k pull-down) - PERMANENT AAN in setup(),
                             // niet per-actie geschakeld: low-side switch in de LED-massa, dus de
                             // strip heeft enkel massaretour zolang de gate HIGH is. "Uit" = CRGB::Black.
#define BUTTON_PIN       3   // Drukknop tussen 3V3 en GPIO3 (INPUT_PULLDOWN -> HIGH = ingedrukt)
#define BATTERY_ADC_PIN  4   // Spanningsdeler 2x 100k (ADC1)
#define BUZZER_PIN       5   // Passieve buzzer via 100Ohm (digitaal)
#define WARNING_LED_PIN  6   // GPIO6 - drukknop-feedback-LED (aan bij actief, uit zolang ingedrukt)
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
const float BATT_KRITIEK       = 3.2;   // V, onder deze waarde -> MSG_FOUT (batterij kritiek)
const unsigned long BATT_CHECK_INTERVAL = 5000;  // ms
unsigned long laatsteBattCheck = 0;

// ====================================================================
// DRUKKNOP (GPIO3 - tussen 3V3 en GPIO3, INPUT_PULLDOWN)
// ====================================================================
// Het framework is altijd actief, met of zonder fysieke knop: zonder knop
// houdt de pulldown de pin LOW -> geen valse triggers. Bij indrukken (HIGH)
// sturen we MSG_KNOP naar de master (drukknop-logica voor o.a. spellogica).
// Drukknop-feedback (kogelvrij tellen): de ISR vangt ELKE druk (ook tijdens de BLE-scan), telt
// cumulatief en stuurt de teller meerdere cycli opnieuw -> geen verloren drukken. GPIO6-LED brandt
// als de paal "actief" is (gewapend door Node-RED) en gaat uit zolang de knop ingedrukt is.
volatile bool     knopArmed    = false;        // paal actief gezet door Node-RED (ACTIE_KNOP_ARM)
volatile uint16_t knopTeller   = 0;            // cumulatieve druk-teller (reset bij ARM)
volatile uint8_t  knopResend   = 0;            // resterende herhaal-verzendingen van de teller
volatile int64_t  laatsteTelUs = 0;            // tijdstip laatste getelde druk (debounce contactdender)
const int64_t     KNOP_DEBOUNCE_US = 80000;    // 80 ms: dendert weg, laat snel mashen (~12/s) toe

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
//  0 | ACTIE_NIETS         | stop   | Alle 7 LEDs zwart (CRGB::Black); MOSFET blijft aan
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
// 12 | ACTIE_BUZZER_TOON   | buzzer | Buzzer-tuning: continue toon op instelbare freq (via MSG_BUZZER_TOON,
//    |                     |        | NIET via commando_message_v2). Loopt buiten de melodie-state-machine.
// 13 | ACTIE_TIJDBOM       | anim   | Tikkende tijdbom: korte rode flits ~2 Hz (ontmantel-paal, tijdbom-event)
// 14 | ACTIE_TORNADO       | kleur  | Donkergrijs continu (tornado-center; zuigt aanliggende uren naar zich toe)
// 15 | ACTIE_TORNADO_RAND  | anim   | Trage grijze pulse (aanliggend uur van een tornado)
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
const uint8_t ACTIE_BUZZER_TOON  = 12;
const uint8_t ACTIE_TIJDBOM      = 13;
const uint8_t ACTIE_TORNADO      = 14;
const uint8_t ACTIE_TORNADO_RAND = 15;
// 16 = Klokslag-LED: continu gerenderde teamkleur (solid/flikker/ademend) op basis van een
// MSG_KLOKSLAG-bericht. Geen FIFO/ACK; updateAnimatie() blijft tekenen tot een nieuw bericht.
const uint8_t ACTIE_KLOKSLAG     = 16;
// 17/18 = drukknop-feedback: ARM = paal actief (GPIO6-LED aan, teller=0), UIT = paal inactief (LED uit).
const uint8_t ACTIE_KNOP_ARM     = 17;
const uint8_t ACTIE_KNOP_UIT     = 18;
// 19 = regenboog-test: roterende regenboog over de 7 LEDs. Puur voor kleur-/LED-controle (via een
// Node-RED-inject). updateAnimatie() blijft tekenen tot een andere actie binnenkomt (bv. ACTIE_NIETS).
const uint8_t ACTIE_REGENBOOG    = 19;

// ====================================================================
// MELODIE STATE + NOTEN TABEL
// ====================================================================
struct Noot {
    uint16_t freq;   // Hz; 0 = pauze (noTone)
    uint16_t duur;   // ms; freq=0 EN duur=0 markeren het einde
};

// Buzzer-piep: één duidelijke toon bij het afroepen van een uur. Einde = {0,0}.
// Niet-const: setup() zet [0].freq op BUZZER_FREQ_TABEL[PAAL_ID] (per-paal kalibratie).
static Noot MELODIE_PIEP[] = {
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

// Buzzer-tuning (MSG_BUZZER_TOON): een CONTINUE toon, los van de melodie-state-machine.
// OnDataRecv (WiFi-task) zet enkel deze volatile waarden; de loop past tone()/noTone() toe
// (geen tone() in de callback). freq=0 betekent stoppen.
volatile uint16_t testToonFreq  = 0;       // gewenste continue toon-frequentie (Hz)
volatile bool     testToonDirty = false;   // er is een nieuwe waarde toe te passen

// Klokslag-LED (MSG_KLOKSLAG): OnDataRecv zet enkel deze volatile waarden; de loop past ze toe
// (verwerkKlokslag) en updateAnimatie() rendert continu (flikker/ademend). modus: 0/1/2/3.
volatile uint8_t klokslagR = 0, klokslagG = 0, klokslagB = 0;
volatile uint8_t klokslagHelderheid = 0;
volatile uint8_t klokslagModus = 3;        // default rust
volatile bool    klokslagDirty = false;

// Huidige LED-actie + starttijd, voor de geanimeerde acties (8 = nuke, 11 = oogst).
// updateAnimatie() leest deze en blijft tekenen tot een nieuwe actie binnenkomt.
volatile uint8_t huidigeActie = ACTIE_NIETS;
unsigned long actieStartMs = 0;

// ====================================================================
// FREERTOS MUTEX — beschermt FastLED.show() aanroepen
// ====================================================================
// Er is GEEN aparte animatietaak: alle LED-schrijvers (voerActieUit, updateAnimatie,
// verwerkKlokslag) draaien uit loop(); OnDataRecv zet enkel volatile vlaggen. De mutex is
// dus een (benigne) extra garantie tegen toekomstige concurrency, niet strikt nodig.
SemaphoreHandle_t xLedMutex = NULL;

// ====================================================================
// WHITELIST (v2): OUI-prefix + RSSI-drempel
// ====================================================================
// Geen hardcoded MAC-lijst meer (die vereiste herflashen bij elke beacon-wissel).
// We laten alleen MAC's door met het beacon-OUI-prefix (de eerste 3 bytes van de
// fabrikant) EN een RSSI boven de drempel. Zo vallen omstanders/telefoons (ander
// OUI of te zwak) weg en hoeft een beacon-wissel geen herflash.
const uint8_t BEACON_OUI[3] = { 0x48, 0x87, 0x2d };
const int8_t  RSSI_DREMPEL  = -85;   // dBm; zwakker dan dit wordt genegeerd

// ====================================================================
// MAC ADRES MASTER (afgeleid uit PAAL_ID)
// ====================================================================
// Eén bron van waarheid: de slave kiest zijn master-MAC op basis van PAAL_ID
// (1-8 -> master1, 9-16 -> master2, 17-24 -> master3). Niets extra per slave te
// configureren behalve PAAL_ID. masterAddress wordt in setup() gevuld.
uint8_t masterMacs[3][6] = {
  { 0xF0, 0x24, 0xF9, 0x5A, 0x01, 0x90 },   // master 1 (palen 1-8)
  { 0xF0, 0x24, 0xF9, 0x59, 0x7B, 0x80 },   // master 2 (palen 9-16)
  { 0xF0, 0x24, 0xF9, 0x59, 0x21, 0x24 },   // master 3 (palen 17-24)
};
uint8_t masterAddress[6];   // gevuld in setup() uit masterMacs[groep]

// ====================================================================
// DATASTRUCTS
// ====================================================================
#define MAX_SPELERS 30

typedef struct __attribute__((packed)) batch_message_v2 {
  uint8_t  msg_type;        // = MSG_BATCH
  uint8_t  paal_id;         // 1..24
  uint8_t  aantal;          // aantal spelers in deze batch (0..30)
  uint16_t batt_mv;         // batterijspanning in mV (0 = niet gemeten)
  struct {
    uint8_t mac[6];         // binair MAC-adres (big-endian, zoals weergegeven)
    int8_t  rssi;           // dBm
  } spelers[MAX_SPELERS];
} batch_message_v2;
static_assert(sizeof(batch_message_v2) <= 250, "batch_message_v2 te groot voor ESP-NOW");
batch_message_v2 batchData;

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
  uint8_t  ernst;           // 0 = info, 1 = waarschuwing, 2 = fout
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
  uint8_t  paal_id;
  uint8_t  r, g, b;         // teamkleur
  uint8_t  helderheid;      // 0..255 (engine schaalt al met voortgang P/H)
  uint8_t  modus;           // 0=owned/solid, 1=capturing/flikker, 2=frozen, 3=rust-ademend
} klokslag_message;

// ====================================================================
// TOESTANDSVARIABELEN
// ====================================================================
// Commando-ringbuffer (SPSC): OnDataRecv is de PRODUCENT (schrijft alleen cmdTail),
// loop()/verwerkCommandos() is de CONSUMENT (schrijft alleen cmdHead). Op de single-core
// C3 volstaat dit met volatile indices, zonder mutex. Zo gaat een commando dat binnenkomt
// tijdens voerActieUit()/delay() niet verloren (was: één variabele die bovenaan de loop
// gewist werd), en worden twee commando's in één cyclus allebei in volgorde uitgevoerd.
#define CMD_BUF_SLOTS 8
struct CmdItem { uint8_t actie; uint16_t seq; };
volatile CmdItem cmdBuf[CMD_BUF_SLOTS];
volatile uint8_t  cmdHead = 0;   // alleen loop schrijft (consument)
volatile uint8_t  cmdTail = 0;   // alleen OnDataRecv schrijft (producent)
volatile uint16_t cmdDrops = 0;  // commando's gedropt bij volle buffer
uint16_t          gemeldeCmdDrops = 0;             // laatst gemelde drop-stand

uint16_t          laatsteUitgevoerdeSeq = 0xFFFF;  // sentinel: nog niets uitgevoerd
unsigned long     laatsteHeartbeat  = 0;           // millis() laatste heartbeat
uint16_t          bleOverflowTeller = 0;           // >MAX_SPELERS in deze batch
bool              vorigeBattKritiek = false;       // voor fout-transitie batterij-kritiek

NimBLEScan *pBLEScan = nullptr;

// Forward-declaratie: de zend-helpers hieronder gebruiken de batterijmeting,
// die pas verderop gedefinieerd is.
float leesBatterijSpanning();

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
  // De slave ontvangt alleen MSG_COMMANDO. PRODUCENT van de ringbuffer: kort houden
  // (geen zware Serial-printf), valideren en in de buffer pushen. De consument
  // (verwerkCommandos) logt en voert uit.
  if (len < 1) return;

  // Buzzer-tuning: continue toon. Geen FIFO/ACK; zet enkel de volatile doelfrequentie
  // (de loop past tone()/noTone() toe — geen tone() in deze WiFi-callback).
  if (incomingData[0] == MSG_BUZZER_TOON) {
    if (len < (int)sizeof(buzzer_toon_message)) return;
    buzzer_toon_message bt;
    memcpy(&bt, incomingData, sizeof(bt));
    if (bt.paal_id != PAAL_ID) return;
    testToonFreq  = bt.freq_hz;
    testToonDirty = true;
    return;
  }

  // Klokslag-LED: teamkleur + helderheid + modus. Geen FIFO/ACK; zet de volatile staat
  // (de loop past ze toe via verwerkKlokslag(); updateAnimatie() rendert continu).
  if (incomingData[0] == MSG_KLOKSLAG) {
    if (len < (int)sizeof(klokslag_message)) return;
    klokslag_message km;
    memcpy(&km, incomingData, sizeof(km));
    if (km.paal_id != PAAL_ID) return;
    klokslagR = km.r; klokslagG = km.g; klokslagB = km.b;
    klokslagHelderheid = km.helderheid; klokslagModus = km.modus;
    klokslagDirty = true;
    return;
  }

  if (incomingData[0] != MSG_COMMANDO) return;
  if (len < (int)sizeof(commando_message_v2)) return;

  commando_message_v2 ontvangen;
  memcpy(&ontvangen, incomingData, sizeof(ontvangen));
  if (ontvangen.paal_id != PAAL_ID) return;

  uint8_t next = (cmdTail + 1) % CMD_BUF_SLOTS;
  if (next == cmdHead) {            // buffer vol -> nieuwste droppen
    cmdDrops++;
    return;
  }
  cmdBuf[cmdTail].actie = ontvangen.actie_id;
  cmdBuf[cmdTail].seq   = ontvangen.cmd_seq;
  cmdTail = next;
}

// ====================================================================
// ESP-NOW ZEND-HELPERS (slave -> master)
// ====================================================================
void stuurCmdAck(uint16_t seq, uint8_t status) {
  cmd_ack_message m = { MSG_CMD_ACK, (uint8_t)PAAL_ID, seq, status };
  esp_now_send(masterAddress, (uint8_t *)&m, sizeof(m));
}

void stuurHeartbeat() {
  heartbeat_message m;
  m.msg_type  = MSG_HEARTBEAT;
  m.paal_id   = PAAL_ID;
  m.batt_mv   = (uint16_t)(leesBatterijSpanning() * 1000.0f);
  m.uptime_s  = millis() / 1000;
  m.fw_versie = FW_VERSIE;
  esp_now_send(masterAddress, (uint8_t *)&m, sizeof(m));
}

void stuurFout(uint8_t ernst, uint8_t foutcode, uint32_t detail) {
  fout_message m = { MSG_FOUT, (uint8_t)PAAL_ID, ernst, foutcode, detail };
  esp_now_send(masterAddress, (uint8_t *)&m, sizeof(m));
}

void stuurKnop() {
  knop_message m = { MSG_KNOP, (uint8_t)PAAL_ID, knopTeller };
  esp_now_send(masterAddress, (uint8_t *)&m, sizeof(m));
}

// ====================================================================
// BLE RESULTATEN
// ====================================================================
class BeaconZoeker : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *apparaat) override {
    int rssi = apparaat->getRSSI();

    // Binair MAC ophalen. NimBLE bewaart het adres LSB-first (omgekeerd t.o.v. de
    // weergave); we draaien het om naar big-endian zodat mac[0..2] het OUI is.
    // NB: adres in een lokale var houden — getNative() wijst naar interne opslag.
    NimBLEAddress adres = apparaat->getAddress();
    const uint8_t *native = adres.getNative();
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = native[5 - i];

    // Whitelist v2: alleen het beacon-OUI-prefix EN sterk genoeg signaal doorlaten.
    if (mac[0] != BEACON_OUI[0] || mac[1] != BEACON_OUI[1] || mac[2] != BEACON_OUI[2]) return;
    if (rssi < RSSI_DREMPEL) return;

    // Dedup binnen deze batch: dezelfde MAC mag maar één keer voorkomen. Een beacon
    // adverteert meerdere keren per seconde — zonder dedup zou spelers[] volstromen
    // met duplicaten. Bestaat de MAC al? Behoud de sterkste RSSI van deze scan.
    for (int i = 0; i < batchData.aantal; i++) {
      if (memcmp(mac, batchData.spelers[i].mac, 6) == 0) {
        if ((int8_t)rssi > batchData.spelers[i].rssi) batchData.spelers[i].rssi = (int8_t)rssi;
        return;
      }
    }

    // Nieuwe MAC: toevoegen als er nog plek is; anders tellen voor een MSG_FOUT.
    if (batchData.aantal < MAX_SPELERS) {
      memcpy(batchData.spelers[batchData.aantal].mac, mac, 6);
      batchData.spelers[batchData.aantal].rssi = (int8_t)rssi;
      batchData.aantal++;
      Serial.printf("[BLE] Beacon %02x:%02x:%02x:%02x:%02x:%02x RSSI: %d\n",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], rssi);
    } else {
      bleOverflowTeller++;   // >MAX_SPELERS in dit vak -> fout na de scan
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

  // Fout-melding bij de transitie naar kritiek (niet elke check opnieuw spammen).
  bool kritiek = (v_batt < BATT_KRITIEK);
  if (kritiek && !vorigeBattKritiek) {
    stuurFout(2, FOUT_BATT_KRITIEK, (uint32_t)(v_batt * 1000.0f));
  }
  vorigeBattKritiek = kritiek;
}

// ====================================================================
// DRUKKNOP-ISR (GPIO3, CHANGE) — feedback-LED + kogelvrije teller
// ====================================================================
// Loopt ook tijdens de blokkerende BLE-scan (interrupt). Enkel actief als de paal
// "gewapend" is (knopArmed). Zolang ingedrukt -> GPIO6-LED uit; bij een druk (rising
// edge) telt knopTeller op en wordt de teller ~6 cycli lang opnieuw verstuurd (de loop
// doet de eigenlijke esp_now_send; ISR's blijven kort en doen geen radio-werk).
// IRAM_ATTR + gpio_*_level zijn IRAM-veilig.
void IRAM_ATTR knopISR() {
  if (!knopArmed) return;
  int pressed = gpio_get_level((gpio_num_t)BUTTON_PIN);            // 1 = ingedrukt
  gpio_set_level((gpio_num_t)WARNING_LED_PIN, pressed ? 0 : 1);   // LED-feedback (ongedebounced -> snel)
  if (pressed) {                                                  // tel met debounce tegen contactdender
    int64_t nu = esp_timer_get_time();
    if (nu - laatsteTelUs > KNOP_DEBOUNCE_US) { knopTeller++; knopResend = 6; laatsteTelUs = nu; }
  }
}

// Loop-helper: stuur de teller zolang er nog herhalingen gepland zijn (kogelvrij tegen radioverlies).
void serviceKnopVerzending() {
  if (knopResend > 0) {
    stuurKnop();
    knopResend--;
  }
}

// ====================================================================
// LED-AANSTURING (niet-blokkerend, elke loop)
// ====================================================================
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

// Buzzer-tuning: pas een nieuwe continue toon toe (MSG_BUZZER_TOON). tone() (geen duur)
// houdt aan tot een volgende waarde — ook tijdens de blokkerende BLE-scan. We zetten
// melodie.type op 0 zodat updateMelodie() de continue toon niet onderbreekt.
void verwerkTestToon() {
  if (!testToonDirty) return;
  testToonDirty = false;
  uint16_t f = testToonFreq;
  melodie.type = 0;
  if (f > 0) {
    tone(BUZZER_PIN, f);
    Serial.printf("[BUZZER] Test-toon %u Hz\n", f);
  } else {
    noTone(BUZZER_PIN);
    Serial.println("[BUZZER] Test-toon uit");
  }
}

// Klokslag-LED: pas een nieuw MSG_KLOKSLAG toe. Zet de actie op ACTIE_KLOKSLAG zodat
// updateAnimatie() de teamkleur continu rendert (flikker/ademend). MOSFET staat al permanent aan.
void updateAnimatie();   // forward-declaratie (gedefinieerd verderop)
void verwerkKlokslag() {
  if (!klokslagDirty) return;
  klokslagDirty = false;
  huidigeActie = ACTIE_KLOKSLAG;
  actieStartMs = millis();
  updateAnimatie();   // teken meteen het eerste frame
}

// ====================================================================
// LED-ANIMATIES (millis-gebaseerd, geen aparte task)
// ====================================================================
// Rendert frames voor de geanimeerde acties (8 = nuke-ring, 11 = oogst). Wordt
// vaak aangeroepen vanuit de wacht-loop; solid acties (0/1/2/4/9/10) doen hier niets.
void updateAnimatie() {
  if (huidigeActie != ACTIE_NUKE && huidigeActie != ACTIE_OOGST && huidigeActie != ACTIE_TIJDBOM && huidigeActie != ACTIE_TORNADO_RAND && huidigeActie != ACTIE_KLOKSLAG && huidigeActie != ACTIE_REGENBOOG) return;
  const unsigned long t = millis() - actieStartMs;

  if (huidigeActie == ACTIE_REGENBOOG) {
    // Kleur-/LED-test: spreidt de volledige kleurencirkel over de 7 LEDs (deltaHue = 255/7 ≈ 36) en
    // roteert hem traag. Elke LED doorloopt zo het hele spectrum -> dode LEDs, foute R/G/B-volgorde of
    // een scheve witbalans vallen meteen op.
    uint8_t startHue = (uint8_t)(millis() / 15);
    if (xSemaphoreTake(xLedMutex, pdMS_TO_TICKS(20))) {
      fill_rainbow(leds, NUM_LEDS, startHue, 255 / NUM_LEDS);
      FastLED.show();
      xSemaphoreGive(xLedMutex);
    }
    return;
  }

  if (huidigeActie == ACTIE_KLOKSLAG) {
    // Klokslag-LED: teamkleur op helderheid (engine schaalt al met P/H).
    // modus 0 owned = solid, 1 capturing = kaarsflikker, 2 frozen = solid, 3 rust = ademend dim wit.
    const uint8_t m = klokslagModus;
    CRGB kleur;
    if (m == 3) {
      uint8_t val = beatsin8(8, 18, 70);                 // zacht ademend dim wit
      kleur = CRGB(val, val, val);
    } else {
      uint8_t scale = klokslagHelderheid;
      if (m == 1) {                                      // capturing: kaarsflikker bovenop helderheid
        uint16_t fl = beatsin8(50, 200, 255);
        scale = (uint8_t)((uint16_t)klokslagHelderheid * fl / 255);
      }
      kleur = CRGB(klokslagR, klokslagG, klokslagB);
      kleur.nscale8_video(scale);
    }
    if (xSemaphoreTake(xLedMutex, pdMS_TO_TICKS(20))) {
      fill_solid(leds, NUM_LEDS, kleur);
      FastLED.show();
      xSemaphoreGive(xLedMutex);
    }
    return;
  }

  if (huidigeActie == ACTIE_TORNADO_RAND) {
    // Aanliggend tornado-uur: trage grijze pulse (rustige animatie).
    uint8_t val = beatsin8(12, 25, 110);
    if (xSemaphoreTake(xLedMutex, pdMS_TO_TICKS(20))) {
      fill_solid(leds, NUM_LEDS, CHSV(0, 0, val));
      FastLED.show();
      xSemaphoreGive(xLedMutex);
    }
    return;
  }

  if (huidigeActie == ACTIE_TIJDBOM) {
    // Tikkende tijdbom: korte rode flits ~2 Hz (ontmantel-paal van het tijdbom-event).
    bool aan = ((t % 500) < 120);
    CRGB kleur = aan ? CRGB(255, 30, 0) : CRGB(20, 0, 0);
    if (xSemaphoreTake(xLedMutex, pdMS_TO_TICKS(20))) {
      fill_solid(leds, NUM_LEDS, kleur);
      FastLED.show();
      xSemaphoreGive(xLedMutex);
    }
    return;
  }

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

  // --- Drukknop-feedback: raakt de WS2812B-strip/huidigeActie NIET ----
  if (actie == ACTIE_KNOP_ARM) {
    knopArmed = true; knopTeller = 0; knopResend = 2;   // teller=0 + verstuur zodat dashboard op 0 komt
    digitalWrite(WARNING_LED_PIN, (digitalRead(BUTTON_PIN) == HIGH) ? LOW : HIGH);
    Serial.printf("[KNOP] paal %d ACTIEF (LED aan)\n", PAAL_ID);
    return;
  }
  if (actie == ACTIE_KNOP_UIT) {
    knopArmed = false; digitalWrite(WARNING_LED_PIN, LOW);
    Serial.printf("[KNOP] paal %d inactief (LED uit)\n", PAAL_ID);
    return;
  }

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

  // --- Geanimeerde acties (8 = nuke-ring, 11 = oogst, 13 = tijdbom, 19 = regenboog): updateAnimatie() tekent ---
  if (actie == ACTIE_NUKE || actie == ACTIE_OOGST || actie == ACTIE_TIJDBOM || actie == ACTIE_TORNADO_RAND || actie == ACTIE_REGENBOOG) {
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
    case ACTIE_TORNADO:     kleur = CRGB( 40,  40,  45); break;   // donkergrijs (tornado-center)
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
    // MOSFET blijft aan: "uit" is puur software-zwart (CRGB::Black).
  } else {
    if (xSemaphoreTake(xLedMutex, pdMS_TO_TICKS(100))) {
      fill_solid(leds, NUM_LEDS, kleur);
      FastLED.show();
      FastLED.show();   // belt-and-suspenders: een solid wordt maar 1x getekend -> 2e show borgt de latch
      xSemaphoreGive(xLedMutex);
    }
  }
}

// ====================================================================
// COMMANDO-RINGBUFFER DRAINEN (consument)
// ====================================================================
// Draineert de ring in volgorde en voert elk commando uit. Wordt op meerdere
// punten in de loop aangeroepen (na de scan, na het zenden, in het luistervenster)
// zodat een commando nooit blijft hangen of gewist wordt. Idempotent op cmd_seq:
// een master-retry van een al uitgevoerd commando wordt niet opnieuw uitgevoerd,
// maar wél opnieuw bevestigd (MSG_CMD_ACK). ACK altijd NA de uitvoering.
void verwerkCommandos() {
  while (cmdHead != cmdTail) {
    uint8_t  actie = cmdBuf[cmdHead].actie;
    uint16_t seq   = cmdBuf[cmdHead].seq;
    cmdHead = (cmdHead + 1) % CMD_BUF_SLOTS;

    // Bekende commando-acties: 0..15 (LED/anim/buzzer-melodie). 12 (buzzer-toon) en 16 (klokslag)
    // komen via een eigen msg_type, niet via deze FIFO.
    uint8_t ackStatus = (actie <= ACTIE_TORNADO_RAND ||
                         actie == ACTIE_KNOP_ARM || actie == ACTIE_KNOP_UIT) ? 0 : 1;   // 1 = onbekende actie
    if (seq != laatsteUitgevoerdeSeq) {
      Serial.printf("[CMD] Actie %d uitvoeren (seq %u)\n", actie, seq);
      if (ackStatus == 0) voerActieUit(actie);
      laatsteUitgevoerdeSeq = seq;
    } else {
      Serial.printf("[CMD] Seq %u al uitgevoerd, alleen her-ACK\n", seq);
    }
    stuurCmdAck(seq, ackStatus);
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

  // MOSFET (low-side in de LED-massa): PERMANENT aan en nooit meer geschakeld.
  // De strip krijgt zijn massaretour enkel via het MOSFET-kanaal, dus dit moet HIGH
  // staan vóór de eerste FastLED.show() anders kan de strip niets aansturen.
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, HIGH);

  // LED strip init
  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  // Voeding-vangnet: schaalt de helderheid terug als de totale stroom een marginale 5V-rail
  // zou overschrijden (voorkomt brownout = "deels aan"). Stem 700mA af op je echte voeding.
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 700);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  // Buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
  // Per-paal kalibratie van de piep-toon: kies de gekalibreerde frequentie voor DEZE paal.
  MELODIE_PIEP[0].freq = (PAAL_ID >= 1 && PAAL_ID <= 24) ? BUZZER_FREQ_TABEL[PAAL_ID] : 1500;

  // Ingebouwde LED (active-LOW): uit bij start (HIGH = uit)
  pinMode(BUILTIN_LED_PIN, OUTPUT);
  digitalWrite(BUILTIN_LED_PIN, HIGH);

  // Drukknop (tussen 3V3 en GPIO3): interne pulldown zodat de pin zonder
  // aangesloten knop LOW blijft en er geen valse triggers ontstaan.
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);

  // Drukknop-feedback-LED (GPIO6): uit bij start; wordt aangestuurd door de knop-ISR.
  pinMode(WARNING_LED_PIN, OUTPUT);
  digitalWrite(WARNING_LED_PIN, LOW);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), knopISR, CHANGE);

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

  // Kies de master-MAC op basis van PAAL_ID (1-8 -> m1, 9-16 -> m2, 17-24 -> m3).
  int groep = (PAAL_ID <= 8) ? 0 : (PAAL_ID <= 16) ? 1 : 2;
  memcpy(masterAddress, masterMacs[groep], 6);
  Serial.printf("[SETUP] Paal %d -> master %d\n", PAAL_ID, groep + 1);

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
  verwerkTestToon(); // buzzer-tuning: continue toon toepassen indien gewijzigd
  verwerkKlokslag(); // Klokslag-LED toepassen indien gewijzigd
  updateMelodie();   // nootovergangen bijhouden voor BLE-scan start
  updateAnimatie();

  batchData.msg_type = MSG_BATCH;
  batchData.paal_id  = PAAL_ID;
  batchData.aantal   = 0;
  batchData.batt_mv  = (uint16_t)(leesBatterijSpanning() * 1000.0f);
  bleOverflowTeller  = 0;
  // GEEN flag-reset meer: pending commando's blijven in de ringbuffer staan tot ze
  // gedraineerd zijn, ongeacht waar in de cyclus ze binnenkwamen.

  Serial.println("\n[SCAN] Start...");

  BLEScanResults results = pBLEScan->start(SCAN_DUUR_S, false);
  pBLEScan->clearResults();
  delay(20);

  verwerkTestToon(); // buzzer-tuning: nieuwe toon die tijdens de scan binnenkwam toepassen
  verwerkKlokslag(); // Klokslag-LED die tijdens de scan binnenkwam toepassen
  updateMelodie();   // inhaal na BLE-scan (max ~1 s vertraging op nootovergang)
  updateAnimatie();
  verwerkCommandos();   // commando's die tijdens de scan binnenkwamen meteen afhandelen

  Serial.printf("[SCAN] Klaar, %d beacons gevonden (batt %u mV)\n",
                batchData.aantal, batchData.batt_mv);

  // Random backoff: ontkoppelt de zendmomenten van meerdere slaves zodat ze
  // niet elke cyclus in fase blijven en elkaar wegdrukken. esp_random() is
  // een hardware-RNG, dus per bordje verschillend — geen randomSeed() nodig.
  uint32_t backoff = esp_random() % (MAX_BACKOFF_MS + 1);
  Serial.printf("[BACKOFF] %u ms\n", backoff);
  delay(backoff);

  // Altijd versturen, ook bij 0 spelers: zo weet de master (en het dashboard)
  // dat een leeg vak ook echt leeg is. Bij overslaan blijft de oude stand staan.
  // VARIABELE LENGTE: enkel het gebruikte deel (header + aantal*7) gaat de lucht in —
  // 5 B bij een leeg vak i.p.v. altijd 215 B. Kortere frames = minder airtime =
  // betrouwbaarder op de single-antenne C3 direct na de BLE-scan.
  size_t batchLen = offsetof(batch_message_v2, spelers)
                  + (size_t)batchData.aantal * sizeof(batchData.spelers[0]);
  Serial.printf("[SEND] Versturen naar master (%d spelers, %u bytes)...\n",
                batchData.aantal, (unsigned)batchLen);

  esp_err_t result = esp_now_send(masterAddress,
                                   (uint8_t *)&batchData,
                                   batchLen);

  if (result != ESP_OK) {
    Serial.printf("[SEND] esp_now_send fout: %d\n", result);
    stuurFout(2, FOUT_ESPNOW_ZEND, (uint32_t)result);
  }

  // BLE-overflow (>MAX_SPELERS in dit vak): geen stille drop meer, maar een fout.
  if (bleOverflowTeller > 0) {
    Serial.printf("[BLE] Overflow: %u beacons boven %d genegeerd\n", bleOverflowTeller, MAX_SPELERS);
    stuurFout(1, FOUT_BLE_OVERFLOW, bleOverflowTeller);
  }

  // Periodieke heartbeat ("ik leef"), onafhankelijk van detecties.
  if (millis() - laatsteHeartbeat >= HEARTBEAT_INTERVAL_S * 1000UL) {
    laatsteHeartbeat = millis();
    stuurHeartbeat();
  }

  verwerkCommandos();   // commando's die net binnen het zendmoment kwamen meteen afhandelen

  // Luistervenster: blijf de ring draineren én de hardware servicen tot de timeout.
  unsigned long startWacht = millis();
  while (millis() - startWacht < WACHT_TIMEOUT) {
    checkBatterij();
    serviceKnopVerzending();   // kogelvrije teller: herhaal-verzendingen afhandelen
    updateIngebouwdeLed();
    verwerkTestToon();
    verwerkKlokslag();
    updateMelodie();
    updateAnimatie();
    verwerkCommandos();
    delay(1);
  }
  verwerkCommandos();   // laatste keer ná het venster

  // Drop-teller van de ringbuffer melden zodra hij toeneemt (diagnose).
  if (cmdDrops != gemeldeCmdDrops) {
    Serial.printf("[CMD] Ringbuffer-drops: %u\n", cmdDrops);
    gemeldeCmdDrops = cmdDrops;
  }

  delay(50);
}
