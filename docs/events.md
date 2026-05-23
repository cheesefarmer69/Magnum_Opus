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
    selectie: "rang",                 // "rang" | "willekeurig" | "alle"
    veld: "levensuren",               // alleen bij selectie "rang"
    richting: "laagste",              // alleen bij rang: "hoogste" | "laagste"
    aantal: 1                         // aantal doelwitten (rang/willekeurig)
  },

  gevolgen: [                         // VERPLICHT: array van één of meer gevolgen
    { type: "score", delta: 3 }
  ]
}
```

### Veldreferentie

| Veld            | Verplicht | Betekenis                                                        |
|-----------------|-----------|------------------------------------------------------------------|
| `id`            | ja        | Unieke sleutel.                                                  |
| `naam`          | ja        | Weergavenaam (dashboard "Huidig event", debug).                 |
| `categorie`     | ja        | `speler` / `uur` / `wereld`. Stuurt ook de test-injects (Deel D).|
| `tekst`         | ja        | Tekst die wordt voorgelezen (gepubliceerd op `audio/afspelen`). |
| `reactietijd_s` | ja        | Seconden pauze ná dit event vóór het volgende.                  |
| `doelwit`       | ja        | Wie/wat geraakt wordt — zie hieronder.                          |
| `gevolgen`      | ja        | Eén of meer gevolgen — zie hieronder.                           |

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

1. **Kies de categorie** (`speler` / `uur` / `wereld`) — dit bepaalt ook met
   welke test-inject je het straks forceert.
2. **Schrijf `naam` en `tekst`** — de tekst is wat de spelers horen.
3. **Bepaal het doelwit** — wie of wat raakt het? Kies `type` en `selectie`
   (rang voor "de hoogste/laagste", willekeurig voor toeval, alle voor iedereen).
4. **Koppel één of meer gevolgen** — commando (LED/buzzer), score (levensuren),
   en/of effect (blijvend).
5. **Zet `reactietijd_s`** — hoeveel rust krijgen de spelers erna?
6. **Voeg het object toe** aan de array in `[CONFIG] POF events` (flow 06) en
   deploy. Test met de bijbehorende `[TEST] <categorie>-event`-inject.

## Voorbeelden

### Speler-event (rang + score)

```js
{
  id: "kosmische_gift", naam: "Kosmische gift", categorie: "speler",
  tekst: "Een gulle ster schenkt de speler met de minste levensuren een gift.",
  reactietijd_s: 15,
  doelwit: { type: "speler", selectie: "rang", veld: "levensuren", richting: "laagste", aantal: 1 },
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
