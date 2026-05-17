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
- **MQTT → Master:** ontvangt commando's van topic `commando/master1`,
  schrijft ze naar de seriële poort

---

## Hoe werkt de code?

### Threads

```
main thread:  MQTT client loop (handelt on_connect / on_message af)
thread 1:     lees_poort(master1)   → blocking readline op /dev/ttyMaster1
```

Per master-poort draait één daemon-thread. Momenteel is er één master
(`MEESTERS` lijst), uitbreidbaar voor meerdere masters.

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

`on_mqtt_message()` wordt aangeroepen bij elk bericht op `commando/master1`:

```python
parsed = json.loads(commando)        # valideer JSON
# vereist: "paal" en "actie" aanwezig
ser.write((commando + '\n').encode()) # doorsturen naar master
```

De bridge valideert alleen of de JSON parseerbaar is en de vereiste velden
bevat. Verdere validatie (geldig paal-ID, etc.) doet de master zelf.

### Reconnect-gedrag

| Component | Gedrag bij verbindingsverlies |
|-----------|-------------------------------|
| Seriële poort | `lees_poort()` vangt de exception, wacht 5s, herverbindt in lus |
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

De seriële poort en commando-topic staan hardcoded in `MEESTERS`:

```python
MEESTERS = [
    {"poort": "/dev/ttyMaster1", "baud": 115200, "commando_topic": "commando/master1"}
]
```

`/dev/ttyMaster1` is een udev-symlink (zie `config/udev/99-esp-masters.rules`).
De master moet in USB-poort 1-1.4 zitten, anders bestaat de symlink niet.

---

## MQTT-topics op een rij

| Topic | Richting | Payload |
|-------|----------|---------|
| `plaatjes/data` | Bridge → Node-RED | `{"paal":1,"mac":"aa:bb:cc:dd:ee:ff","rssi":-67}` |
| `commando/master1` | Node-RED → Bridge | `{"paal":1,"actie":2}` |

---

## Logoutput begrijpen

| Logbericht | Betekenis |
|------------|-----------|
| `MQTT verbonden (rc=0)` | Verbinding met broker OK |
| `Geabonneerd op commando/master1` | Ready om commando's te ontvangen |
| `Verbinding maken met /dev/ttyMaster1...` | Seriële poort wordt geopend |
| `Verbonden met /dev/ttyMaster1` | Serieel verbonden, bridge actief |
| `[DATA] /dev/ttyMaster1: {'paal': 2, ...}` | JSON ontvangen en gepubliceerd |
| `[DEBUG] /dev/ttyMaster1: [RECV] Paal 2...` | Debug-output van master, niet doorgestuurd |
| `[MQTT] Ontvangen op commando/master1: {...}` | Commando van Node-RED ontvangen |
| `[SERIEEL] Commando verstuurd naar ...: {...}` | Commando doorgestuurd naar master |
| `[MQTT] Ongeldig formaat, verwacht: ...` | JSON mist "paal" of "actie" veld |
| `Fout op /dev/ttyMaster1: ..., opnieuw in 5s` | Verbinding verloren, wordt herproefd |
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

**`Fout op /dev/ttyMaster1: [Errno 2] No such file`**
→ De master is niet aangesloten, of zit in de verkeerde USB-poort.
Controleer: `ls /dev/ttyMaster*` op de Pi. Zit de master in USB-poort 1-1.4?

**Data komt aan op bridge maar niet in Node-RED**
→ MQTT-broker niet bereikbaar, of topic verschil.
Check: `mosquitto_sub -h 127.0.0.1 -t "plaatjes/data"` op de Pi.

**Commando's van Node-RED komen niet aan bij master**
→ Controleer of het topic exact `commando/master1` is.
Check of de bridge subscribed is: zoek `Geabonneerd op commando/master1` in de logs.
`docker logs serial-bridge` toont de volledige loghistorie.

**Bridge loopt vast na reboot Pi**
→ De master-USB is nog niet gereed als de container start. De `lees_poort()`
retry-lus vangt dit op en verbindt zodra `/dev/ttyMaster1` beschikbaar is.
Geen actie nodig — geef het 10 seconden.

**`[SERIEEL] Geen verbinding voor commando/master1`**
→ De seriële verbinding is op dat moment verbroken (bijv. master herstart).
De thread herverbindt automatisch binnen 5 seconden.
