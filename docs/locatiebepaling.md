# Locatiebepaling & beacon-stabiliteit

Dit document beschrijft hoe het systeem bepaalt bij welke paal een speler staat,
hoe je dat live bijregelt, en hoe je instabiele beacons opspoort en kalibreert.

## Eerst: komt er wel data binnen?

Tuning heeft geen zin als er niets binnenkomt. De keten is:

```
master (USB) → bridge.py (serial-bridge) → MQTT plaatjes/data → Node-RED
```

Veelvoorkomende oorzaak: **je hebt nog een seriële monitor open** (PlatformIO,
Arduino IDE, `screen`, ...). Die vergrendelt de poort, zodat `bridge.py` op de Pi
hem niet kan openen — maar **één proces tegelijk** kan een seriële poort lezen.
Je ziet de data dan wél in jouw monitor, maar de Pi krijgt niets.

### Beslisboom

**1. Staat de Node-RED mqtt-in op groen "connected"?** (editor, bolletje onder
"Ontvang Paal/MAC Data"). Groen → Node-RED is verbonden met de broker en zou élk
bericht ontvangen; de fout zit dan **vóór de broker** (bridge/serieel). Geel/rood →
Node-RED bereikt de broker niet (controleer broker-adres `192.168.1.43:1883` en de
netwerk-mode van de Node-RED container).

**2. Bewijs het MQTT↔Node-RED-pad met de zelftest.** In de editor staat een inject
**"TEST: publiceer plaatjes/data"** (tab 01 Locatiebepaling) die via de broker een
testdetectie publiceert. Klik hem:
- Radar zet **Lilou op Paal 1** (en de debug toont het bericht) → MQTT + Node-RED
  werken **100 %**. Het probleem zit gegarandeerd upstream → ga naar 3.
- Niets → ondanks groen toch een broker/pad-probleem (zeldzaam) — check broker-logs.

**3. Kijk wat de bridge doet:** `docker logs serial-bridge`. Sinds de heartbeat
print hij elke 10 s:
```
[STATUS] 1234 berichten gepubliceerd (42 in 10s), open poorten: ['/dev/ttyUSB0'], routes: {'commando/master1': '/dev/ttyUSB0'}
```
- `open poorten: GEEN` → de bridge vindt geen master: **sluit elke seriële
  monitor**, en zit de master in de **Pi** (niet je pc)? Check
  `ls -l /dev/ttyUSB*` en `lsusb | grep 1a86`. Heeft de container toegang
  (`-v /dev:/dev` + cgroup-rule in `deploy.sh`) en is de udev-regel
  (`MODE=0666`) geïnstalleerd? De USB-poort maakt niet meer uit — de bridge
  detecteert elke CH340 automatisch.
- `routes: nog niet geleerd` terwijl er wel data binnenkomt → normaal tot de
  eerste batch; de routering wordt geleerd uit de `paal_id`.
- poort verbonden maar teller blijft **0** → de master stuurt geen geldige
  JSON-regels naar de Pi.
- teller **loopt op** → de bridge publiceert; dan moet Node-RED het zien (stap 2
  bevestigt dat het pad werkt).

**4. Directe bus-test (optioneel).** Installeer de MQTT-tools en luister mee:
```bash
sudo apt install -y mosquitto-clients
mosquitto_sub -h 192.168.1.43 -t plaatjes/data -v
```

> Node-RED leest MQTT op `192.168.1.43:1883`; `bridge.py` publiceert op
> `127.0.0.1:1883`. Beide raken dezelfde Mosquitto — dat hoort te kloppen.
> **Onthoud:** maar één proces kan een seriële poort lezen; een open
> PlatformIO/Arduino-monitor blokkeert `bridge.py`.

## Hoe de locatiebepaling werkt

De functie **Locatiebepaling Spelers** (Node-RED, tab "01 Locatiebepaling")
ontvangt elk `plaatjes/data`-bericht `{paal, mac, rssi}` en beslist per speler bij
welke paal die hoort. De aanpak (na de herziening):

1. **Kalibratie-offset** per beacon wordt bij de RSSI opgeteld (zie onder).
2. **RSSI-vloer**: metingen zwakker dan de vloer zijn ruis en tellen niet mee.
3. **Venster**: per paal worden alleen recente samples (jonger dan *Venster*) bewaard.
4. **Mediaan** per paal i.p.v. gemiddelde — één uitschieter van een instabiele
   beacon kan de schatting zo niet kapen.
5. **Beslissing met grace + sustained-switch**:
   - Staat de speler al bij een paal en die paal krijgt nog samples? Dan moet een
     andere paal **minstens `Hysterese` dB sterker** zijn én dat **`Switch-samples`
     keer op rij** vóórdat er gewisseld wordt. Dit voorkomt heen-en-weer-springen.
   - Krijgt de huidige paal even geen samples (beacon mist advertenties)? Dan wordt
     de paal **vastgehouden** zolang de stilte korter is dan `Grace`. Pas daarna mag
     hij naar de sterkste kandidaat springen.

> **Waarom dit het springen oplost:** vroeger werd de huidige paal direct losgelaten
> zodra hij 5,5 s geen data kreeg, en sprong de speler naar élke paal met data — ook
> een veel zwakkere. Met een trage beacon (adv-interval 700 ms) gebeurde dat
> voortdurend. De grace-periode en sustained-switch maken de schatting stabiel.

## Bakens toewijzen en vervangen (dashboard, zonder deploy)

De koppeling **baken-MAC → spelernaam** (`global.spelersLijst`) bepaalt wie een beacon is. Je beheert ze
**live vanaf het dashboard** — pagina **Beacons & Locatie**, groep **"Spelers / bakens beheren"** — zonder
`flows.json` te editen of te deployen.

**Een reservebaken toewijzen / een baken vervangen (± 30 s):**
1. **Wapper het (nieuwe) baken** even vlak bij een willekeurige paal. Het verschijnt in de group als
   **"Nieuw baken: baken xx:xx:…"** (de nieuwste MAC die nog niet gekoppeld is).
2. Kies de **speler** in de dropdown.
3. Druk **Koppel baken → speler**. Koppel je aan een speler die al een baken had, dan vervangt dit dat
   automatisch (elke speler houdt precies één baken). **Ontkoppel speler** maakt een baken weer vrij.

De wijziging is **meteen** actief (`Locatiebepaling Spelers` leest `spelersLijst` per bericht) en wordt
**retained** bewaard op `config/spelers` → ze overleeft een herstart én een `deploy-flows`.

> **Een baken her-toewijzen dat al aan iemand anders hangt:** ontkoppel eerst die speler (dan is het baken
> weer "nieuw"), koppel het daarna aan de nieuwe. (Zo grijp je nooit per ongeluk een baken dat middenin
> het spel actief is.)

> **Bron van waarheid.** Zodra je het dashboard gebruikt, is de retained `config/spelers` leidend en
> **overschrijft** ze na een deploy de flows.json-seed `[CONFIG] Spelerslijst` (die enkel nog een
> **bootstrap**-default is). Wil je terug naar de seed-lijst: wis het retained topic
> (`mosquitto_pub -h 192.168.1.43 -r -n -t config/spelers`) en deploy opnieuw.

## BLE-scan-duur (versere detectie, minder scoring-latentie)

De **settle-latentie** — de tijd tussen een fysieke paalwissel en een "settled" positie — is de som van
twee schakels:

1. **Slave-scancyclus** (~scan + backoff ≤150 ms + luistervenster 200 ms + 50 ms). De BLE-scan was vast
   1 s; hij is nu **runtime instelbaar** (zie onder).
2. **Locatiebepaling** (venster 5–7 s, grace 3–5 s, switch-samples 2–3). Dit domineert de settle-tijd.

Een kortere scan verhoogt de **sample-rate** (versere detectie) én verkleint schakel 1, waardoor je ook
de locatie-parameters (venster/switch) strakker kunt zetten. Complementair vangt de **settle-grace** in
de PoF-engine (controle op T+grace i.p.v. T) de resterende traagheid op — zie `docs/spel/event-systeem.md`.

**Instellen (dashboard-group "Scan-duur (BLE)" op deze pagina):**

| Regelaar | Effect |
|----------|--------|
| **Scan-duur — alle slaves (ms)** | zet dezelfde scan-vensterduur op **alle** palen (400–1000 ms). |
| **Paal (individueel)** + **Scan-duur — deze paal (ms)** + **Pas toe** | zet één paal apart. |

Onder water stuurt Node-RED `{"paal":N,"actie":20,"scan_ms":M}` → `MSG_SCAN_CONFIG` naar de slave. De slave
scant **niet-blokkerend**, begrensd door een `millis()`-venster, en **clamp't 300..2000 ms** (default 1000).
De waarde is **volatile** (weg bij een slave-reboot); Node-RED **herstelt** hem automatisch op de
eerstvolgende heartbeat (uptime-daling = reboot-detectie) en bewaart de stand retained op `config/scan-duur`.

**Afweging (belangrijk).** Bij adv-interval **300 ms** en de scan-duty (window 64 / interval 80 = ~80 %)
vangt een scan van 400 ms gemiddeld ~1,2 beacon, 600 ms ~1,8, 1000 ms ~3. Onder ~500 ms wordt het venster
kort t.o.v. het adv-interval → controleer in de **ruwe-RSSI-tabel** dat er nog genoeg samples (`n`) per
venster binnenkomen. **Aanbevolen startpunt: ~700 ms** (versere detectie zonder samples te verliezen);
ga alleen lager als de ruwe-RSSI-tabel dat toelaat.

## Live bijregelen (dashboard "Beacons & Locatie")

Open in het dashboard de pagina **Beacons & Locatie**, groep **Locatie-instellingen**.
De sliders passen `global.locParams` **direct** aan — geen redeploy nodig. Onderaan
toont een regel de actieve parameters.

| Parameter | Betekenis | Te hoog | Te laag | Aanbevolen |
|-----------|-----------|---------|---------|------------|
| **Venster (ms)** | hoe lang RSSI meetelt | traag reagerend | te weinig data, onrustig | 5000–7000 |
| **Hysterese (dB)** | hoeveel sterker een buurpaal moet zijn om te wisselen | "plakt" te lang | springt sneller | 5–8 |
| **RSSI-vloer (dBm)** | zwakker = genegeerd | mist verre detecties | ruis telt mee | −90…−95 |
| **Grace (ms)** | huidige paal vasthouden bij stilte | reageert traag op echt vertrek | springt bij gemiste adv | 3000–5000 |
| **Switch-samples** | aantal keer dat uitdager moet leiden | wisselt traag | wisselt makkelijk | 2–3 |
| **Min-samples** | minimum recente samples om kandidaat te zijn | mist snelle wissels | losse ruis-detectie telt | 2 |

## Ruwe RSSI bekijken & de vloer bepalen

Groep **Ruwe RSSI (diagnose)** op dezelfde pagina. Zet de schakelaar **aan** en de
tabel toont per beacon, per paal die hem ziet: **Laatste / Min / Max / n** (aantal
samples in de laatste 6 s). Per beacon staat de sterkste paal bovenaan.

Zo bepaal je de **RSSI-vloer**:
1. Leg één beacon stil op een bekende plek, vlak bij één paal.
2. Lees in de tabel de RSSI van die "echte" paal (sterk, bv. −55 dBm) en van de
   verre palen die hem ook oppikken (zwak, bv. −90 dBm).
3. Zet de vloer **in de kloof** ertussen — net onder de zwakste paal die je nog
   als "echt" wil meetellen, en boven het ruisniveau van verre palen. Vaak
   −88…−92 dBm.

Zet de schakelaar daarna weer **uit** (de tabel stopt dan met bijwerken).

## Elke parameter in detail

Het algoritme verwerkt elk bericht zo: *kalibratie → vloer → venster-samples →
mediaan per paal → sterkste paal (argmax) → beslissing (grace + sustained-switch)*.
Elke parameter grijpt op één van die stappen in:

- **RSSI-vloer (dBm)** — filtert vóór alles: samples zwakker dan de vloer worden
  weggegooid. *Te hoog* (bv. −80): je verliest legitieme, wat zwakkere detecties en
  de speler "valt weg". *Te laag* (bv. −98): ruis van verre palen wordt kandidaat en
  veroorzaakt sprongen. Bepaal hem met de ruwe-RSSI-tabel.
- **Venster (ms)** — hoe lang een sample meetelt voor de mediaan. *Te kort*: weinig
  samples → mediaan is onrustig, springerig. *Te lang*: reageert traag als de speler
  echt verplaatst. Vuistregel: 6–10× het adv-interval, zodat er altijd meerdere
  samples per paal in het venster zitten.
- **Min-samples** — hoeveel recente samples een paal minstens nodig heeft om
  überhaupt kandidaat te zijn. *1*: één losse (ruis-)detectie telt al mee. *2*:
  filtert toevalstreffers. Hoger mist snelle, echte wissels.
- **Mediaan** (vast, geen slider) — per paal wordt de mediaan-RSSI genomen i.p.v.
  het gemiddelde, zodat één uitschieter de schatting niet kaapt.
- **Hysterese (dB)** — een andere paal moet zóveel sterker zijn dan de huidige
  vóór wisselen overwogen wordt. Zet hem **iets boven de normale ruis-amplitude**
  (de σ uit de beacon-stabiliteitstabel). *Te laag*: wisselt bij ruis. *Te hoog*:
  blijft te lang aan de oude paal plakken.
- **Switch-samples** — de uitdager moet de hysterese-drempel zó vaak achter elkaar
  halen vóór er gewisseld wordt. Demp tegen kortstondige pieken. *2–3* is doorgaans
  goed; verhoog als het nog te snel wisselt.
- **Grace (ms)** — als de huidige paal even geen samples krijgt (gemiste
  advertentie), wordt hij zo lang vastgehouden. Zet hem **iets groter dan de grootste
  normale MaxGap** (beacon-stabiliteitstabel). *Te laag*: één gemiste advertentie =
  sprong. *Te hoog*: reageert traag als de speler echt weggaat.

## Werkwijze: de perfecte instellingen vinden

Doe dit in deze volgorde — elke stap leunt op de vorige:

1. **Data-check.** Volg "Eerst: komt er wel data binnen?" tot er rijen in de
   ruwe-RSSI-tabel verschijnen. Pas dan verder.
2. **Kalibreren.** Leg de beacons op gelijke afstand van één paal en geef offsets
   (groep Beacon-kalibratie) tot ze ongeveer dezelfde RSSI tonen.
3. **Vloer.** Bepaal de RSSI-vloer met de ruwe-RSSI-tabel (zie boven).
4. **Venster.** Lees de **Rate** in de beacon-stabiliteitstabel; kies venster ≈
   6–10× het adv-interval (bij 300 ms adv → ~2000–3000 ms; bij 700 ms → ~5000–7000 ms).
5. **Min-samples** = 2.
6. **Hysterese.** Ga met een beacon op de grens tussen twee palen staan en lees in de
   ruwe-RSSI-tabel het verschil + de schommeling (σ). Zet hysterese net boven die σ.
7. **Grace.** Zet iets boven de grootste normale **MaxGap** uit de stabiliteitstabel.
8. **Switch-samples** = 2; verhoog naar 3 als het nog te makkelijk wisselt.
9. **Testen.** Eén stilstaande speler (mag niet springen) én één wandelende speler
   (moet vlot maar zonder flikkeren volgen). Herhaal stap 6–8 tot beide goed zijn.

Verander **één parameter tegelijk** en kijk telkens in de radar-tabel (tab
Locatiebepaling) wat het effect is.

## Beacon-stabiliteit (groep "Beacon-stabiliteit")

De tabel toont per beacon (laagste score bovenaan):

- **Rate** — detecties per seconde. Laag = beacon zendt traag of wordt slecht gezien.
- **RSSI σ** — standaardafwijking van de RSSI. Hoog = onstabiel signaal.
- **MaxGap** — grootste gat tussen twee detecties (ms). Hoog = mist advertenties.
- **Palen** — door hoeveel palen tegelijk gezien (context).
- **Score** — 0–100, hoog = stabiel.
- **Advies** — concrete actie.

### Adviezen vertaald naar de beacon-app

Je kunt op elke beacon inloggen met de app en parameters aanpassen. Huidige stand:
adv-interval **700 ms**, tx-power **+2,5 dBm (max)**.

- **"adv-interval verlagen (300 ms)"** — bij lage Rate/grote MaxGap. Een korter
  interval = meer advertenties per seconde = meer samples = stabielere argmax.
  Nadeel: iets meer batterijverbruik. Aanbevolen **300–400 ms**.
- **"mist advertenties – batterij/positie"** — controleer de batterij van de beacon
  en of hij niet afgeschermd zit (lichaam, metaal). 
- **"plaatsing/antenne/tx-power"** — hoge σ duidt op multipath/reflecties. Soms helpt
  een **lagere** tx-power tegen overspraak tussen buurpalen (de speler wordt dan door
  minder palen tegelijk sterk gezien). Test +0 dBm vs +2,5 dBm.

## Beacon-kalibratie (groep "Beacon-kalibratie")

Sommige beacons zenden structureel sterker of zwakker dan andere. Dat verschuift hun
RSSI consequent, wat de locatiebepaling misleidt. Met een **offset** corrigeer je dat:

1. Leg alle beacons op gelijke afstand van eenzelfde paal.
2. Kijk in de stabiliteitstabel naar hun RSSI (via de radar) — een beacon die
   structureel ~6 dB zwakker meet, geef je offset **+6**.
3. Vul in het kalibratie-veld het getal in en verlaat het veld (Enter/Tab). De offset
   wordt direct toegepast in de locatiebepaling (`global.beaconKalibratie`).

Een goed gekalibreerde set beacons meet bij gelijke afstand vergelijkbare RSSI, wat
het wisselgedrag tussen palen eerlijk en stabiel maakt.

## Dag/avond-profielen (presets)

De RF-omgeving van 's middags is niet die van 's avonds: **dauw en nat gras dempen 2,4 GHz** (zwakkere
RSSI, grotere MaxGap), spelers trekken **jassen** aan (lichaamsdemping) en de **menigte-dichtheid**
verandert de multipath. De `locParams`-kalibratie van 14 u klopt om 21 u dus niet meer. Daarom zijn er
**twee opgeslagen profielen** ("dag"/"avond") die je met één knop wisselt.

**Bediening** (dashboardgroep **"Profielen (dag/avond)"** op Beacons & Locatie):
- **Laad dag** / **Laad avond** — zet `global.locParams` op het opgeslagen profiel; de 6 sliders volgen.
- **Bewaar → dag** / **Bewaar → avond** — slaat de **huidige** sliderstand op als dat profiel.
- **Profiel** (tekst) — toont het actieve profiel.

**Zo gebruik je het:**
1. Kalibreer overdag zoals gewoonlijk (venster/vloer/hysterese/grace/switch/min), druk **Bewaar → dag**.
2. **Plan een herkalibratie-moment bij schemering** in de dagplanning: lees de ruwe-RSSI-tabel opnieuw
   (signalen zijn zwakker, vloer vaak 2–4 dB lager, grace iets groter), stel de sliders bij en druk
   **Bewaar → avond**. Daarna wissel je met één knop.

De profielen (`global.locPresets`) zijn **persistent** (contextStorage, NR7) en overleven een restart.
Bij de allereerste start zijn beide profielen gelijk aan de huidige defaults — je tunet `avond` zelf.

> **Wisselwerking met overdrive.** De presets werken op de **basis**-tuning. Laad je een profiel terwijl
> een minigame (overdrive) actief is, dan vervangt het de basis (`locParamsNormaal`) maar blijven de
> overdrive-verkortingen (venster 2500 / grace 1500) live; **Bewaar** slaat nooit de overdrive-waarden
> op. Normaal wissel je profielen tússen spellen, niet tijdens een minigame.

## Overdrive-sensing (automatisch bij minigames)

De minigames **Klokslag** en **Infected** hebben snellere positie-updates nodig dan Plates of Fate
(waar events traag genoeg zijn voor een nauwkeurige, trage tuning). Daarom zet Node-RED (node
"Sla spelType op") automatisch een **overdrive-preset** zodra `spelType` op `klokslag`/`infected` staat:

| Knob | Normaal (PoF) | Overdrive (minigame) |
|------|---------------|----------------------|
| `locParams.vensterMs` | jouw tuning (bv. 6000) | **2500** |
| `locParams.graceMs` | jouw tuning (bv. 4000) | **1500** |
| BLE-scan (`scan_ms`, alle palen) | jouw tuning (bv. 700–1000) | **400** |
| `rssiVloer` / `hystereseDbm` | jouw tuning | **behouden** (kalibratie blijft) |

Kortere venster/grace + scan = **versere detectie**, in ruil voor iets meer ruis. Bij terugkeer naar
Plates of Fate worden je normale waarden hersteld uit de snapshots `locParamsNormaal` + `scanNormaalMs`.
Het is een **preset-swap** van bestaande knoppen (geen apart mechanisme) en heeft **geen effect in de
simulator** (die gebruikt exacte posities via `sim/locatie`, geen BLE). De G8-inname-ondergrens in
Klokslag (`min_inname_s`, zie `docs/spel/klokslag.md`) is complementair: overdrive verkort de latentie,
`min_inname_s` maakt lage uren robuust ongeacht de latentie.

## Globals (overzicht)

| Global | Inhoud |
|--------|--------|
| `locParams` | `{vensterMs, hystereseDbm, rssiVloer, graceMs, switchSamples, minSamples}` |
| `overdriveActief` | `true` tijdens een minigame (Klokslag/Infected) → overdrive-preset actief |
| `locParamsNormaal` / `scanNormaalMs` | snapshot van je normale tuning, hersteld bij terugkeer naar PoF |
| `locPresets` | `{ dag:{…6}, avond:{…6} }` — opgeslagen dag/avond-profielen (persistent) |
| `locProfiel` | `"dag"` / `"avond"` — laatst geladen profiel |
| `beaconKalibratie` | `{ "<mac>": offsetDb }` |
| `beaconBuf` | ruwe sample-buffer per MAC (intern; voedt stabiliteit + ruwe-RSSI-tabel) |
| `rssiDiagAan` | `true/false` — schakelaar voor de ruwe-RSSI diagnose-tabel |
| `palenActief` | actieve palen-ring voor uur-logica; = `paaltjesLijst`, of `1..24` als de simulator in sim-modus staat (`simVeld24`). In hardware-modus **heartbeat-gestuurd**: een paal die > 60 s stil is gaat tijdelijk uit de ring (invariant F4) |
| `simVeld24` | `true` wanneer de simulator een 24-uur veld forceert (sim-modus) |
| `spelerLocaties` | `{ spelerNaam: paalId }` — centrale waarheid |
| `spelerPruneMs` | drempel (ms, default **90000**) van de **ghost-prune**: een speler wiens beacon zo lang niet meer gezien is, wordt uit `spelerLocaties` verwijderd (invariant EV3) |

> De uur-logica (doelwit-keuze, verplaatsingscontrole, scoring) leest `palenActief`
> (met fallback naar `paaltjesLijst`). Zo test de simulator op 24 uren zonder de
> echte `paaltjesLijst` (gebouwde slaves) te wijzigen.

### Levenscyclus van `spelerLocaties` (ghost-prune, S1)

De locatiebepaling **schrijft** posities, maar verwijderde ze vroeger nooit — een beacon die
uitvalt (lege knoopcel, kwijt, speler naar huis) bleef dan eeuwig op zijn laatste paal staan en
spookte in Klokslag/Infected en de doel-telling. Daarom pruned "Evalueer spelstatus" (flow 02,
elke ~1 s, **alleen hardware-modus**) spelers wier beacon langer dan `global.spelerPruneMs`
(default 90 s) niet meer gezien is. Tijdens een nuke geldt het kortere `nukeEscapeMs` (ontsnappen).
Komt de beacon terug, dan zet de eerstvolgende detectie de speler gewoon weer neer. In sim-modus
is "Sim directe locatie" de enige schrijver en gebeurt er niets.
