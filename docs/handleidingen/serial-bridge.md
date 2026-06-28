# Handleiding: Serial Bridge (Raspberry Pi)

**Bestand:** `pi/serial-bridge/bridge.py`
**Draait als:** Docker container (`--network host`) op de Pi

---

## Wat doet de bridge?

De bridge is de verbindingsschakel tussen de ESP32-master (serieel/USB)
en Node-RED (MQTT). Hij vertaalt in beide richtingen:

```
Master (USB Serial)  ←→  bridge.py  ←→  MQTT broker  ←→  Node-RED
```

- **Master → MQTT:** leest JSON-regels van de seriële poort, publiceert
  op topic `plaatjes/data`
- **MQTT → Master:** ontvangt commando's van topics `commando/master1..3`,
  schrijft ze naar de juiste seriële poort

> **Inhoud-agnostisch (protocol v2):** de bridge kent het ESP-NOW-wire-format niet — de **master**
> vertaalt alle binaire v2-berichten al naar JSON-regels. De bridge publiceert elke geldige JSON-regel
> ongewijzigd door. De v2-types (heartbeat `{"hb":1}`, fout `{"fout":..}`, knop `{"knop":1}`,
> uitvoering `{"status":"uitgevoerd","seq":..}`) stromen dus vanzelf mee — bridge.py wijzigde niet voor v2.

---

## Poort-detectie (poort-onafhankelijk, schaalt naar 3 masters)

De bridge gebruikt **geen vaste `/dev/ttyMaster1` meer**. In plaats daarvan:

- **Auto-detectie:** een detectie-thread scant elke 5s met
  `serial.tools.list_ports` naar alle CH340-chips (VID `0x1A86`, PID `0x7523`)
  en opent automatisch elke gevonden poort — ongeacht in welke USB-poort een
  master zit, en ook als een master later (her)ingeplugd wordt.
- **Routering leren:** uit de binnenkomende `paal_id` leidt de bridge af welke
  master op een poort zit (palen 1–8 → master1, 9–16 → master2, 17–24 →
  master3) en koppelt zo `commando/masterN` aan de juiste poort. De bridge leert
  **alleen** uit regels zónder `"status"`-veld (batch/heartbeat/fout/knop/batt) —
  die dragen de eigen paal van de master. Status-echo's zoals
  `{"status":"buiten_bereik","paal":17,"master":1}` dragen de paal van een
  afgewezen, vreemd commando; daaruit leren zou de route vergiftigen (bv.
  `commando/master3` → master1-poort) en alle commando's voor die paal naar de
  verkeerde master sturen.
- **Inkomend** wordt ongewijzigd op `plaatjes/data` gepubliceerd; de `paal_id`
  in de data routeert verder in Node-RED.

Dit werkt voor 1, 2 of 3 masters zonder code- of poortwijziging.

---

## Hoe werkt de code?

### Threads

```
main thread:    MQTT client loop (handelt on_connect / on_message af)
detectie-thread: scant elke 5s naar CH340-poorten, start leesthreads
lees-thread/poort: blocking readline per gedetecteerde master-poort
```

Per gedetecteerde master-poort draait één daemon-thread (`lees_poort`).
Nieuwe poorten worden automatisch opgepikt door de detectie-thread.

### Richting 1: Master → MQTT

`lees_poort()` leest elke regel van de seriële poort:

```python
lijn = ser.readline()   # blokkeert tot \n
data = json.loads(lijn) # parseerbaar JSON?
client.publish("plaatjes/data", json.dumps(data))
```

- Geldige JSON → gepubliceerd op `plaatjes/data`
- Niet-JSON (debug-regels als `[RECV] ...`) → gelogd als `[DEBUG]`, niet verstuurd

### Richting 2: MQTT → Master

`on_mqtt_message()` wordt aangeroepen bij elk bericht op `commando/master1..3`:

```python
parsed = json.loads(commando)         # valideer JSON
# vereist: "paal" en "actie" aanwezig
ser = topic_naar_serieel.get(topic)   # geleerde poort voor deze master
ser.write((commando + '\n').encode()) # doorsturen naar master
```

De bridge valideert alleen of de JSON parseerbaar is en de vereiste velden
bevat. Verdere validatie (geldig paal-ID, etc.) doet de master zelf. Is de
routering voor een master nog niet geleerd (die master heeft nog geen batch
gestuurd), dan wordt het commando genegeerd tot de eerste data binnenkomt.

### Reconnect-gedrag

| Component | Gedrag bij verbindingsverlies |
|-----------|-------------------------------|
| Seriële poort | `lees_poort()` geeft de poort vrij bij een fout; de detectie-thread heropent hem binnen ~5s |
| MQTT broker | paho auto-reconnect (1–30s exponential backoff) |
| MQTT subscriptions | worden hernieuwd in `on_connect()` bij elke (her)verbinding |

---

## Configuratie

Via environment variables (in `docker-compose.yml` of `.env`):

| Variabele | Standaard | Betekenis |
|-----------|-----------|-----------|
| `MQTT_BROKER` | `127.0.0.1` | IP van de MQTT broker |
| `MQTT_PORT` | `1883` | Poort van de broker |
| `MQTT_DATA_TOPIC` | `plaatjes/data` | Topic voor detectie-data naar Node-RED |

Poorten worden niet meer geconfigureerd: de CH340-id's en de paal_id→master
mapping staan in de code (`CH340_VID`/`CH340_PID`, `paal_naar_topic`). De
master mag in om het even welke USB-poort zitten.

De udev-regel (`config/udev/99-esp-masters.rules`) zet de tty-devices alleen
nog op `MODE=0666` zodat de container ze kan openen — geen poort-lock meer.
De container krijgt toegang via `--device-cgroup-rule 'c 188:* rmw'` + `-v
/dev:/dev` (zie `deploy.sh`).

---

## MQTT-topics op een rij

| Topic | Richting | Payload |
|-------|----------|---------|
| `plaatjes/data` | Bridge → Node-RED | `{"paal":1,"mac":"aa:bb:cc:dd:ee:ff","rssi":-67}` |
| `commando/master1` | Node-RED → Bridge | `{"paal":1,"actie":2}` (palen 1–7) |
| `commando/master2` | Node-RED → Bridge | `{"paal":8,"actie":2}` (palen 8–16) |
| `commando/master3` | Node-RED → Bridge | `{"paal":17,"actie":2}` (palen 17–24) |

---

## Logoutput begrijpen

| Logbericht | Betekenis |
|------------|-----------|
| `MQTT verbonden (rc=0)` | Verbinding met broker OK |
| `Geabonneerd op commando/master1` | Ready om commando's te ontvangen |
| `[DETECTIE] Nieuwe master-poort: /dev/ttyUSB0` | CH340 gevonden, leesthread gestart |
| `Verbonden met /dev/ttyUSB0` | Serieel verbonden, bridge actief |
| `[ROUTE] /dev/ttyUSB0 -> commando/master1 (paal 3)` | Routering geleerd uit paal_id |
| `[DATA] /dev/ttyUSB0: {'paal': 2, ...}` | JSON ontvangen en gepubliceerd |
| `[DEBUG] /dev/ttyUSB0: [RECV] Paal 2...` | Debug-output van master, niet doorgestuurd |
| `[MQTT] Ontvangen op commando/master1: {...}` | Commando van Node-RED ontvangen |
| `[SERIEEL] Commando verstuurd naar ...: {...}` | Commando doorgestuurd naar master |
| `[SERIEEL] Nog geen poort geleerd voor ...` | Master heeft nog geen batch gestuurd; commando genegeerd |
| `[MQTT] Ongeldig formaat, verwacht: ...` | JSON mist "paal" of "actie" veld |
| `Fout op /dev/ttyUSB0: ..., poort vrijgegeven` | Verbinding verloren; detectie heropent |
| `MQTT verbinding verloren (rc=N)` | Broker niet bereikbaar, auto-reconnect start |

---

## Deployen na wijziging

```bash
# Op de Pi:
cd ~/Magnum_Opus
git pull
./pi/deploy.sh
```

`deploy.sh` herbouwt en herstart **alleen** de `serial-bridge` container.
Node-RED en Mosquitto worden niet aangeraakt.

---

## Veelvoorkomende problemen

**Geen `[DETECTIE]`-regel / `open poorten: GEEN` in de heartbeat**
→ Geen CH340 gevonden. Controleer: `ls -l /dev/ttyUSB*` en
`lsusb | grep 1a86` op de Pi. Zit een master ingeplugd en heeft de container
toegang (`-v /dev:/dev` + cgroup-rule in `deploy.sh`)? Is de udev-regel
geïnstalleerd (`MODE=0666`)?

**Data komt aan op bridge maar niet in Node-RED**
→ MQTT-broker niet bereikbaar, of topic verschil.
Check: `mosquitto_sub -h 127.0.0.1 -t "plaatjes/data"` op de Pi.

**Commando's van Node-RED komen niet aan bij master**
→ Controleer of het topic exact `commando/masterN` is (N = 1/2/3 afhankelijk
van de paal). Zie je `[ROUTE] ... -> commando/masterN` in de logs? Zo niet,
dan heeft die master nog geen batch gestuurd en is de routering nog niet
geleerd. `docker logs serial-bridge` toont de volledige loghistorie.

**Bridge loopt vast na reboot Pi**
→ De master-USB is nog niet gereed als de container start. De detectie-thread
scant elke 5s en verbindt zodra een CH340 verschijnt. Geen actie nodig.

**`[SERIEEL] Nog geen poort geleerd voor commando/masterN`**
→ Die master heeft nog geen data gestuurd, dus de bridge weet nog niet op
welke poort hij zit. Lost zichzelf op zodra de eerste batch binnenkomt.
