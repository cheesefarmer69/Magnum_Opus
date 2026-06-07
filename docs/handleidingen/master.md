# Handleiding: Master (ESP32 WROOM-32)

**Bestand:** `firmware/Master/src/main.cpp`
**Hardware:** ESP32 WROOM-32, verbonden met Raspberry Pi via USB (CH340)

---

## Wat doet de master?

1. Ontvangt batches van spelerdetecties van alle slaves via ESP-NOW
2. Stuurt elke detectie als JSON-regel door naar de Pi via USB Serial
3. Leest JSON-commando's van de Pi via USB Serial
4. Stuurt die commando's via ESP-NOW door naar de juiste slave

De master is een doorzender: hij vertaalt ESP-NOW ↔ Serial JSON. Er zit
geen spellogica in de master.

---

## Hoe werkt de dataflow?

```
Slave → ESP-NOW → OnDataRecv()
                    ├── sender-MAC gate (vindSlaveIndex)
                    │    ├── in slaveAdressen[]? → ga door
                    │    └── onbekend?           → [GATE] log, drop
                    └── per speler: Serial.printf JSON → Pi

Pi → Serial → verwerkSerieel()
               ├── parse paal + actie
               └── esp_now_send() → slave[paal - 1]
```

### Richting 1: Slave → Pi

Elke batch van een slave levert één JSON-regel per gevonden speler:

```json
{"paal":3,"mac":"48:87:2d:9d:bb:7d","rssi":-71}
```

Bij een batch met 2 spelers op paal 3 verschijnen er 2 regels.

> **Sender-MAC gate**: ESP-NOW levert pakketten van **élke** afzender aan
> `OnDataRecv()` — `esp_now_add_peer()` regelt alleen zenden, niet ontvangst.
> De master vergelijkt de afzender-MAC met `slaveAdressen[]` en dropt
> pakketten van slaves die daar niet in staan. Dit maakt segmentatie
> mogelijk: in een veld met 3 masters / 24 slaves accepteert elke master
> alleen zijn eigen 8 slaves.

### Richting 2: Pi → Slave

De Pi stuurt een commando als JSON-regel:

```json
{"paal":3,"actie":2}
```

De master parset dit, zoekt het slave-MAC op in `slaveAdressen[paal-1]`
en stuurt een `commando_message` via ESP-NOW.

---

## Slaves registreren

```cpp
const int AANTAL_SLAVES = 3;

uint8_t slaveAdressen[AANTAL_SLAVES][6] = {
  {0xAC, 0xA7, 0x04, 0xBD, 0x3A, 0x48},  // paal 1
  {0xAC, 0xA7, 0x04, 0xB9, 0xE1, 0xC0},  // paal 2
  {0x48, 0x87, 0x2d, 0x9d, 0xbb, 0x7d},  // paal 3
};
```

- Index 0 = paal 1, index 1 = paal 2, etc.
- Rijen met alleen `0x00` worden overgeslagen (placeholder)
- Slave-MAC lees je uit de Serial Monitor van de slave: de banner
  `SLAVE MAC-ADRES : ...` die bij het opstarten eenmalig wordt getoond

**Nieuwe slave toevoegen:**
1. Flash slave, lees MAC uit Serial Monitor
2. Voeg MAC toe aan `slaveAdressen[]`
3. Verhoog `AANTAL_SLAVES`
4. Herflash master

---

## Initialisatievolgorde

```
1. WiFi.mode(WIFI_STA)
2. esp_now_init()
3. esp_wifi_set_channel(WIFI_KANAAL)   — kanaal vastzetten NA init
4. esp_wifi_set_ps(WIFI_PS_NONE)       — power saving uit voor betrouwbare ontvangst
5. esp_now_add_peer() voor elke slave
```

---

## Serieel protocol (115200 baud, \n-afgesloten)

| Richting | Formaat | Voorbeeld |
|----------|---------|-----------|
| Master → Pi | `{"paal":N,"mac":"xx:xx","rssi":-NN}` | één regel per speler |
| Pi → Master | `{"paal":N,"actie":N}` | één commando |
| Master → Pi (bevestiging) | `{"status":"verstuurd","paal":N}` | na ontvangen commando |
| Master → Pi (debug) | `[RECV] ...` , `[PEER] ...` | worden genegeerd door bridge |

Niet-JSON regels (die beginnen met `[`) worden door `bridge.py` als
debug-output beschouwd en niet doorgestuurd naar MQTT.

---

## Serial Monitor output begrijpen

| Output | Betekenis |
|--------|-----------|
| `[GATE] Genegeerd: AC:A7:...` | Pakket van een slave die NIET in `slaveAdressen[]` staat — gedropt |
| `[RECV] 124 bytes van paal 2 (AC:A7:...)` | Batch ontvangen van een geregistreerde slave |
| `[RECV] Paal 2, 3 spelers, batt 3.87V` | Batch inhoud + gerapporteerde batterij-spanning slave |
| `{"paal":2,"mac":"...","rssi":-65}` | JSON doorgestuurd naar Pi (per gevonden speler) |
| `{"paal":2,"batt":3.87}` | JSON met batterij-spanning, één regel per batch (`batt > 0`) |
| `[RECV] Te kort: 10 < 206, genegeerd` | Corrupt/kort pakket ontvangen (206 = sizeof batch_message) |
| `[SEND] Status: OK` | ESP-NOW commando succesvol verzonden naar slave |
| `[SEND] Status: MISLUKT` | Slave niet bereikbaar |
| `[PEER] Paal 3 toegevoegd: ...` | Slave als peer geregistreerd bij startup |
| `[PEER] Paal 1 overgeslagen` | Placeholder MAC, wordt niet geregistreerd |
| `{"status":"onbekende paal","paal":25}` | Pi stuurde paal-ID buiten bereik |

---

## Veelvoorkomende problemen

**Slaves sturen wel, master ontvangt niets**
→ WiFi-kanaal verschil. Check `[SETUP] WiFi kanaal:` bij master én slave.
Beide moeten `WIFI_KANAAL = 1` (of wat je ingesteld hebt).

**Master ziet `[GATE] Genegeerd` voor een slave die wél bij hem hoort**
→ Slave-MAC staat niet (of verkeerd) in `slaveAdressen[]`. Kopieer de MAC
exact uit de banner `SLAVE MAC-ADRES : ...` in de slave Serial Monitor en
voeg toe aan de array, `AANTAL_SLAVES` mee ophogen, herflash de master.

**`[SEND] Status: MISLUKT` na commando**
→ Slave-MAC in `slaveAdressen[]` klopt niet, of slave staat uit.
Check het MAC in de slave Serial Monitor (banner `SLAVE MAC-ADRES : ...`).

**Pi stuurt commando's maar master doet niets**
→ De bridge detecteert de master automatisch (CH340, elke USB-poort) en leert
de routering uit de `paal_id`. Check in `docker logs serial-bridge` of er een
`[ROUTE] ... -> commando/masterN`-regel staat; zo niet, dan heeft deze master
nog geen batch gestuurd. Controleer ook `ls -l /dev/ttyUSB*` op de Pi.

**Master reageert niet op seriële input**
→ `verwerkSerieel()` verwacht exact de velden `"paal"` en `"actie"` in de JSON.
Ontbreekt een van de twee? Dan wordt de lijn stil genegeerd.
