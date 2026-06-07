import os
import serial
import serial.tools.list_ports
import paho.mqtt.client as mqtt
import threading
import json
import time

# Config via environment variables met sensible defaults
MQTT_BROKER = os.getenv("MQTT_BROKER", "127.0.0.1")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_DATA_TOPIC = os.getenv("MQTT_DATA_TOPIC", "plaatjes/data")

# CH340 USB-UART (op de master-bordjes). Poort-onafhankelijk: we detecteren
# ALLE CH340-poorten automatisch i.p.v. een vaste /dev/ttyMaster1. Zo werkt het
# ongeacht in welke USB-poort een master zit en schaalt het naar 3 masters.
CH340_VID = 0x1A86
CH340_PID = 0x7523
SERIEEL_BAUD = 115200
HERDETECTIE_INTERVAL = 5  # s, hoe vaak we naar nieuwe/verdwenen poorten zoeken

# Commando-topics per master. De koppeling topic -> fysieke poort wordt geleerd
# uit de binnenkomende paal_id (zie paal_naar_topic): een master die palen 1-7
# rapporteert is master1, 8-16 master2, 17-24 master3.
COMMANDO_TOPICS = ["commando/master1", "commando/master2", "commando/master3"]

# Thread-safe opslag voor seriële verbindingen
serieel_lock = threading.Lock()
# device-pad -> serial.Serial (welke poorten open zijn)
open_poorten = {}
# commando-topic -> serial.Serial (geleerd uit binnenkomende paal_id)
topic_naar_serieel = {}

# Diagnose-teller: hoeveel berichten naar MQTT gepubliceerd
publicaties = 0
pub_lock = threading.Lock()


def paal_naar_topic(paal_id):
    """Map een paal_id (1-24) op het commando-topic van de bijbehorende master."""
    try:
        p = int(paal_id)
    except (TypeError, ValueError):
        return None
    if 1 <= p <= 7:
        return "commando/master1"
    if 8 <= p <= 16:
        return "commando/master2"
    if 17 <= p <= 24:
        return "commando/master3"
    return None


def heartbeat():
    """Print elke 10s of de bridge leest en publiceert — zichtbaar in docker logs."""
    vorige = 0
    while True:
        time.sleep(10)
        with pub_lock:
            nu = publicaties
        with serieel_lock:
            poorten = list(open_poorten.keys())
            routes = {t: s.port for t, s in topic_naar_serieel.items()}
        print(f"[STATUS] {nu} berichten gepubliceerd ({nu - vorige} in 10s), "
              f"open poorten: {poorten if poorten else 'GEEN'}, "
              f"routes: {routes if routes else 'nog niet geleerd'}")
        vorige = nu


# ---- MQTT SETUP ----
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)


def on_connect(client, userdata, flags, rc, properties=None):
    print(f"MQTT verbonden (rc={rc})")
    # Hersubscribe bij elke (her)verbinding op alle master-commando-topics
    for topic in COMMANDO_TOPICS:
        client.subscribe(topic)
        print(f"Geabonneerd op {topic}")


def on_disconnect(client, userdata, flags, rc, properties=None):
    print(f"MQTT verbinding verloren (rc={rc}), auto-reconnect actief")


# Ontvangen commando van Node-RED -> doorsturen naar juiste Master
def on_mqtt_message(client, userdata, msg):
    topic = msg.topic
    commando = msg.payload.decode('utf-8').strip()

    print(f"[MQTT] Ontvangen op {topic}: {commando}")

    # Valideer dat het geldige JSON is
    try:
        parsed = json.loads(commando)
        if "paal" not in parsed or "actie" not in parsed:
            print(f"[MQTT] Ongeldig formaat, verwacht: {{\"paal\":1,\"actie\":1}}")
            return
    except json.JSONDecodeError:
        print(f"[MQTT] Geen geldige JSON: {commando}")
        return

    with serieel_lock:
        ser = topic_naar_serieel.get(topic)

    if ser is None:
        # Routering nog niet geleerd: de master die deze palen aanstuurt heeft
        # nog geen batch gestuurd. Data komt snel binnen, dus we negeren dit
        # commando i.p.v. het naar de verkeerde poort te sturen.
        print(f"[SERIEEL] Nog geen poort geleerd voor {topic}, commando genegeerd")
        return

    try:
        ser.write((commando + '\n').encode('utf-8'))
        print(f"[SERIEEL] Commando verstuurd naar {topic} ({ser.port}): {commando}")
    except Exception as e:
        print(f"[SERIEEL] Schrijffout op {topic} ({ser.port}): {e}")


# Lezen van een Master -> doorsturen naar MQTT + routering leren
def lees_poort(device):
    global publicaties
    geleerde_topics = set()
    try:
        print(f"Verbinding maken met {device}...")
        ser = serial.Serial(device, SERIEEL_BAUD, timeout=1)

        # Wacht even en flush bootloader rommel
        time.sleep(2)
        ser.reset_input_buffer()

        with serieel_lock:
            open_poorten[device] = ser

        print(f"Verbonden met {device}")

        while True:
            lijn = ser.readline().decode('utf-8', errors='ignore').strip()
            if not lijn:
                continue

            try:
                data = json.loads(lijn)
            except json.JSONDecodeError:
                # Debug output van de ESP, toon maar stuur niet door
                print(f"[DEBUG] {device}: {lijn}")
                continue

            # Leer welke master op deze poort zit uit de paal_id, zodat
            # commando/masterN naar de juiste poort gerouteerd kan worden.
            topic = paal_naar_topic(data.get("paal"))
            if topic and topic not in geleerde_topics:
                with serieel_lock:
                    topic_naar_serieel[topic] = ser
                geleerde_topics.add(topic)
                print(f"[ROUTE] {device} -> {topic} (paal {data.get('paal')})")

            # Inkomende data ongewijzigd doorpubliceren; de paal_id in de data
            # routeert verder in Node-RED (werkt voor elk aantal masters).
            client.publish(MQTT_DATA_TOPIC, json.dumps(data))
            with pub_lock:
                publicaties += 1
            print(f"[DATA] {device}: {data}")

    except Exception as e:
        print(f"Fout op {device}: {e}, poort vrijgegeven")
    finally:
        # Poort + geleerde routes opruimen zodat de detectie-lus opnieuw kan
        # verbinden en een herstart in een andere poort correct opgepikt wordt.
        with serieel_lock:
            open_poorten.pop(device, None)
            for t in geleerde_topics:
                if topic_naar_serieel.get(t) is not None and \
                        topic_naar_serieel[t].port == device:
                    topic_naar_serieel.pop(t, None)


def vind_ch340_poorten():
    """Geef alle device-paden van aangesloten CH340 USB-UARTs terug."""
    poorten = []
    for p in serial.tools.list_ports.comports():
        if p.vid == CH340_VID and p.pid == CH340_PID:
            poorten.append(p.device)
    return poorten


def detectie_lus():
    """Detecteer periodiek nieuwe CH340-poorten en start er een leesthread voor."""
    while True:
        try:
            for device in vind_ch340_poorten():
                with serieel_lock:
                    al_open = device in open_poorten
                if not al_open:
                    print(f"[DETECTIE] Nieuwe master-poort: {device}")
                    threading.Thread(target=lees_poort, args=(device,),
                                     daemon=True).start()
        except Exception as e:
            print(f"[DETECTIE] Fout bij poortdetectie: {e}")
        time.sleep(HERDETECTIE_INTERVAL)


# ---- START ----
print(f"Bridge -> MQTT {MQTT_BROKER}:{MQTT_PORT}, data-topic {MQTT_DATA_TOPIC}")
print("Master-poorten worden automatisch gedetecteerd (CH340), "
      "ongeacht USB-poort.")
client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_message = on_mqtt_message
client.reconnect_delay_set(min_delay=1, max_delay=30)

# Verbind met auto-reconnect
while True:
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        break
    except Exception as e:
        print(f"MQTT verbinding mislukt: {e}, opnieuw in 5s...")
        time.sleep(5)

# Detectie-thread: opent (en heropent) automatisch elke master-poort
threading.Thread(target=detectie_lus, daemon=True).start()

# Heartbeat-thread voor diagnose (publish-teller + poortstatus elke 10s)
threading.Thread(target=heartbeat, daemon=True).start()

# MQTT loop in main thread (handelt reconnect automatisch af)
print("Serial bridge actief.")
client.loop_forever()
