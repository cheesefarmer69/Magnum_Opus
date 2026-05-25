# Plates of Fate — events opstellen

Dit document beschrijft het **standaardformaat** voor Plates-of-Fate events en
hoe je er stap voor stap een nieuw event mee opstelt. De engine die deze events
afspeelt staat in Node-RED flow 06 (zie
`pi/node-red/blokken/06_plates_of_fate/README.md`).

Events worden bewaard in `global.pofEvents`, gezet door de inject
`[CONFIG] POF events` in flow 06. Eén event is één object in die array.

## Het standaardformaat

```js
{
  id: "uniek_id",                     // VERPLICHT, uniek (kebab/lowercase)
  naam: "Mensvriendelijke naam",      // VERPLICHT, voor dashboard en log
  categorie: "speler",                // VERPLICHT: "speler" | "uur" | "wereld"
  tekst: "Wat voorgelezen wordt.",    // VERPLICHT, gaat naar audio/afspelen
  reactietijd_s: 15,                  // VERPLICHT, pauze (s) vóór het volgende event

  doelwit: {                          // VERPLICHT (mag type "geen" zijn)
    type: "speler",                   // "speler" | "uur" | "geen"
    selectie: "willekeurig",          // "rang" | "willekeurig" | "alle"
    veld: "levensuren",               // alleen bij selectie "rang"
    richting: "laagste",              // alleen bij rang: "hoogste" | "laagste"
    aantal: "laag"                    // OPTIE: "enkel"|"laag"|"midden"|"hoog" (of vast getal)
  },

  getal: "midden",                    // OPTIONEEL: rolt een getal en vult 'x' in tekst in
  voorwaarde: "min",                  // OPTIONEEL: "min" | "max" | "geen" (beweging-controle)

  gevolgen: [                         // VERPLICHT: array van één of meer gevolgen
    { type: "geen" }
  ]
}
```

### Veldreferentie

| Veld            | Verplicht | Betekenis                                                        |
|-----------------|-----------|------------------------------------------------------------------|
| `id`            | ja        | Unieke sleutel.                                                  |
| `naam`          | ja        | Weergavenaam (dashboard "Huidig event", debug).                 |
| `categorie`     | ja        | `speler` / `uur` / `wereld`. Bepaalt in welke `[CONFIG]`-inject het hoort.|
| `tekst`         | ja        | Tekst die wordt voorgelezen (gepubliceerd op `audio/afspelen`). |
| `reactietijd_s` | ja        | Seconden reactietijd ná het voorlezen, vóór de controle.        |
| `doelwit`       | ja        | Wie/wat geraakt wordt — zie hieronder.                          |
| `getal`         | nee       | Optie die een getal rolt en `x` in de tekst invult — zie "Getallen". |
| `voorwaarde`    | nee       | `min` / `max` / `geen` — beweging-controle na de reactietijd.   |
| `gevolgen`      | ja        | Eén of meer gevolgen — zie hieronder.                           |

### Opties: `enkel` / `laag` / `midden` / `hoog`

`doelwit.aantal` en `getal` worden opgegeven als optie; de engine rolt er een
getal uit binnen het bereik:

| Optie    | Bereik (willekeurig) |
|----------|----------------------|
| `enkel`  | 1                    |
| `laag`   | 1 – 3                |
| `midden` | 4 – 6                |
| `hoog`   | 7 – 10               |

`doelwit.aantal` bepaalt zo het aantal gekozen spelers/uren; `getal` bepaalt het
getal dat `x` in de tekst vervangt (zie hieronder).

### Getallen in de tekst (`x`)

Zet je `getal` op een optie, dan rolt de engine één getal en vervangt elke losse
`x` in `tekst` erdoor (eenmalig, net vóór dit event). Voorbeeld: `tekst:
"Minimum x uur vooruit."` met `getal:"midden"` wordt bv. "Minimum 5 uur vooruit."
Datzelfde getal is meteen de waarde waartegen de `voorwaarde` controleert.

### Beweging-controle (`voorwaarde`)

Na de reactietijd vergelijkt de engine elke speler zijn **netto vooruit**-verplaatsing
(start→eind paal, met de klok mee) met het gerolde `getal`:

| `voorwaarde` | Doelwit-speler slaagt als…           | Niet-doelwit slaagt als… |
|--------------|--------------------------------------|--------------------------|
| `min`        | netto ≥ getal (genoeg vooruit)       | niet bewogen (netto 0)   |
| `max`        | netto ≤ getal (niet te ver)          | niet bewogen (netto 0)   |
| `geen`       | geen controle                        | geen controle            |

Het resultaat (✅/❌ per speler) verschijnt in de tabel **Controle** op de
Bediening-pagina. (Voorlopig enkel tonen — straffen volgen later.)

### `doelwit` — wie/wat wordt geraakt

| `type`    | `selectie`     | `veld` (bij rang)            | `richting` | `aantal` |
|-----------|----------------|------------------------------|------------|----------|
| `speler`  | `rang`         | `levensuren` of `achterstand`| hoogste/laagste | N   |
| `speler`  | `willekeurig`  | —                            | —          | N        |
| `speler`  | `alle`         | —                            | —          | —        |
| `uur`     | `rang`         | `nummer` of `bezetting`      | hoogste/laagste | N   |
| `uur`     | `willekeurig`  | —                            | —          | N        |
| `uur`     | `alle`         | —                            | —          | —        |
| `geen`    | —              | —                            | —          | —        |

- **`rang`** sorteert en pakt de top `aantal`. Voorbeelden:
  - "speler met de minste levensuren" → `{type:"speler", selectie:"rang", veld:"levensuren", richting:"laagste", aantal:1}`
  - "de 2 hoogste uren" → `{type:"uur", selectie:"rang", veld:"nummer", richting:"hoogste", aantal:2}`
  - "de 2 drukste uren" → `{type:"uur", selectie:"rang", veld:"bezetting", richting:"hoogste", aantal:2}`
- **`bezetting`** = aantal spelers dat momenteel op dat uur staat (uit `global.spelerLocaties`).
- **`geen`** wordt gebruikt voor wereld-events die niemand specifiek raken.

### `gevolgen` — wat er gebeurt (één of meer)

Elk gevolg is één object in de array. Je mag er meerdere combineren.

| `type`      | Velden                                  | Effect                                                       |
|-------------|-----------------------------------------|--------------------------------------------------------------|
| `commando`  | `actie` (0–4)                           | Stuurt `{paal, actie}` naar elk doel-uur via `commando/master1` (LED/buzzer). Bij speler-doelwit: de paal waar die speler staat. |
| `score`     | `delta` (geheel, +/-)                   | Past direct de levensuren van de doel-spelers aan (clamp ≥ 0).|
| `effect`    | `niveau`, `effect`, `duurRondes`, `data`| Plaatst een **blijvend effect** (zie hieronder).             |
| `geen`      | —                                       | Geen neveneffect. Gebruik dit voor pure beweging-opdrachten (met `voorwaarde`). |

`actie`-waarden (commando): `0` uit · `1` rood · `2` groen · `3` buzzer aan ·
`4` buzzer uit (zie `docs/protocol.md`).

### Blijvende effecten (`type: "effect"`)

Een blijvend effect blijft `duurRondes` rondes actief (één ronde = één event) en
loopt daarna vanzelf af. Het wordt opgeslagen in een van drie registers:

| `niveau`  | Register            | Voorbeeld-`effect`                      | Afgedwongen?                          |
|-----------|---------------------|-----------------------------------------|----------------------------------------|
| `speler`  | `spelerEffecten`    | `mag_niet_bewegen`                      | ✅ puntensysteem negeert beweging      |
| `wereld`  | `wereldEffecten`    | `events_sneller`                        | ✅ engine halveert de reactietijd      |
| `uur`     | `bordStaat[uur]`    | `gevaarlijk`                            | ⛔ alleen opgeslagen + getoond (nog)   |

> Nieuwe effect-namen kun je vrij verzinnen; ze verschijnen meteen in de
> "Actieve effecten"-tabel. Wil je dat een nieuw effect ook iets *doet*, voeg
> dan een afdwinging toe op de juiste plek (zie flow 06 README → afdwinging).

## Stap voor stap een nieuw event

1. **Kies de categorie** (`speler` / `uur` / `wereld`) — dit bepaalt in welke
   `[CONFIG] <categorie>-events`-inject je het event zet.
2. **Schrijf `naam` en `tekst`** — de tekst is wat de spelers horen.
3. **Bepaal het doelwit** — wie of wat raakt het? Kies `type` en `selectie`
   (rang voor "de hoogste/laagste", willekeurig voor toeval, alle voor iedereen).
4. **Koppel één of meer gevolgen** — commando (LED/buzzer), score (levensuren),
   en/of effect (blijvend).
5. **Zet `reactietijd_s`** — hoeveel reactietijd krijgen de spelers?
6. **Voeg het object toe** aan de juiste `[CONFIG] <categorie>-events`-inject
   (flow 06) en deploy.

## De ronde-cyclus (engine)

Per event doorloopt de engine (zie flow 06) deze fasen, zichtbaar op de
Bediening-pagina:

1. **Aanloop** — 5 s countdown ("Volgend event over: Ns"). In manueel-modus
   wacht de engine in plaats daarvan op de knop **Volgende event**.
2. **Event gekozen** — als het event een `getal` heeft, wordt dat nu gerold en
   `x` in de tekst ingevuld (eenmalig).
3. **Voorlezen** — de (ingevulde) tekst gaat naar `audio/afspelen` en de
   getroffen spelers/uren worden bepaald en voorgelezen.
4. **Reactietijd** — `reactietijd_s` countdown waarin spelers bewegen.
5. **Controle** — als `voorwaarde` gezet is, wordt elke speler gecontroleerd
   (zie hierboven) en het resultaat getoond in de tabel **Controle**.

## Voorbeelden

### Beweging-event (min/max, getal, controle)

```js
{ id:"verplaatsing1", naam:"verplaatsingMin", categorie:"speler",
  tekst:"Minimum x uur vooruit.", reactietijd_s:15,
  doelwit:{ type:"speler", selectie:"willekeurig", aantal:"laag" },
  getal:"midden", voorwaarde:"min", gevolgen:[{ type:"geen" }] }
```
De engine kiest 1–3 spelers (`aantal:"laag"`), rolt een getal 4–6 (`getal:"midden"`)
in plaats van `x`, en controleert na 15 s of die spelers minstens dat aantal palen
vooruit zijn. Niet-gekozen spelers moeten stil blijven.

### Speler-event (rang + score)

```js
{
  id: "kosmische_gift", naam: "Kosmische gift", categorie: "speler",
  tekst: "Een gulle ster schenkt de speler met de minste levensuren een gift.",
  reactietijd_s: 15,
  doelwit: { type: "speler", selectie: "rang", veld: "levensuren", richting: "laagste", aantal: "enkel" },
  gevolgen: [ { type: "score", delta: 3 } ]
}
```

### Uur-event (rang + commando)

```js
{
  id: "zonnewende", naam: "Zonnewende", categorie: "uur",
  tekst: "De zon staat hoog. De twee hoogste uren lichten groen op.",
  reactietijd_s: 15,
  doelwit: { type: "uur", selectie: "rang", veld: "nummer", richting: "hoogste", aantal: 2 },
  gevolgen: [ { type: "commando", actie: 2 } ]
}
```

### Wereld-event (blijvend effect, meerdere gevolgen)

```js
{
  id: "tijdslot", naam: "Tijdslot", categorie: "wereld",
  tekst: "De tijd verstrakt: events komen sneller en de koploper staat stil.",
  reactietijd_s: 20,
  doelwit: { type: "geen", selectie: "alle", aantal: 0 },
  gevolgen: [
    { type: "effect", niveau: "wereld", effect: "events_sneller", duurRondes: 3, data: {} }
  ]
}
```

> Combineer gerust: een event kan bv. tegelijk een `commando` (LED rood) én een
> `effect` (`gevaarlijk` op datzelfde uur) hebben.
