#pragma once
// ====================================================================
// PAAL-MAC TABEL — één bron van waarheid voor slave én master
// ====================================================================
// Koppelt het WiFi-STA-MAC van elk slave-bordje aan zijn PAAL_ID.
//
//  - De SLAVE zoekt bij boot zijn EIGEN MAC op (esp_read_mac) -> PAAL_ID.
//    Zo past één en dezelfde binary op alle 24 borden: geen PAAL_ID meer
//    per bord aanpassen/compileren (verdwijnt het "verkeerd nummer geflasht"-risico).
//  - De MASTER vult hieruit zijn slaveAdressen[] voor zijn eigen paalbereik
//    (PAAL_MIN..PAAL_MAX). Palen die hier ontbreken blijven placeholder.
//
// Nieuw bord toevoegen:
//   1. Flash de (enige) slave-binary op het bord.
//   2. Lees het MAC uit de seriële banner  "SLAVE MAC-ADRES : xx:xx:xx:xx:xx:xx".
//   3. Voeg hieronder één regel  {{0x.., ...}, <paal>}  toe.
//   4. Herflash de master die dat paalbereik bedient.
//
// STATUS: palen 1, 2, 3, 9, 17 bekend. Nog te leveren (MAC's volgen van Nic):
//         palen 4-8, 10-16, 18-24.

#include <stdint.h>

struct PaalMac { uint8_t mac[6]; uint8_t paal; };

static const PaalMac PAAL_MACS[] = {
  {{0xAC, 0xA7, 0x04, 0xBD, 0x3A, 0x48},  1},
  {{0xAC, 0xA7, 0x04, 0xC0, 0xC6, 0x14},  2},
  {{0x8C, 0xFD, 0x49, 0x54, 0xC4, 0x38},  3},
  {{0xAC, 0xA7, 0x04, 0xC0, 0x7F, 0xC8},  9},
  {{0x8C, 0xFD, 0x49, 0x54, 0xDF, 0xF0}, 17},
  // {{0x.., 0x.., 0x.., 0x.., 0x.., 0x..},  4},   // <- voorbeeld: vul aan zodra bekend
};
static const int PAAL_MACS_N = (int)(sizeof(PAAL_MACS) / sizeof(PAAL_MACS[0]));
