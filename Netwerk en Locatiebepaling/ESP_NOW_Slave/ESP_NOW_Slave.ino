// BLEDevice: beheert de Bluetooth-chip
// BLEScan: verzorgt het scanproces
// BLEAdvertisedDevice: vertegenwoordigt elk gevonden apparaat als een object
// esp_now: draadloos communicatieprotocol zonder router
// WiFi: nodig om de WiFi antenne te activeren voor ESP-NOW
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_now.h>
#include <WiFi.h>

// MAC-adres van de Master, hiernaar worden alle batches verstuurd
uint8_t masterAddress[] = {0xF0, 0x24, 0xF9, 0x59, 0x21, 0x24};

// Whitelist: alleen beacons met deze MAC-adressen worden doorgestuurd
// Nieuwe beacon toevoegen = één regel toevoegen + aantalBeacons verhogen
const char* toegelatenBeacons[] = {
    "48:87:2D:9D:BB:7D",
    "48:87:2D:9D:BA:5C",
};
const int aantalBeacons = 2;

// Batch formulier: één pakket dat meerdere beacons tegelijk naar de Master stuurt
// Maximum 11 spelers per pakket op basis van de 250 byte ESP-NOW limiet
typedef struct batch_message {
    int paal_id;
    int aantalGevonden;
    struct {
        char speler_mac[18];
        int rssi;
    } spelers[9];
} batch_message;
batch_message batchData;

esp_now_peer_info_t peerInfo; // Adresboek waarin de Master wordt opgeslagen
BLEScan* pBLEScan;            // Pointer naar het scanobject van de BLE bibliotheek

// Random backoff: als een pakket mislukt wacht de Slave een willekeurige tijd
// voor hij opnieuw probeert, zodat botsingen met andere Slaves worden vermeden
unsigned long wachtStartTijd = 0;
int wachtTijd = 0;
bool moetHerverzenden = false;

// Wordt automatisch aangeroepen na elke verzendpoging
// Zet alleen een vlag, zodat loop() de hertransmissie afhandelt zonder de WiFi driver te blokkeren
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("Pakket niet geleverd");
        wachtStartTijd = millis();
        wachtTijd = random(10, 200);
        moetHerverzenden = true;
    }
}

// Wordt automatisch aangeroepen voor elk gevonden Bluetooth apparaat
// Controleert of het apparaat op de whitelist staat en voegt het toe aan de batch
class BeaconZoeker : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice apparaat) override {
        String gevondenMac = String(apparaat.getAddress().toString().c_str());

        // Vergelijk gevonden MAC met elke beacon op de whitelist
        for (int i = 0; i < aantalBeacons; i++) {
            if (gevondenMac.equalsIgnoreCase(toegelatenBeacons[i])) {

                // Voeg toe aan batch zolang het maximum van 11 niet bereikt is
                if (batchData.aantalGevonden < 11) {
                    gevondenMac.toCharArray(
                        batchData.spelers[batchData.aantalGevonden].speler_mac, 18
                    );
                    batchData.spelers[batchData.aantalGevonden].rssi = apparaat.getRSSI();
                    batchData.aantalGevonden++;
                }
                break; // Beacon gevonden, stop met zoeken in de lijst
            }
        }
    }
};

void setup() {
    Serial.begin(115200);
    delay(3000);

    // WiFi antenne activeren en ESP-NOW opstarten
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init mislukt"); return; }
    esp_now_register_send_cb(OnDataSent); // Koppel de verzend-callback

    // Master toevoegen aan het adresboek zodat ESP-NOW weet waar naartoe te sturen
    memcpy(peerInfo.peer_addr, masterAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) { Serial.println("Master toevoegen mislukt"); return; }

    // Bluetooth scanner opstarten en configureren
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new BeaconZoeker());
    pBLEScan->setActiveScan(false); // Passief scannen volstaat voor MAC + RSSI
    pBLEScan->setInterval(100);     // Nieuwe scancyclus elke 62.5ms
    pBLEScan->setWindow(99);        // Luistert 61.875ms per cyclus
    Serial.println("Slave klaar.");
}

void loop() {
    // Reset batch zodat elke scanronde met een lege lijst begint
    batchData.paal_id = 1;
    batchData.aantalGevonden = 0;

    // Scan 1 seconde, BeaconZoeker vult de batch tijdens het scannen
    pBLEScan->start(1, false);
    pBLEScan->clearResults();

    // Stuur de batch naar de Master, maar alleen als er iets gevonden werd
    if (batchData.aantalGevonden > 0) {
        esp_now_send(masterAddress, (uint8_t *) &batchData, sizeof(batchData));
    }

    // Als een vorige verzending mislukte en de wachttijd verstreken is, probeer opnieuw
    if (moetHerverzenden && (millis() - wachtStartTijd >= wachtTijd)) {
        moetHerverzenden = false;
        esp_now_send(masterAddress, (uint8_t *) &batchData, sizeof(batchData));
    }

    delay(10);
}