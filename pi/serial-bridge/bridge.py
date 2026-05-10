import os
import serial
import paho.mqtt.client as mqtt
import threading
import json
import time

# Config via environment variables met sensible defaults
MQTT_BROKER = os.getenv("MQTT_BROKER", "127.0.0.1")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_DATA_TOPIC = os.getenv("MQTT_DATA_TOPIC", "plaatjes/data")

MEESTERS = [
    {"poort": "/dev/ttyMaster1", "baud": 115200, "commando_topic": "commando/master1"}
]

# Thread-safe opslag voor seriële verbindingen
serieel_verbindingen = {}
serieel_lock = threading.Lock()

# ---- MQTT SETUP ----
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)


def on_connect(client, userdata, flags, rc, properties=None):
    print(f"MQTT verbonden (rc={rc})")
    # Hersubscribe bij elke (her)verbinding
    for config in MEESTERS:
        client.subscribe(config['commando_topic'])
        print(f"Geabonneerd op {config['commando_topic']}")


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
        if topic in serieel_verbindingen:
            ser = serieel_verbindingen[topic]
            try:
                ser.write((commando + '\n').encode('utf-8'))
                print(f"[SERIEEL] Commando verstuurd naar {topic}: {commando}")
            except Exception as e:
                print(f"[SERIEEL] Schrijffout op {topic}: {e}")
        else:
            print(f"[SERIEEL] Geen verbinding voor {topic}")


# Lezen van Master -> doorsturen naar MQTT
def lees_poort(config):
    while True:
        try:
            print(f"Verbinding maken met {config['poort']}...")
            ser = serial.Serial(config['poort'], config['baud'], timeout=1)

            # Wacht even en flush bootloader rommel
            time.sleep(2)
            ser.reset_input_buffer()

            with serieel_lock:
                serieel_verbindingen[config['commando_topic']] = ser

            print(f"Verbonden met {config['poort']}")

            while True:
                lijn = ser.readline().decode('utf-8', errors='ignore').strip()
                if not lijn:
                    continue

                try:
                    data = json.loads(lijn)
                    client.publish(MQTT_DATA_TOPIC, json.dumps(data))
                    print(f"[DATA] {config['poort']}: {data}")
                except json.JSONDecodeError:
                    # Debug output van de ESP, toon maar stuur niet door
                    print(f"[DEBUG] {config['poort']}: {lijn}")

        except Exception as e:
            with serieel_lock:
                serieel_verbindingen.pop(config['commando_topic'], None)
            print(f"Fout op {config['poort']}: {e}, opnieuw in 5s...")
            time.sleep(5)


# ---- START ----
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

# Start een thread per seriële poort
for config in MEESTERS:
    thread = threading.Thread(target=lees_poort, args=(config,), daemon=True)
    thread.start()

# MQTT loop in main thread (handelt reconnect automatisch af)
print("Serial bridge actief.")
client.loop_forever()