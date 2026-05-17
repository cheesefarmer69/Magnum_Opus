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

### Frequentie en timing

- Slave scant BLE met scanInterval/scanWindow afgestemd op antenne-coexistence
  (single-antenne C3 moet schakelen tussen ESP-NOW en BLE)
- Batch wordt verzonden zodra scan-cyclus klaar is.

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

### MQTT-config in Node-RED

- **Server**: `192.168.1.43` (NIET `127.0.0.1` — Node-RED draait in bridge-netwerk)
- **Port**: 1883
- **Protocol**: MQTT V3.1.1
- **QoS**: 2 voor commando's (exactly-once), 0 of 1 voor data acceptabel

### MQTT-config in bridge.py (serial-bridge container)

- **Server**: `127.0.0.1` (bridge draait in host-netwerk, dus localhost = Pi)
- **Port**: 1883

## 6. Slave-registratie (master code)

MAC-adressen van slaves zijn hardcoded in master's `slaveAdressen[]` array.

Bij het toevoegen van een nieuwe slave:
1. Flash slave en lees MAC uit Serial Monitor (`[SETUP] Slave MAC: xx:xx:...`)
2. Voeg toe aan master's `slaveAdressen[]`
3. Verhoog `AANTAL_SLAVES`
4. Herflash master

Slots met placeholder MAC `0x00:0x00:0x00:0x00:0x00:0x00` worden overgeslagen
zodat je veilig vooruit kunt definiëren.

## Wijzigingsgeschiedenis

- 2026-05-17: actie_id tabel bijgewerkt — alle 5 acties (0–4) geïmplementeerd in slave firmware
- 2026-05-10: initieel document, opgesteld bij overstap naar VS Code + GitHub workflow