# Communicatieprotocol Magnum Opus

Dit document beschrijft alle datastromen tussen componenten in het systeem.

**Belangrijke regel:** bij wijzigingen — pas EERST dit document aan, dan pas de code.
Dit voorkomt dat componenten uit sync raken.

## Overzicht

```
[Slaves (ESP32-C3)] --ESP-NOW--> [Master (ESP32 WROOM)] --Serial USB--> [Pi: bridge.py] --MQTT--> [Node-RED]
                                                                              ^                       |
[Slaves (ESP32-C3)] <--ESP-NOW-- [Master (ESP32 WROOM)] <--Serial USB-- [Pi: bridge.py] <--MQTT-------+
```

## 1. Slave → Master (ESP-NOW)

Slave detecteert BLE-beacons van spelers en stuurt batches naar de master.
Het systeem heeft **3 masters**, elk met **8 slaves** (palen). Totaal 24 palen.

### Datastruct (C++)

```cpp
typedef struct __attribute__((packed)) {
    int paal_id;              // 1, 2, 3, ... (uniek per slave, hardcoded)
    int aantalGevonden;       // aantal gedetecteerde spelers in deze batch
    float batterij_v;         // gemeten batterijspanning in volt (0.0 = onbekend)
    struct {
        char speler_mac[18];  // formaat: "aa:bb:cc:dd:ee:ff" (lowercase)
        int rssi;             // signaalsterkte in dBm (typisch -30 tot -90)
    } spelers[9];             // max 9 spelers per batch
} batch_message;
```

**Belangrijk:**
- `__attribute__((packed))` aan beide kanten — voorkomt alignment-issues
  tussen Xtensa (WROOM, master) en RISC-V (C3, slave)
- MAC-adressen lowercase — NimBLE returnt ze lowercase
- `spelers[9]` is een vaste array, niet dynamisch; ongebruikte slots
  hebben `aantalGevonden` als afsluiting
- `batterij_v` wordt elke batch meegestuurd. De slave meet via een
  spanningsdeler op `BATTERY_ADC_PIN`. Waarde `0.0` betekent "niet gemeten".

### Frequentie en timing

- Slave scant BLE met scanInterval/scanWindow afgestemd op antenne-coexistence
  (single-antenne C3 moet schakelen tussen ESP-NOW en BLE)
- Batch wordt **elke** scan-cyclus verzonden, óók bij 0 gevonden spelers.
  Zo herkent het systeem een leeggelopen vak en blijft de stand niet hangen.
- **Dedup binnen een batch**: een beacon adverteert meerdere keren per seconde,
  maar elke whitelisted MAC komt maximaal één keer voor in `spelers[]`. Bij
  meerdere advertenties van dezelfde beacon binnen één scan houdt de slave de
  sterkste RSSI. Zonder deze dedup zou `spelers[9]` volstromen met duplicaten
  en zou de master tientallen JSON-regels per seconde doorsturen voor één paal.
- Vóór verzenden wacht de slave een willekeurige tijd (`0..MAX_BACKOFF_MS`,
  standaard 150 ms, hardware-RNG `esp_random()`). Deze random backoff
  ontkoppelt de zendmomenten van meerdere slaves zodat hun pakketten elkaar
  niet structureel wegdrukken bij de master.

## 2. Master → Slave (ESP-NOW)

Master stuurt commando's naar specifieke slaves voor uitvoer (LED, geluid, etc.).

### Datastruct (C++)

```cpp
typedef struct __attribute__((packed)) {
    int paal_id;       // doel-slave (1, 2, 3, ...)
    uint8_t actie_id;  // wat te doen
} commando_message;
```

### Geldige `actie_id` waarden

| ID | Constante | Gedrag |
|----|-----------|--------|
| 0  | `ACTIE_NIETS`       | LEDs uit (CRGB::Black), MOSFET uit |
| 1  | `ACTIE_ROOD`        | LED strip rood, MOSFET aan |
| 2  | `ACTIE_GROEN`       | LED strip groen, MOSFET aan |
| 3  | `ACTIE_BUZZER_AAN`  | Buzzer 1 kHz aan (`tone()`) |
| 4  | `ACTIE_BUZZER_UIT`  | Buzzer uit (`noTone()`) |

Bij `ACTIE_ROOD` en `ACTIE_GROEN` wordt de MOSFET eerst HIGH gezet (5 ms
delay) voordat FastLED de LEDs aanstuurt — dit voorkomt een voedingsvalletje
op de LED-strip bij inschakelen.

### Reliability: master retry-queue

De master verstuurt commando's niet meer fire-and-forget. Elk commando gaat
in een interne FIFO-queue. Het actieve commando wordt verzonden met
`esp_now_send()` en wacht op `OnDataSent()`:

- **`ESP_NOW_SEND_SUCCESS`** = slave-radio heeft het pakket ontvangen
  (MAC-laag ACK). Commando is dan klaar, volgende uit de queue.
- **`!SUCCESS`** of geen callback binnen 250 ms = retry. Tot max 5 pogingen.
- Na 5 mislukte pogingen geeft de master het commando op met een
  `opgegeven`-log-regel. Dit verschijnt op Serial → Pi → Node-RED.

Dit lost het probleem op waarbij een commando soms gemist werd omdat de
slave net aan het BLE-scannen of zelf aan het zenden was. Snel achter
elkaar verstuurde commando's blijven in volgorde dankzij de queue.

Queue-grootte: 16. Bij vol stuurt master `{"status":"queue_vol"}` terug.

## 3. Master → Pi (Serial USB)

Master stuurt detecties door naar de Pi, één JSON-bericht per regel.

- **Poort op Pi**: `/dev/ttyMaster1` (symlink via udev rule, zie `config/udev/`)
- **Baudrate**: 115200
- **Regelafsluiting**: `\n`
- **Encoding**: UTF-8

### Formaat: detectie

```json
{"paal":1,"mac":"aa:bb:cc:dd:ee:ff","rssi":-67}
```

Eén JSON-object per gedetecteerde speler. Bij een batch met 5 spelers
stuurt master 5 aparte regels.

### Formaat: batterij

```json
{"paal":1,"batt":3.87}
```

Eén regel per ontvangen batch — onafhankelijk van of er spelers in zaten.
Zo blijft de batterij-status van een paal up-to-date óók als er niemand
in de buurt is. Node-RED bewaart de laatste waarde per paal in
`global.status_batterijPaal`.

### Debug-output

Niet-JSON regels (bijvoorbeeld `[SETUP] Master MAC: ...`) worden door
bridge.py als debug-output behandeld: gelogd maar niet doorgestuurd naar MQTT.

## 4. Pi → Master (Serial USB)

Pi stuurt commando's terug naar de master, doorgegeven vanuit Node-RED.

### Formaat: commando

```json
{"paal":1,"actie":1}
```

Master valideert dat `paal_id` binnen `AANTAL_SLAVES` valt en zet het
commando in de retry-queue (zie sectie 2 "Reliability"). Master antwoordt
per regel met een status-JSON:

| Status         | Betekenis |
|----------------|-----------|
| `queued`       | Commando in queue gezet, wordt async verzonden. |
| `ack`          | Slave-radio bevestigd. Bevat `pogingen` (1 = direct gelukt). |
| `send_err`     | `esp_now_send()` gaf geen ESP_OK. Retry volgt automatisch. |
| `opgegeven`    | Na 5 mislukte pogingen — commando verloren. |
| `queue_vol`    | Queue zit aan max (16), commando geweigerd. |
| `onbekende paal` | `paal_id` valt buiten `AANTAL_SLAVES`. |

## 5. Pi ↔ Node-RED (MQTT)

Broker: Eclipse Mosquitto op `192.168.1.43:1883`, anonymous access toegestaan
(lokaal netwerk).

| Topic              | Richting           | Payload                                      |
|--------------------|--------------------|----------------------------------------------|
| `plaatjes/data`    | Pi → Node-RED      | `{"paal":1,"mac":"aa:bb:..","rssi":-67}`     |
| `commando/master1` | Node-RED → Pi      | `{"paal":1,"actie":1}`                       |
| `audio/afspelen`   | Node-RED → audio-player | `{"fase":"event","tekst":"...","segments":["events/x_voor.wav","getallen/3.wav","events/x_na.wav"],"prioriteit":"normaal"}` |
| `locatie/spelers`  | Node-RED → browser | `{"Lilou":5,"Maud":12}` — opgeloste paal per speler (algoritme-uitkomst) |
| `spel/historie`    | Node-RED → browser | `{"actief":true,"start":"...","events":[{"nr":1,"tekst":"...","doelwit":["Lilou"]}]}` |
| `pof/status`       | Node-RED → browser | `{"actief":true,"fase":"reactie","eventNaam":"...","eventTekst":"...","doelwit":[],"doelwitReveal":"• Lilou","getalWaarde":2,"teller":7,"maxTeller":10}` |
| `pof/controle`     | Node-RED → browser | `{"event":"...","resultaten":[{"speler":"Lilou","status":"TE WEINIG","verplaatst":1,"tag":"-"}]}` |

### Plates-of-Fate: doelwit-reveal en `pof/status`

Wanneer een event gekozen is, kiest Node-RED de doelwitten (spelers of uren).
Die worden **één-voor-één** onthuld door een server-side sequencer-function
("Doelwit reveal"): elke ~1,2 s wordt een naam toegevoegd aan de global
`pofDoelwitReveal`. Pas **nadat de laatste naam getoond is** start de
reactietijd-aftelling (de sequencer triggert dan "Voer gevolg uit").

`pof/status` (elke seconde gepubliceerd) draagt de actuele stand:
- `eventNaam` / `eventTekst`: naam en ingevulde tekst van het huidige event.
- `doelwit`: volledige array van gekozen doelwitten.
- `doelwitReveal`: de progressief opgebouwde tekst (`• naam\n• naam`), zodat
  de simulator dezelfde één-voor-één-onthulling toont als het dashboard.
- `fase`: `idle` / `aanloop` / `bezig` / `reactie` / `wacht*`.

Zo tonen het Node-RED dashboard (ui_text "Doelwit") én de browser-simulator
identieke informatie zonder browser-specifieke scripting.

### Plates-of-Fate controle-resultaten (`pof/controle`)

Na de reactietijd controleert "Verifieer beweging" of elke speler aan de
beweging-voorwaarde voldeed. Het resultaat wordt — naast de dashboard-tabel —
ook gepubliceerd op `pof/controle`. Elke `status` (`OK`, `TE WEINIG`, `TE VEEL`,
`OK (stil)`, `BEWOOG (mocht niet)`) is in feite een **foutcode** van het event
na zijn controle. De browser-simulator logt deze regels onder de checkbox
"Foutcodes".

### Locatiebepaling-globals (Node-RED)

De locatiebepaling en beacon-diagnose gebruiken enkele globals — details en
afregeling staan in `docs/locatiebepaling.md`:

- `locParams` — live tuning van venster/hysterese/vloer/grace/switch/min-samples
  (instelbaar via de dashboard-pagina "Beacons & Locatie", geen redeploy nodig).
- `beaconKalibratie` — `{ "<mac>": offsetDb }`, RSSI-offset per beacon.
- `beaconBuf` — interne sample-buffer voor de stabiliteitsanalyse.
- `spelerLocaties` — `{ spelerNaam: paalId }`, de centrale waarheid.

### MQTT-config in Node-RED

- **Server**: `192.168.1.43` (NIET `127.0.0.1` — Node-RED draait in bridge-netwerk)
- **Port**: 1883
- **Protocol**: MQTT V3.1.1
- **QoS**: 2 voor commando's (exactly-once), 0 of 1 voor data acceptabel

### MQTT-config in bridge.py (serial-bridge container)

- **Server**: `127.0.0.1` (bridge draait in host-netwerk, dus localhost = Pi)
- **Port**: 1883

### MQTT WebSocket-listener (poort 9001)

Naast de standaard TCP-listener op 1883 luistert Mosquitto óók op poort 9001
voor het **WebSocket**-protocol. Dit is bedoeld voor browser-gebaseerde
deelnemers (zoals de simulator in `pi/simulator/`) die geen rauwe TCP
kunnen openen.

```
listener 9001
protocol websockets
```

Topics en payloads zijn identiek aan de TCP-listener — alleen de transport-
laag verschilt. Een browser-client verbindt via `ws://192.168.1.43:9001`.

### Simulator als legitieme MQTT-deelnemer

`pi/simulator/` is een browser-app die zich op precies dezelfde naad in het
systeem (`plaatjes/data` en `commando/master1`) gedraagt als de echte
hardware. Twee modi:

- **Monitor**: subscribe-only — kijkt passief mee met een echt spel.
- **Simulatie**: publiceert `plaatjes/data` op basis van virtuele speler-
  posities (RSSI berekend via een log-distance path-loss model). Vervangt
  daarmee `bridge.py` als bron; zet die laatste uit om dubbele detecties
  te voorkomen.

Node-RED en de firmware worden voor de simulator niet aangepast — hij
houdt zich aan dit protocol.

### Audio-abstractie (`audio/afspelen`)

De Plates-of-Fate engine (Node-RED flow 06) publiceert audio-verzoeken op
`audio/afspelen`. Het is bewust een **abstractie**: de engine zegt *welke*
audiosegmenten in *welke volgorde*, niet *hoe* ze klinken. De consument is de
**`audio-player`** Pi-service (`pi/audio-player/`) die de WAV-segmenten
sequentieel via `aplay` over de aux-jack speelt. Zie `docs/handleidingen/audio-player.md`.

```json
{"fase":"event","tekst":"Minimum 3 uur vooruit.","segments":["events/verplaatsing1_voor.wav","getallen/3.wav","events/verplaatsing1_na.wav"],"prioriteit":"normaal"}
```

- `fase`: `"event"` of `"doelwit"`.
- `tekst`: leesbare tekst (simulator-log + fallback); de player gebruikt `segments`.
- `segments`: lijst WAV-bestandsnamen relatief t.o.v. de audio-map, in afspeelvolgorde
  (knip-en-plak: begin + getal + eind voor events; `doelwit/voor` + per doelwit een
  clip + `doelwit/na`).
- `prioriteit`: vrije tekst voor latere afspeel-volgorde.

Plates-of-Fate **gevolgen** die LED's/buzzers aansturen hergebruiken het
bestaande `commando/master1`-pad (zie sectie 2, `actie_id` 0–4) — er is dus
geen apart commando-formaat voor events.

## 6. Slave-registratie en sender-MAC gate (master code)

MAC-adressen van slaves zijn hardcoded in master's `slaveAdressen[]` array.
Deze array vervult **twee** rollen:

1. **Peer-lijst voor zenden**: alle MACs worden via `esp_now_add_peer()`
   geregistreerd zodat de master commando's naar die slaves kan sturen.
2. **Ontvangst-whitelist**: `OnDataRecv()` vergelijkt de afzender-MAC met
   `slaveAdressen[]`. Pakketten van slaves die niet in deze lijst staan
   worden gedropt en NIET doorgestuurd naar de Pi.

> **Waarom een ontvangst-whitelist nodig is**: ESP-NOW levert standaard
> pakketten van **elke** afzender aan de receive-callback. `esp_now_add_peer()`
> is alleen nodig om te kunnen zenden, het filtert geen binnenkomst.
> Zonder deze gate zou een master pakketten ontvangen en doorzetten van
> alle 24 slaves in het veld — terwijl één master maar 8 specifieke slaves
> hoort te bedienen.

Bij het toevoegen van een nieuwe slave:
1. Flash slave en lees MAC uit Serial Monitor (banner `SLAVE MAC-ADRES : ...`)
2. Voeg toe aan master's `slaveAdressen[]`
3. Verhoog `AANTAL_SLAVES`
4. Herflash master

Slots met placeholder MAC `0x00:0x00:0x00:0x00:0x00:0x00` worden overgeslagen
bij zowel peer-registratie als de ontvangst-gate, zodat je veilig vooruit
kunt definiëren.

## Wijzigingsgeschiedenis

- 2026-05-29: master kreeg retry-queue voor commando's (max 5 pogingen per
  commando, 250 ms tussen retries, FIFO-queue van 16). Statusantwoorden
  uitgebreid: `queued`, `ack`, `send_err`, `opgegeven`, `queue_vol`. Lost
  het probleem op waarbij een commando soms gemist werd door de slave
  tijdens BLE-scannen of bij collision.
- 2026-05-28: Mosquitto-broker extra listener op poort 9001 (WebSocket) voor
  browser-clients zoals `pi/simulator/`. TCP 1883 voor bridge.py + Node-RED
  blijft ongewijzigd.
- 2026-05-23: nieuw MQTT-topic `audio/afspelen` (Node-RED → geluidsbox) als
  abstractie voor de Plates-of-Fate engine. Engine-gevolgen voor LED/buzzer
  hergebruiken `commando/master1`.
- 2026-05-20: `batch_message` uitgebreid met `float batterij_v`. Master
  stuurt per batch één extra JSON-regel `{"paal":N,"batt":3.87}` naar de Pi.
  Node-RED toont dit in de Spelstatus-tabel onder een toggle "Toon batterij".
- 2026-05-20: master filtert binnenkomende ESP-NOW pakketten op afzender-MAC
  tegen `slaveAdressen[]`. Vreemde slaves worden gelogd als `[GATE]` en niet
  doorgezet naar de Pi. Maakt 1 master → 8 slaves segmentatie mogelijk in
  een veld met 3 masters / 24 slaves.
- 2026-05-20: slave dedupliceert nu binnen één batch (elke whitelisted MAC
  max één entry per scan, sterkste RSSI behouden) — voorheen vulde
  `spelers[9]` zich met duplicaten van dezelfde beacon
- 2026-05-18: slave verstuurt nu elke cyclus (ook bij 0 spelers) + random
  backoff vóór verzenden tegen botsende ESP-NOW-pakketten van meerdere slaves
- 2026-05-17: actie_id tabel bijgewerkt — alle 5 acties (0–4) geïmplementeerd in slave firmware
- 2026-05-10: initieel document, opgesteld bij overstap naar VS Code + GitHub workflow