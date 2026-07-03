#!/usr/bin/env bash
# Deploy de volledige Node-RED flow via de Admin API (Linux/Pi / Git Bash).
#
# Vervangt ALLE flows + dashboard-UI in een keer, op node-ID, zonder duplicaten
# en zonder handmatig wissen/importeren in de browser.
#
# Gebruik:
#   ./deploy-flows.sh                      # naar http://192.168.1.43:1880
#   ./deploy-flows.sh http://<host>:1880   # andere host/poort
#   NODERED_URL=http://x:1880 ./deploy-flows.sh
#
# Vereist: curl; Node-RED bereikbaar; geen admin-authenticatie.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FLOWS="$SCRIPT_DIR/flows.json"
URL="${1:-${NODERED_URL:-http://192.168.1.43:1880}}"

if [[ ! -f "$FLOWS" ]]; then
  echo "FOUT: $FLOWS niet gevonden" >&2
  exit 1
fi

# 1. JSON valideren (best effort) zodat we nooit een kapotte flow pushen
if command -v python3 >/dev/null 2>&1; then
  python3 -c "import json,sys; json.load(open(sys.argv[1]))" "$FLOWS" && echo "JSON OK" \
    || { echo "FOUT: ongeldige JSON" >&2; exit 1; }
elif command -v jq >/dev/null 2>&1; then
  jq empty "$FLOWS" && echo "JSON OK" || { echo "FOUT: ongeldige JSON" >&2; exit 1; }
else
  echo "WAARSCHUWING: geen python3/jq gevonden om JSON te valideren, ga door..."
fi

# 2. Volledige deploy via de Admin API
echo "Deploy naar $URL/flows ..."
http_code=$(curl -sS -o /tmp/nr_deploy_resp -w "%{http_code}" \
  -X POST "$URL/flows" \
  -H "Content-Type: application/json" \
  -H "Node-RED-Deployment-Type: full" \
  --data-binary @"$FLOWS")

if [[ "$http_code" == "204" || "$http_code" == "200" ]]; then
  echo "OK - alle flows vervangen (HTTP $http_code)."
else
  echo "FOUT: Node-RED gaf HTTP $http_code" >&2
  cat /tmp/nr_deploy_resp >&2 2>/dev/null || true
  echo >&2
  echo "Tip: draait Node-RED op $URL ? Staat admin-auth uit?" >&2
  exit 1
fi
