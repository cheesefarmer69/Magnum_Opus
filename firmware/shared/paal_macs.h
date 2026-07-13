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
// STATUS: alle 24 palen ingevuld.
//   - Palen 4, 5, 8 en 23 kregen in juli 2026 een NIEUW bordje (de oude waren defect:
//     brownout op batterij / geen BLE-detectie). Hun MAC's hieronder zijn die van de
//     vervangborden — de oude MAC's zijn vervallen en komen nergens meer voor.
//   - Palen 10-16, 18-22 en 24 zijn MACHINAAL uitgelezen (esptool via tools/lees-mac.ps1);
//     hun regels staan byte-identiek in tools/paal-macs-verzameld.txt.
//   - Palen 1, 2, 3, 9, 17 stammen uit de eerste versie van deze tabel; 6 en 7 zijn
//     handmatig ingevoerd maar in het veld correct gebleken.
//
// LET OP bij het wisselen van een bord: naast deze regel moet je ook de SLAVE (nieuwe
// binary) en de MASTER van dat paalbereik herflashen — de master bouwt zijn peer-tabel
// (slaveAdressen[]) uit deze header. Zonder master-herflash dropt hij de nieuwe slave
// in de ontvangst-gate ([GATE] Genegeerd).
//
// Symptoom bij een FOUT MAC hier: de slave vindt zichzelf niet -> PAAL_ID 0 ->
// rode fout-blink (~4 Hz) en het bord doet volledig niet mee. Geen blink = MAC klopt.

#include <stdint.h>

struct PaalMac { uint8_t mac[6]; uint8_t paal; };

static const PaalMac PAAL_MACS[] = {
  {{0xAC, 0xA7, 0x04, 0xBD, 0x3A, 0x48},  1},
  {{0xAC, 0xA7, 0x04, 0xC0, 0xC6, 0x14},  2},
  {{0x8C, 0xFD, 0x49, 0x54, 0xC4, 0x38},  3},
  {{0x8C, 0xFD, 0x49, 0x54, 0xBC, 0x10},  4},
  {{0xAC, 0xA7, 0x04, 0xD3, 0x01, 0x30},  5},
  {{0xAC, 0xA7, 0x04, 0xD5, 0x2A, 0xA0},  6},
  {{0xAC, 0xA7, 0x04, 0xBF, 0xB5, 0xA8},  7},
  {{0xAC, 0xA7, 0x04, 0xBF, 0x4D, 0x0C},  8},
  {{0xAC, 0xA7, 0x04, 0xC0, 0x7F, 0xC8},  9},
  {{0xAC, 0xA7, 0x04, 0xBC, 0xB5, 0x54}, 10},
  {{0xAC, 0xA7, 0x04, 0xC0, 0x79, 0xDC}, 11},
  {{0xAC, 0xA7, 0x04, 0xBE, 0x6D, 0x78}, 12},
  {{0x8C, 0xFD, 0x49, 0x54, 0xCB, 0x48}, 13},
  {{0xAC, 0xA7, 0x04, 0xBB, 0xF5, 0x78}, 14},
  {{0xAC, 0xA7, 0x04, 0xD2, 0xD5, 0x4C}, 15},
  {{0xAC, 0xA7, 0x04, 0xD4, 0x9A, 0xA8}, 16},
  {{0x8C, 0xFD, 0x49, 0x54, 0xDF, 0xF0}, 17},
  {{0x8C, 0xFD, 0x49, 0x54, 0xC9, 0xE4}, 18},
  {{0xAC, 0xA7, 0x04, 0xBD, 0x25, 0xB8}, 19},
  {{0x8C, 0xFD, 0x49, 0x55, 0x14, 0xE8}, 20},
  {{0xAC, 0xA7, 0x04, 0xB8, 0x96, 0xC8}, 21},
  {{0xAC, 0xA7, 0x04, 0xB9, 0xD6, 0x90}, 22},
  {{0xAC, 0xA7, 0x04, 0xBE, 0x60, 0x28}, 23},
  {{0xAC, 0xA7, 0x04, 0xBA, 0x60, 0x18}, 24},
};
static const int PAAL_MACS_N = (int)(sizeof(PAAL_MACS) / sizeof(PAAL_MACS[0]));
