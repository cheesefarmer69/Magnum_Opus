#!/bin/bash
# deploy-audio.sh — herbouwt en herstart de audio-player container.
# Gebruik na een git pull op de Pi wanneer player.py of de Dockerfile wijzigt.
# WAV-bestanden zelf hoeven GEEN rebuild: die staan in audio/ en worden als
# volume gemount. Drop nieuwe bestanden gewoon in pi/audio-player/audio/...
#
# Voorwaarden:
#  - Docker draait, MQTT-broker draait op 127.0.0.1:1883
#  - De Pi-audiojack werkt (test met: aplay -l)
#  - De repo staat op ~/Magnum_Opus

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
AUDIO_DIR="$SCRIPT_DIR/audio-player"

echo "[deploy-audio] Werkdirectory: $AUDIO_DIR"
cd "$AUDIO_DIR"

echo "[deploy-audio] Image builden..."
docker build -t audio-player .

echo "[deploy-audio] Oude container stoppen..."
docker stop audio-player 2>/dev/null || true
docker rm audio-player 2>/dev/null || true

echo "[deploy-audio] Nieuwe container starten (host network, /dev/snd, audio-volume)..."
docker run -d \
  --name audio-player \
  --restart unless-stopped \
  --network host \
  --device=/dev/snd \
  -e MQTT_BROKER=127.0.0.1 \
  -e MQTT_PORT=1883 \
  -e AUDIO_TOPIC=audio/afspelen \
  -e AUDIO_DIR=/app/audio \
  -e AUDIO_DEV=default \
  -v "$AUDIO_DIR/audio:/app/audio" \
  audio-player

echo "[deploy-audio] Klaar. Container status:"
docker ps --filter name=audio-player --format "table {{.Names}}\t{{.Status}}"

echo "[deploy-audio] Laatste log-regels:"
sleep 2
docker logs --tail 10 audio-player
