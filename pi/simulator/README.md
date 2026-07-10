# Magnum Opus — Simulator

Browser-gebaseerde virtuele testomgeving voor het Magnum Opus spel. Verbindt
via MQTT-over-WebSocket met de Mosquitto-broker op de Pi en gedraagt zich als
een gewone MQTT-deelnemer — Node-RED merkt geen verschil met echte hardware.

## Overzicht

De simulator sluit aan op precies dezelfde MQTT-naad als de echte hardware:

```
[Simulator browser]
    ↕  MQTT-over-WebSocket (poort 9001)
[Mosquitto broker op Pi — thuis 192.168.1.43, veld-AP 192.168.50.1, veld-kabel 192.168.51.1]
    ↕  MQTT TCP (poort 1883)
[Node-RED spellogica]
```

De simulator wordt **door de Pi zelf geserveerd** op `http://<pi-adres>:1880/sim/` (Node-RED
`httpStatic`, read-only mount van deze map — zie `pi/node-red/docker-compose.yml` + `settings.js`).
Het broker-veld vult zichzelf in met het adres uit je adresbalk; zie
`docs/handleidingen/verbinden-met-de-hub.md` voor alle adressen/situaties.

**Monitor-modus**: de simulator luistert alleen. Geen enkel effect op het
draaiende spel.

**Simulatie-modus**: de simulator publiceert `plaatjes/data` en vervangt
daarmee `bridge.py` als bron. Node-RED reageert op virtuele spelers alsof
het echte hardware is.

---

## Speltype kiezen (Plates of Fate / Klokslag)

Naast **Modus** staat in de header een **Spel**-keuze: **🎲 Plates of Fate** of **🕐 Klokslag**.
De keuze is één bron van waarheid via het retained topic `spel/type` — kiezen in de simulator óf in
het Node-RED Bediening-dashboard werkt beide kanten op en blijft bewaard na een reconnect.

De **modus** (Monitor/Simulatie) is ook op het Node-RED Bediening-dashboard schakelbaar; de keuze
synct via retained `sim/modus` beide kanten op (de simulator-radio schuift mee).

**PoF-doelen.** Bij een actief doel toont de zijbalk naast de "Spelers"-kop het **percentage**
spelers dat zijn doel haalde (`X% doel (a/n)`) en worden de **namen van geslaagde spelers
gehighlight** (groen). Data komt uit `pof/doelstatus`.

Welk spel actief is, staat **duidelijk in de banner** boven de event-balk (kleur + icoon wisselt mee).
Bij **Klokslag** verbergt de simulator de PoF-panelen (events, toestanden, middernacht) en toont in
plaats daarvan de **Klokslag-timer**, het **scorebord per team** en de **teamlegenda**. De LED's
tonen dan per paal de teamkleur: rust = zacht ademend wit, inname = kaarsflikker met helderheid ∝
voortgang `P/H`, ingenomen = constant fel, gelijkspel/verval = bevroren kleur. Zie
`docs/spel/klokslag.md` voor de regels en `pi/node-red/blokken/07_klokslag/` voor de engine.

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
| Portaal tonen (paarse lijn) + erdoor teleporteren | ✓ (tonen) | ✓ (teleport) |
| Toestanden-paneel (actieve tags per uur + resterende events) | ✓ | ✓ |
| LED-toestanden tonen (paars=portaal, goud=happy hour) | ✓ | ✓ |
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

Geen build-stap nodig. **Aanbevolen:** open `http://<pi-adres>:1880/sim/` — de Pi serveert de
simulator zelf (werkt op elk toestel, ook gsm/tablet, en het broker-veld staat dan automatisch
juist). Alternatief blijft `index.html` direct openen in je browser; lukt `file://` niet
(strikte CSP/CORS in sommige browsers), start dan een lokale webserver:

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
3. Controleer het broker-adres (vult zichzelf in; thuis `192.168.1.43`, veld-AP `192.168.50.1`,
   veld-kabel `192.168.51.1`), poort `9001`.
4. Druk **Connect** — status linksbovenaan wordt groen ("online").
5. Selecteer modus **Monitor** (standaard).
6. Wat je nu ziet:
   - **LED-bolletjes** naast de palen kleuren mee bij elk `commando/master1`-bericht. *(De simulator
     volgt momenteel alleen `commando/master1`; `commando/master2`/`master3` komen bij Batch 4.)*
   - **Speler-posities** volgen de **uitkomst van het locatie-algoritme** (topic
     `locatie/spelers`), niet de ruwe paal-berichten — dus geen geflikker meer.
   - **Huidig event** toont enkel de tekst die effectief voorgelezen wordt
     ("3 uren worden Happy Hour."); links ervan staat een **teller** met het aantal
     events dat deze partij gevallen is (reset bij Stop).
   - **Log-paneel** met een **dropdown-filter** ("Toon ▾"): vink aan wat je wil zien —
     **Info · Commando's · Audio · Foutcodes**. De gekozen filters én de loghoogte blijven
     bewaard (localStorage). Sleep de bovenrand om de hoogte in te stellen; het speelveld
     krimpt mee i.p.v. weggeduwd te worden, en de oudste logregels verdwijnen vanzelf.
     Ook **"Toestand afgelopen"** verschijnt in de log zodra een toestand verdwijnt.
   - **Audio**-aanvragen (oranje in de log) verschijnen bij Plates-of-Fate events.
   - **Historiek-paneel** (rechts, klik op de verticale "Historiek"-tab) toont de
     events van het lopende spel chronologisch (topic `spel/historie`).
   - **Portaal**: zodra een portaal-event valt, kleuren twee palen continu paars en
     verschijnt er een **paarse stippellijn** tussen de twee uren (topic
     `pof/portalen`).

### Portaal gebruiken (simulatiemodus)

Sleep een speler naar een portaal-uur en **laat hem daar los**: de simulator laat hem
één publish-cyclus op dat uur staan en teleporteert hem dan naar het **partner-uur**.
Doordat Node-RED zo een directe paalwissel A→B ziet, scoort het die sprong als portaal
(**0 levensuren**); de stappen die je ervoor/erna nog versleept, tellen wel gewoon mee.
De automatische random-walk gebruikt het portaal niet — test portaalgebruik dus met
slepen.

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

## Drukknoppen

Sommige palen hebben een fysieke drukknop (configureerbaar via `[CONFIG] Drukknop-palen` in
Node-RED, retained op `config/drukknoppen`). Tussen het speelveld en de event-kolom staat het
**Drukknoppen-paneel**: één knop per geconfigureerde paal, in **twee kolommen** met per knop het
uur ("Paal 7"). Klik een knop om hem in te drukken — de simulator publiceert dan
`{"paal":N,"knop":1}` op `plaatjes/data` (exact zoals de hardware via `MSG_KNOP`) en toont een korte
**flits** bij die paal op het veld. Een knop werkt op **elk** moment, in welke event-fase ook.

Bij een actief **tijdbom**-event krijgen de gekozen **ontmantel-palen** een rode rand in het paneel
(en knipperen ze rood op het veld, `ACTIE_TIJDBOM`). Bom-spelers dragen een 💣-badge met hun
resterende events. Druk de knop op een ontmantel-paal waar een bom-speler staat om te ontmantelen
(dag 80% / nacht 50%).

## Systeeminstellingen

De knop **⚙ Systeeminstellingen** rechtsboven opent een paneel met:

- **Nacht (middernacht) actief** — het middernacht-mechanisme aan/uit (verhuisd vanuit het
  Middernacht-paneel; publiceert `sim/middernacht-config`).
- **Toestanden exclusief** — tijdbom & ziekte niet samen op één speler (uit = ze mogen samen).
- **Tempo** — reactietijd-multiplier om events sneller/trager te laten verlopen tijdens het testen.

De laatste twee publiceren (retained) op `sim/systeem-config` `{toestandExclusief, tempo}`.

## Spelinstellingen

De knop **🎲 Spelinstellingen** (naast Systeeminstellingen) opent een paneel met **spelregel**-instellingen:

- **Slechte aura** (default aan) — negatieve speler-events (ziekte, tijdbom) treffen 's avonds (uur 20–6)
  en op middernacht (uur 24) vaker, zodat de dag veiliger is.
- **Thuisbank** (default uit) — wie bij de controle **exact op zijn startuur landt**, stort zijn
  verzamelde levensuren onverliesbaar in `globaleStats` en begint aan een nieuwe ronde. **Geblokkeerd**
  zolang hij een *geneesbare* toestand draagt (ziekte of tijdbom), zodat er altijd iets te verliezen
  blijft. Zie invarianten TB1–TB4.
- **Spel-tempo** (uitlezing) — de huidige tempo-factor (0,6–1,3), gestuurd door de `Sneller`/`Trager`
  wereld-events; uit `pof/status.spelTempo`.

De eerste twee publiceren (retained) op `sim/spel-config` `{badAura, thuisbank}`.

## Event-tiers (zeldzaamheid)

In het **Events**-paneel heeft elke event-kaart een **tier-dropdown** (common/uncommon/rare/epic/legendary).
De tier bepaalt de kans dat een event gekozen wordt (gewichten 50/25/15/8/2). De keuze wordt retained
gepubliceerd op `sim/tiers-config` → `global.eventTiers`. Default-tier komt uit het event zelf.

Elke event-kaart heeft ook een **"→ wachtrij"-knop**: die zet dat event **vooraan** in de wachtrij
(= het volgende event), publiceert `sim/wachtrij-toevoegen` `{id}`. De "Volgende events"-rij (max 5)
schuift door. Werkt enkel tijdens een lopend spel (anders houdt Node-RED de wachtrij leeg).

## Tijd terug (↶)

Naast de titel **Huidig event** staat een **↶-knop**: één ronde terug in de tijd. De simulator publiceert
`sim/tijd-terug`; Node-RED herstelt de laatste snapshot (stats, posities, effecten, ziekte/tijdbom,
middernacht, tempo) en zet via `pof/herstel-posities` de spelers terug op het veld. (Sim-modus; op echte
hardware kun je spelers niet terugzetten.)

## Spelers-toestanden

In de zijbalk onder **Spelers** staan de **zieke** spelers (🤒) én de **tijdbom**-spelers (💣), elk met hun
aftelteller (countdown van 10 events).

## Volgende events — wegklikken

Het paneel **Volgende events** toont de geplande wachtrij (`pof/status.wachtrij`). Met de **✕** naast een
rij klik je dat aankomende event weg: de simulator publiceert `sim/wachtrij-weg` `{index}`, Node-RED haalt
die entry uit `global.pofWachtrij`, en de rij schuift door (vult zich daarna weer aan tot 5).

---

## Browser-eisen

Moderne browser (Chrome / Firefox / Edge 2024+). De MQTT-bibliotheek
(`mqtt.min.js` v5.10.1) is **lokaal gebundeld** in `vendor/mqtt.min.js` —
géén internet/CDN nodig, de simulator werkt dus ook op het veld (offline).

---

## Autonoom AI-agent testen (`tools/speltest/`)

Naast deze handmatige browser-simulator bestaat er een **autonoom testharnas** dat
hetzelfde spel speelt zónder browser: het publiceert dezelfde `sim/*`-topics en bestuurt
de engine via het nieuwe `sim/bediening`-commando (zie `docs/protocol.md` §5). Een
onafhankelijk **orakel** toetst elke ronde tegen de spelregels en rapporteert bugs,
crashes en exploits. Scripted strategieën (braaf/grens/overtreder/chaos/exploit) en een
live Claude-subagent-modus delen één rapportformaat. Zie `tools/speltest/README.md`.

## Beperkingen / volgende versie

- Geen scenario-opname of -replay (gepland voor v2).
- Geen log-export naar bestand.
- Volgt momenteel alleen `commando/master1`. De topics `commando/master2`/`master3` worden
  toegevoegd bij de multi-master refactor (Batch 4).
- Geen RSSI-/signaalsimulatie: de simulator test het spelverloop, niet de
  radioprestaties van de hardware. Voor RSSI-diagnose zie `docs/locatiebepaling.md`
  (ruwe-RSSI-tabel met echte hardware).
- De LED-bolletjes tonen de minimale actie-set: paars (portaal), goud (happy hour) of
  uit. Andere kleuren/animaties bestaan niet meer in de firmware.
- Het **Toestanden**-paneel (rechtsonder in de zijbalk) toont per tag welke uur-toestand
  actief is en hoeveel events die nog blijft (via topic `pof/toestanden`).
