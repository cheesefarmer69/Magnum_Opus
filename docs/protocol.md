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

Master valideert dat `paal_id` binnen `AANTAL_SLAVES` valt en stuurt
dan via ESP-NOW door naar de juiste slave.

## 5. Pi ↔ Node-RED (MQTT)

Broker: Eclipse Mosquitto op `192.168.1.43:1883`, anonymous access toegestaan
(lokaal netwerk).

| Topic              | Richting           | Payload                                      |
|--------------------|--------------------|----------------------------------------------|
| `plaatjes/data`    | Pi → Node-RED      | `{"paal":1,"mac":"aa:bb:..","rssi":-67}`     |
| `commando/master1` | Node-RED → Pi      | `{"paal":1,"actie":1}`                       |
| `audio/afspelen`   | Node-RED → box     | `{"tekst":"...","fase":"event","prioriteit":"normaal"}` |
| `pof/status`       | Node-RED → browser | `{"actief":true,"fase":"reactie","eventNaam":"...","doelwit":[],"getalWaarde":2,"teller":7,"maxTeller":10}` |

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
`audio/afspelen`. Het is bewust een **abstractie**: de engine zegt alleen
*wat* voorgelezen moet worden, niet *hoe*. Een aparte consument (op de
geluidsbox/Pi) zet dit later om naar spraak (TTS) of speelt opgenomen
bestanden af — die consument bestaat nog niet.

```json
{"tekst":"De zon staat hoog...","fase":"event","prioriteit":"normaal"}
```

- `fase`: `"event"` (event wordt voorgelezen) of `"doelwit"` (getroffen
  uren/spelers worden voorgelezen).
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