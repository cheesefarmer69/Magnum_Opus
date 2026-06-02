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
| Exacte posities doorsturen (`sim/locatie`) | ✗ | ✓ |
| Spelers verslepen / toevoegen / verwijderen | ✗ | ✓ |
| Automatische random walk per speler | ✗ | ✓ |
| Meerdere virtuele spelers tegelijk | n.v.t. | ✓ |
| Deterministische positie (geen RSSI-model) | n.v.t. | ✓ |
| 24-uur veld (onafhankelijk van paaltjesLijst) | ✗ | ✓ |
| Verplaatsingscontrole testen (te veel / terug in tijd) | ✗ | ✓ |
| Buzzer-piep per afgeroepen uur (🔔) | ✓ | ✓ |
| LED-animaties testen (actie 11–16) | ✓ | ✓ |
| Plates-of-Fate flow testen zonder hardware | ✗ | ✓ |
| Foutcodes enkel bij echte overtreding | ✓ | ✓ |
| Scenario opnemen/afspelen | ✗ (v2) | ✗ (v2) |

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

De simulator is een **spelverloop- en conflict-tester**. De hardware wordt
verondersteld te werken, dus er is **geen RSSI-model**: de simulator stuurt de
exacte paal van elke speler direct door (topic `sim/locatie`) en werkt op een
**24-uur veld** (via `sim/modus {sim24:true}`), onafhankelijk van `paaltjesLijst`.

**Standalone.** Zodra je op **Simulatie** staat, schakelt Node-RED de echte
locatiebepaling uit (`simVeld24`): alleen de simulator bepaalt dan de posities, ook
als de echte serial-bridge/hardware nog draait. Er is een aparte dashboard-pagina
**"Simulatie"** met de PoF-besturing én de live radar van de virtuele spelers, zodat
je het hele spelverloop op één scherm test. Schakel je terug naar **Monitor**, dan
toont alles weer puur het echte spel.

### Voorbereiding: stop de echte bridge

Om dubbele bronnen te voorkomen, stop je `bridge.py` op de Pi:

```bash
ssh pi@192.168.1.43
docker stop serial-bridge
```

### Stap voor stap

1. Open de simulator in je browser en verbind (zie Monitor stap 1–4).
2. Selecteer modus **Simulatie** — Node-RED schakelt over op het 24-uur veld.
3. Beheer spelers in de zijbalk: **+ Speler** toevoegen, **✕** (rood) verwijderen,
   zo test je met het gewenste aantal.
4. **Sleep** een speler naar een uur, of zet hem op **Auto**. De simulator stuurt
   zijn exacte uur door — `spelerLocaties` volgt meteen, zonder ruis of vertraging.
5. Druk in Node-RED een Plates-of-Fate event aan. Bij een **uur-event** klinkt per
   afgeroepen uur de buzzer-piep (slave-actie 23) en toont de simulator 600 ms een
   🔔 bij die paal.
6. Na de reactietijd verschijnt in de log enkel een **foutcode** als er écht een
   regel overtreden is (TE WEINIG / TE VEEL / TERUG IN TIJD / BEWOOG mocht niet);
   bij een correcte ronde een groene "Controle OK".
7. **Pause** in de header bevriest de tick (geen posities meer doorgestuurd).

### Verplaatsingscontrole

Op het 24-uur veld controleert Node-RED per doelwit-speler de netto verplaatsing:
te weinig (`min`), te veel (`max`) of achteruit ("terug in tijd") worden geflagd;
niet-doelwitten die toch bewegen ook. Omdat de simulator posities deterministisch
doorstuurt, klopt deze controle exact met wat je op het veld plaatst.

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
- Geen RSSI-/signaalsimulatie: de simulator test het spelverloop, niet de
  radioprestaties van de hardware. Voor RSSI-diagnose zie `docs/locatiebepaling.md`
  (ruwe-RSSI-tabel met echte hardware).
- LED-animaties in de browser benaderen de firmware-animaties maar zijn
  niet pixel-identiek aan de fysieke LED-strip.
