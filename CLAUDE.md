# Magnum Opus — Project Context voor Claude Code

Dit is een persoonlijk hobbyproject van Nic (cheesefarmer69 op GitHub).
Werktaal: Nederlands voor commentaar/uitleg, Engels voor code-namen.

## Wat is dit project?

Een interactief spel gespeeld op een 24-hoekig speelveld van ca. 23m doorsnede.
Het speelveld heeft de vorm van een donut: er is een binnenste cirkel die geen
deel uitmaakt van het speelveld, zodat alleen de ring tussen die binnenste
cirkel en de buitenste rand bespeelbaar is.

Op het midden van elke buitenste zijde van de 24-hoek staat een paaltje
(dus NIET op de hoekpunten), in totaal 24 palen.
Spelers dragen Bluetooth Low Energy beacons. De palen detecteren welke
spelers binnen hun zijvak zijn en sturen die data naar een centrale hub, die
de spellogica orkestreert en commando's terugstuurt (LEDs, geluid, etc.).

## Architectuur

```
[Slaves (ESP32-C3)] --ESP-NOW--> [Master (ESP32 WROOM)] --USB Serial--> [Pi: serial-bridge] --MQTT--> [Node-RED]
                                                                              ^                         |
                                                                              |                         |
                                                                              +-------MQTT--------------+
                                                                              ^
                                                              [pi/simulator/ — browser, MQTT-over-WS poort 9001]
```

- **Slaves**: één per paal, scannen BLE-beacons van spelers en uitvoeren commando's
- **Master**: ontvangt batches van alle slaves via ESP-NOW, communiceert seriëel met Pi
- **Pi**: vertaalt seriëel ↔ MQTT, draait Node-RED voor spellogica en MQTT-broker
- **Simulator** (`pi/simulator/`): browser-app, verbindt via MQTT-over-WebSocket (poort 9001).
  Monitor-modus: passief meekijken. Simulatie-modus: publiceert `plaatjes/data` als hardware-vervanger.

## Hardware

- **Master**: ESP32 WROOM-32, USB verbonden met Pi via CH340 USB-UART (vendor 1a86, product 7523)
- **Slaves**: ESP32-C3 SuperMini , één per paal, 24 palen
- **Master/slave-indeling**: Master 1 stuurt slaves 1–8 aan, Master 2 slaves 9–16, Master 3 slaves 17–24
- **Hub**: Raspberry Pi 4 model B 1GB RAM (IP 192.168.1.43, statisch), Raspberry Pi OS
- **Custom PCB**: ontworpen in EasyEDA, besteld bij JLCPCB (zie docs/hardware/)

## Software-stack

- **Firmware**: Arduino-framework via PlatformIO in VS Code
- **Serial-bridge**: Python 3.11-slim in Docker container (network: host)
- **Audio-player**: Python 3.11-slim + alsa-utils in Docker (network: host, `--device=/dev/snd`).
  Subscribet `audio/afspelen` en speelt WAV-segmenten via `aplay` over de aux-jack. Zie `pi/audio-player/` en `docs/handleidingen/audio-player.md`.
- **MQTT broker**: Eclipse Mosquitto in Docker
- **Orchestratie**: Node-RED in Docker

Voor exacte versies: zie `docs/versions.md`.

## Library- en versie-conventies (kritisch)

- ESP32 Arduino core: 2.0.17 (via platform = espressif32 @ 6.5.0)
- NimBLE-Arduino: 1.4.2 — **NIET upgraden naar 2.x** (breaking changes)
- FastLED: 3.10.3
- paho-mqtt: 2.x — gebruik `mqtt.CallbackAPIVersion.VERSION2` syntax
- pyserial: 3.5

## Communicatieprotocol

Zie `docs/protocol.md` voor exacte datastructs en JSON-formaten.

Snelle samenvatting:
- **Slave → Master (ESP-NOW)**: `batch_message` met paal_id + array van detected spelers (mac + rssi)
- **Master → Slave (ESP-NOW)**: `commando_message` met paal_id + actie_id
- **Master ↔ Pi (Serial)**: JSON per regel, 115200 baud, `\n`-afgesloten
- **Pi ↔ Node-RED (MQTT)**: topics `plaatjes/data` (uit) en `commando/master1` (in)

`__attribute__((packed))` aan beide kanten van ESP-NOW gebruiken om alignment-issues
tussen Xtensa (WROOM) en RISC-V (C3) te voorkomen.

## Code-conventies

- Geen `delay()` in main loops — alles non-blocking met `millis()`
- Seriële baudrate altijd 115200
- Slave-MAC's staan in de **gedeelde** tabel `firmware/shared/paal_macs.h` (`MAC → PAAL_ID`, één bron van waarheid voor slave én master). De slave zoekt bij boot zijn eigen MAC op → runtime `PAAL_ID` (**één binary voor alle 24 borden**); de master vult daaruit `slaveAdressen[]` voor zijn bereik. (Was: per-master `slave_macs.h` + compile-time `PAAL_ID`.)
- Commentaar in het Nederlands waar logisch, code-namen in het Engels
- C++ structs voor ESP-NOW: altijd `__attribute__((packed))`
- Python: f-strings, type hints waar logisch, geen overdreven OOP

## Workflow

### Firmware
1. Bewerk in VS Code via PlatformIO
2. Selecteer de environment onderin de status bar:
   - **Master**: `master1` (palen 1–8), `master2` (9–16) of `master3` (17–24). Eén codebase;
     het paalbereik komt uit `build_flags` (`PAAL_MIN/PAAL_MAX/MASTER_NR`); de slave-MAC's uit de gedeelde
     `firmware/shared/paal_macs.h`. Flash de juiste env naar de juiste fysieke master.
   - **Slave**: **één binary voor alle 24 borden** — je zet **niets** meer per bordje. De slave leest bij
     boot zijn eigen MAC en zoekt zijn `PAAL_ID` op in `firmware/shared/paal_macs.h` (staat het MAC er niet
     in → fout-blink + doet niet mee). Nieuw bord: flash de binary, lees het MAC uit de banner, voeg één
     regel `{mac, paal}` toe aan `paal_macs.h`, herflash de betrokken master.
3. Build (vinkje) → Upload (pijl) → Serial Monitor (stekker)

### Pi-code
1. Bewerk lokaal in VS Code (`pi/serial-bridge/`)
2. Commit en push naar GitHub
3. Op de Pi: `cd ~/Magnum_Opus && git pull && ./pi/deploy.sh`
4. Het deploy-script herbouwt en herstart **alleen** de serial-bridge container
5. Node-RED en Mosquitto worden NIET aangepast door deploy.sh

### Node-RED
- Bewerk in browser-UI op http://192.168.1.43:1880 **of** rechtstreeks in
  `pi/node-red/flows.json` (chirurgische edits, zie memory).
- **Flows deployen = `pi/node-red/deploy-flows.ps1` (Windows) of `deploy-flows.sh`
  (Pi).** Dit pusht `flows.json` via de Node-RED Admin API (`POST /flows`, full).
  ⚠️ **`docker restart magnum-Opus` herlaadt de repo-`flows.json` NIET** — de
  container draait op zijn eigen `/data/flows.json`. Na elke wijziging aan
  `flows.json` dus **altijd `deploy-flows.ps1` draaien**, anders verandert er niets
  in de draaiende Node-RED.
- Exporteer flows uit de browser-UI terug naar `pi/node-red/flows.json` als je in de
  UI hebt bewerkt (anders raakt de repo achter).
- MQTT broker-server in Node-RED config: `192.168.1.43` (niet 127.0.0.1 — Node-RED draait in een bridge-netwerk, niet host-netwerk)

## Belangrijke configbestanden

- `firmware/Master/platformio.ini` — versies + de drie master-envs (`master1/2/3`, paalbereik via `build_flags`)
- `firmware/shared/paal_macs.h` — **gedeelde** `MAC → PAAL_ID`-tabel (slave zoekt eigen `PAAL_ID`, master vult `slaveAdressen[]`); via `-I ../shared` in beide `platformio.ini`
- `firmware/Slave/platformio.ini` — versies en build config slave
- `pi/serial-bridge/bridge.py` — Python bridge code
- `pi/serial-bridge/Dockerfile` — bridge container definitie
- `pi/serial-bridge/requirements.txt` — Python dependencies met pinned versions
- `pi/audio-player/player.py` — audio-afspeelservice (WAV-segmenten via aplay)
- `pi/audio-player/audio/` — gestructureerde WAV-map (volume-mount, geen rebuild nodig)
- `pi/deploy.sh` — deploy script voor de serial-bridge
- `pi/deploy-audio.sh` — deploy script voor de audio-player container
- `config/mqtt/mosquitto.conf` — MQTT broker config (anonymous access toegestaan, lokaal netwerk)
- `config/udev/99-esp-masters.rules` — udev rule die alle CH340-tty's op `MODE 0666` zet (poort-onafhankelijk; bridge detecteert masters zelf)

## Bekende valkuilen en aandachtspunten

- udev rule is **poort-onafhankelijk**: ze zet enkel `MODE 0666` op elke CH340-tty zodat de bridge-container erbij kan, ongeacht de USB-poort. De serial-bridge detecteert masters zelf (CH340 VID/PID) en routeert per `paal_id` — geen vaste `ttyMaster1`-symlink meer nodig.
- **Route-leren in de bridge gebeurt via de master-`announce`** (`{"announce":1,"master":N,"paal_min":..,"paal_max":..}`, elke ~3 s + bij boot, zie `docs/protocol.md §3`). Vroeger leerde de bridge `commando/masterN → poort` enkel uit een batch/heartbeat mét `paal`, dus zonder rapporterende slave bleef de route ongeleerd en werden commando's genegeerd (`Nog geen poort geleerd`). De master kondigt zich nu proactief aan; de bridge (her)koppelt de poort en publiceert de announce niet naar MQTT. Een master moet wél met de **juiste env** geflasht zijn (`MASTER_NR`), anders kondigt hij de verkeerde master aan (zichtbaar in de `[ROUTE]`-log).
- BLE-scan op C3 moet wijken voor ESP-NOW listening — de C3 heeft maar 1 antenne. Volgorde: WiFi.mode → esp_now_init → NimBLEDevice.init (BLE eerst initialiseren breekt ESP-NOW).
- `esp_now_peer_info_t` mag **lokaal** in setup gedeclareerd worden, mits volledig ge-`memset` en alleen gebruikt vóór `esp_now_add_peer()` terugkeert (die kopieert de struct). De oude valkuil betrof een struct die búíten zijn scope nog gebruikt werd — niet de lokale declaratie zelf.
- Container `serial-bridge` draait in `--network host` mode. Andere containers (Node-RED) in default bridge mode.
- Docker image tags zijn nog `latest` voor Mosquitto en Node-RED. Aandachtspunt: pin naar specifieke versies wanneer projecten stabiel zijn.
- Mosquitto WebSocket-listener op poort 9001 vereist `log_dest stdout` in `config/mqtt/mosquitto.conf`. Bij `log_dest file ...` crasht de container omdat `/mosquitto/log/` niet bestaat in het Docker-image.
- **BLE-scan-duur is runtime instelbaar** (`MSG_SCAN_CONFIG`/actie 20, dashboard "Scan-duur (BLE)"): de slave scant **niet-blokkerend**, begrensd met `millis()` (`scanDuurMs`, default 1000, clamp 300–2000) — NimBLE 1.4.2 kan enkel in hele seconden blokkeren. Kortere scan = versere detectie = minder scoring-latentie. Volatile → Node-RED herstelt na reboot via de heartbeat. Zie `docs/locatiebepaling.md` + `docs/protocol.md`.
- **Settle-grace** (`global.pofSettleGrace`, default 3 s) in de PoF-engine: de verplaatsingscontrole draait op **T+grace** i.p.v. T (nieuwe fase `grace` in "Engine tick") zodat trage paalwissels nog in het juiste event landen. Instelbaar via Systeeminstellingen (`sim/systeem-config`). Zie `docs/spel/event-systeem.md §4` + invariant V9.
- De **volledige actie-tabel** (0–20) staat in `docs/protocol.md §2` — dé bron. Voeg nooit een actie/berichttype toe zonder die tabel + de slave/master-firmware samen bij te werken.
- **2,4 GHz-kanaal (H6):** ESP-NOW zit **hard op kanaal 1** (slave `WIFI_KANAAL`, master `main.cpp`). Pin het **Pi-accesspoint op kanaal 6 of 11** (weg van kanaal 1), anders concurreert dashboard-/Klokslag-WebSocket-verkeer met de ESP-NOW-airtime op de single-antenne C3 → gemiste detecties/commando's onder belasting. Doe op de speeldag een kanaalscan. Zie `docs/hardware/hardware-info.md`.
- **Batterij:** de slave meldt firmware-kritiek < **3,2 V** (`MSG_FOUT`); Node-RED toont vanaf < **3,5 V** de niet-blokkerende dashboard-waarschuwing "vervang batterij" (foutcode ST-005, `BATT_VERVANG_V` in "Evalueer spelstatus"). **Hot-swap** kan tijdens het spel — de slave reboot, heartbeat herstelt hem, het spel loopt door. Zie `docs/hardware/hardware-info.md`.
- **Spelerslijst (H8):** de baken-MAC → naam-koppeling is **dashboard-bewerkbaar** (Beacons & Locatie → "Spelers / bakens beheren") en **retained op `config/spelers`** (overleeft deploy/herstart, wint van de flows.json-seed `[CONFIG] Spelerslijst`, die enkel nog bootstrap is). Baken wisselen zonder deploy: wapper het bij een paal → kies speler → Koppel. Zie `docs/locatiebepaling.md`.
- **Hub = single point of failure (H10):** Pi 4 + 3 masters + audio + AP + Node-RED + broker op één SD-kaart. Reserve-SD (gekloond) + powerbank-pass-through + runbook `docs/handleidingen/hub-noodherstel.md`. De spelstand overleeft een herstart dankzij `contextStorage` (`settings.js`) + retained `spel/state`.
- **Doelwit-dichtheid (G3):** het aantal doelwitten voor een string-optie (`laag/midden/hoog`) schaalt met N (actieve spelers) via `global.doelwitDichtheid` (default 0,25; dashboard "Spelbalans" op Bediening) → consistent % van het veld bij weinig én veel spelers. `enkel`/vast getal/array/`alle` schalen niet. Groep-events krijgen een tier-boost bij N>15. **De weging-hook zit in `Kies event` ÉN `Bouw pof/status`** (vooruit-geplande wachtrij) — pas beide samen aan. Zie invariant EV6.
- **Geheugen op de 1 GB-Pi (L4):** 1 GB volstaat mits de global-caps: `spelHistorie` ≤30 (3 unshift-plekken), `pofSnapshots` diepte 10 zonder `pofHuidigSpel`-kloon, `globaleStats.skills` ≤50. Onbegrensde globals zitten óók in de 30 s-`spel/state`-dump + `contextStorage`-disk (elke 15 s). Houd open dashboard-/simulator-tabs beperkt. Zie `docs/hardware/hardware-info.md` ("Geheugen") + invariant S6.


## Voor Claude Code — werkstijl

- Eerst lezen, dan voorstellen, dan wijzigen. Geen grote refactors zonder overleg.
- Bij wijzigingen aan communicatie: pas eerst `docs/protocol.md` aan, dan de code.
- Bij firmware-wijzigingen: bouw met PlatformIO build, controleer dat het compileert voor de wijziging gecommit wordt.
- Bij Python-wijzigingen: let op de threading (`serieel_lock`) en MQTT-reconnect.
- Geen hardcoded credentials of secrets in commits.