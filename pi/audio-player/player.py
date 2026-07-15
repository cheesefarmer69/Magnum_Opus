import os
import re
import json
import time
import wave
import queue
import threading
import subprocess
import paho.mqtt.client as mqtt

# ---- Config via environment variables ----
MQTT_BROKER = os.getenv("MQTT_BROKER", "127.0.0.1")
MQTT_PORT   = int(os.getenv("MQTT_PORT", "1883"))
AUDIO_TOPIC = os.getenv("AUDIO_TOPIC", "audio/afspelen")
MUZIEK_TOPIC = os.getenv("MUZIEK_TOPIC", "audio/muziek")   # bestuurbaar kanaal (pauze/hervat/stop)
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


# ---- Bestuurbaar audiokanaal (topic audio/muziek) ----
# Los van de segment-queue: een LANGE track (bv. de Poolse song of de dood-cutscene) die je kan
# PAUZEREN (positie onthouden) en HERVATTEN, of hard STOPPEN mid-track. aplay kan niet seeken, dus
# we openen de WAV met `wave`, springen naar de bewaarde frame-offset en pijpen de resterende PCM
# naar `aplay` (stdin). De positie schatten we via de wandklok (aplay buffert -> benaderend, ruim
# genoeg voor een song/cutscene). Speelt naast de afroepen door ALSA (default/dmix mixt).
_FORMAAT = {1: "U8", 2: "S16_LE", 3: "S24_3LE", 4: "S32_LE"}


class KanaalSpeler:
    def __init__(self):
        self._lock = threading.Lock()
        self._track = None          # relpad van de geladen WAV
        self._offset = 0            # frame-positie voor de volgende resume
        self._framerate = 44100
        self._totaal = 0            # totaal aantal frames
        self._nch = 2
        self._sampw = 2
        self._proc = None           # lopend aplay-proces
        self._thread = None         # lopende stream-thread
        self._stop_evt = threading.Event()

    def _stop_intern(self):
        """Onderbreek een lopende stream en bewaar de bereikte positie. Lock al vast."""
        if self._thread and self._thread.is_alive():
            self._stop_evt.set()
            if self._proc:
                try:
                    self._proc.terminate()
                except Exception:
                    pass
            self._thread.join(timeout=3)
        self._proc = None
        self._thread = None

    def _stream(self, start_frame, framerate, totaal, pad, nch, sampw):
        fmt = _FORMAAT.get(sampw, "S16_LE")
        t0 = time.monotonic()
        proc = None
        try:
            proc = subprocess.Popen(
                ["aplay", "-q", "-D", AUDIO_DEV, "-c", str(nch), "-r", str(framerate), "-f", fmt],
                stdin=subprocess.PIPE)
            self._proc = proc
            with wave.open(pad, "rb") as w:
                w.setpos(min(start_frame, totaal))
                brok = max(1, framerate // 8)   # ~0,125 s per chunk
                while not self._stop_evt.is_set():
                    data = w.readframes(brok)
                    if not data:
                        break
                    proc.stdin.write(data)
                proc.stdin.close()
                if not self._stop_evt.is_set():
                    proc.wait(timeout=30)
        except (BrokenPipeError, OSError) as e:
            print(f"[MUZIEK] streamfout {pad}: {e}")
        except subprocess.TimeoutExpired:
            pass
        finally:
            # Positie bijwerken uit de wandklok (aplay geeft geen positie terug).
            gespeeld = int((time.monotonic() - t0) * framerate)
            with self._lock:
                if self._proc is proc:   # niet overschrijven als er al een nieuwe stream loopt
                    self._offset = min(totaal, start_frame + max(0, gespeeld))

    def _laad(self, track):
        pad = os.path.join(AUDIO_DIR, track)
        with wave.open(pad, "rb") as w:
            self._framerate = w.getframerate()
            self._totaal = w.getnframes()
            self._nch = w.getnchannels()
            self._sampw = w.getsampwidth()
        self._track = track

    def play(self, track):
        """Start een track vanaf het begin (offset 0)."""
        with self._lock:
            self._stop_intern()
            try:
                self._laad(track)
            except (wave.Error, OSError, EOFError) as e:
                print(f"[MUZIEK] kan {track} niet laden: {e}")
                return
            self._offset = 0
            self._start_locked()

    def resume(self):
        """Hervat de geladen track vanaf de bewaarde positie."""
        with self._lock:
            if not self._track:
                return
            if self._offset >= self._totaal:
                return   # track al uit
            self._start_locked()

    def _start_locked(self):
        self._stop_intern()
        self._stop_evt = threading.Event()
        pad = os.path.join(AUDIO_DIR, self._track)
        args = (self._offset, self._framerate, self._totaal, pad, self._nch, self._sampw)
        self._thread = threading.Thread(target=self._stream, args=args, daemon=True)
        self._thread.start()
        print(f"[MUZIEK] {self._track} @ frame {self._offset}/{self._totaal}")

    def pause(self):
        with self._lock:
            self._stop_intern()
            print(f"[MUZIEK] pauze @ frame {self._offset}")

    def stop(self):
        with self._lock:
            self._stop_intern()
            self._offset = 0
            print("[MUZIEK] stop (positie gereset)")


kanaal = KanaalSpeler()


def verwerk_muziek(data: dict) -> None:
    """Bestuur het audiokanaal: {"cmd": play|pause|resume|stop, "track": "relpad"}."""
    cmd = data.get("cmd")
    track = data.get("track")
    if cmd == "play":
        if isinstance(track, str) and ".." not in track:
            kanaal.play(track)
        else:
            print(f"[MUZIEK] play zonder geldige track: {data!r}")
    elif cmd == "resume":
        kanaal.resume()
    elif cmd == "pause":
        kanaal.pause()
    elif cmd == "stop":
        kanaal.stop()
    else:
        print(f"[MUZIEK] onbekend commando: {data!r}")


# Gedetecteerde (kaart, control) — één keer opsporen en cachen. None = nog niet gezocht.
_mixer_cache = None


def _kaart_kandidaten() -> list:
    """Kaarten om te proberen, in volgorde: expliciete MIXER_CARD, wat `aplay -l` meldt (naam +
    index), en tot slot 0-3 als vangnet. Zo werkt het volume ook als de kaart niet 'Headphones' heet."""
    kandidaten = []
    if MIXER_CARD and MIXER_CARD not in kandidaten:
        kandidaten.append(MIXER_CARD)
    try:
        uit = subprocess.run(["aplay", "-l"], capture_output=True, text=True, timeout=5).stdout
        for m in re.finditer(r"card (\d+): (\w+)", uit):
            for k in (m.group(2), m.group(1)):   # kaartnaam én -index
                if k not in kandidaten:
                    kandidaten.append(k)
    except (subprocess.SubprocessError, FileNotFoundError):
        pass
    for i in ("0", "1", "2", "3"):
        if i not in kandidaten:
            kandidaten.append(i)
    return kandidaten


def detecteer_mixer() -> tuple:
    """Zoek de eerste geluidskaart met een bruikbare volume-control en cache (kaart, control).

    De aux-jack heet op Raspberry Pi OS Bookworm meestal 'Headphone', op oudere images 'PCM' — en
    de KAART heet niet overal 'Headphones'. We tasten daarom alle kaarten af i.p.v. te gokken, zodat
    het volume niet stilletjes genegeerd wordt op een afwijkend image. Override: MIXER_CARD/MIXER_CONTROL.
    """
    global _mixer_cache
    if _mixer_cache is not None:
        return _mixer_cache
    voorkeur = ("Headphone", "PCM", "Master", "Speaker", "Playback")
    geprobeerd = []
    for kaart in _kaart_kandidaten():
        geprobeerd.append(str(kaart))
        try:
            uit = subprocess.run(["amixer", "-c", str(kaart), "scontrols"],
                                 capture_output=True, text=True, timeout=5).stdout
        except (subprocess.SubprocessError, FileNotFoundError):
            continue
        controls = re.findall(r"Simple mixer control '([^']+)'", uit)
        if not controls:
            continue
        if MIXER_CONTROL and MIXER_CONTROL in controls:
            keuze = MIXER_CONTROL
        else:
            keuze = next((c for c in voorkeur if c in controls), controls[0])
        _mixer_cache = (str(kaart), keuze)
        print(f"[VOLUME] Mixer gedetecteerd: kaart {kaart!r}, control {keuze!r}")
        return _mixer_cache
    print(f"[VOLUME] Geen geluidskaart met een volume-control gevonden (geprobeerd: {geprobeerd}). "
          "Zet MIXER_CARD/MIXER_CONTROL handmatig in pi/deploy-audio.sh, of check "
          "`docker exec audio-player aplay -l`.")
    _mixer_cache = ("", "")
    return _mixer_cache


def zet_volume(procent: int) -> None:
    """Zet het afspeelvolume (0-100 %). Werkt ook MIDDEN in een lopend segment."""
    global _mixer_cache
    procent = max(0, min(100, int(procent)))
    kaart, control = detecteer_mixer()
    if not control:
        print(f"[VOLUME] Kan {procent}% niet zetten: geen mixer-control gevonden.")
        return
    try:
        # -M = 'mapped' volume: lineair voor het OOR. Zonder -M is de bcm2835-schaal in dB, en
        # dan klinkt "80%" al bijna vol -- de slider zou dan nauwelijks iets doen in het onderste
        # bereik. Met -M loopt de schuif zoals je verwacht.
        subprocess.run(["amixer", "-q", "-M", "-c", str(kaart), "sset", control, f"{procent}%"],
                       check=True, timeout=5)
        print(f"[VOLUME] {kaart}/{control} -> {procent}%")
    except subprocess.CalledProcessError as e:
        _mixer_cache = None   # detectie resetten: misschien is de kaart-indeling gewijzigd
        print(f"[VOLUME] amixer-fout ({kaart}/{control}): {e} - detectie gereset, volgende poging zoekt opnieuw.")
    except subprocess.TimeoutExpired:
        print("[VOLUME] amixer reageerde niet binnen 5s")
    except FileNotFoundError:
        print("[VOLUME] 'amixer' niet gevonden - is alsa-utils geinstalleerd?")


# ---- MQTT ----
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)


def on_connect(client, userdata, flags, rc, properties=None):
    print(f"MQTT verbonden (rc={rc})")
    client.subscribe([(AUDIO_TOPIC, 0), (VOLUME_TOPIC, 0), (MUZIEK_TOPIC, 0)])
    print(f"Geabonneerd op {AUDIO_TOPIC}, {VOLUME_TOPIC} en {MUZIEK_TOPIC}")
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

    if msg.topic == MUZIEK_TOPIC:
        # Bestuurbaar kanaal (pauze/hervat/stop) — BEWUST niet via de segment-queue: een
        # pauze/stop moet meteen werken, niet pas als de wachtrij leeg is.
        if isinstance(data, dict):
            verwerk_muziek(data)
        else:
            print(f"[MUZIEK] Geen object op {msg.topic}: {data!r}")
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
