#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

const int scanTime = 1; // Hoelang één scanronde duurt (in seconden)
BLEScan* pBLEScan;      // Pointer naar het scanobject van de BLE-bibliotheek

// Wordt automatisch aangeroepen voor elk gevonden Bluetooth-apparaat
class BeaconZoeker : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice apparaat) {

        // getAddress() → toString() → c_str() → zet MAC-adres om naar Arduino String
        String gevondenMac = String(apparaat.getAddress().toString().c_str());

        Serial.print("[SCAN] Gevonden: ");
        Serial.print(gevondenMac);
        Serial.print(" | RSSI: ");
        Serial.println(apparaat.getRSSI()); // Onttrekt de signaalsterkte in dBm

        if (gevondenMac.equalsIgnoreCase("48:87:2D:9D:BB:7D")) {
            Serial.print(">>> BEACON GEVONDEN | RSSI: ");
            Serial.print(apparaat.getRSSI());
            Serial.println(" dBm");
        }
    }
};

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("[SETUP] Bluetooth wordt gestart...");

    BLEDevice::init(""); // Zet de Bluetooth-chip aan, lege string = geen naam uitzenden
    Serial.println("[SETUP] Bluetooth actief.");

    pBLEScan = BLEDevice::getScan();                                  // Vraagt scanobject op uit de bibliotheek
    pBLEScan->setAdvertisedDeviceCallbacks(new BeaconZoeker());       // Koppelt BeaconZoeker aan de scanner
    pBLEScan->setActiveScan(true);                                    // ESP32 luistert én vraagt extra data op
    pBLEScan->setInterval(100);                                       // Nieuwe scancyclus elke 62.5ms
    pBLEScan->setWindow(99);                                          // Luistert 61.875ms per cyclus
    Serial.println("[SETUP] Scanner geconfigureerd. Zoeken begint...");
}

void loop() {
    Serial.println("[LOOP] Nieuwe scanronde gestart...");
    pBLEScan->start(scanTime, false);  // Scant voor scanTime seconden, false = wacht tot klaar
    Serial.println("[LOOP] Scanronde klaar.");
    pBLEScan->clearResults();          // Wist geheugen na elke ronde
    delay(10);
}