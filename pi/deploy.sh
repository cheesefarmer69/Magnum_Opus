#!/bin/bash
#
# deploy.sh — herbouwt en herstart de serial-bridge container.
# Gebruik na elke git pull op de Pi.
#
# Voorwaarden:
#  - Dit script staat in pi/deploy.sh van de magnum-opus repo
#  - De repo is gecloond op /home/pi/magnum-opus
#  - Docker draait
#  - /dev/ttyMaster1 bestaat (anders udev rule installeren, zie config/udev/)
#
# Wijzigingen worden meteen actief; oude container wordt gestopt en vervangen.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BRIDGE_DIR="$SCRIPT_DIR/serial-bridge"

echo "[deploy] Werkdirectory: $BRIDGE_DIR"
cd "$BRIDGE_DIR"

echo "[deploy] Image builden..."
docker build -t serial-bridge .

echo "[deploy] Oude container stoppen..."
docker stop serial-bridge 2>/dev/null || true
docker rm serial-bridge 2>/dev/null || true

echo "[deploy] Nieuwe container starten (host network, ttyMaster1, auto-restart)..."
docker run -d \
  --name serial-bridge \
  --restart unless-stopped \
  --network host \
  --device=/dev/ttyMaster1 \
  -e MQTT_BROKER=127.0.0.1 \
  -e MQTT_PORT=1883 \
  -e MQTT_DATA_TOPIC=plaatjes/data \
  serial-bridge

echo "[deploy] Klaar. Container status:"
docker ps --filter name=serial-bridge --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"

echo "[deploy] Laatste log-regels:"
sleep 2
docker logs --tail 10 serial-bridge