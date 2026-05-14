# Versies en componenten — Magnum Opus

**Bijgehouden op:** 2026-05-10
**Doel:** snapshot van exact welke versies waar draaien, voor reproduceerbaarheid en debugging.

Update dit document bij elke significante versie-wijziging. Bij twijfel:
versie hier vermeld vs. de werkelijkheid op je systeem? Zie de verificatie-commando's onderaan.

---

## Firmware (PlatformIO)

### Master — ESP32 WROOM-32

| Component | Versie | Bron |
|-----------|--------|------|
| PlatformIO platform | `espressif32 @ 6.5.0` | `firmware/Master/platformio.ini` |
| Board | `esp32dev` | idem |
| Framework | Arduino | idem |
| ESP32 Arduino core | 2.0.17 | volgt uit platform 6.5.0 |
| Monitor baud | 115200 | idem |
| Upload baud | 921600 | idem |

**Geen externe libraries** — master gebruikt alleen Arduino core (`WiFi`, `esp_now`, `esp_wifi`).

### Slave — ESP32-C3 SuperMini

| Component | Versie | Bron |
|-----------|--------|------|
| PlatformIO platform | `espressif32 @ 6.5.0` | `firmware/Slave/platformio.ini` |
| Board | `esp32-c3-devkitm-1` | idem |
| Framework | Arduino | idem |
| ESP32 Arduino core | 2.0.17 | volgt uit platform 6.5.0 |
| NimBLE-Arduino | 1.4.2 | **niet upgraden naar 2.x** (breaking changes) |
| FastLED | 3.10.3 | voor WS2812B LED-strip |
| Monitor baud | 115200 | idem |
| Upload baud | 921600 | idem |

---

## Raspberry Pi (hub)

| Component | Versie |
|-----------|--------|
| OS | Raspberry Pi OS (Raspbian GNU/Linux 12, Bookworm) |
| Kernel | 6.1.0-rpi7-rpi-v8 |
| Architectuur | ARM64 (aarch64) |
| Docker | 27.3.1, build ce12230 |

---

## Container images

Tags zijn nog op `latest` — bij significant breekrisico of vóór een release overstappen op gepinde versies. De image-ID's hieronder dienen als referentiepunt voor wat er feitelijk draaide op de bijhouddatum.

| Service | Image | Tag | Image ID |
|---------|-------|-----|----------|
| MQTT broker | `eclipse-mosquitto` | latest | `a91f36de744c` |
| Node-RED | `nodered/node-red` | latest | `ab580c156804` |
| Serial bridge | `serial-bridge` (lokaal gebouwd) | latest | `e107134fd251` |

### Node-RED runtime details

| Component | Versie |
|-----------|--------|
| Node-RED | 4.1.7 |
| Node.js (in container) | 20.20.0 |

### Serial-bridge runtime details

| Component | Versie |
|-----------|--------|
| Base image | `python:3.11-slim` |
| Python | 3.11 |

---

## Python packages (serial-bridge)

Gepind in `pi/serial-bridge/requirements.txt`:

| Package | Versie | Notitie |
|---------|--------|---------|
| paho-mqtt | 2.1.0 | gebruikt `CallbackAPIVersion.VERSION2` syntax |
| pyserial | 3.5 | seriële communicatie met master |

---

## Compatibiliteitsregels (bekende valkuilen)

- **NimBLE-Arduino 1.4.2 → 2.x:** breaking changes in API. Niet upgraden zonder ook de slave-code aan te passen.
- **paho-mqtt 1.x → 2.x:** vereist `mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)`. De huidige `bridge.py` gebruikt deze API; downgraden naar 1.x breekt de code.
- **ESP32 Arduino core 2.x → 3.x:** veranderingen in WiFi/ESP-NOW API. Niet upgraden zonder code-review.
- **Mosquitto-config:** `allow_anonymous true` werkt alleen voor lokaal netwerk-gebruik. Bij netwerk-blootstelling: auth toevoegen.
- **Docker network modes:**
  - `serial-bridge` draait in `--network host` → `MQTT_BROKER=127.0.0.1` werkt
  - `magnum-Opus` (Node-RED) draait in default bridge → moet `192.168.1.43` gebruiken om broker te bereiken

---

## Verificatie-commando's

Om te controleren of de werkelijkheid overeenkomt met dit document.

### Op je laptop (in repo root)

```bash
cat firmware/Master/platformio.ini
cat firmware/Slave/platformio.ini
cat pi/serial-bridge/requirements.txt
```

### Op de Pi (PuTTY)

```bash
cat /etc/os-release | grep -E "PRETTY_NAME|VERSION_ID"
uname -r
docker --version
docker inspect MQTT-broker --format='{{.Config.Image}}'
docker inspect magnum-Opus --format='{{.Config.Image}}'
docker images --format "table {{.Repository}}\t{{.Tag}}\t{{.ID}}" | grep -E "mosquitto|node-red|serial-bridge"
docker exec magnum-Opus node-red --version
```

---

## Wijzigingsgeschiedenis

- **2026-05-10**: initieel document. PIO-platform en libraries gepind. Container-images nog op `latest`.