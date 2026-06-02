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
- **Master/slave-indeling**: Master 1 stuurt slaves 1–7 aan, Master 2 slaves 8–16, Master 3 slaves 17–24
- **Hub**: Raspberry Pi 4 model B 1GB RAM (IP 192.168.1.43, statisch), Raspberry Pi OS
- **Custom PCB**: ontworpen in EasyEDA, besteld bij JLCPCB (zie docs/pcb/)

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
- Slave MAC-adressen worden hardcoded in master's `slaveAdressen[]` array
- Commentaar in het Nederlands waar logisch, code-namen in het Engels
- C++ structs voor ESP-NOW: altijd `__attribute__((packed))`
- Python: f-strings, type hints waar logisch, geen overdreven OOP

## Workflow

### Firmware
1. Bewerk in VS Code via PlatformIO
2. Selecteer environment (Master of Slave) onderin de status bar
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

- `firmware/Master/platformio.ini` — versies en build config master
- `firmware/Slave/platformio.ini` — versies en build config slave
- `pi/serial-bridge/bridge.py` — Python bridge code
- `pi/serial-bridge/Dockerfile` — bridge container definitie
- `pi/serial-bridge/requirements.txt` — Python dependencies met pinned versions
- `pi/audio-player/player.py` — audio-afspeelservice (WAV-segmenten via aplay)
- `pi/audio-player/audio/` — gestructureerde WAV-map (volume-mount, geen rebuild nodig)
- `pi/deploy.sh` — deploy script voor de serial-bridge
- `pi/deploy-audio.sh` — deploy script voor de audio-player container
- `config/mqtt/mosquitto.conf` — MQTT broker config (anonymous access toegestaan, lokaal netwerk)
- `config/udev/99-esp-masters.rules` — udev rule voor stabiele /dev/ttyMaster1 symlink

## Bekende valkuilen en aandachtspunten

- udev rule is gebonden aan fysieke USB-poort 1-1.4 op de Pi. Master in andere poort = geen ttyMaster1.
- BLE-scan op C3 moet wijken voor ESP-NOW listening — de C3 heeft maar 1 antenne. Volgorde: WiFi.mode → esp_now_init → NimBLEDevice.init (BLE eerst initialiseren breekt ESP-NOW).
- `esp_now_peer_info_t` als static declareren in setup om stack-corruption te vermijden.
- Container `serial-bridge` draait in `--network host` mode. Andere containers (Node-RED) in default bridge mode.
- Docker image tags zijn nog `latest` voor Mosquitto en Node-RED. Aandachtspunt: pin naar specifieke versies wanneer projecten stabiel zijn.
- Mosquitto WebSocket-listener op poort 9001 vereist `log_dest stdout` in `config/mqtt/mosquitto.conf`. Bij `log_dest file ...` crasht de container omdat `/mosquitto/log/` niet bestaat in het Docker-image.


## Voor Claude Code — werkstijl

- Eerst lezen, dan voorstellen, dan wijzigen. Geen grote refactors zonder overleg.
- Bij wijzigingen aan communicatie: pas eerst `docs/protocol.md` aan, dan de code.
- Bij firmware-wijzigingen: bouw met PlatformIO build, controleer dat het compileert voor de wijziging gecommit wordt.
- Bij Python-wijzigingen: let op de threading (`serieel_lock`) en MQTT-reconnect.
- Geen hardcoded credentials of secrets in commits.