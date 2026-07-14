import os
import re
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

# ---- Volume (dashboard Admin -> retained topic audio/volume) ----
# De container heeft alsa-utils (aplay EN amixer) en krijgt /dev/snd volledig doorgegeven --
# dus ook controlC0, het mixer-device. ALSA-mixerstanden zijn kernel-globaal: wat we hier zetten
# geldt meteen voor de hele Pi.
VOLUME_TOPIC  = os.getenv("VOLUME_TOPIC", "audio/volume")
MIXER_CARD    = os.getenv("MIXER_CARD", "Headphones")   # Pi 4 aux-jack; `aplay -l` toont de naam
MIXER_CONTROL = os.getenv("MIXER_CONTROL", "")          # leeg = automatisch opsporen

# Wachtrij van af te spelen segment-lijsten. Elke entry is een lijst van
# bestandsnamen (relatief t.o.v. AUDIO_DIR), die sequentieel worden afgespeeld.
# Begrensd (maxsize) met drop-oldest in on_message: zo blijft de box bij drukte
# dicht bij real-time i.p.v. structureel achter het spel aan te lopen.
QUEUE_MAX = int(os.getenv("AUDIO_QUEUE_MAX", "8"))
afspeel_queue: "queue.Queue[list]" = queue.Queue(maxsize=QUEUE_MAX)


def speel_segment(relpad: str) -> None:
    """Speel een enkel WAV-bestand af via aplay. Ontbrekend bestand = overslaan."""
    pad = os.path.join(AUDIO_DIR, relpad)
    if not os.path.isfile(pad):
        print(f"[AUDIO] Bestand ontbreekt, overgeslagen: {relpad}")
        return
    try:
        # -q = quiet, -D = ALSA-device. Blokkeert tot het segment klaar is.
        # timeout: een hangende ALSA (device bezet/kapot) mag de worker niet permanent
        # blokkeren — dan valt alle audio stil zonder foutmelding. Segmenten zijn kort;
        # 30 s is ruim en aplay wordt bij overschrijding gekild.
        subprocess.run(["aplay", "-q", "-D", AUDIO_DEV, pad], check=True, timeout=30)
        print(f"[AUDIO] Afgespeeld: {relpad}")
    except subprocess.TimeoutExpired:
        print(f"[AUDIO] aplay hing >30s bij {relpad} - gekild, wachtrij loopt door")
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


def zoek_mixer_control() -> str:
    """Vind de naam van de volume-control op MIXER_CARD.

    De aux-jack heet op Raspberry Pi OS Bookworm meestal 'Headphone', op oudere images 'PCM'.
    We zoeken hem op i.p.v. te gokken, zodat een ander image niet stilletjes het volume negeert.
    """
    if MIXER_CONTROL:
        return MIXER_CONTROL
    try:
        uit = subprocess.run(["amixer", "-c", MIXER_CARD, "scontrols"],
                             capture_output=True, text=True, timeout=5).stdout
    except (subprocess.SubprocessError, FileNotFoundError) as e:
        print(f"[VOLUME] amixer niet bruikbaar: {e}")
        return ""
    gevonden = re.findall(r"Simple mixer control '([^']+)'", uit)
    for voorkeur in ("Headphone", "PCM", "Master", "Speaker"):
        if voorkeur in gevonden:
            return voorkeur
    if gevonden:
        return gevonden[0]
    print(f"[VOLUME] Geen mixer-control gevonden op kaart {MIXER_CARD!r}")
    return ""


def zet_volume(procent: int) -> None:
    """Zet het afspeelvolume (0-100 %). Werkt ook MIDDEN in een lopend segment."""
    procent = max(0, min(100, int(procent)))
    control = zoek_mixer_control()
    if not control:
        return
    try:
        # -M = 'mapped' volume: lineair voor het OOR. Zonder -M is de bcm2835-schaal in dB, en
        # dan klinkt "80%" al bijna vol -- de slider zou dan nauwelijks iets doen in het onderste
        # bereik. Met -M loopt de schuif zoals je verwacht.
        subprocess.run(["amixer", "-q", "-M", "-c", MIXER_CARD, "sset", control, f"{procent}%"],
                       check=True, timeout=5)
        print(f"[VOLUME] {MIXER_CARD}/{control} -> {procent}%")
    except subprocess.CalledProcessError as e:
        print(f"[VOLUME] amixer-fout ({MIXER_CARD}/{control}): {e}")
    except subprocess.TimeoutExpired:
        print("[VOLUME] amixer reageerde niet binnen 5s")
    except FileNotFoundError:
        print("[VOLUME] 'amixer' niet gevonden - is alsa-utils geinstalleerd?")


# ---- MQTT ----
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)


def on_connect(client, userdata, flags, rc, properties=None):
    print(f"MQTT verbonden (rc={rc})")
    client.subscribe([(AUDIO_TOPIC, 0), (VOLUME_TOPIC, 0)])
    print(f"Geabonneerd op {AUDIO_TOPIC} en {VOLUME_TOPIC}")
    # VOLUME_TOPIC is retained: de laatst ingestelde stand komt hier meteen binnen, dus het
    # volume herstelt zichzelf na een container-herstart of een reboot van de Pi.


def on_disconnect(client, userdata, flags, rc, properties=None):
    print(f"MQTT verbinding verloren (rc={rc}), auto-reconnect actief")


def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode("utf-8"))
    except json.JSONDecodeError:
        print(f"[MQTT] Geen geldige JSON op {msg.topic}")
        return

    if msg.topic == VOLUME_TOPIC:
        # Kaal getal (75) of {"volume": 75}. BEWUST niet via de afspeel-queue: het volume moet
        # ook midden in een lopend segment meteen werken, niet pas als de wachtrij leeg is.
        waarde = data.get("volume") if isinstance(data, dict) else data
        try:
            zet_volume(int(waarde))
        except (TypeError, ValueError):
            print(f"[VOLUME] Ongeldige waarde op {msg.topic}: {data!r}")
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

    # Begrensde wachtrij met drop-oldest: bij een volle queue het OUDSTE (nog niet
    # gespeelde) item weggooien en het nieuwe erin zetten, i.p.v. blokkeren. Zo loopt
    # de audio niet structureel achter bij een burst events.
    try:
        afspeel_queue.put_nowait(veilig)
    except queue.Full:
        try:
            gedropt = afspeel_queue.get_nowait()
            afspeel_queue.task_done()
            print(f"[AUDIO] Wachtrij vol (max {QUEUE_MAX}) - oudste gedropt: {gedropt}")
        except queue.Empty:
            pass
        try:
            afspeel_queue.put_nowait(veilig)
        except queue.Full:
            print("[AUDIO] Wachtrij nog vol na drop - nieuw item overgeslagen")


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
