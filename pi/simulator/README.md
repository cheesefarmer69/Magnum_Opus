# Magnum Opus — Simulator

Browser-gebaseerde virtuele testomgeving voor het Magnum Opus spel. Verbindt
via MQTT-over-WebSocket met de Mosquitto-broker op de Pi en gedraagt zich
als zomaar weer een MQTT-deelnemer: de Node-RED engine merkt geen verschil
tussen de simulator en de echte hardware.

## Wat het doet

- **Bovenaanzicht** van het speelveld (24-hoek-ring, zoals docs/playfield.md).
- **Per paal een LED-bolletje** dat real-time de actuele LED-actie toont
  (kleuren, animaties zoals geprogrammeerd in firmware/Slave actie-IDs 0–22).
- **Twee modi**:
  - **Monitor**: passief meekijken met een echt spel. Subscribet op
    `commando/master1`, `audio/afspelen` en `plaatjes/data`.
  - **Simulatie**: speelt het hele veld na zonder hardware. Publiceert
    `plaatjes/data` op basis van virtuele speler-posities (RSSI berekend
    uit afstand met log-distance pathloss + ruis).
- **Speler-besturing**: drag de cirkels op de canvas, of zet speler op
  "auto" voor random walk. Voeg spelers toe / verwijder ze in de zijbalk.
- **Log**: alle in- en uitgaande MQTT-berichten met tijdstempel.

## Eenmalige setup: WebSocket-listener op Mosquitto

De broker draait standaard alleen op TCP-poort 1883. Voor browser-gebruik
moet hij ook websockets accepteren. `config/mqtt/mosquitto.conf` bevat
inmiddels:

```
listener 9001
protocol websockets
```

Op de Pi: `git pull && docker restart <mosquitto-container>`. Daarna
luistert Mosquitto op zowel 1883 (bridge.py, Node-RED) als 9001 (browser).

## Starten

Geen build-stap. Open `index.html` rechtstreeks in je browser. Op de meeste
moderne browsers werkt `file://` met mqtt-over-ws prima; lukt dat niet
(strikte CSP/CORS), draai een lokale webserver:

```powershell
cd "c:\PROJECTEN ELEKTRONICA\Magnum Opus\VS_Code\pi\simulator"
python -m http.server 8080
# open http://localhost:8080/ in browser
```

In de UI:
1. Vul broker in (default `192.168.1.43:9001`).
2. Druk **Connect**. Status linksboven moet "online" worden.
3. Kies modus: **Monitor** of **Simulatie**.

## Monitor-modus

Veilig, leest alleen. Start het echte spel (bridge.py + Node-RED), open de
sim, en je ziet:
- LED-bolletjes naast de palen kleuren mee bij elk `commando/master1`-bericht.
- Speler-cirkels verspringen naar de paal waar Node-RED hen plaatst.
- Audio-aanvragen in de log onder de canvas.

## Simulatie-modus

Vervangt de hardware. Voor één-bron-van-waarheid: stop de echte bridge:

```bash
ssh pi@192.168.1.43
docker stop serial-bridge
```

Zet modus op **Simulatie**. Sleep spelers naar palen of zet ze op auto.
De engine in Node-RED reageert alsof er echte hardware praat. Druk in
Node-RED op een Plates-of-Fate event en bekijk hoe het bord-LED's en
spelers zich gedragen.

## Tuning RSSI-model

In `sim.js`, bovenaan:

```js
const RSSI0_DBM    = -45;   // signaal op 1 m
const PATH_LOSS_N  = 2.5;   // hoger = meer demping (open buiten ~2.0-2.5)
const RSSI_SIGMA   = 3;     // ruis-stdev in dBm
const RSSI_DREMPEL = -85;   // palen onder dit niet rapporteren
```

Pas aan na een echte veldtest om het model passend te maken bij de
werkelijke metingen.

## Browser-eisen

Een moderne browser (Chrome / Firefox / Edge 2024+). Voor MQTT-over-WS
wordt `mqtt.min.js` v5 via unpkg CDN geladen, dus internet-toegang is
nodig bij eerste laden (browser-cache vangt herhaalde sessies op).

## Beperkingen / volgende versie

- Geen scenario-opname/replay (komt in v2).
- Geen log-export.
- RSSI-tuning gebeurt nu via code-edit, niet via UI.
- LED-animaties in de browser benaderen de firmware-animaties qua kleur
  en frequentie, maar zijn niet pixel-identiek aan de fysieke strip.
