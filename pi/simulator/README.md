# Magnum Opus — Simulator

Browser-gebaseerde virtuele testomgeving voor het Magnum Opus spel. Verbindt
via MQTT-over-WebSocket met de Mosquitto-broker op de Pi en gedraagt zich als
een gewone MQTT-deelnemer — Node-RED merkt geen verschil met echte hardware.

## Overzicht

De simulator sluit aan op precies dezelfde MQTT-naad als de echte hardware:

```
[Simulator browser]
    ↕  MQTT-over-WebSocket (poort 9001)
[Mosquitto broker op Pi — 192.168.1.43]
    ↕  MQTT TCP (poort 1883)
[Node-RED spellogica]
```

**Monitor-modus**: de simulator luistert alleen. Geen enkel effect op het
draaiende spel.

**Simulatie-modus**: de simulator publiceert `plaatjes/data` en vervangt
daarmee `bridge.py` als bron. Node-RED reageert op virtuele spelers alsof
het echte hardware is.

---

## Capabilities per modus

| Functie | Monitor | Simulatie |
|---------|---------|-----------|
| LED-kleuren per paal real-time zien | ✓ | ✓ |
| Speler-posities bijhouden (via detecties) | ✓ | ✓ |
| Speelveldbord visueel (bovenaanzicht) | ✓ | ✓ |
| Audio-aanvragen zien in log | ✓ | ✓ |
| Inkomende commando's loggen | ✓ | ✓ |
| Detecties publiceren (`plaatjes/data`) | ✗ | ✓ |
| Spelers verslepen op het veld | ✗ | ✓ |
| Automatische random walk per speler | ✗ | ✓ |
| Meerdere virtuele spelers tegelijk | n.v.t. | ✓ |
| RSSI-ruis simuleren (log-distance model) | n.v.t. | ✓ |
| Locatiebepaling in Node-RED testen | ✗ | ✓ |
| LED-animaties testen (actie 11–16) | ✓ | ✓ |
| Plates-of-Fate flow testen zonder hardware | ✗ | ✓ |
| Scenario opnemen/afspelen | ✗ (v2) | ✗ (v2) |
| Log exporteren | ✗ (v2) | ✗ (v2) |
| RSSI-drempel instellen via UI | ✗ | ✗ (code-edit) |

---

## Eenmalige setup

### 1. WebSocket-listener op Mosquitto activeren

De broker moet naast TCP 1883 ook WebSockets accepteren op poort 9001.
`config/mqtt/mosquitto.conf` bevat al:

```
listener 9001
protocol websockets

log_dest stdout
```

`log_dest stdout` is verplicht voor Docker — de container heeft geen
`/mosquitto/log/`-map, en zonder dit crasht de container bij (her)start.

Op de Pi na `git pull`:

```bash
docker restart <naam-van-mosquitto-container>
# Controleer met:
docker logs <naam-van-mosquitto-container> | grep 9001
# Verwacht: "Opening websockets listen socket on port 9001"
```

### 2. Simulator starten

Geen build-stap nodig. Open `index.html` direct in je browser. Lukt `file://`
niet (strikte CSP/CORS in sommige browsers), start dan een lokale webserver:

```powershell
cd "c:\PROJECTEN ELEKTRONICA\Magnum Opus\VS_Code\pi\simulator"
python -m http.server 8080
# Open http://localhost:8080/ in browser
```

---

## Monitor-modus

Kijk passief mee met een echt spel zonder enig risico.

### Stap voor stap

1. Zorg dat het spel actief is: `bridge.py` draait, Node-RED is gestart.
2. Open de simulator in je browser.
3. Controleer het broker-adres: `192.168.1.43`, poort `9001`.
4. Druk **Connect** — status linksbovenaan wordt groen ("online").
5. Selecteer modus **Monitor** (standaard).
6. Wat je nu ziet:
   - **LED-bolletjes** naast de palen kleuren mee bij elk `commando/master1`-bericht.
   - **Speler-posities** volgen de **uitkomst van het locatie-algoritme** (topic
     `locatie/spelers`), niet de ruwe paal-berichten — dus geen geflikker meer.
   - **Log-paneel** toont alle inkomende berichten; vink "Foutcodes" aan voor
     controle-resultaten, "Commando's" voor enkel LED-commando's.
   - **Audio**-aanvragen (oranje in de log) verschijnen bij Plates-of-Fate events.
   - **Historiek-paneel** (rechts, klik op de verticale "Historiek"-tab) toont de
     events van het lopende spel chronologisch (topic `spel/historie`).

### Kleuren in de log

| Kleur | Betekenis |
|-------|-----------|
| Blauw | Commando van Node-RED (`commando/master1`) |
| Groen | Detectie-data (`plaatjes/data`) |
| Oranje | Audio-aanvraag (`audio/afspelen`) |
| Grijs | Info-berichten (verbinding, etc.) |
| Rood | Fouten |

### Foutzoeken (Monitor)

**Status blijft "offline"**
- Controleer of Mosquitto draait: `docker ps` op de Pi.
- Controleer of poort 9001 open is: `docker logs <mosquitto>` → zoek naar `"websockets listen socket on port 9001"`.
- Controleer of je browser het `ws://`-protocol blokkeert (Chrome via `file://` kan CORS issues geven — gebruik dan de lokale webserver).

**LED-bolletjes bewegen niet**
- Controleer of Node-RED commando's stuurt: kijk in de Node-RED debug-tab.
- Controleer of `commando/master1` het correcte topic is (zie `docs/protocol.md`).

---

## Simulatie-modus

Vervangt de hardware volledig. Node-RED ziet virtuele spelers als echte detecties.

### Voorbereiding: stop de echte bridge

Om dubbele detecties te voorkomen, stop je `bridge.py` op de Pi:

```bash
ssh pi@192.168.1.43
docker stop serial-bridge
```

### Stap voor stap

1. Open de simulator in je browser en verbind (zie Monitor stap 1–4).
2. Selecteer modus **Simulatie**.
3. Voeg spelers toe via **+ Speler** in de zijbalk.
4. **Sleep** een speler naar een paal op het canvas — de simulator berekent
   RSSI voor alle palen en publiceert detecties.
5. Of zet een speler op **Auto** (knop naast de naam) voor random walk.
6. **Node-RED Locatiebepaling** reageert: de tabel vult zich met paal en RSSI.
7. Druk in Node-RED een Plates-of-Fate event aan en bekijk de LED-bolletjes.
8. Druk **Pause** in de header om de simulatie-tick te bevriezen (spelers
   stoppen met auto-bewegen, geen nieuwe detecties worden gepubliceerd).

### RSSI-model

De simulator berekent voor elke combinatie speler–paal een RSSI via het
log-distance path-loss model:

```
RSSI(d) = RSSI0 − 10 · n · log10(d / 1m) + N(0, σ²)
```

Standaardwaarden (aanpasbaar bovenaan `sim.js`):

| Parameter | Waarde | Betekenis |
|-----------|--------|-----------|
| `RSSI0_DBM` | −45 dBm | Signaalsterkte op 1 m |
| `PATH_LOSS_N` | 2.5 | Dempingsexponent (buiten: 2.0–2.5) |
| `RSSI_SIGMA` | 3 dBm | Ruis-standaardafwijking |
| `RSSI_DREMPEL` | −85 dBm | Palen onder dit worden niet gerapporteerd |

Palen boven de drempel worden gepubliceerd als `{"paal":N,"mac":"...","rssi":-67}`.
Zo werkt de locatiebepaling in Node-RED identiek als bij echte hardware.

### Tuning na veldtest

Na een echte veldmeting pas je de constanten aan bovenaan `sim.js` zodat
het simulatiemodel overeenkomt met de werkelijkheid.

### Foutzoeken (Simulatie)

**Node-RED ziet geen detecties**
- Controleer of `serial-bridge` gestopt is — anders zijn er twee bronnen.
- Controleer in de simulator-log of groene `plaatjes/data`-berichten verschijnen.
- Controleer of QoS en topic overeenkomen met wat Node-RED verwacht.

**Speler verschijnt niet op het canvas**
- Klik **+ Speler** en sleep de cirkel weg van het midden — spelers spawnen
  op het midden en overlappen elkaar initieel.

**Auto-walk werkt niet**
- Zorg dat de modus **Simulatie** is (niet Monitor) — auto-walk publiceert
  detecties en is alleen actief in simulatiemodus.

---

## Browser-eisen

Moderne browser (Chrome / Firefox / Edge 2024+). De MQTT-bibliotheek
(`mqtt.min.js` v5) wordt geladen via unpkg CDN — internetverbinding nodig
bij het eerste laden (daarna gecached door de browser).

---

## Beperkingen / volgende versie

- Geen scenario-opname of -replay (gepland voor v2).
- Geen log-export naar bestand.
- RSSI-tuning via code-edit (`sim.js`), niet via UI.
- LED-animaties in de browser benaderen de firmware-animaties maar zijn
  niet pixel-identiek aan de fysieke LED-strip.
