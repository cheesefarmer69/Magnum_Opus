# Locatiebepaling & beacon-stabiliteit

Dit document beschrijft hoe het systeem bepaalt bij welke paal een speler staat,
hoe je dat live bijregelt, en hoe je instabiele beacons opspoort en kalibreert.

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

**Werkwijze tunen:** verander één slider, observeer in de radar-tabel of de speler
stabiel blijft, en stel bij. Begin bij de aanbevolen waarden.

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

## Globals (overzicht)

| Global | Inhoud |
|--------|--------|
| `locParams` | `{vensterMs, hystereseDbm, rssiVloer, graceMs, switchSamples, minSamples}` |
| `beaconKalibratie` | `{ "<mac>": offsetDb }` |
| `beaconBuf` | ruwe sample-buffer per MAC (intern, voor de stabiliteitsanalyse) |
| `beaconStats` | (gereserveerd) |
| `spelerLocaties` | `{ spelerNaam: paalId }` — centrale waarheid |
