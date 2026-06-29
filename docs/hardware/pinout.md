# Pinout — Magnum Opus

Komt overeen met de firmware (`firmware/Slave/src/main.cpp` en
`firmware/Master/src/main.cpp`). Werk dit bestand bij wanneer de firmware-pins
wijzigen.

## Slave — ESP32-C3 SuperMini

| GPIO | Functie | Richting | Notitie |
|------|---------|----------|---------|
| GPIO0 | WS2812B data | OUT | 7 LEDs via 330Ω serieweerstand. **Let op:** GPIO0 is een strapping/boot-pin → data-glitch-risico bij boot. WS2812B-aansturing gebruikt de **IDF-RMT-driver** (`-DFASTLED_RMT_BUILTIN_DRIVER=1`) tegen RMT-underrun (anders licht maar ~3 van 7 LEDs op door WiFi/BLE-interrupt-starvation). |
| GPIO1 | MOSFET gate | OUT | IRLZ44N via 220Ω, 10k pull-down. **Permanent AAN** (in `setup()` HIGH gezet, daarna nooit meer geschakeld). De IRLZ44N is een **low-side** switch in de LED-**massa**: de strip krijgt zijn massaretour enkel via het kanaal drain↔source, dat enkel open is zolang de gate HIGH is. Voor adresseerbare LEDs is deze power-gate overbodig (uit = `CRGB::Black`); hij wordt behouden maar continu aan gehouden. "Uit" zetten gebeurt dus puur in software, niet via deze pin. |
| GPIO3 | Drukknop | IN | Tussen 3V3 en GPIO3, `INPUT_PULLDOWN`. HIGH = ingedrukt. Werkt met of zonder fysieke knop (zonder knop houdt de pulldown de pin LOW → geen valse triggers) |
| GPIO4 | Batterij-ADC | IN (ADC1) | Spanningsdeler 2× 100k (V_adc = V_batt / 2), 12-bit |
| GPIO5 | Buzzer | OUT | Passieve piezo via 100Ω |
| GPIO6 | Rode LED | OUT | Via 150Ω. **Drukknop-feedback-LED**: brandt als de paal "actief" staat (`ACTIE_KNOP_ARM`), gaat uit zolang de knop ingedrukt is (via knop-ISR) → speler ziet of zijn druk pakt |
| GPIO8 | Ingebouwde LED | OUT | Onboard LED, **active-LOW** (LOW = aan). Knippert kort (~40 ms) bij elke succesvolle ESP-NOW-zend |

Batterij-drempel (firmware): kritiek < 3.2 V → `MSG_FOUT` naar de master (geen
LED-indicatie meer).

## Master — ESP32 WROOM-32

| GPIO | Functie | Richting | Notitie |
|------|---------|----------|---------|
| GPIO2 | Ingebouwde LED | OUT | Onboard LED, **active-HIGH** (HIGH = aan). Pulst kort (~30 ms) bij elke ontvangen slave-batch |
| USB | Serial naar Pi | — | CH340 USB-UART (VID 1a86, PID 7523), 115200 baud |

De master communiceert met de slaves via ESP-NOW (geen extra pinnen nodig) en
met de Pi via de USB-serieelverbinding.
