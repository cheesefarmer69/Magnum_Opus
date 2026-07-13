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
 └── scan-cyclus (~scanDuurMs + backoff + 200ms venster + 50ms):
      ├── BLE scan (scanDuurMs, NIET-blokkerend, begrensd met millis(); default 1000 ms, clamp 300..2000)
      │    └── tijdens het venster: verwerkCommandos()/melodie/animatie draaien door (niet meer bevroren)
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
| `PAAL_ID` | runtime | **Niet** meer per bord instellen: bij boot opgezocht uit `firmware/shared/paal_macs.h` op basis van het eigen MAC (0 = onbekend bord → doet niet mee). |
| `WIFI_KANAAL` | ~15 | Moet overeenkomen met de master (standaard: 1) |
| `SCAN_MS_DEFAULT` / `scanDuurMs` | ~14 | BLE-scan-vensterduur in **ms** (default 1000). Runtime instelbaar via `MSG_SCAN_CONFIG` (dashboard "Scan-duur (BLE)"); de slave clamp't `SCAN_MS_MIN`..`SCAN_MS_MAX` = 300..2000. |
| `WACHT_TIMEOUT` | ~12 | Max wachttijd (ms) op commando na verzenden |
| `MAX_BACKOFF_MS` | ~16 | Bovengrens (ms) van de willekeurige zendvertraging |
| `BUZZER_FREQ` | ~20 | Per-paal piep-frequentie (Hz) — kalibreer voor max. volume |
| `BEACON_OUI[3]` | ~175 | OUI-prefix (eerste 3 MAC-bytes) van de toegelaten beacons |
| `RSSI_DREMPEL` | ~176 | Minimale RSSI (dBm) om een beacon door te laten |
| `HEARTBEAT_INTERVAL_S` | ~34 | Interval (s) van het "ik leef"-bericht |
| `FW_VERSIE` | ~33 | Firmware-versie meegestuurd in de heartbeat |
| `masterMacs[3][6]` | ~200 | MAC's van master 1/2/3 — de slave kiest automatisch op `PAAL_ID` (1–8→m1, 9–16→m2, 17–24→m3) |
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

> **BLE-scan-duur (runtime instelbaar):** de scan is niet meer een vaste `start(1 s)` maar een
> **niet-blokkerende** scan die met `millis()` begrensd wordt op `scanDuurMs` (NimBLE 1.4.2 blokkeert
> enkel in hele seconden). Een kortere scan = versere detectie = minder scoring-latentie. Stel hem in via
> het dashboard **"Scan-duur (BLE)"** (Beacons & Locatie): Node-RED stuurt `{"paal":N,"actie":20,
> "scan_ms":M}` → de master vertaalt dat naar `MSG_SCAN_CONFIG`. De slave clamp't 300..2000 ms en logt
> `[SCAN] Venster nu M ms`. De waarde is volatile (weg bij reboot); Node-RED herstelt ze op de eerstvolgende
> heartbeat. Scan-duty verhoogd naar `setWindow(64)`/`setInterval(80)` (~80 %) zodat korte scans genoeg
> samples zien. Zie `docs/locatiebepaling.md` en `docs/protocol.md`.

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
| `[SCAN] Start (600 ms)...` | Nieuwe BLE-scan gestart, met de actieve vensterduur |
| `[SCAN] Venster nu 600 ms` | Nieuwe scan-duur toegepast (ontvangen via `MSG_SCAN_CONFIG`) |
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

> De acties **12–20** (buzzer-toon, klokslag-LED, knop-arm/uit, regenboog-test, **scan-config**) staan
> niet in bovenstaande tabel omdat het geen renderbare LED/melodie-acties zijn: 12/16/20 worden door de
> **master** onderschept en als een eigen ESP-NOW-bericht (`MSG_BUZZER_TOON`/`MSG_KLOKSLAG`/`MSG_SCAN_CONFIG`)
> verstuurd (fire-and-forget, geen FIFO/ACK). De volledige, gezaghebbende actie-tabel staat in
> `docs/protocol.md §2`.

---

## Nieuwe slave in gebruik nemen

> **Eén binary voor alle 24 borden.** Je stelt **niets** meer per bordje in. De slave leest bij boot zijn
> eigen MAC (`esp_read_mac`) en zoekt zijn `PAAL_ID` op in de gedeelde tabel `firmware/shared/paal_macs.h`.
> Staat het MAC er nog niet in, dan **doet het bord niet mee** (rode LED knippert, geen batches) i.p.v. een
> verkeerd paalnummer te claimen.

1. Flash de (enige) slave-binary **via PlatformIO** (`Upload`), **niet via de Arduino IDE**.
   > ⚠️ **Altijd PlatformIO, nooit de Arduino IDE.** De Arduino IDE negeert
   > `platformio.ini` volledig en mist daardoor de build-flag
   > `-DFASTLED_RMT_BUILTIN_DRIVER=1`. Zonder die vlag verhongert WiFi/BLE
   > FastLED's RMT-refill-ISR → RMT-underrun → maar ~3 van de 7 LEDs branden.
   > CLI-equivalent: `pio run -e esp32-c3-devkitm-1 -t upload`. Wijzig je een
   > `build_flag`, forceer dan een schone build (`pio run -e esp32-c3-devkitm-1
   > -t clean` daarna opnieuw uploaden), want een gewijzigde flag hercompileert
   > gecachte library-objecten (zoals FastLED) niet altijd vanzelf.
2. Lees het MAC uit. **Twee manieren:**

   **(a) Aanbevolen — `esptool`, zonder flashen of monitor** (omzeilt de C3-USB-CDC-valkuilen):
   zet het bord in **download-mode** (BOOT vasthouden → RESET tikken → BOOT loslaten) en draai:
   ```powershell
   cd firmware\tools
   .\lees-mac.ps1 -Port COM7 -Paal 5     # -Port weglaten mag als er 1 poort is
   ```
   Het print het MAC + de kant-en-klare `paal_macs.h`-regel (en bewaart die met `-Paal`). Zie ook
   het werkblad `firmware/tools/mac-tabel.md`. Handig als je 24 borden na elkaar afgaat.

   **(b) Via de Serial Monitor** — bij het opstarten toont de slave eenmalig een banner:
   ```
   ============================================
     SLAVE MAC-ADRES : xx:xx:xx:xx:xx:xx
   ============================================
   ```
   (Staat het MAC nog niet in `paal_macs.h`, dan logt hij `!! ONBEKEND BORD ...` en knippert de rode LED.)
   > Blijft de monitor **leeg**? Zie "Bord niet vindbaar / lege monitor (C3 SuperMini)" hieronder.
   > Als los hulpmiddel bestaat er ook `firmware/tools/mac-lezer/` (mini-sketch die enkel het MAC toont
   > + de onboard-LED laat knipperen als "ik leef"-teken).
3. Voeg één regel `{{0x.., …}, <paal>}` toe aan `firmware/shared/paal_macs.h` met dit MAC + het gewenste
   paalnummer. Dit is de **enige** plek die je aanpast (bron van waarheid voor slave én master).
4. Herflash de master die dat paalbereik bedient (die vult `slaveAdressen[]` uit dezelfde tabel). Het
   slave-bord pakt zijn `PAAL_ID` bij de volgende boot vanzelf op — geen slave-herflash nodig als de binary
   al draait.

> Staat een paal nog niet in `paal_macs.h`, dan wordt hij overgeslagen bij de master (peer/gate) en doet
> het bord zelf niet mee — vul de MAC's in zodra de borden geflasht en uitgelezen zijn.

---

## Veelvoorkomende problemen

### Een paal maakt geen contact — beslisboom

Begin **altijd** met deze twee splitsingen. Ze snijden de zoekruimte in één keer doormidden en
voorkomen dat je uren in de verkeerde laag zoekt.

**Splitsing 1 — wat zegt het dashboard?** (Spelstatus → tabel "Palen / Slaves")

| Status | Betekenis | Waar zoeken |
|---|---|---|
| **`GEEN CONTACT`** | sinds de start van Node-RED is er **nooit één byte** van die paal gekomen | structureel: bord uit/stuk, niet geflasht, of de master dropt hem |
| **`VEROUDERD (uit ring)`** | hij **wérkte** en is daarna weggevallen (> 60 s stil) | batterij, afstand/RF, kanaal-conflict (H6) |

Ze zijn niet inwisselbaar: een lege batterij geeft `VEROUDERD`, nooit `GEEN CONTACT`.

**Splitsing 2 — wat zegt de slave zelf?** (Serial Monitor @115200)

| Wat je ziet | Conclusie |
|---|---|
| **Rode fout-blink (~4 Hz)** + `!! ONBEKEND BORD: MAC ...` | Het MAC van dít bord staat **niet** in `firmware/shared/paal_macs.h` → toevoegen/corrigeren, dan slave **én** master herflashen. Geen blink = het MAC klopt. |
| `Eigen MAC herkend -> PAAL_ID N` + **`[ESP-NOW] Batch verzonden OK`** | ⚠️ **De slave is vrijgepleit — zoek verder bij de MASTER.** Zie hieronder. |
| `Eigen MAC herkend -> PAAL_ID N` + **`[ESP-NOW] Verzending MISLUKT`** | De master hoort hem niet: kanaal-mismatch, master uit, peer ontbreekt, of te ver/te veel interferentie. |
| Monitor blijft **leeg** (ook na BOOT/RESET-truc) | Bord is niet geflasht of stuk → zie *"Bord niet vindbaar voor upload"* onderaan. |

> **`[ESP-NOW] Batch verzonden OK` betekent méér dan het lijkt.** Die regel komt uit de
> **send-callback** met `ESP_NOW_SEND_SUCCESS` — dat is een **ACK van de master op MAC-niveau**. Het
> pakket is dus **fysiek bij de master aangekomen**. Zie je dit én tóch `GEEN CONTACT` in het
> dashboard, dan is de slave, zijn MAC, zijn firmware en de radio-link **allemaal in orde** en zit de
> fout **bij de master** (of daarna: bridge → MQTT → Node-RED). Ga dan naar de master-log:
>
> ```
> [SETUP] Master 1, palen 1-8 (8 slaves)          <-- staat er "1-7"? verkeerde build-flags
> [PEER] Paal 4 toegevoegd: 8C:FD:49:54:BC:10     <-- master kent hem
> [PEER] Paal 4 overgeslagen (geen MAC ingevuld)  <-- master draait op een oude paal_macs.h
> [GATE] Genegeerd: AC:A7:04:.. (niet in slaveAdressen[])   <-- ontvangen maar gedropt
> [RECV] Paal 4, 2 spelers, batt 3870 mV          <-- correct ontvangen -> zoek downstream
> ```
> **Fix bij `overgeslagen` / `[GATE] Genegeerd`:** herflash de master met de juiste env
> (`master1`/`master2`/`master3`) — hij bouwt `slaveAdressen[]` uit `paal_macs.h`.
>
> De master-poort is bezet door de bridge; lees mee via `VERBOSE=1` +
> `docker logs -f serial-bridge`, of stop de bridge tijdelijk (`docker stop serial-bridge`).

### Overige problemen

**ESP-NOW Verzending MISLUKT**
→ Master staat op een ander WiFi-kanaal, of masterAddress klopt niet.
Check: `[SETUP] WiFi kanaal:` in Serial Monitor van beide apparaten.

**Geen spelers gevonden terwijl beacon actief is**
→ Beacon-OUI komt niet overeen met `BEACON_OUI` (`48:87:2d`), RSSI is zwakker dan `RSSI_DREMPEL`,
of de beacon adverteert niet (batterij leeg?). Check: zet tijdelijk een `Serial.printf` in `onResult()`
vóór het OUI/RSSI-filter, of verlaag tijdelijk `RSSI_DREMPEL`.

**Commando's komen niet aan**
→ Slave is tijdens de scan — commando wordt wél gebufferd en, sinds de gevensterde scan, óók al
**tijdens** het scan-venster verwerkt (`verwerkCommandos()` draait door). Als het structureel mislukt:
check of master het juiste slave-MAC gebruikt.

**Batterij lijkt verkeerd / `MSG_FOUT` batterij-kritiek onverwacht**
→ Check `[BATT]` in de Serial Monitor; kritiek wordt gemeld onder `BATT_KRITIEK`.
Mogelijk ook: ADC-kalibratie — meet de spanning met een multimeter en vergelijk.
(De GPIO6-LED is nu de **drukknop-feedback-LED**, geen batterij-indicatie.)

**Bord niet vindbaar voor upload / lege seriële monitor (C3 SuperMini)**
De C3 SuperMini gebruikt de **ingebouwde USB Serial/JTAG** (geen CH340). De auto-reset naar de
bootloader is onbetrouwbaar zodra er al een USB-CDC-sketch draait → de volgende upload komt de
bootloader niet in en de COM-poort lijkt "weg" (of wisselt van nummer). De monitor kan leeg blijven
door CDC-timing, of doordat het bord in download-mode hangt.

→ **Forceer download-mode:** **BOOT (GPIO9) vasthouden → RESET tikken → BOOT loslaten** (klonen met
  maar één knop: BOOT vasthouden terwijl je de USB insteekt). Daarna werkt Upload / `esptool` weer.
→ **Lege monitor:** gebruik **"Upload and Monitor"** (poort wordt dan correct herpakt), controleer de
  COM-poort, gebruik een **datakabel** (geen laadkabel), en druk na de upload één keer **RESET**.
  In `platformio.ini` helpen `monitor_dtr = 0` + `monitor_rts = 0` tegen een monitor die de chip in
  reset houdt.
→ **MAC uitlezen lukt ook zonder monitor:** `firmware/tools/lees-mac.ps1` (esptool `read_mac`).
