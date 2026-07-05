// ============================================================
// MAC-lezer — ESP32-C3 SuperMini
// ------------------------------------------------------------
// Toont het WiFi-STA-MAC van dit bordje in de seriële monitor.
// Dit is EXACT het MAC dat de echte slave-firmware bij boot opzoekt
// in firmware/shared/paal_macs.h (esp_read_mac(..., ESP_MAC_WIFI_STA)),
// dus wat je hier ziet mag je 1-op-1 in die tabel zetten.
//
// Los hulpsketchje — raakt de echte slave-firmware niet aan.
// ============================================================
#include <Arduino.h>
#include "esp_mac.h"   // esp_read_mac(): eigen MAC uit eFuse, ook zonder WiFi-init

// Onboard LED van de C3 SuperMini (GPIO8, active-LOW). Knippert als "ik leef"-teken,
// zodat je ook ziet dat de sketch draait als de seriele monitor leeg blijft.
static const int LED_PIN = 8;

static void printMac() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);   // zelfde bron als de slave gebruikt

  // 1) Leesbaar
  char leesbaar[18];
  snprintf(leesbaar, sizeof(leesbaar), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // 2) Kant-en-klare regel voor firmware/shared/paal_macs.h
  char plakregel[64];
  snprintf(plakregel, sizeof(plakregel),
           "  {{0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}, <paal>},",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Serial.println();
  Serial.println("========================================");
  Serial.print(  "  SLAVE MAC-ADRES : ");
  Serial.println(leesbaar);
  Serial.println("  Voor paal_macs.h (vervang <paal>):");
  Serial.println(plakregel);
  Serial.println("========================================");
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);   // uit (active-LOW)
  Serial.begin(115200);
  delay(1000);   // zelfde patroon als de werkende slave: geef de USB-CDC tijd
                 // (GEEN 'while(!Serial)' — die blokkeert/gedraagt zich raar op de C3)
  printMac();
}

void loop() {
  // ~3 s knipperen als "ik leef"-teken, dan het MAC opnieuw printen zodat je het
  // altijd kunt aflezen, ook als je de monitor pas later opent.
  for (int i = 0; i < 6; i++) {
    digitalWrite(LED_PIN, LOW);  delay(120);   // aan
    digitalWrite(LED_PIN, HIGH); delay(380);   // uit
  }
  printMac();
}
