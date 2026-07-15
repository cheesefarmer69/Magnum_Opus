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
- **Hub**: Raspberry Pi 4 model B 1GB RAM, Raspberry Pi OS. Dual-homed:
  - thuis: eth0 via thuisrouter = `192.168.1.43` (dev/deploy);
  - veld (geen router): eigen WiFi-AP `MagnumOpus` op wlan0 = `192.168.50.1` (NetworkManager-profiel
    `MagnumOpus-AP`, WPA2, kanaal 6, ipv4 shared — géén hostapd) + kabel-profiel `Veld-eth` op
    eth0 = `192.168.51.1` (shared; fallback als DHCP thuis faalt). Zie `docs/handleidingen/verbinden-met-de-hub.md`.
- **Custom PCB**: ontworpen in EasyEDA, besteld bij JLCPCB (zie docs/hardware/)

## Software-stack

- **Firmware**: Arduino-framework via PlatformIO in VS Code
- **Serial-bridge**: Python 3.11-slim in Docker container (network: host)
- **Audio-player**: Python 3.11-slim + alsa-utils in Docker (network: host, `--device=/dev/snd`).
  Subscribet `audio/afspelen` en speelt WAV-segmenten via `aplay` over de aux-jack. Zie `pi/audio-player/` en `docs/handleidingen/audio-player.md`.
- **MQTT broker**: Eclipse Mosquitto in Docker — **gecodificeerd als service in
  `pi/node-red/docker-compose.yml`** (gepinde tag, `restart: unless-stopped`, config-mount uit
  `config/mqtt/mosquitto.conf`, persistent `/mosquitto/data` voor retained messages)
- **Orchestratie**: Node-RED in Docker (zelfde compose)

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
- MQTT broker-server in Node-RED config: `host.docker.internal` (via `extra_hosts: host-gateway`
  in `docker-compose.yml` — niet 127.0.0.1 want Node-RED draait in een bridge-netwerk, en niet
  een hardcoded IP want op het veld heeft eth0 geen `192.168.1.43`)

## Documentatie-ingang

- **`docs/handboek/`** — het complete handboek (veldopzet, testprocedure, technische opbouw,
  spelersuitleg, alle events/dynamieken). Dit is de **narratieve** laag; **normatief** blijven
  `docs/invarianten.md`, `docs/protocol.md` en `docs/spel/*` — houd het handboek daarmee in sync
  bij regel-/protocolwijzigingen.

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
- BLE-scan op C3 moet wijken voor ESP-NOW listening — de C3 heeft maar 1 antenne. De **werkende, veld-geteste** init-volgorde in de slave is: NimBLEDevice.init → WiFi.mode(WIFI_STA) → esp_now_init → kanaal vastzetten → callbacks → peer (zie `firmware/Slave/src/main.cpp` setup()). Deze volgorde NIET herschikken zonder hardwaretest — RF-init-volgorde is gevoelig op de single-antenne C3.
- `esp_now_peer_info_t` mag **lokaal** in setup gedeclareerd worden, mits volledig ge-`memset` en alleen gebruikt vóór `esp_now_add_peer()` terugkeert (die kopieert de struct). De oude valkuil betrof een struct die búíten zijn scope nog gebruikt werd — niet de lokale declaratie zelf.
- Container `serial-bridge` draait in `--network host` mode. Andere containers (Node-RED, Mosquitto) in default bridge mode met published ports.
- **SD-bescherming (speldag-hardening, juli 2026):** Docker-log-rotatie host-breed via `/etc/docker/daemon.json` (`max-size 10m`/`max-file 3`; geldt per container na recreate) + dezelfde caps in compose en deploy-scripts; `deploy.sh`/`deploy-audio.sh` doen `docker image prune -f`; de bridge print per-bericht-`[DATA]`/`[DEBUG]`-regels enkel met `VERBOSE=1` (env). `status_lastSeenMac` krijgt een TTL-prune (onbekende MACs > 5 min weg — omstander-telefoons roteren hun BLE-MAC) en `beaconBuf` pruned zichzelf tijd-gebaseerd. Zie `docs/hardware/hardware-info.md` ("Opslag") + invariant S6.
- Mosquitto WebSocket-listener op poort 9001 vereist `log_dest stdout` in `config/mqtt/mosquitto.conf`. Bij `log_dest file ...` crasht de container omdat `/mosquitto/log/` niet bestaat in het Docker-image.
- **BLE-scan-duur is runtime instelbaar** (`MSG_SCAN_CONFIG`/actie 20, dashboard "Scan-duur (BLE)"): de slave scant **niet-blokkerend**, begrensd met `millis()` (`scanDuurMs`, default 1000, clamp 300–2000) — NimBLE 1.4.2 kan enkel in hele seconden blokkeren. Kortere scan = versere detectie = minder scoring-latentie. Volatile → Node-RED herstelt na reboot via de heartbeat. Zie `docs/locatiebepaling.md` + `docs/protocol.md`.
- **Settle-grace** (`global.pofSettleGrace`, default 3 s) in de PoF-engine: de verplaatsingscontrole draait op **T+grace** i.p.v. T (nieuwe fase `grace` in "Engine tick") zodat trage paalwissels nog in het juiste event landen. Instelbaar via Systeeminstellingen (`sim/systeem-config`). Zie `docs/spel/event-systeem.md §4` + invariant V9.
- De **volledige actie-tabel** (0–24) staat in `docs/protocol.md §2` — dé bron. Voeg nooit een actie/berichttype toe zonder die tabel + de slave/master-firmware samen bij te werken. Acties **22 `ACTIE_KNOP_GOED`** / **23 `ACTIE_KNOP_FOUT`** = korte groen/rood-flits (auto-stop na `KNOP_FLITS_MS`) + positief/negatief zoemerdeuntje; FIFO/ACK (geldig gemaakt in `verwerkCommandos()`), voor de drukknop-events-modus + knoppendans. Actie **24 `ACTIE_ONTPLOFFING`** = witte knal → uitdovende rode strobe (`ONTPLOF_MS` 1,6 s) + dalende sirene-sweep; gestuurd bij een mislukte tijdbom-ontmanteling én bij een afgelopen bom-teller. Geen struct-/`msg_type`-wijziging → **alleen slaves herflashen**. ⚠️ Een nieuwe actie moet óók in de **ACK-whitelist** in `verwerkCommandos()` — staat ze daar niet, dan wordt ze geweigerd (ACK-status 1) en nooit uitgevoerd.
- **Tijdbom-ontmantelknop moet GEARMD worden (`ACTIE_KNOP_ARM`, 17):** de slave negeert elke druk zolang `knopArmed` false is (`if (!knopArmed) return;` in de knop-ISR). "Voer gevolg uit" armt daarom elke gekozen ontmantelpaal; "Knop-verwerking"/"Tijdbom-beheer" ontwapenen ze weer (18) zodra de kans verbruikt of de bom weg is. Vóór juli 2026 gebeurde dat níét → de ontmantelknop werkte **enkel in de simulator** (die publiceert `{paal,knop:1}` rechtstreeks op `plaatjes/data`) en was op het veld volledig dood. De ontmanteling is **eenmalig**: na de druk gaat de paal uit `tijdbomOntmantelPalen`, verdwijnt zijn `bordStaat`-tijdbom-effect én wordt hij uit `paalLedActie` **verwijderd** (niet op 0 gezet) — anders stuurt "Sync toestanden + LEDs" er een actie 0 achteraan die de flits/ontploffing afkapt.
- **LED-helderheid is runtime instelbaar** (`MSG_LED_CONFIG`/actie 21, dashboard "LED-helderheid" op Beacons & Locatie): globale FastLED-brightness (slider + Min/Middel/Max) op alle palen, slave clamp't 5–255. Componeert met de per-LED-schaling van Klokslag/animaties. Volatile → default 150 bij boot; Node-RED herstelt na reboot via de heartbeat + retained `config/led-helderheid`. Overdag hoog voor zichtbaarheid; "max" (255) ~verdubbelt de LED-stroom (batterij). Zie invariant HW9 + `docs/protocol.md`.
- **Lang-draai-hardening (juli 2026, L5b):** de master-FIFO's worden vanuit twee cores gemuteerd → alle `q.head`/`q.count`-mutaties lopen onder de **`cmdMux`-spinlock** (`taskENTER_CRITICAL`); nooit een FIFO-mutatie buiten die lock toevoegen, en nooit `esp_now_send`/`logRegel` er bínnen (FreeRTOS-verbod). Beide firmwares draaien **`enableLoopWDT()`** (5 s task-WDT, panic=reset) — één loop-pass moet onder de 5 s blijven, dus `SCAN_MS_MAX` (2000) niet verhogen zonder herrekenen. De slave-scan staat op **`setMaxResults(0)`** (callback-only; anders bewaart NimBLE álle omstander-telefoons per cyclus → heap-fragmentatie). Master parset serial **heap-vrij** (`jsonVeld()`, geen Arduino-String). Node-RED: `status_lastSeenMac` niet-roster TTL 60 s + cap 100, `pofPad` ≤60/`pofVrijPad` ≤10 hops per speler, **klok-guards** op alle TTL-prunes (veld heeft geen NTP — timestamp uit de toekomst wordt teruggeclampt). Host: mem-limits (node-red 600m/broker 128m/bridge 64m/audio 96m), healthcheck+autoheal op broker/bridge/audio, CPU-temp-tegel + **ST-007** ≥75 °C (vereist de `/sys/class/thermal`-mount in compose), `config/network/apply-network.sh` (Veld-eth autoconnect uit + AP-kanaal 6) hoort bij elke SD-kloon.
- **Phase-locked commando-retry (juli 2026):** de master herzendt een pending commando **direct** wanneer een batch/heartbeat van die slave binnenkomt (= diens vrije radio-venster ná de BLE-scan, `slaveVensterVlag` + guard 50 ms); `APP_ACK_TIMEOUT` (600 ms) is enkel nog de blinde fallback. **Niet terugtunen naar ~1500 ms** — die waarde hoorde bij de oude blokkerende scan (de huidige slave voert commando's in ~5 ms uit; de bottleneck is RF-capture, want de C3-radio is tijdens de scan ~80 % BLE-bezet). `verwerkLogQueue` is **budget-begrensd** (`Serial.setTxBufferSize(2048)` vóór `begin()`, alleen schrijven wat in de TX-ring past) — nooit een ongelimiteerde while op Serial terugzetten, die stalt de loop 150-200 ms en dropt batt-regels (= "verouderde" palen). De slave her-ACK't een mid-scan verzonden ACK éénmalig ná `pBLEScan->stop()`; kale `delay()`s in de slave-loop zijn `servicedWait()` (commando's blijven bediend). Zie `docs/protocol.md` §0/§2.
- **2,4 GHz-kanaal (H6):** ESP-NOW zit **hard op kanaal 1** (slave `WIFI_KANAAL`, master `main.cpp`). Pin het **Pi-accesspoint op kanaal 6 of 11** (weg van kanaal 1), anders concurreert dashboard-/Klokslag-WebSocket-verkeer met de ESP-NOW-airtime op de single-antenne C3 → gemiste detecties/commando's onder belasting. Doe op de speeldag een kanaalscan. Zie `docs/hardware/hardware-info.md`.
- **Batterij:** de slave meldt firmware-kritiek < **3,2 V** (`MSG_FOUT`); Node-RED toont vanaf < **3,5 V** de niet-blokkerende dashboard-waarschuwing "vervang batterij" (foutcode ST-005, `BATT_VERVANG_V` in "Evalueer spelstatus"). **Hot-swap** kan tijdens het spel — de slave reboot, heartbeat herstelt hem, het spel loopt door. Zie `docs/hardware/hardware-info.md`.
- **Spelerslijst (H8):** de baken-MAC → naam-koppeling is **dashboard-bewerkbaar** (Beacons & Locatie → "Spelers / bakens beheren") en **retained op `config/spelers`** (overleeft deploy/herstart, wint van de flows.json-seed `[CONFIG] Spelerslijst`, die enkel nog bootstrap is). Baken wisselen zonder deploy: wapper het bij een paal → kies speler → Koppel. Zie `docs/locatiebepaling.md`.
- **Hub = single point of failure (H10):** Pi 4 + 3 masters + audio + AP + Node-RED + broker op één SD-kaart. Reserve-SD (gekloond) + powerbank-pass-through + runbook `docs/handleidingen/hub-noodherstel.md`. De spelstand overleeft een herstart dankzij `contextStorage` (`settings.js`) + retained `spel/state`.
- **Doelwit-dichtheid (G3, juli 2026 sub-lineair):** het aantal doelwitten voor een string-optie groeit met **√N** i.p.v. lineair: `clamp(round(mult × √N × (dichtheid/0,25)), 1, min(N,6))` met `mult` 0,35/0,55/0,90 (`laag`/`midden`/`hoog`); `global.doelwitDichtheid` default 0,25 (dashboard "Spelbalans" op Bediening). Bij 31 spelers → **2/3/5** (was 5/8/10 → veld verzadigde). `enkel`/vast getal/array/`vast(Opties)`/`alle` schalen niet. De **aantal**-formule staat enkel in `Kies event`; de **groep-tier-boost** (N>15) staat in `Kies event` ÉN `Bouw pof/status` (vooruit-geplande wachtrij) — pas die twee samen aan. Zie invariant EV6.
- **Geheugen op de 1 GB-Pi (L4):** 1 GB volstaat mits de global-caps: `spelHistorie` ≤30 (3 unshift-plekken), **≤250 events per partij** (rolling in `Kies event` via `eventTeller`; `Bouw spel-state snapshot` saneert oude vette entries; de 1 s-sim-uitzending stuurt op `spel/historie` enkel de laatste 40), `pofSnapshots` diepte 10 zonder `pofHuidigSpel`-kloon, `globaleStats.skills` ≤50. Onbegrensde globals zitten óók in de 30 s-`spel/state`-dump + `contextStorage`-disk (elke 15 s) — een dagenlange testpartij zonder events-cap kostte ~1 MB/s MQTT + 100 % CPU (97 GB in 3 dagen). Houd open dashboard-/simulator-tabs beperkt. Zie `docs/hardware/hardware-info.md` ("Geheugen") + invariant S6.
- **Proportioneel valsspel-model (V11, juli 2026):** valsspelen kost **geen** levensuren meer. Een foute doelwit-zet levert `delta = max(0, legaalBasis − overtreding)` (vloer 0) in **`Verifieer beweging`** (`c6a0000000000027`) — nooit negatief, dus **geen sterfte door valsspel** (de sterfte-op-negatief-tak is een vangnet dat niet meer vuurt). Dodelijke straffen zitten los in M3/N1/tornado/bom/Z4; de enige uitzondering op de vloer is `MIDDERNACHT STIL` (−1, M9). Statussen `PENDELEN` (tijdreizen: heen én terug in één pad) en de suffix `VRIJ GEWANDELD` (V10) horen ook bij de valsspeel-set. Valsspeelpunten + aura en god-punt-consumptie blijven. Pas dit, de scoringtabel in `docs/invarianten.md` §2 én `tools/speltest/config.py` (`FOUT_STATUSSEN`) samen aan. Bij `avondModus` blijft de inversie (winst → kost) apart gelden.
- **Vrij wandelen is verboden (V10, juli 2026):** verplaatsen mag enkel wanneer een event het toestaat. `Bereken levensuren` (`c4a0000000000003`) legt **elke** hop buiten het event-venster vast in **`global.pofVrijPad`** — dus ook in `regroup` (na een nuke) en `idle`; er zijn **geen fase-uitzonderingen** meer. Opname gebeurt enkel zolang `pofActief && spelToestand === "lopend"` (Klokslag/Infected/gestopt spel: niets). `Verifieer beweging` zet dan bij de eerstvolgende controle `delta → 0` + 1 valsspeelpunt (suffix `VRIJ GEWANDELD`). **Geen** uren-verlies, **geen** sterfte — een RSSI-flapper mag niemand doden. Een god-punt vergeeft het (hoogstens 1 punt per controle, ook als er óók een foute zet was). Vrijgesteld: alleen gepauzeerden en body-swap-doelwitten. **`global.pofVrijVanaf`** is een genade-drempel (= `pofSettleGrace`) die na elke controle gezet wordt, plus na `sim/tijd-terug`, zodat een laat settlende of programmatisch herstelde hop niet dubbel telt. Nieuwe globals staan in `resetPartij` (`settings.js`) → **container-herstart** nodig, niet enkel `deploy-flows`.
- **God-punten pas bij spel-einde (D7, juli 2026):** de `+2` verhuisde van `Doel-controle` (`d8a0000000000020`, live) naar `transferStats()` in **`Spel aan/uit`** (`c6a0000000000080`), achter de `doelLocked`+`godAward`-latch. Tijdens de partij stijgt het saldo dus nooit — anders beloont het eindspel de leider die stilstaat. De goal-lock D8 (`doelLocked`/`doelUren`) blijft wél in `Doel-controle`.
- **Thuisbank (TB1–TB4, juli 2026, default UIT):** `global.thuisbankAan` via retained `sim/spel-config` `{badAura, thuisbank}` → `Sla spel-config op` (`spel_config_fn1`), checkbox `#thuisbank` in de simulator-Spelinstellingen. Wie bij de controle **exact op `spelerStats[n].startUur` landt** (en er niet aan de ronde begon) stort `max(0, totaalUren − doelUren)` in `globaleStats` en gaat op 0; geblokkeerd bij ziekte/tijdbom. `startUur` wordt gezet bij Start (uit `spelerLocaties`) en lazily in `Verifieer beweging`; `zeroHuidig()` wist het.
- **Operator-ingrepen op Admin (achter `admin_unlocked`):** "Handmatig bijstellen" corrigeert één spelerveld (`totaalUren`/`sterftes`/`valsspeelpunten`/`godPunten`) in `spelerStats` (S9); "Palen handmatig uit/in" zet `global.palenHandmatigUit` → de **L3-ring** in `Evalueer spelstatus` (`a1b2c3d4e5f60107`) slaat die palen over met behoud van de ≥2-vloer (F5). Speler-**pauze** = volledig uit het spel: niet gescoord in `Verifieer beweging` + genegeerd in Klokslag/Infected/tweeling/etenstijd, en `gespauzeerdePlayers` zit in de `spel/state`-dump/rehydrate (overleeft herstart, S8b). "Tijd terug" (undo, `pofSnapshots`) heeft nu een knop op Bediening.
- **Dashboard 2.0-valkuilen (juli 2026-audit):** (1) een **decoupled switch** (`passthru:false`+`decouple`) toont na een pagina-herlaad ALTIJD "uit" tenzij zijn feedback-node een `topic:"init"`-tak heeft die de echte global leest + een once-inject (patroon: `semi_auto_fb`/`semi_auto_init`) — alle zes hoofdschakelaars (Spel/Pauze/Sim/Manueel/Toon-batterij/Override) hebben die nu; nieuwe decoupled switches MOETEN dat patroon meekrijgen. (2) `msg.options` naar een `ui-dropdown` alleen **bij wijziging** pushen (optSig-guard in context) — ongeconditioneerd elke 2 s reset de selectie van de operator ("dropdown springt terug" → oogt als kapot). (3) De **NO-GO-gate** zit in `Spel aan/uit` (`c6a0000000000080`, 4e output zet de switch visueel terug bij een geweigerde start); "Verwerk bediening" (`b3d1000000000006`) doet enkel nog pauzeer/hervat/toon. (4) Tabel-feeders hebben een **change-guard** (`pubSig` in context; datastore replay't de laatste msg aan nieuwe clients) — niet weghalen "omdat de tabel dan vaker ververst". (5) De ranglijst-pagina is **Leaderbord** (`lb1…`); de oude duplicaat-pagina is verwijderd.
- **Dashboard-indeling (juli 2026):** de **Globaal**- en **Noodstop**-groepen zijn weg uit Bediening. Cumulatieve stats staan op een aparte **Leaderboard**-pagina (sorteer op `totaalUren`, niet `totaalUren%24`); stoppen loopt via de Speltoestand-schakelaar. Bovenaan **Spelstatus** toont een `ui-template` per master een groen/rood bolletje, afgeleid uit `status_lastSeenPaal` (NR10). Geen firmware/bridge-wijziging.
- **Veld-waarheid & robuustheid (S1/L3/G1/R4/C8):** "Evalueer spelstatus" pruned in hardware-modus **ghost-spelers** uit `spelerLocaties` (`spelerPruneMs`, default 90 s; tijdens nuke `nukeEscapeMs`) én haalt **stille palen** (> 60 s) tijdelijk uit `palenActief` (terug bij heartbeat; nooit < 2). Gepauzeerden tellen ook in Klokslag/Infected niet mee. De reactietijd heeft een **sensing-vloer** (SP6, ~7 s bij default-tuning) — tempo-stapeling kan fysiek correcte zetten niet meer bestraffen. `resetPartij()` verhoogt `pofGeneration`; geplande reveal-timeouts checken dat token (geen na-vuur op een gestopt spel). De bridge alarmeert **ST-006** bij twee borden met hetzelfde `MASTER_NR`. Zie invarianten EV3/F4/SP6/NR9/C8.

- **Gedeelde `tweelingDood`-helper (TW3, fase 2):** de mee-sterf-regel staat op **één** plek — `functionGlobalContext.tweelingDood(global, namen)` in `pi/node-red/settings.js`. Aangeroepen door `Verifieer beweging` (sterfte-snapshot), `Middernacht` (oogst) en `Ziekte-beheer` (dood bij 0 én de Z9-wipe), telkens met een `typeof`-guard + `node.warn`-degradatie. De **nuke** roept hem bewust **niet** aan (`TW5`/`N8`: een nuke breekt geen banden). Zoals `resetPartij` vereist een wijziging aan `settings.js` een **container-herstart**, niet enkel `deploy-flows.ps1`.
- **Gedeelde `resetSpeler`-helper (S10, juli 2026):** `functionGlobalContext.resetSpeler(global, naam)` in `settings.js` (naast `resetPartij`/`tweelingDood`) haalt **één** speler uit **alle** toestanden en laat elk doelwit waarvoor hij gekozen was vervallen. Aangeroepen door de Admin-knop "Reset speler-toestand" (`speler_reset_fn`). **Wist wél:** ziekte, tijdbom, speler-effecten, dienaar-**én**-meester, `luisterNaam` als key **én** als waarde, tweelingband (**zonder dood** — dus níét `tweelingDood`), etenstijd (wolf → jacht op, schaap → enkel hijzelf), infected, `geenWinstVolgende`/`mnGestraft`/`pofPad`/`pofVrijPad`/`pofGenezen`, en `pofVerificatie.doelwit`/`pofHuidigEvent.doelwit`/`pofDoelBereikt`/`pofLaatsteControle`. **Wist niet:** score (S9 is daarvoor), locatie, pauze-stand. Container-herstart nodig.
- **Engine-modi (EV9, juli 2026):** naast `pofManueel` bestaat nu **`pofSemiAuto`** ("Met timer", Bediening → Speltoestand): alles automatisch (aanloop/event/reveal/reactietijd/controle/afroep) maar ná de controle fase **`wacht`** → het volgende event start pas op de knop. **Semi wint van manueel** (dan telt de reactietijd tóch af, geen `wacht_controle`). `global.testModus` hangt op dezelfde vlag mee. De beslissing staat op **zes** plekken die samen moeten wijzigen: `Engine tick` (spelstart, `pof_volgende`-gate + aanloop, einde `regroup`), `Verifieer beweging` (tornado, bomaanslag, normale controle) en `Spel aan/uit` (beginfase). `Voer gevolg uit` bepaalt enkel of er een `wacht_controle` komt.
- **Test-modus (TM1–TM3, juli 2026):** schakelaar **🧪 Test** in de simulator-topbalk → retained `sim/test-modus` → `test_modus_fn1`. **AAN** = momentopname van **56 registers** in `global.testSnapshot` (incl. `spelerStats`, **`globaleStats`**, **`spelHistorie`**, `spelNummer` en de **middernacht-π-klok** — anders vervuilt een testronde alsnog het leaderboard/de historiek). **UIT** = exact terugzetten + `pof/ziekte`/`pof/tijdbom`/`pof/toestanden`/`pof/dienaars` herpubliceren + LED-rebuild. `spelerLocaties` zit **bewust niet** in de snapshot (fysieke waarheid); het herstel zet `pofVrijPad`/`pofVrijVanaf` zodat het geen V10-straf oplevert. Idempotent bij een herhaald retained bericht.
- **Volume van de box (juli 2026):** Admin → "Geluid (box)" → retained **`audio/volume`** → `player.py` draait `amixer -q **-M** -c $MIXER_CARD sset <control> N%`. **`-M` is essentieel** (de bcm2835-schaal is dB-lineair; zonder `-M` doet de onderste helft van de schuif niets). Het volume gaat **niet** door de afspeel-queue (werkt dus midden in een lopend segment) en herstelt zichzelf na een herstart dankzij het retained topic. `player.py` spoort de mixer-control zelf op (`Headphone` op Bookworm, `PCM` op oudere images). Wijziging aan `player.py` vereist **`./pi/deploy-audio.sh`** (rebuild — het zit ín de image).
- **Pi hangt 's nachts (juli 2026):** `restart: unless-stopped` herstart enkel een **gecrashte** container, niet een **hangende**. Daarom heeft node-red nu een **healthcheck** (haalt echt `http://127.0.0.1:1880/` op) + een **`autoheal`**-container die `unhealthy` containers herstart (Docker doet dat zélf niet). ⚠️ `docker compose pull` één keer **thuis** draaien — op het veld is er geen internet. De **waarschijnlijkste** oorzaak van "moet elke dag rebooten" is echter het **netwerk**: faalt thuis-DHCP, dan kaapt het veldprofiel `Veld-eth` eth0 en hangt de Pi op `192.168.51.1`. Fix: vast IP/DHCP-reservering + `nmcli con mod Veld-eth connection.autoconnect no`. Triage-recept in `docs/handleidingen/verbinden-met-de-hub.md`.
- **Tweeling-scoring (TW2/TW6, fase 2):** je verdient enkel als je partner **ook legaal bewoog**; anders wordt de al toegekende winst in `Verifieer beweging` **teruggedraaid** via de registers `_winst`/`_winstNaar` (die laatste vangt de dienaar→meester-redirect op). De oude "asymmetrisch → beiden alle uren kwijt"-straf is weg. Beide op hetzelfde uur eindigen **breekt de band** (geen beloning). Corrigeer bij een clawback óók `rij._delta`, anders liegt `pof/controle`.
- **Wolf (ET1b/ET2b, fase 2):** de wolf komt uit de **laagste 5 van `globaleStats.totaalUren`** buiten de kleur-groep (`Voer gevolg uit`), en zijn vangst telt in `Verifieer beweging` **alleen** als zijn basis-status met `OK` begint én `pofVrijPad` leeg is. Daarvoor houdt de spelerslus `_basis[naam]` + `_vrijGelopen[naam]` bij — voeg nieuwe statussen dus met een `OK`-prefix toe als ze legaal moeten tellen.
- **Identiteitscrisis raakt alleen naam + kleur (fase 2):** `kiesGroep()` in `Kies event` gebruikt `_attrVan(n)` — `kleur` komt van `luisterNaam`, terwijl `jaar`/`maand`/`seizoen` van de speler zelf blijven. `pariteit` staat los (positie). De alfabetische verschuiving sorteert op de **volledige** sleutel in `spelerEigenschappen` (achternaam incluis), dus "Alix Blond" < "Alix Bruin".
- **Middernacht M10 (fase 2):** een bewegings-doelwit dat bij een dichte poort al **op** `middernachtPaal` staat (`gateDist === 0`) krijgt `OK (poort blokkeert)`, `delta 0`, geen valsspeelpunt en **geen** `MIDDERNACHT STIL`. Die −1 (M9) geldt nu enkel nog voor **niet-doelwitten**.
- **Ziekte Z9 (fase 2):** `Ziekte-beheer` (`c6a0000000000071`) heeft een **5e output** naar `audio/afspelen` (`c6a000000000000f`). Staat er geen medicijn meer op het bord terwijl er zieken zijn → allen dood + tweeling-propagatie + de gesproken mededeling. Nieuwe WAV nog op te nemen: `events/afgelopen/alle_zieken_gestorven.wav`.
- **`flows.json` chirurgisch patchen:** node-regels zijn **niet allemaal alfabetisch** gekeyd (`lb00000000000004` niet, de meeste wel). Dump dus **zonder** `sort_keys` (`json.dumps(obj, separators=(",",":"), ensure_ascii=False)`), 4 spaties inspringen, CRLF bewaren — dan is de round-trip byte-identiek. Valideer met `esprima` (ES5: `??` en BigInt falen al op HEAD in 4 nodes) en draai de function-nodes echt uit met `py-mini-racer`.


## Voor Claude Code — werkstijl

- Eerst lezen, dan voorstellen, dan wijzigen. Geen grote refactors zonder overleg.
- Bij wijzigingen aan communicatie: pas eerst `docs/protocol.md` aan, dan de code.
- Bij firmware-wijzigingen: bouw met PlatformIO build, controleer dat het compileert voor de wijziging gecommit wordt.
- Bij Python-wijzigingen: let op de threading (`serieel_lock`) en MQTT-reconnect.
- Geen hardcoded credentials of secrets in commits.