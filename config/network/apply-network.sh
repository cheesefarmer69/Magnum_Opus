#!/bin/bash
#
# apply-network.sh — codificeert het netwerkprofiel van de hub (Pi 4).
#
# WAAROM DIT SCRIPT BESTAAT: de fix voor de "Veld-eth kaapt eth0"-valkuil (dé
# waarschijnlijkste oorzaak van 'moet elke dag rebooten', zie
# docs/handleidingen/verbinden-met-de-hub.md) bestond alleen als handmatig
# nmcli-commando in de docs. Na elke SD-herkloon moest iemand dat onthouden.
# Dit script is idempotent: draai het gerust opnieuw (onderdeel van de
# SD-kloon-checklist in docs/handleidingen/hub-noodherstel.md).
#
# Gebruik (op de Pi):
#   sudo bash ~/Magnum_Opus/config/network/apply-network.sh
#
# Wat het doet:
#  1. Veld-eth (het 192.168.51.1-shared-profiel op eth0) NIET meer laten
#     autoconnecten. Zonder deze stap kaapt Veld-eth eth0 zodra thuis-DHCP één
#     keer hapert, en hangt de Pi op 192.168.51.1 tot een handmatige ingreep.
#     Op het veld activeer je hem bewust:  sudo nmcli con up Veld-eth
#  2. Het AP-profiel (MagnumOpus-AP) pinnen op kanaal 6 — weg van ESP-NOW's
#     kanaal 1 (H6): dashboard-/WebSocket-verkeer mag niet met de paal-radio's
#     concurreren.
#  3. Autoconnect-prioriteiten: thuis-DHCP wint op eth0.

set -e

if ! command -v nmcli >/dev/null; then
  echo "[network] nmcli niet gevonden - is dit wel de Pi (NetworkManager)?" >&2
  exit 1
fi

echo "[network] Profielen nu:"
nmcli -t -f NAME,DEVICE,AUTOCONNECT con show | sed 's/^/  /'

# 1. Veld-eth: bestaat het profiel, zet autoconnect uit (idempotent).
if nmcli -t -f NAME con show | grep -qx "Veld-eth"; then
  nmcli con mod Veld-eth connection.autoconnect no
  echo "[network] Veld-eth: autoconnect UIT (op het veld: sudo nmcli con up Veld-eth)"
else
  echo "[network] Veld-eth-profiel niet gevonden - overslaan (zie verbinden-met-de-hub.md om het aan te maken)"
fi

# 2. AP op kanaal 6 (H6: weg van ESP-NOW kanaal 1).
if nmcli -t -f NAME con show | grep -qx "MagnumOpus-AP"; then
  nmcli con mod MagnumOpus-AP 802-11-wireless.channel 6
  echo "[network] MagnumOpus-AP: kanaal gepind op 6"
else
  echo "[network] MagnumOpus-AP-profiel niet gevonden - overslaan"
fi

# 3. Thuisprofiel op eth0 (default 'Wired connection 1'): hoogste prioriteit.
if nmcli -t -f NAME con show | grep -qx "Wired connection 1"; then
  nmcli con mod "Wired connection 1" connection.autoconnect-priority 10
  echo "[network] Wired connection 1: autoconnect-priority 10 (wint van alles op eth0)"
fi

echo "[network] Klaar. Controle:"
nmcli -t -f NAME,DEVICE,AUTOCONNECT con show | sed 's/^/  /'
