# Handleiding: Slave (ESP32-C3 SuperMini)

**Bestand:** `firmware/Slave/src/main.cpp`
**Hardware:** ESP32-C3 SuperMini, één per paal (24 stuks)

---

## Wat doet de slave?

1. Scant continu op BLE-advertenties van speler-beacons
2. Filtert op een vaste whitelist van bekende beacon-MAC-adressen
3. Stuurt gevonden spelers als batch via ESP-NOW naar de master
4. Wacht kort op een terugkoppelingscommando van de master
5. Voert dat commando uit (LED-kleur, buzzer)
6. Bewaakt de batterijspanning en knippert een warning-LED bij lage spanning

---

## Hoe werkt de hoofdlus?

```
loop()
 └── scan-cyclus (elke ~1100-1300ms):
      ├── BLE scan (1 seconde, blokkerend)
      │    └── BeaconZoeker::onResult() per gevonden apparaat
      │         └── MAC in whitelist? →
      │              ├── bestaat al in batch? → sterkste RSSI behouden
      │              └── nieuw? → toevoegen (max 9 unieke MACs)
      │
      ├── random backoff (0..MAX_BACKOFF_MS) — willekeurige zendvertraging
      │
      ├── esp_now_send() → master   (ALTIJD, ook bij 0 spelers)
      │
      ├── wacht max 200ms op OnDataRecv() callback
      │    ├── checkBatterij()        elke 5s (binnenin de wacht-loop)
      │    ├── checkKnop()            rising-edge drukknop GPIO3 (debounce)
      │    ├── updateRodeLed()        GPIO6: knop-puls > batterij-waarschuwing
      │    └── updateIngebouwdeLed()  GPIO8: knippert kort na geslaagde zend
      │
      └── [als commandoOntvangen] → voerActieUit()
```

> **Altijd versturen:** de slave stuurt elke cyclus een batch, óók bij 0
> gevonden spelers. Zo weet de master (en het dashboard) dat een leeg vak
> ook echt leeg is — een oude spelersstand blijft anders eindeloos staan.
>
> **Dedup binnen batch:** een beacon adverteert ~10×/s, dus tijdens een 1s-scan
> vuurt `onResult()` vaak voor dezelfde MAC. De callback voegt zo'n MAC maar
> één keer toe aan `batchData.spelers[]` en behoudt de sterkste RSSI. Zonder
> dit zou de array volstromen met duplicaten en zou de master tientallen
> JSON-regels per seconde naar Node-RED sturen voor één paal.
>
> **Random backoff:** vóór het verzenden wacht de slave een willekeurige
> tijd (`0..MAX_BACKOFF_MS`, hardware-RNG via `esp_random()`). Dit verbreekt
> de fase-vergrendeling tussen meerdere slaves zodat ze niet elke cyclus
> tegelijk zenden en elkaars pakket wegdrukken bij de master.
>
> `checkBatterij()`, `checkKnop()` en de LED-updates worden aangeroepen tijdens
> het 200ms wacht-venster na het verzenden — dat venster draait nu elke cyclus.
>
> **Drukknop (GPIO3):** tussen 3V3 en GPIO3, `INPUT_PULLDOWN`. Een druk (rising
> edge, met debounce) geeft een puls op de rode LED (GPIO6) en stuurt een
> hook-regel `{"paal":N,"knop":1}` over serial. Het framework draait altijd,
> ook zonder fysieke knop: de pulldown houdt de pin dan LOW → geen valse
> triggers. De rode LED is gedeeld: een knop-puls (~150 ms) heeft voorrang op
> de batterij-waarschuwing.
>
> **Ingebouwde LED (GPIO8, active-LOW):** knippert kort (~40 ms) bij elke
> succesvolle ESP-NOW-zend als visuele zend-indicator.
>
> De BLE-scan blokkeert 1 seconde. Commando's die tijdens de scan
> binnenkomen via ESP-NOW worden gebufferd in de callback en direct
> na de scan verwerkt — ze gaan niet verloren.

---

## Belangrijke parameters aanpassen

| Constante | Bestand regel | Betekenis |
|-----------|--------------|-----------|
| `PAAL_ID` | ~14 | Uniek ID van deze paal (1–24) — **per slave aanpassen** |
| `WIFI_KANAAL` | ~15 | Moet overeenkomen met de master (standaard: 1) |
| `SCAN_DUUR_S` | ~11 | BLE-scanduur in seconden |
| `WACHT_TIMEOUT` | ~12 | Max wachttijd (ms) op commando na verzenden |
| `MAX_BACKOFF_MS` | ~16 | Bovengrens (ms) van de willekeurige zendvertraging |
| `toegelatenBeacons[]` | ~80 | Whitelist van beacon-MAC-adressen |
| `masterAddress[]` | ~89 | MAC-adres van de master — lees uit Serial Monitor van master |
| `KNOP_DEBOUNCE_MS` | ~57 | Ontdender-tijd (ms) van de drukknop |
| `KNOP_PULS_MS` | ~58 | Duur (ms) van de rode-LED puls bij een druk |
| `BATT_WAARSCHUWING` | ~46 | Spanning (V) waaronder LED langzaam knippert |
| `BATT_KRITIEK` | ~47 | Spanning (V) waaronder LED snel knippert |

> **Batterijmeting:** `leesBatterijSpanning()` gebruikt `analogReadMilliVolts()`
> (fabriekskalibratie van de ADC), niet `analogRead()` met een vaste 3.3V-referentie.
> Dat laatste meet op de ESP32-C3 structureel enkele procenten te laag.
>
> **Buzzervolume:** een passieve buzzer is het luidst rond zijn resonantie­frequentie
> (typisch 2–4 kHz). De buzzer-piep (`ACTIE_BUZZER_PIEP`) gebruikt 1500 Hz; klinkt hij
> te stil, pas dan de frequentie in `MELODIE_PIEP` aan. Een actieve buzzer (met ingebouwde
> oscillator) hoort NIET met `tone()` aangestuurd te worden maar met `digitalWrite(HIGH)`.

---

## Initialisatievolgorde (kritisch voor C3)

De ESP32-C3 heeft één antenne voor zowel WiFi/ESP-NOW als BLE.
De huidige code gebruikt de volgende volgorde:

```
1. FastLED / pins / ADC       — hardware initialiseren
2. NimBLEDevice::init()       — BLE initialiseren
3. WiFi.mode(WIFI_STA)        — WiFi-stack opstarten
4. esp_wifi_set_ps(WIFI_PS_MIN_MODEM)  — minimaal power saving (coëxistentie)
5. esp_now_init()             — ESP-NOW initialiseren
6. esp_wifi_set_channel()     — kanaal vastzetten NA esp_now_init
7. esp_now_add_peer()         — master registreren
```

> Let op: NimBLE wordt hier vóór WiFi/ESP-NOW geïnitialiseerd.
> Dit werkt in de huidige firmware, maar bij problemen met ESP-NOW
> is initialisatie ná `esp_now_init()` de veiligste volgorde.

---

## Serial Monitor output begrijpen

| Prefix | Betekenis |
|--------|-----------|
| `[SCAN] Start...` | Nieuwe BLE-scan gestart |
| `[BLE] Whitelisted: xx:xx RSSI: -67` | Speler gevonden en toegevoegd aan batch |
| `[SCAN] Klaar, 2 whitelisted gevonden (batt 3.87V)` | Scan klaar, 2 spelers in batch, huidige batterij-spanning |
| `[BACKOFF] 87 ms` | Willekeurige zendvertraging vóór verzenden |
| `[SEND] Versturen naar master (2 spelers)...` | Batch wordt verstuurd (ook bij 0 spelers) |
| `[ESP-NOW] Batch verzonden OK` | Master heeft pakket ontvangen |
| `[ESP-NOW] Verzending MISLUKT` | Master niet bereikbaar (kanaal? MAC?) |
| `[CMD] Actie ontvangen: 1` | Commando 1 (PORTAAL/paars) ontvangen van master |
| `[ACTIE] LED 1` | Commando wordt uitgevoerd |
| `[CMD] Timeout, geen commando` | Geen commando binnen 200ms na verzenden |
| `[BATT] 3.87V` | Batterijspanning om de 5 seconden |
| `{"paal":N,"knop":1}` | Drukknop ingedrukt (rising edge) |
| `[ESP-NOW] Init MISLUKT!` | Fatale fout — ESP knippert warning-LED snel |

---

## Actie-ID's

Minimale set: enkel acties die aan een spel-event hangen. De LED-toestanden (portaal,
happy hour) worden centraal door Node-RED gestuurd op basis van de actieve effecten;
loopt een effect af of stopt het spel, dan stuurt Node-RED `ACTIE_NIETS`.

| ID | Constante | Gedrag |
|----|-----------|--------|
| 0 | `ACTIE_NIETS` | LEDs uit, MOSFET uit |
| 1 | `ACTIE_PORTAAL` | LED strip paars continu (portaal-toestand) |
| 2 | `ACTIE_HAPPY_HOUR` | LED strip goud continu (happy-hour-toestand) |
| 3 | `ACTIE_BUZZER_PIEP` | Eén piep 1500 Hz, 600 ms (uur-afroep / zoemer-test) |
| 4 | `ACTIE_MEDICIJN` | LED strip felroze continu (medicijn, ziekte-event) |
| 5 | `ACTIE_ZIEK_W3` | Zoemer: ziekenhuis-monitor-piep + 3 hartslagen (zieke speler, nog 3 events) |
| 6 | `ACTIE_ZIEK_W2` | Zoemer: monitor-piep + 2 hartslagen (nog 2 events) |
| 7 | `ACTIE_ZIEK_W1` | Zoemer: monitor-piep + 1 hartslag (nog 1 event) |
| 8 | `ACTIE_NUKE` | LED **geanimeerd**: pulserend radioactief geel↔groen (NUKE-ring) |
| 9 | `ACTIE_MN_OPEN` | LED zacht wit continu (middernacht-poort open) |
| 10 | `ACTIE_MN_DICHT` | LED rood continu (middernacht-poort dicht) |
| 11 | `ACTIE_OOGST` | LED **geanimeerd**: dramatische wit/rood-strobe (middernacht-oogst) |

De zoemer-acties (3/5/6/7) zijn niet-blokkende melodieën (`MELODIE_*` + `updateMelodie()`); de solide
LED-acties (1/2/4/9/10) blijven continu tot Node-RED `ACTIE_NIETS` stuurt. De **geanimeerde** acties
(8 = nuke, 11 = oogst) worden gerenderd door `updateAnimatie()` (millis-gebaseerd, gebruikt `CHSV` +
`beatsin8`, aangeroepen in de wacht-loop naast `updateMelodie()`) en blijven animeren tot een nieuwe
actie binnenkomt. `huidigeActie` onthoudt de actieve LED-staat.

---

## Nieuwe slave in gebruik nemen

1. Stel `PAAL_ID` in op het gewenste paal-nummer
2. Flash de slave met PlatformIO (`Upload`)
3. Open Serial Monitor — bij het opstarten toont de slave eenmalig een
   banner met zijn MAC-adres:
   ```
   ============================================
     SLAVE MAC-ADRES : xx:xx:xx:xx:xx:xx
   ============================================
   ```
4. Voeg dit MAC-adres toe aan de master (zie master-handleiding)
5. Herflash de master

---

## Veelvoorkomende problemen

**ESP-NOW Verzending MISLUKT**
→ Master staat op een ander WiFi-kanaal, of masterAddress klopt niet.
Check: `[SETUP] WiFi kanaal:` in Serial Monitor van beide apparaten.

**Geen spelers gevonden terwijl beacon actief is**
→ MAC-adres staat niet in `toegelatenBeacons[]`, of beacon adverteert niet (batterij leeg?).
Check: zet tijdelijk een `Serial.printf` in `onResult()` voor de whitelist-check.

**Commando's komen niet aan**
→ Slave is tijdens scan (1s) — commando wordt wél gebufferd en direct erna verwerkt.
Als het structureel mislukt: check of master het juiste slave-MAC gebruikt.

**Warning-LED brandt altijd**
→ Batterijspanning onder `BATT_WAARSCHUWING`. Check `[BATT]` in Serial Monitor.
Mogelijk ook: ADC-kalibratie — meet de spanning met een multimeter en vergelijk.
