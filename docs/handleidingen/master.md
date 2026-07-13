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
                    └── dispatch op msg_type (eerste byte):
                         ├── MSG_BATCH     → per speler JSON (binair MAC → string) + batt
                         ├── MSG_CMD_ACK   → slot vrij + {"status":"uitgevoerd","seq"}
                         ├── MSG_HEARTBEAT → {"paal","hb":1,..}
                         ├── MSG_FOUT      → {"paal","fout",..}
                         └── MSG_KNOP      → {"paal","knop":1}

Pi → Serial → verwerkSerieel()
               ├── parse paal + actie  (master kent cmd_seq toe)
               └── per-slave FIFO (4) → esp_now_send() head commando_message_v2; retry tot MSG_CMD_ACK
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

De master kent het commando een `cmd_seq` toe, zet het in de **per-slave FIFO** van die paal
(diepte 4) en stuurt het head-item als `commando_message_v2` via ESP-NOW. Het commando geldt pas
als **uitgevoerd** wanneer de slave een `MSG_CMD_ACK` terugstuurt (applicatie-ACK, ná uitvoering) —
niet bij de MAC-laag-ACK. Komt er binnen ~1,5 s geen ACK, dan stuurt de master hetzelfde `cmd_seq`
opnieuw (idempotent); na `MAX_POGINGEN` volgt `opgegeven` en gaat de FIFO verder met het volgende item.

> **Geen laatste-wint meer:** twee snel opeenvolgende, verschillende commando's naar dezelfde paal
> (bv. buzzer-piep + portaal) blijven **allebei** in de FIFO en worden in volgorde afgeleverd. Stapelen
> er meer dan 4 op (bv. een dode paal), dan wordt het **oudste** gedropt (`fifo_vol`).

---

## Multi-master: drie environments, één codebase

Het veld heeft **3 masters**: master 1 → palen **1–8**, master 2 → **9–16**, master 3 → **17–24**.
Eén codebase, drie PlatformIO-environments in `platformio.ini`:

```ini
[env:master1]  build_flags = -DPAAL_MIN=1  -DPAAL_MAX=8  -DMASTER_NR=1 -I ../shared
[env:master2]  build_flags = -DPAAL_MIN=9  -DPAAL_MAX=16 -DMASTER_NR=2 -I ../shared
[env:master3]  build_flags = -DPAAL_MIN=17 -DPAAL_MAX=24 -DMASTER_NR=3 -I ../shared
```

> ⚠️ **Paal 8 hoort bij master 1, niet bij master 2.** Dit document zei vroeger `PAAL_MAX=7` /
> `PAAL_MIN=8`; dat was **fout** en liet paal 8 buiten elk masterbereik vallen. De waarden hierboven
> komen letterlijk uit `firmware/Master/platformio.ini` en matchen de slave (`PAAL_ID <= 8 → master 1`).
> Controleer bij twijfel de boot-banner van de master: er hoort **`[SETUP] Master 1, palen 1-8 (8 slaves)`**
> te staan.

In de code: `AANTAL_SLAVES = PAAL_MAX − PAAL_MIN + 1` en `paalNaarIndex(paal) = paal − PAAL_MIN` (de
globale paal-ID → 0-based index; index 0 = `PAAL_MIN`). Het verzonden commando draagt de **globale**
`paal_id` (`PAAL_MIN + index`). Een commando buiten het bereik → `{"status":"buiten_bereik","paal":N,"master":M}`.

> **Selecteer de juiste env** onderin de VS Code-statusbalk en flash die naar de bijbehorende fysieke
> master. `pio run` (zonder `-e`) bouwt alle drie.

## Slaves registreren

De slave-MAC's staan in de **gedeelde** tabel `firmware/shared/paal_macs.h` (`MAC → PAAL_ID`, één bron
van waarheid voor slave én master — het oude per-master `slave_macs.h` bestaat niet meer). De master
vult daaruit bij boot zijn eigen peer-tabel:

```cpp
static void vulSlaveAdressen() {
  for (int i = 0; i < PAAL_MACS_N; i++) {
    int paal = PAAL_MACS[i].paal;
    if (paal >= PAAL_MIN && paal <= PAAL_MAX)          // alleen zijn eigen bereik
      memcpy(slaveAdressen[paal - PAAL_MIN], PAAL_MACS[i].mac, 6);
  }
}
```

- Rij-index = `paal − PAAL_MIN` (master1: paal 1 = index 0). Staat een paal **niet** in `paal_macs.h`,
  dan blijft zijn rij all-zero = **placeholder** → overgeslagen bij peer-registratie **én** bij de
  ontvangst-gate.
- Elke master kent **uitsluitend zijn eigen** slaves → de sender-MAC-gate segmenteert automatisch.

**Wat de master bij boot logt** (dit is je belangrijkste diagnose-bron):
```
[SETUP] Master 1, palen 1-8 (8 slaves)
[PEER] Paal 1 toegevoegd: AC:A7:04:BD:3A:48
[PEER] Paal 4 overgeslagen (geen MAC ingevuld)   <-- paal staat niet in paal_macs.h
[PEER] Paal 5 toevoegen MISLUKT!                 <-- esp_now_add_peer faalde
```

**Nieuwe slave toevoegen/vervangen:**
1. Flash de slave met de **universele binary** (één binary voor alle 24 borden — je zet niets per bord).
2. Lees zijn MAC: `firmware/tools/lees-mac.ps1 -Port COMx -Paal N`, of uit de banner
   `SLAVE MAC-ADRES : ...` in de Serial Monitor.
3. Voeg/vervang de regel `{{0x.., ...}, N},` in **`firmware/shared/paal_macs.h`**.
4. **Herflash de betrokken master** — hij bouwt `slaveAdressen[]` uit diezelfde header, dus zonder
   herflash blijft hij de nieuwe slave in de gate droppen (`[GATE] Genegeerd`).

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
| Master → Pi (status) | `{"status":"queued"/"ack"/...,"paal":N}` | per commando, zie statustabel in `docs/protocol.md` §4 |
| Master → Pi (debug) | `[RECV] ...` , `[PEER] ...` | worden genegeerd door bridge |

Niet-JSON regels (die beginnen met `[`) worden door `bridge.py` als
debug-output beschouwd en niet doorgestuurd naar MQTT.

> **Niet-blokkerend serieel.** De master leest seriële input byte-per-byte in een
> eigen regelbuffer (geen `readStringUntil`, dus geen 1 s Stream-timeout): een trage
> of partiële regel van de Pi bevriest de loop niet. Alle serial-**output** loopt via
> één task (de loop drijft een interne log-queue af) zodat regels nooit door elkaar
> geweven raken — zie `docs/protocol.md` §3.

---

## Serial Monitor output begrijpen

| Output | Betekenis |
|--------|-----------|
| `[GATE] Genegeerd: AC:A7:... (niet in slaveAdressen[])` | Pakket van een slave die NIET in `slaveAdressen[]` staat — gedropt |
| `[RECV] Paal 2, 3 spelers, batt 3870 mV` | Batch inhoud + gerapporteerde batterij-spanning slave (mV, integer) |
| `{"paal":2,"mac":"...","rssi":-65}` | JSON doorgestuurd naar Pi (per gevonden speler) |
| `{"paal":2,"batt":3.87}` | JSON met batterij-spanning, één regel per batch (`batt > 0`) |
| `[RECV] Batch te kort: 10 < 33 (aantal 4)` | Pakket korter dan de header of dan `5 + aantal×7` (batch is variabel-lang, max 215 B) — gedropt |
| `[RECV] Batch ongeldig: aantal 40 > 30` | `aantal`-veld groter dan `MAX_SPELERS` (30) — gedropt |
| `{"status":"queued","paal":3,"actie":2}` | Commando achteraan de per-slave FIFO gezet (in volgorde, geen laatste-wint) |
| `{"status":"fifo_vol","paal":3,"gedropt_seq":40}` | FIFO van die paal zat vol (4) — oudste commando gedropt |
| `{"status":"uitgevoerd","paal":3,"seq":42}` | Slave bevestigde **uitvoering** (`MSG_CMD_ACK status 0`) — vervangt de v1 `ack` |
| `{"status":"geweigerd","paal":3,"seq":42}` | Slave gaf `MSG_CMD_ACK status 1` (onbekende/geweigerde actie) |
| `{"status":"opgegeven","paal":3,...}` | Na `MAX_POGINGEN` zonder applicatie-ACK — slave niet bereikbaar |
| `{"status":"geen_slave","paal":3}` | Commando naar een leeg/placeholder slot (all-zero MAC) — geweigerd |
| `{"paal":3,"hb":1,"batt":3.87,"uptime":1234,"fw":2}` | Heartbeat van een slave (uit `MSG_HEARTBEAT`) |
| `{"paal":3,"fout":1,"ernst":2,"detail":3150}` | Foutmelding van een slave (uit `MSG_FOUT`) |
| `{"paal":3,"knop":1}` | Drukknop op een slave (uit `MSG_KNOP`, via ESP-NOW) |
| `{"status":"log_drop","aantal":N}` | Serial-log-queue zat vol onder pieklast; N regels verloren |
| `[PEER] Paal 3 toegevoegd: ...` | Slave als peer geregistreerd bij startup |
| `[PEER] Paal 1 overgeslagen` | Placeholder MAC, wordt niet geregistreerd |
| `[SETUP] Master 2, palen 9-16 (8 slaves)` | Opstart-banner: welke master + paalbereik (uit de env) |
| `{"status":"buiten_bereik","paal":25,"master":2}` | Pi stuurde een paal buiten het bereik van deze master → routeringsfout |

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
