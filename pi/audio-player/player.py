import os
import json
import time
import queue
import threading
import subprocess
import paho.mqtt.client as mqtt

# ---- Config via environment variables ----
MQTT_BROKER = os.getenv("MQTT_BROKER", "127.0.0.1")
MQTT_PORT   = int(os.getenv("MQTT_PORT", "1883"))
AUDIO_TOPIC = os.getenv("AUDIO_TOPIC", "audio/afspelen")
AUDIO_DIR   = os.getenv("AUDIO_DIR", "/app/audio")
AUDIO_DEV   = os.getenv("AUDIO_DEV", "default")   # ALSA-device, bv. "plughw:0,0"

# Wachtrij van af te spelen segment-lijsten. Elke entry is een lijst van
# bestandsnamen (relatief t.o.v. AUDIO_DIR), die sequentieel worden afgespeeld.
afspeel_queue: "queue.Queue[list]" = queue.Queue()


def speel_segment(relpad: str) -> None:
    """Speel een enkel WAV-bestand af via aplay. Ontbrekend bestand = overslaan."""
    pad = os.path.join(AUDIO_DIR, relpad)
    if not os.path.isfile(pad):
        print(f"[AUDIO] Bestand ontbreekt, overgeslagen: {relpad}")
        return
    try:
        # -q = quiet, -D = ALSA-device. Blokkeert tot het segment klaar is.
        subprocess.run(["aplay", "-q", "-D", AUDIO_DEV, pad], check=True)
        print(f"[AUDIO] Afgespeeld: {relpad}")
    except subprocess.CalledProcessError as e:
        print(f"[AUDIO] aplay-fout bij {relpad}: {e}")
    except FileNotFoundError:
        print("[AUDIO] 'aplay' niet gevonden - is alsa-utils geinstalleerd?")


def afspeel_worker() -> None:
    """Speelt segment-lijsten een voor een af zodat audio niet overlapt."""
    while True:
        segmenten = afspeel_queue.get()
        try:
            for relpad in segmenten:
                speel_segment(relpad)
        finally:
            afspeel_queue.task_done()


# ---- MQTT ----
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)


def on_connect(client, userdata, flags, rc, properties=None):
    print(f"MQTT verbonden (rc={rc})")
    client.subscribe(AUDIO_TOPIC)
    print(f"Geabonneerd op {AUDIO_TOPIC}")


def on_disconnect(client, userdata, flags, rc, properties=None):
    print(f"MQTT verbinding verloren (rc={rc}), auto-reconnect actief")


def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode("utf-8"))
    except json.JSONDecodeError:
        print(f"[MQTT] Geen geldige JSON op {msg.topic}")
        return

    segmenten = data.get("segments")
    if not isinstance(segmenten, list) or not segmenten:
        # Geen segment-lijst: niets af te spelen (alleen tekst voor de simulator).
        tekst = data.get("tekst", "")
        print(f"[AUDIO] Geen segmenten (tekst: {tekst!r}) - overgeslagen")
        return

    # Alleen strings doorlaten en pad-traversal weren.
    veilig = [s for s in segmenten if isinstance(s, str) and ".." not in s]
    print(f"[AUDIO] In wachtrij ({data.get('fase','?')}): {veilig}")
    afspeel_queue.put(veilig)


# ---- Start ----
client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_message = on_message
client.reconnect_delay_set(min_delay=1, max_delay=30)

threading.Thread(target=afspeel_worker, daemon=True).start()

while True:
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        break
    except Exception as e:
        print(f"MQTT verbinding mislukt: {e}, opnieuw in 5s...")
        time.sleep(5)

print(f"Audio-player actief. Map: {AUDIO_DIR}, device: {AUDIO_DEV}")
client.loop_forever()
