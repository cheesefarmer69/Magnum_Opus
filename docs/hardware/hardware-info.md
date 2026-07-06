# Hardware-info — Magnum Opus

Overkoepelende hardware-informatie die niet in de pin-tabel (`pinout.md`) of de
speelveld-geometrie (`playfield.md`) past: **voeding & batterij**, **weersbestendigheid**
en de **bekende aandachtspunten / PCB rev-B**.

---

## Voeding & batterij

Elke paal (slave, ESP32-C3 SuperMini) draait op **één Li-ion/LiPo-cel (1S, ~3,0–4,2 V)**.
De celspanning wordt gemeten op **GPIO4** via een spanningsdeler **2× 100 kΩ** (V_adc = V_batt / 2),
met `analogReadMilliVolts()` (eFuse-fabriekskalibratie, 8× gemiddeld — zie `leesBatterijSpanning()`
in `firmware/Slave/src/main.cpp`). De WS2812B-LEDs draaien op een **5 V-rail** (FastLED begrenst op
5 V / 700 mA als voeding-vangnet tegen brownout).

> **LED-helderheid ↔ batterij-runtime (HW9).** De helderheid is runtime instelbaar vanaf het dashboard
> ("LED-helderheid", actie 21). Meer helderheid = meer LED-stroom: bij 7 LED's trekt vol wit ~420 mA op
> **max (255)** tegenover ~250 mA op de default **150**. De 700 mA-power-cap throttelt daarbij niet (geen
> hardwarerisico), maar "max" ~verdubbelt de LED-bijdrage aan het verbruik → kortere runtime. Zet 'm
> overdag hoog voor zichtbaarheid en 's avonds op **Middel** om batterij te sparen.

### Bij welke spanning is de batterij "te laag"?

| Spanning (cel) | Betekenis | Waar |
|----------------|-----------|------|
| **≥ ~3,6 V** | gezond | — |
| **< 3,5 V** | **dashboard-waarschuwing "vervang batterij"** (proactief, ~20–30 % over) | Node-RED `BATT_VERVANG_V`, foutcode **ST-005** (WAARSCHUWING, niet-blokkerend) |
| **< 3,2 V** | firmware meldt **kritiek** (`BATT_KRITIEK`) via een eenmalige `MSG_FOUT` (ernst 2) | `checkBatterij()` in de slave, elke 5 s |
| **< ~3,0 V** | cel is leeg; verder ontladen beschadigt de cel | — |

**Vuistregel: wissel de batterij zodra het dashboard de waarschuwing (< 3,5 V) toont.** Dat geeft
voorsprong vóór de 3,2 V-kritiekgrens en houdt de cel gezond. De 3,5 V-drempel is één plek instelbaar
(`BATT_VERVANG_V` in de node "Evalueer spelstatus").

> **Exacte harde ondergrens (voedingstopologie).** Tot welke celspanning de paal écht blijft werken,
> hangt af van de voedingstrap tussen cel en ESP/LEDs (boost naar 5 V, of directe 5 V-pin + onboard-LDO).
> *(In te vullen op basis van het PCB-schema.)* Een boost naar 5 V houdt ESP + LEDs stabiel tot de boost
> onder last geen kop meer heeft (~3,0–3,3 V cel); een directe LDO-voeding laat 3,3 V wegzakken zodra de
> cel onder ~3,4–3,5 V komt. In beide gevallen is de **3,5 V-waarschuwing** een veilig wisselmoment.

### Hot-swap: batterij vervangen tijdens het spel

**De batterij vervangen kan tijdens een lopend spel zonder problemen** — je hoeft het spel niet te stoppen:

1. Trek de (bijna lege) cel eruit en zet een volle in.
2. De paal **reboot** (~enkele seconden). Bij het opstarten:
   - zoekt hij zijn eigen MAC op in `paal_macs.h` → zelfde `PAAL_ID` (zie provisioning hieronder);
   - stuurt binnen ~1 cyclus weer een **heartbeat + batches**; de master herkent hem meteen via de
     ontvangst-whitelist (`slaveAdressen[]`);
   - Node-RED **herstelt automatisch de ingestelde BLE-scan-duur** op de eerstvolgende heartbeat.
3. Een korte onderbreking (< `SLAVE_STALE_MS` = 60 s) triggert **geen** fout en **stopt het spel niet**.
   Spelers vlak bij die paal houden kort hun laatste positie; zodra de paal weer detecteert, loopt alles
   verder. **Er is geen extra handeling nodig — het spel loopt gewoon door.**

---

## Weersbestendigheid

**Weersbestendigheid is in het ontwerp meegenomen** (behuizing + afdichting van de palen en de
elektronica). Dit is bij normaal buitengebruik **geen aandachtspunt**: er hoeft tijdens het spel niet
op weersinvloeden gelet te worden.

---

## Geheugen (1 GB Raspberry Pi) — L4

**Oordeel: 1 GB is voldoende** voor het hele spel (Node-RED-engine, locatiebepaling, audio, broker, AP) én
een handvol dashboard-tabs — **mits** de onbegrensde global-context begrensd blijft. Ruw budget: OS ~120 MB
+ Docker ~70 MB + mosquitto/serial-bridge/audio/AP ~120 MB + Node-RED ~150–300 MB ≈ **450–650 MB baseline**,
dus ~350–500 MB speling. Node-RED is de swingfactor.

**Begrensd (caps in `flows.json`):**
- `spelHistorie` → **laatste 30 partijen** (oudere gedropt; de **globale stats** blijven apart bewaard).
  Zonder cap groeide die onbegrensd — én ze zit in de 30 s-`spel/state`-dump (retained MQTT + SD elke 30 s).
- `pofSnapshots` (tijd-terug) → **diepte 10** en **zonder** de groeiende event-log (`pofHuidigSpel`); dat
  snijdt de per-partij-piek van ~1 MB naar ~0,15–0,3 MB. (Tijd-terug herstelt stats/posities/effecten/π
  sowieso; de event-log is cosmetisch.)
- `globaleStats[n].skills` → **laatste 50** per speler.

**Operationele tips:**
- **Houd het aantal open dashboard-/simulator-tabs beperkt.** Elke tab is een WebSocket-client die alle
  retained + de 1 s-`pof/status`-berichten meekrijgt; veel tabs = meer geheugen én CPU. Sluit ongebruikte.
- Monitor op de speeldag: `free -h` en `docker stats --no-stream` (let op de node-red-container); OOM-kills
  zie je met `dmesg | grep -i oom`.
- `contextStorage` (localfilesystem) flusht **alle** globals elke 15 s naar de SD — de caps hierboven houden
  ook die writes klein.

## Aandachtspunten / PCB rev-B

Bekende, **niet-blokkerende** verbeterpunten. Niets hiervan houdt het huidige systeem tegen; het is een
lijst voor de volgende bordrevisie en de operationele voorbereiding.

### H3 — Provisioning (OPGELOST in firmware)
Vroeger had elke paal een **compile-time `PAAL_ID`** → 24 aparte builds en kans op "verkeerd nummer
geflasht". **Nu:** de slave zoekt bij boot zijn eigen MAC op in de gedeelde tabel
`firmware/shared/paal_macs.h` (`MAC → PAAL_ID`) → **één binary voor alle 24 borden**; de master vult
daaruit ook zijn `slaveAdressen[]`. Eén header = enige bron van waarheid voor beide kanten. Zie
`docs/handleidingen/slave.md`.

### H4 — Geen OTA / veld-updatepad *(niet geïmplementeerd)*
Een slave kan vandaag **alleen via USB** geüpdatet worden; tijdens het spel praten de borden enkel
ESP-NOW (geen WiFi). Een firmwarebug op de speeldag = 24 palen uit de grond + open schroeven + USB.
**Minimale variant (later):** een "onderhoudsmodus"-commando (nieuw `msg_type`) dat een slave WiFi-STA
naar het Pi-AP laat opzetten en **ArduinoOTA** start; normaal spel blijft ESP-NOW-only. Alternatief:
ESP-NOW-gebaseerde OTA (lib bestaat, complexer). Zelfs alleen de **3 masters** via de Pi-USB flashen
(esptool vanaf de Pi) scheelt al.

### H5 — IRLZ44N-power-gate is overbodig *(niet geïmplementeerd — PCB rev-B)*
De IRLZ44N (GPIO1) staat **permanent AAN** en doet niets nuttigs: voor adresseerbare LEDs zet je "uit"
in software (`CRGB::Black`). Twee subtiele nadelen: (1) een **low-side switch in de LED-massa** verschuift
de massa-referentie van de **datalijn** met V_DS → marginaal datasignaal; (2) de **3,3 V-gate** zit onder
de 4–5 V waarop de R_DS(on) van de IRLZ44N gespecificeerd is. **Rev-B:** schrappen of vervangen door een
0 Ω-jumper. Wil je écht power-gaten (deep sleep) → **high-side P-FET in de 5 V-lijn**, niet low-side in de
massa. Zie `pinout.md` (GPIO1).

### H6 — 2,4 GHz-kanaalbeheer is niet vastgelegd
ESP-NOW staat **hard op kanaal 1** (slave `WIFI_KANAAL`, master `main.cpp`). Het kanaal van het
**Pi-accesspoint** (dashboard, simulator, MQTT-over-WebSocket) staat **nergens vastgelegd**. Landt dat AP
óók op kanaal 1, dan concurreert al het WiFi-verkeer — vooral **Klokslag dat 4×/s over WebSockets
publiceert** — rechtstreeks met de ESP-NOW-airtime op de single-antenne C3 → vertraagde/verloren detecties
en commando's, enkel zichtbaar onder belasting.
**Afspraak:** pin het **Pi-AP op kanaal 6 of 11** (weg van ESP-NOW's kanaal 1), en doe op de **speeldag een
kanaalscan** van de omgeving (buren-WiFi) om het minst drukke kanaal te kiezen. Zie `CLAUDE.md` +
`docs/versions.md`.

### H7 — LED-data op strapping-pin GPIO0 *(niet geïmplementeerd — PCB rev-B)*
De WS2812B-data hangt aan **GPIO0**, een **strapping/boot-pin**: bij een **brownout-reset** (spannings-dip,
bv. LED-stroomstoot) kan de pin naar het verkeerde niveau getrokken worden en de C3 in **USB-download-modus**
duwen → de paal is "dood" (draait het programma niet) tot een power-cycle. Dit is het **meest reële** risico
van de vier. **Rev-B:** LED-data naar een vrije **niet-strapping** pin (bv. GPIO10 of GPIO7) en GPIO0
vrijhouden of met een pull-up borgen. Zie `pinout.md` (GPIO0).

### H10 — Centrale hub = single point of failure *(operationeel, geen PCB)*
Aan de ene Raspberry Pi 4 (1 GB) hangen **3 masters, audio, het WiFi-AP, Node-RED én de MQTT-broker**, op
**één SD-kaart** (klassieke Pi-faalmodus). Zonder plan-B ligt bij een SD-corruptie of stroom-uitval **het
hele spel** stil. **Strategie (fysiek voor te bereiden):** (1) een **gekloonde reserve-SD** in de kist
(periodiek her-klonen), (2) voeding via de Pi-lader op een **1000 Wh power station** (dip reboot de Pi niet), (3)
optioneel een **kant-en-klare reserve-Pi**. De spelstand zelf overleeft een herstart dankzij
`contextStorage` + retained `spel/state`. Volledig stappenplan: **`docs/handleidingen/hub-noodherstel.md`**
("hub vervangen in 10 min").

---

> Zie ook: `docs/hardware/pinout.md` (GPIO-toewijzing), `docs/hardware/playfield.md` (geometrie),
> `docs/protocol.md` (`MSG_FOUT`/batterij-berichten), `docs/handleidingen/slave.md` (provisioning +
> batterijparameters).
