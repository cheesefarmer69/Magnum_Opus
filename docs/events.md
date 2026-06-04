# Plates of Fate — events opstellen

> **Per-categorie overzicht** van alle (huidige en toekomstige) events — werking,
> doelwitbepaling en controle — staat in `docs/event-catalogus.md`
> (hoofdstukken Verplaatsing / Toestand / Wereld). Dit document is de technische
> **schema-referentie** voor het opstellen van een event.

Events worden bewaard in `global.pofEvents`, gevuld door de `[CONFIG] <categorie>-events`
injects in Node-RED flow 06 (zie `pi/node-red/blokken/06_plates_of_fate/README.md`).
Eén event is één object in die array.

## Het standaardformaat

```js
{
  id: "uniek_id",            // VERPLICHT — unieke sleutel (kebab/lowercase)
  naam: "Mooie naam",        // VERPLICHT — weergavenaam voor dashboard en log
  categorie: "toestand",     // VERPLICHT — "speler" | "toestand" | "wereld"
  tekst: "Wat klinkt.",      // VERPLICHT — voorgelezen tekst (naar audio/afspelen)
  reactietijd_s: 15,         // VERPLICHT — seconden reactietijd vóór de controle

  doelwit: {                 // VERPLICHT — wie/wat het event raakt (mag type "geen")
    type: "uur",             //   "speler" (spelers) | "uur" (palen) | "geen" (niemand)
    selectie: "willekeurig", //   "willekeurig" (toeval) | "alle" | "rang" (zie Geavanceerd)
    aantal: "laag"           //   "enkel"|"laag"|"midden"|"hoog" of een vast getal
  },

  getal: "midden",           // OPTIONEEL — rolt een getal en vult elke 'x' in de tekst in
  voorwaarde: "min",         // OPTIONEEL — "min" | "max" | "geen" (beweging-controle)
  max: 1,                    // OPTIONEEL — max. aantal tegelijk actieve instanties (toestand)
  audioVoor: "id_voor.wav",  // OPTIONEEL — WAV vóór het getal in de afroep
  audioNa: "id_na.wav",      // OPTIONEEL — WAV ná het getal in de afroep

  gevolgen: [                // VERPLICHT — array van één of meer gevolgen
    { type: "effect", niveau: "uur", effect: "portaal", duurRondes: "kort", data: {} }
  ]
}
```

### Veldreferentie

| Veld            | Verplicht | Wat het is / wat je invult |
|-----------------|-----------|-----------------------------|
| `id`            | ja        | Unieke sleutel (kebab/lowercase). |
| `naam`          | ja        | Weergavenaam (dashboard "Huidig event", debug, log). |
| `categorie`     | ja        | `speler` / `toestand` / `wereld` — bepaalt in welke `[CONFIG]`-inject het hoort. |
| `tekst`         | ja        | Wat wordt voorgelezen. Een losse `x` wordt door `getal` vervangen. |
| `reactietijd_s` | ja        | Seconden reactietijd ná het voorlezen, vóór de controle. |
| `doelwit`       | ja        | Wie/wat geraakt wordt — object met `type`, `selectie`, `aantal`. |
| `getal`         | nee       | Optie/getal dat `x` in de tekst invult én de controle-waarde bepaalt. |
| `voorwaarde`    | nee       | `min` / `max` / `geen` — beweging-controle na de reactietijd. |
| `max`           | nee       | Hoeveel instanties van dít event tegelijk actief mogen zijn (toestand). |
| `audioVoor` / `audioNa` | nee | WAV-bestandsnamen voor de afroep (knip-en-plak rond het getal). |
| `gevolgen`      | ja        | Eén of meer gevolgen (`commando` / `score` / `effect` / `geen`). |

---

## Uitleg per veld

### `categorie`

Bepaalt het soort event én in welke config-inject het hoort:

- **`speler`** — een verplaatsing-event: gekozen spelers moeten binnen de reactietijd aan
  een beweging-`voorwaarde` voldoen. Meestal `gevolgen: [{type:"geen"}]`.
- **`toestand`** — kent iets toe aan een speler of uur (een blijvend `effect`, een `score`,
  of een `commando`). Bv. portaal of happy hour.
- **`wereld`** — verandert iets voor het hele spel (meestal een wereld-`effect`).

### `doelwit` — wie/wat wordt geraakt

| `type`   | Betekenis | `selectie` | `aantal` |
|----------|-----------|------------|----------|
| `speler` | kiest spelers | `willekeurig` / `alle` / `rang` | aantal spelers |
| `uur`    | kiest palen/uren | `willekeurig` / `alle` / `rang` | aantal uren |
| `geen`   | raakt niemand specifiek (wereld-events) | — | — |

- **`willekeurig`** — een steekproef van `aantal` spelers/uren (zonder terugleggen).
- **`alle`** — alle actieve spelers / alle actieve palen.
- **`rang`** — gesorteerd op een veld; zie **Geavanceerd: rang-selectie**.
- **Actieve spelers** = enkel spelers met een bekende positie (`spelerLocaties`) en niet
  gepauzeerd. **Actieve palen** = `palenActief` (in simulatiemodus 1..24, anders `paaltjesLijst`).

### Opties `enkel` / `laag` / `midden` / `hoog`

`doelwit.aantal` en `getal` mogen een optie zijn; de engine rolt er een getal uit:

| Optie    | Bereik |
|----------|--------|
| `enkel`  | 1      |
| `laag`   | 1 – 3  |
| `midden` | 4 – 6  |
| `hoog`   | 7 – 10 |

`doelwit.aantal` bepaalt zo het aantal gekozen spelers/uren; `getal` bepaalt het getal dat
`x` in de tekst vervangt.

### Getallen in de tekst (`getal` → `x`)

Zet je `getal`, dan rolt de engine één getal en vervangt elke losse `x` in `tekst` erdoor
(eenmalig, net vóór dit event). Voorbeeld: `tekst: "Minimum x uur vooruit."` met
`getal:"midden"` wordt bv. "Minimum 5 uur vooruit." Datzelfde getal is meteen de waarde
waartegen de `voorwaarde` controleert.

### Beweging-controle (`voorwaarde`) en scoring

Levensuren worden **pas bij de controle** toegekend (niet live), op basis van de **netto
vooruit**-verplaatsing (start→eind, portaal-sprongen meegerekend) t.o.v. het gerolde
`getal`. Bij elk event mag enkel het doelwit van een verplaatsing-event (`voorwaarde`
min/max) bewegen; anderen moeten stil blijven. Δ = de levensuren die worden toegekend
(of afgetrokken):

| Geval | Status | Δ levensuren |
|-------|--------|--------------|
| doelwit `min`, netto ≥ getal | OK | **+netto** (×2 op happy hour) |
| doelwit `min`, 0 ≤ netto < getal | TE WEINIG | **−netto** |
| doelwit `max`, netto ≤ getal | OK | **+netto** (×2 op happy hour) |
| doelwit `max`, netto > getal | TE VEEL | **−(netto − getal)** |
| doelwit, netto < 0 (mag niet terug) | TERUG IN TIJD | **−\|netto\|** |
| niet-doelwit dat beweegt | BEWOOG (mocht niet) | **−\|netto\|** |
| stil blijven staan | OK (stil) | 0 |

Zou Δ een speler **onder 0** brengen, dan blijft hij op 0 met **+1 sterfte** (hij speelt
door). Een legale portaal-sprong (ook van een hoger naar een lager uur) telt als vooruit
en geeft dus geen "TERUG IN TIJD". Het resultaat per speler verschijnt in de tabel
**Controle** en in de globale stats.

### Max-engine (`max`)

Toestand-events met een blijvend effect kunnen zich opstapelen op het veld. Zet `max: N`
om hooguit `N` gelijktijdig actieve instanties van dít event toe te laten. De engine telt
vóór elke keuze de actieve instanties (distinct `instId` met `bron === id`) over alle
effect-registers en slaat het event over zolang `max` bereikt is. Loopt een effect af
(`resterendeRondes ≤ 0`), dan komt er vanzelf weer ruimte.

### `gevolgen` — wat er gebeurt (één of meer)

Elk gevolg is één object in de array; combineren mag.

| `type`     | Velden                                    | Effect |
|------------|-------------------------------------------|--------|
| `commando` | `actie` (0–3)                             | Stuurt `{paal, actie}` naar elk doel-uur via `commando/master1`. Bij speler-doelwit: de paal waar die speler staat. |
| `score`    | `delta` (geheel, +/-)                     | Past direct de levensuren van de doel-spelers aan (clamp ≥ 0). |
| `effect`   | `niveau`, `effect`, `duurRondes`, `data`  | Plaatst een **blijvend effect** (zie hieronder). |
| `geen`     | —                                         | Geen neveneffect. Gebruik dit voor pure beweging-opdrachten (met `voorwaarde`). |

`actie`-waarden (commando): `0` uit · `1` portaal (paars) · `2` happy hour (goud) ·
`3` buzzer-piep (zie `docs/protocol.md`).

> **LED-toestanden hoef je normaal niet met een `commando` te zetten.** De centrale node
> "Sync toestanden + LEDs" leidt de LED-kleur rechtstreeks af uit het actieve `effect`
> (`portaal` → paars, `happy_hour` → goud) en zet de LED ook weer uit als het effect
> afloopt. Een `commando`-gevolg is dus enkel voor losse, eenmalige LED/zoemer-acties.

### Blijvende effecten (`type: "effect"`)

Een blijvend effect blijft `duurRondes` rondes actief (één ronde = één event) en loopt
daarna vanzelf af. `duurRondes` mag een vast getal zijn óf een optie-string die de engine
rolt: `kort` (2–4) · `middel` (4–7) · `lang` (7–12). Opslag in één van drie registers:

| `niveau` | Register          | Voorbeeld-`effect`        | Afgedwongen? |
|----------|-------------------|---------------------------|--------------|
| `speler` | `spelerEffecten`  | `mag_niet_bewegen`        | ✅ puntensysteem negeert beweging |
| `wereld` | `wereldEffecten`  | `events_sneller`          | ✅ engine halveert de reactietijd |
| `uur`    | `bordStaat[uur]`  | `portaal` · `happy_hour`  | ✅ scoring (portaal-sprong = 0, happy hour ×2) |

> Elk effect krijgt automatisch een `bron` (het event-id) en een `instId` (één per
> afvuring). Eén event dat meerdere palen/spelers tegelijk raakt, telt zo als **één**
> instantie voor de `max`-engine. Bij een uur-effect op precies 2 palen krijgt elk effect
> een `data.partner` (het andere uur) — dat koppelt bv. de twee portaal-uren.

### Aantal-prefix in de afroep

Raakt een event spelers of uren, dan roept de Pi vóór de event-tekst eerst het **aantal**
doelwitten af + het zelfstandig naamwoord (enkel/meervoud): "3 spelers …", "1 speler …",
"2 uren …". Hiervoor komen `getallen/<n>.wav` en `woorden/<speler|spelers|uur|uren>.wav`
vooraan de audio-segmenten (zie `pi/audio-player/audio/README.md`). De doelwitten zelf
worden daarna één voor één opgesomd.

### Geavanceerd: rang-selectie (`veld` + `richting`)

Wil je niet willekeurig kiezen maar "de hoogste/laagste", gebruik dan `selectie: "rang"`
en voeg twee extra velden toe aan het `doelwit`-object:

```js
doelwit: { type: "speler", selectie: "rang", veld: "levensuren", richting: "laagste", aantal: 1 }
```

| veld | bij `type` | Betekenis |
|------|-----------|-----------|
| `levensuren`  | speler | sorteer op `totaalUren` |
| `nummer`      | uur    | sorteer op uur-nummer (1..24) |
| `bezetting`   | uur    | sorteer op aantal spelers op dat uur (`spelerLocaties`) |

`richting` is `hoogste` of `laagste`; de engine sorteert en neemt de eerste `aantal`.
Voorbeelden: "speler met de minste levensuren" (`veld:levensuren, richting:laagste`),
"de 2 drukste uren" (`type:uur, veld:bezetting, richting:hoogste, aantal:2`).

---

## Stap voor stap een nieuw event

1. **Kies de categorie** (`speler` / `toestand` / `wereld`) — bepaalt de config-inject.
2. **Schrijf `naam` en `tekst`** — de tekst is wat de spelers horen.
3. **Bepaal het doelwit** — `type` + `selectie` (`willekeurig`/`alle`, of `rang`) + `aantal`.
4. **Koppel één of meer gevolgen** — `effect` (blijvend), `score` (levensuren), of
   `commando` (losse LED/zoemer).
5. **Zet `reactietijd_s`** (en evt. `getal` / `voorwaarde` / `max`).
6. **Voeg het object toe** aan de juiste `[CONFIG] <categorie>-events`-inject en deploy
   (`pi/node-red/deploy-flows.ps1`).

## De ronde-cyclus (engine)

1. **Aanloop** — 5 s countdown. In manueel-modus: wacht op **Volgende event**.
2. **Event gekozen** — `getal` gerold (`x` ingevuld), doelwit gekozen, `max` gecheckt.
3. **Voorlezen** — aantal-prefix + tekst naar `audio/afspelen`; doelwitten één voor één.
4. **Reactietijd** — `reactietijd_s` countdown waarin spelers bewegen.
5. **Controle** — als `voorwaarde` gezet is, wordt elke speler gecontroleerd.

## Voorbeelden

### Verplaatsing-event (speler, min/max + getal + controle)

```js
{ id:"verplaatsing1", naam:"verplaatsingMin", categorie:"speler",
  tekst:"Minimum x uur vooruit.", reactietijd_s:15,
  doelwit:{ type:"speler", selectie:"willekeurig", aantal:"laag" },
  getal:"midden", voorwaarde:"min", gevolgen:[{ type:"geen" }] }
```
Kiest 1–3 spelers, rolt een getal 4–6 in plaats van `x`, en controleert na 15 s of die
spelers minstens dat aantal palen vooruit zijn. Niet-gekozen spelers moeten stil blijven.

### Toestand-event (portaal: blijvend effect, max, paar-koppeling)

```js
{ id:"portalen", naam:"Portalen", categorie:"toestand",
  tekst:"Een portaal opent tussen twee uren.", reactietijd_s:15, max:1,
  doelwit:{ type:"uur", selectie:"willekeurig", aantal:2 },
  audioVoor:"portalen_voor.wav", audioNa:"portalen_na.wav",
  gevolgen:[ { type:"effect", niveau:"uur", effect:"portaal", duurRondes:"kort", data:{} } ] }
```
Kiest 2 willekeurige uren; elk krijgt een `portaal`-effect met `data.partner` = het andere
uur. De LED's worden paars via de centrale LED-node (geen `commando` nodig). `max: 1` houdt
het bij één portaal tegelijk. Een sprong tussen de twee portaal-uren geeft **0 levensuren**
(zie `docs/spel.md`).

### Toestand-event (happy hour: ×2-scoring)

```js
{ id:"happy_hour", naam:"Happy Hour", categorie:"toestand",
  tekst:"worden Happy Hour.", reactietijd_s:15, max:4,
  doelwit:{ type:"uur", selectie:"willekeurig", aantal:"laag" },
  audioVoor:"happy_hour_voor.wav", audioNa:"happy_hour_na.wav",
  gevolgen:[ { type:"effect", niveau:"uur", effect:"happy_hour", duurRondes:"kort", data:{} } ] }
```
Kiest 1–3 willekeurige uren (afroep: "3 uren worden Happy Hour"). Die uren worden goud.
Eindigt een speler een verplaatsing op een happy-hour-uur, dan tellen de verdiende
levensuren dubbel (zie `docs/spel.md`). `max: 4` laat tot 4 happy-hour-uren tegelijk toe.

### Toestand-event (rang + score)

```js
{ id:"kosmische_gift", naam:"Kosmische gift", categorie:"toestand",
  tekst:"Een gulle ster schenkt de speler met de minste levensuren een gift.",
  reactietijd_s:15,
  doelwit:{ type:"speler", selectie:"rang", veld:"levensuren", richting:"laagste", aantal:"enkel" },
  gevolgen:[ { type:"score", delta:3 } ] }
```

### Wereld-event (blijvend effect)

```js
{ id:"tijdslot", naam:"Tijdslot", categorie:"wereld",
  tekst:"De tijd verstrakt: events komen sneller.", reactietijd_s:20,
  doelwit:{ type:"geen" },
  gevolgen:[ { type:"effect", niveau:"wereld", effect:"events_sneller", duurRondes:3, data:{} } ] }
```
