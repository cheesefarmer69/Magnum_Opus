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
#
# Protocol v2 (Batch 1): de bridge is INHOUD-AGNOSTISCH. De master vertaalt alle
# binaire ESP-NOW-berichten al naar JSON-regels; de bridge publiceert elke geldige
# JSON-regel ongewijzigd op plaatjes/data. De nieuwe v2-types (heartbeat {"hb":1},
# fout {"fout":..}, knop {"knop":1}, uitvoering {"status":"uitgevoerd","seq":..})
# stromen dus vanzelf door — geen wijziging hier nodig. Routing leert nog steeds uit
# paal_id (zie paal_naar_topic).
CH340_VID = 0x1A86
CH340_PID = 0x7523
SERIEEL_BAUD = 115200
HERDETECTIE_INTERVAL = 5  # s, hoe vaak we naar nieuwe/verdwenen poorten zoeken

# Commando-topics per master. De koppeling topic -> fysieke poort wordt geleerd
# uit de binnenkomende paal_id (zie paal_naar_topic): een master die palen 1-8
# rapporteert is master1, 9-16 master2, 17-24 master3.
COMMANDO_TOPICS = ["commando/master1", "commando/master2", "commando/master3"]

# Thread-safe opslag voor seriële verbindingen
serieel_lock = threading.Lock()
# device-pad -> serial.Serial (welke poorten open zijn)
open_poorten = {}
# commando-topic -> serial.Serial (geleerd uit binnenkomende paal_id)
topic_naar_serieel = {}
# R6: master-nr -> (device, tijdstip) van de laatste announce. Announcet hetzelfde
# master-nr kort na elkaar op TWEE verschillende poorten, dan zijn twee borden met
# hetzelfde MASTER_NR geflasht -> stille route-flip-flop. We alarmeren dan via een
# bridge_fout-bericht op plaatjes/data (Node-RED toont ST-006 in de pre-flight).
laatste_announce = {}
laatste_conflict_alarm = {}

# Diagnose-teller: hoeveel berichten naar MQTT gepubliceerd
publicaties = 0
pub_lock = threading.Lock()


def paal_naar_topic(paal_id):
    """Map een paal_id (1-24) op het commando-topic van de bijbehorende master."""
    try:
        p = int(paal_id)
    except (TypeError, ValueError):
        return None
    if 1 <= p <= 8:
        return "commando/master1"
    if 9 <= p <= 16:
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

            # Identiteits-aankondiging van een master ({"announce":1,"master":N,...}): leer hieruit
            # METEEN welke poort welke master is -> commando/masterN routeert ook zonder dat er al een
            # slave detecteert. Altijd (her)koppelen zodat een her-geplugde master naar de juiste poort
            # blijft wijzen. De aankondiging is bridge-plumbing en wordt NIET naar MQTT doorgestuurd.
            if data.get("announce"):
                m = data.get("master")
                topic = f"commando/master{m}" if m in (1, 2, 3) else None
                if topic:
                    nu_s = time.time()
                    with serieel_lock:
                        vorige = laatste_announce.get(m)
                        laatste_announce[m] = (device, nu_s)
                        # R6: conflict = zelfde master-nr announcede < 10 s geleden op een ANDERE
                        # poort die nog open is (een verhuisde master heeft zijn oude poort dicht).
                        conflict = (vorige is not None and vorige[0] != device
                                    and (nu_s - vorige[1]) < 10 and vorige[0] in open_poorten)
                        verandert = topic_naar_serieel.get(topic) is not ser
                        topic_naar_serieel[topic] = ser
                    geleerde_topics.add(topic)
                    if conflict and nu_s - laatste_conflict_alarm.get(m, 0) > 30:
                        laatste_conflict_alarm[m] = nu_s
                        fout = {"bridge_fout": "MASTER_CONFLICT", "master": m,
                                "poorten": [vorige[0], device]}
                        client.publish(MQTT_DATA_TOPIC, json.dumps(fout))
                        print(f"[ROUTE-CONFLICT] master {m} announcet op {vorige[0]} EN {device} "
                              f"- twee borden met hetzelfde MASTER_NR geflasht?")
                    if verandert:
                        print(f"[ROUTE] {device} -> {topic} (announce master {m})")
                continue

            # Leer welke master op deze poort zit uit de paal_id, zodat
            # commando/masterN naar de juiste poort gerouteerd kan worden.
            # BELANGRIJK: alleen leren uit berichten die de EIGEN paal van deze
            # master dragen (batch/heartbeat/fout/knop/batt). Status-echo's
            # (regels met "status":...) zoals {"status":"buiten_bereik","paal":17,
            # "master":1} dragen de paal van een VREEMD commando (de afgewezen
            # paal), niet een eigen slave. Daaruit leren zou de route vergiftigen
            # (bv. commando/master3 -> master1-poort) waardoor álle commando's voor
            # die paal bij de verkeerde master belanden -> "buiten_bereik" in een lus.
            if "status" not in data:
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
