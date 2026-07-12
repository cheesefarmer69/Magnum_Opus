#!/bin/bash
#
# deploy.sh — herbouwt en herstart de serial-bridge container.
# Gebruik na elke git pull op de Pi.
#
# Voorwaarden:
#  - Dit script staat in pi/deploy.sh van de magnum-opus repo
#  - De repo is gecloond op /home/pi/magnum-opus
#  - Docker draait
#  - Eén of meer masters (CH340 USB-UART) hangen aan de Pi, in om het even
#    welke USB-poort. De bridge detecteert ze automatisch.
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

echo "[deploy] Nieuwe container starten (host network, alle USB-serial, auto-restart)..."
# Geen vaste --device meer: de bridge detecteert zelf elke CH340-master, in
# welke USB-poort ook. We geven de container toegang tot alle tty-devices via
# een cgroup-regel (major 188 = USB-serial) + /dev mount, zodat ook een master
# die later (her)ingeplugd wordt automatisch opgepikt wordt.
docker run -d \
  --name serial-bridge \
  --restart unless-stopped \
  --network host \
  --log-opt max-size=10m \
  --log-opt max-file=3 \
  --device-cgroup-rule='c 188:* rmw' \
  -v /dev:/dev \
  -e MQTT_BROKER=127.0.0.1 \
  -e MQTT_PORT=1883 \
  -e MQTT_DATA_TOPIC=plaatjes/data \
  serial-bridge

echo "[deploy] Dangling images opruimen (SD-bescherming)..."
docker image prune -f >/dev/null

echo "[deploy] Klaar. Container status:"
docker ps --filter name=serial-bridge --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"

echo "[deploy] Laatste log-regels:"
sleep 2
docker logs --tail 10 serial-bridge