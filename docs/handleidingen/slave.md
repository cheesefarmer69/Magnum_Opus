# Handleiding: Slave (ESP32-C3 SuperMini)

**Bestand:** `firmware/Slave/src/main.cpp`
**Hardware:** ESP32-C3 SuperMini, één per paal (24 stuks)

---

## Wat doet de slave?

1. Scant continu op BLE-advertenties van speler-beacons
2. Filtert op **OUI-prefix** (`48:87:2d`) + **RSSI-drempel** (protocol v2 — geen hardcoded MAC-lijst meer)
3. Stuurt gevonden spelers als `batch_message_v2` via ESP-NOW naar de master (tot 30 spelers)
4. Wacht kort op een terugkoppelingscommando (`MSG_COMMANDO`) van de master
5. Voert dat commando uit (LED-kleur, buzzer) en stuurt een `MSG_CMD_ACK` ná uitvoering
6. Bewaakt de batterijspanning (warning-LED + `MSG_FOUT` bij kritiek) en stuurt periodiek een heartbeat

---

## Hoe werkt de hoofdlus?

```
loop()
 └── scan-cyclus (elke ~1100-1300ms):
      ├── BLE scan (1 seconde, blokkerend)
      │    └── BeaconZoeker::onResult() per gevonden apparaat
      │         └── OUI == 48:87:2d EN rssi >= RSSI_DREMPEL? →
      │              ├── bestaat al in batch? → sterkste RSSI behouden
      │              └── nieuw? → toevoegen (max 30 unieke MACs; >30 → MSG_FOUT)
      │    └── verwerkCommandos()   (commando's van tijdens de scan afhandelen)
      │
      ├── random backoff (0..MAX_BACKOFF_MS) — willekeurige zendvertraging
      │
      ├── esp_now_send() batch_message_v2 → master   (ALTIJD, ook bij 0 spelers)
      ├── [elke HEARTBEAT_INTERVAL_S] → MSG_HEARTBEAT
      ├── verwerkCommandos()
      │
      ├── luistervenster 200ms (servicet hardware + draineert de ring elke iteratie)
      │    ├── checkBatterij()           elke 5s; bij kritiek → MSG_FOUT
      │    ├── serviceKnopVerzending()    herhaal-zendingen van de knop-teller (kogelvrij)
      │    ├── updateIngebouwdeLed()      GPIO8: knippert kort na geslaagde zend
      │    └── verwerkCommandos()     → per commando: voerActieUit() + MSG_CMD_ACK
      │
      └── verwerkCommandos() (laatste keer ná het venster)

OnDataRecv() (producent) pusht elk MSG_COMMANDO in een SPSC-ringbuffer (8 slots);
verwerkCommandos() (consument) draineert hem in volgorde. Idempotent op cmd_seq.
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
> `checkBatterij()`, `serviceKnopVerzending()` en de LED-updates worden aangeroepen tijdens
> het 200ms wacht-venster na het verzenden — dat venster draait nu elke cyclus.
>
> **Drukknop (GPIO3) — kogelvrij tellen:** tussen 3V3 en GPIO3, `INPUT_PULLDOWN`. Een
> **interrupt** (`knopISR`, CHANGE) vangt **elke** druk, óók tijdens de blokkerende
> BLE-scan, en houdt een **cumulatieve `knopTeller`** bij. Die teller gaat mee in elk
> `MSG_KNOP`-pakket en wordt **~6 cycli lang hervast** (`knopResend`) → een verloren pakket
> wordt door het volgende gecorrigeerd. De master vertaalt dat naar
> `{"paal":N,"knop":1,"teller":M}`. Tellen gebeurt enkel als de paal **gewapend** is
> (`ACTIE_KNOP_ARM` vanuit Node-RED; `ACTIE_KNOP_UIT` zet hem weer inactief).
>
> **GPIO6 — drukknop-feedback-LED:** als de paal gewapend is, **brandt** de LED; **zolang de
> knop ingedrukt is** gaat hij **uit** (door dezelfde `knopISR`, dus instant ook tijdens de
> scan). Zo ziet een speler of zijn druk pakt. Niet gewapend → LED uit.
>
> **Ingebouwde LED (GPIO8, active-LOW):** knippert kort (~40 ms) bij elke
> succesvolle ESP-NOW-zend als visuele zend-indicator.
>
> **Commando-ringbuffer (geen verloren/overschreven commando's):** `OnDataRecv` is de
> producent en pusht elk `MSG_COMMANDO` als `{actie, cmd_seq}` in een SPSC-ringbuffer (8 slots,
> `volatile` indices — geen mutex nodig op de single-core C3). `verwerkCommandos()` is de
> consument en draineert hem **in volgorde** op meerdere punten in de cyclus. Hierdoor:
> - gaat een commando dat binnenkomt tijdens de 1 s BLE-scan, `voerActieUit()` of de `delay(50)`
>   **niet verloren** (er is geen flag-reset bovenaan de loop meer);
> - worden **twee** commando's binnen één cyclus (bv. piep + portaal) **allebei** uitgevoerd;
> - is de afhandeling **idempotent** op `cmd_seq` (een master-retry voert niet dubbel uit, maar
>   her-ACK't wel). Bij een volle buffer wordt de nieuwste gedropt en `cmdDrops` opgehoogd
>   (zichtbaar als `[CMD] Ringbuffer-drops: N`).

---

## Belangrijke parameters aanpassen

| Constante | Bestand regel | Betekenis |
|-----------|--------------|-----------|
| `PAAL_ID` | ~14 | Uniek ID van deze paal (1–24) — **per slave aanpassen** |
| `WIFI_KANAAL` | ~15 | Moet overeenkomen met de master (standaard: 1) |
| `SCAN_DUUR_S` | ~11 | BLE-scanduur in seconden |
| `WACHT_TIMEOUT` | ~12 | Max wachttijd (ms) op commando na verzenden |
| `MAX_BACKOFF_MS` | ~16 | Bovengrens (ms) van de willekeurige zendvertraging |
| `BUZZER_FREQ` | ~20 | Per-paal piep-frequentie (Hz) — kalibreer voor max. volume |
| `BEACON_OUI[3]` | ~175 | OUI-prefix (eerste 3 MAC-bytes) van de toegelaten beacons |
| `RSSI_DREMPEL` | ~176 | Minimale RSSI (dBm) om een beacon door te laten |
| `HEARTBEAT_INTERVAL_S` | ~34 | Interval (s) van het "ik leef"-bericht |
| `FW_VERSIE` | ~33 | Firmware-versie meegestuurd in de heartbeat |
| `masterMacs[3][6]` | ~200 | MAC's van master 1/2/3 — de slave kiest automatisch op `PAAL_ID` (1–7→m1, 8–16→m2, 17–24→m3) |
| `BATT_KRITIEK` | ~47 | Spanning (V) waaronder een `MSG_FOUT` (batterij kritiek) wordt gestuurd |

> **Batterijmeting:** `leesBatterijSpanning()` gebruikt `analogReadMilliVolts()`
> (fabriekskalibratie van de ADC), niet `analogRead()` met een vaste 3.3V-referentie.
> Dat laatste meet op de ESP32-C3 structureel enkele procenten te laag.
>
> **Buzzervolume:** een passieve buzzer is het luidst rond zijn resonantie­frequentie
> (typisch 2–4 kHz). De buzzer-piep (`ACTIE_BUZZER_PIEP`) gebruikt de **per-paal** constante
> `BUZZER_FREQ` (default 1500 Hz; `setup()` zet die in `MELODIE_PIEP[0]`). Klinkt een paal te
> stil, meet dan zijn luidste frequentie en zet die in `BUZZER_FREQ` voor dat bordje. Een actieve
> buzzer (met ingebouwde oscillator) hoort NIET met `tone()` aangestuurd te worden maar met `digitalWrite(HIGH)`.

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
| `[BLE] Beacon 48:87:2d:.. RSSI: -67` | Beacon (OUI + sterk genoeg) toegevoegd aan batch |
| `[SCAN] Klaar, 2 beacons gevonden (batt 3870 mV)` | Scan klaar, 2 spelers in batch, batterij in mV |
| `[BACKOFF] 87 ms` | Willekeurige zendvertraging vóór verzenden |
| `[SEND] Versturen naar master (2 spelers)...` | Batch wordt verstuurd (ook bij 0 spelers) |
| `[ESP-NOW] Batch verzonden OK` | Master heeft pakket ontvangen (MAC-laag) |
| `[ESP-NOW] Verzending MISLUKT` | Master niet bereikbaar (kanaal? MAC?) |
| `[ESP-NOW] Commando voor paal 1, actie 1, seq 42` | `MSG_COMMANDO` ontvangen van master |
| `[CMD] Actie 1 uitvoeren (seq 42)` | Commando uit de ringbuffer wordt uitgevoerd, daarna MSG_CMD_ACK |
| `[CMD] Seq 42 al uitgevoerd, alleen her-ACK` | Idempotent: retry van een al uitgevoerd commando |
| `[CMD] Ringbuffer-drops: N` | N commando's gedropt omdat de ringbuffer vol zat |
| `[BLE] Overflow: N beacons boven 30 genegeerd` | >30 spelers in dit vak → MSG_FOUT |
| `[KNOP] paal N ACTIEF (LED aan)` / `inactief (LED uit)` | Paal gewapend/ontwapend via `ACTIE_KNOP_ARM`/`_UIT` (drukken tellen + GPIO6-LED) |
| `[BATT] 3.87V` | Batterijspanning om de 5 seconden |
| `[ESP-NOW] Init MISLUKT!` | Fatale fout — slave stopt setup (geen ESP-NOW) |

---

## Actie-ID's

Minimale set: enkel acties die aan een spel-event hangen. De LED-toestanden (portaal,
happy hour) worden centraal door Node-RED gestuurd op basis van de actieve effecten;
loopt een effect af of stopt het spel, dan stuurt Node-RED `ACTIE_NIETS`.

| ID | Constante | Gedrag |
|----|-----------|--------|
| 0 | `ACTIE_NIETS` | LEDs uit (`CRGB::Black`); MOSFET blijft aan (permanent) |
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

1. Stel `PAAL_ID` in op het gewenste paal-nummer. De slave kiest daaruit **automatisch** zijn master-MAC
   (1–7→master1, 8–16→master2, 17–24→master3, uit `masterMacs[]`) — verder niets per slave in te stellen.
   Bij opstart logt hij `[SETUP] Paal N -> master M`.
2. Flash de slave **via PlatformIO** (`Upload`), **niet via de Arduino IDE**.
   > ⚠️ **Altijd PlatformIO, nooit de Arduino IDE.** De Arduino IDE negeert
   > `platformio.ini` volledig en mist daardoor de build-flag
   > `-DFASTLED_RMT_BUILTIN_DRIVER=1`. Zonder die vlag verhongert WiFi/BLE
   > FastLED's RMT-refill-ISR → RMT-underrun → maar ~3 van de 7 LEDs branden.
   > CLI-equivalent: `pio run -e esp32-c3-devkitm-1 -t upload`. Wijzig je een
   > `build_flag`, forceer dan een schone build (`pio run -e esp32-c3-devkitm-1
   > -t clean` daarna opnieuw uploaden), want een gewijzigde flag hercompileert
   > gecachte library-objecten (zoals FastLED) niet altijd vanzelf.
3. Open Serial Monitor — bij het opstarten toont de slave eenmalig een
   banner met zijn MAC-adres:
   ```
   ============================================
     SLAVE MAC-ADRES : xx:xx:xx:xx:xx:xx
   ============================================
   ```
4. Vul dit MAC-adres in op rij `paal − PAAL_MIN` in het juiste `MASTER_NR`-blok van
   `firmware/Master/include/slave_macs.h` (zie master-handleiding)
5. Herflash de juiste master-environment

> Staat een master-MAC in `masterMacs[]` nog op `0x00…` (master 2/3 nog niet gebouwd), dan kunnen
> slaves 8–24 nog niet zenden — vul die MAC's in zodra die masters bestaan.

---

## Veelvoorkomende problemen

**ESP-NOW Verzending MISLUKT**
→ Master staat op een ander WiFi-kanaal, of masterAddress klopt niet.
Check: `[SETUP] WiFi kanaal:` in Serial Monitor van beide apparaten.

**Geen spelers gevonden terwijl beacon actief is**
→ Beacon-OUI komt niet overeen met `BEACON_OUI` (`48:87:2d`), RSSI is zwakker dan `RSSI_DREMPEL`,
of de beacon adverteert niet (batterij leeg?). Check: zet tijdelijk een `Serial.printf` in `onResult()`
vóór het OUI/RSSI-filter, of verlaag tijdelijk `RSSI_DREMPEL`.

**Commando's komen niet aan**
→ Slave is tijdens scan (1s) — commando wordt wél gebufferd en direct erna verwerkt.
Als het structureel mislukt: check of master het juiste slave-MAC gebruikt.

**Batterij lijkt verkeerd / `MSG_FOUT` batterij-kritiek onverwacht**
→ Check `[BATT]` in de Serial Monitor; kritiek wordt gemeld onder `BATT_KRITIEK`.
Mogelijk ook: ADC-kalibratie — meet de spanning met een multimeter en vergelijk.
(De GPIO6-LED is nu de **drukknop-feedback-LED**, geen batterij-indicatie.)
