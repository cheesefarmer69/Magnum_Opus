# Plates-of-Fate — event-catalogus

Dit document beschrijft **per categorie** elk event van het spel: hoe het in elkaar
zit, wie of wat de doelwitten zijn (en hoe die bepaald worden), en hoe het event na
afloop gecontroleerd wordt. De drie categorieën zijn:

1. **Verplaatsing** — over de spelers die mogen/moeten bewegen.
2. **Toestand** — wat er precies aan een speler of uur wordt toegekend.
3. **Wereld** — wat er in het hele spel gezamenlijk verandert.

> Voor het veld-voor-veld JSON-schema van een event-object: zie `docs/events.md`.
> Voor de spelregels rond levensuren/sterftes: zie `docs/spel.md`.

## Begrippen (gelden voor alle categorieën)

- **Doelwit**: wie/wat het event raakt. Bepaald door het `doelwit`-object:
  - `selectie`: `willekeurig` (steekproef), `rang` (gesorteerd op een veld), of `alle`.
  - `veld` (bij `rang`): spelers → `levensuren`; uren → `nummer`/`bezetting`.
  - `richting` (bij `rang`): `hoogste` of `laagste`.
  - `aantal`: vast getal, of optie `enkel`=1, `laag`=1–3, `midden`=1–6, `hoog`=3–10.
- **Actieve spelers**: alleen spelers met een bekende positie (`spelerLocaties`) en
  niet gepauzeerd komen in aanmerking als doelwit. Verwijderde of niet-aanwezige
  spelers worden nooit gekozen.
- **Reactietijd** (`reactietijd_s`): tijd waarin spelers mogen reageren vóór de
  controle. Wereld-effect `events_sneller` halveert deze.
- **Afroep**: raakt een event spelers of uren, dan wordt vóór de event-tekst eerst het
  **aantal** doelwitten + het zelfstandig naamwoord afgeroepen (bv. "3 spelers …",
  "1 speler …", "2 uren …"); daarna de event-tekst en ten slotte de doelwitten één voor
  één. Zie `pi/audio-player/audio/README.md`.
- **Max** (`max`): begrenst hoeveel instanties van hetzelfde toestand-event tegelijk
  op het veld mogen staan (zie hoofdstuk 2).
- **Netto-verplaatsing**: het verschil tussen begin- en eindpaal op de 24-uur ring,
  als kortste **signed** afstand: positief = vooruit, negatief = achteruit.
  De beginposities worden vastgelegd op het moment dat het event valt.

---

# Hoofdstuk 1 — Verplaatsing

## Hoe een verplaatsing-event in elkaar zit
Een verplaatsing-event kiest doelwit-**spelers** die binnen de reactietijd aan een
beweging-**voorwaarde** moeten voldoen. Spelers die géén doelwit zijn, moeten stil
blijven staan. Het event heeft meestal `gevolgen: [{type:"geen"}]` — de "straf/beloning"
zit in het puntensysteem (levensuren, toegekend bij de controle), niet in een LED-commando.

| Veld | Rol |
|------|-----|
| `categorie` | `"speler"` |
| `voorwaarde` | `"min"` (minstens x vooruit) of `"max"` (hoogstens x vooruit) |
| `getal` | rolt `x` (bv. `midden` → 1–6) en vult het in de tekst in |
| `doelwit` | type `speler` — wie moet bewegen |

## Huidige events

### verplaatsingMin — "Minimum x uur vooruit."
- **Werking**: de gekozen spelers moeten minstens `x` uur **vooruit** op de klok.
- **Doelwit**: `type: speler`, `selectie: willekeurig`, `aantal: laag` (1–3 actieve
  spelers). `x` = `getal: midden` (1–6).
- **Controle** (na reactietijd, per doelwit-speler) — levensuren-Δ:
  - `netto ≥ x` → **OK**, +netto (×2 op happy hour)
  - `0 ≤ netto < x` → **TE WEINIG**, −netto
  - `netto < 0` → **TERUG IN TIJD**, −|netto|
  - niet-doelwit dat toch beweegt → **BEWOOG (mocht niet)**, −|netto|

### verplaatsingMax — "Maximum x uur."
- **Werking**: de gekozen spelers mogen hoogstens `x` uur vooruit.
- **Doelwit**: identiek aan verplaatsingMin.
- **Controle** — levensuren-Δ:
  - `0 ≤ netto ≤ x` → **OK**, +netto (×2 op happy hour)
  - `netto > x` → **TE VEEL**, −(netto − x)
  - `netto < 0` → **TERUG IN TIJD**, −|netto|
  - niet-doelwit dat beweegt → **BEWOOG (mocht niet)**, −|netto|

## Hoe het doelwit bepaald wordt
De kandidaten zijn de **actieve, niet-gepauzeerde** spelers. Daarna:
- `willekeurig` → `aantal` spelers via steekproef (zonder terugleggen).
- `rang` → sorteer op `veld` (`levensuren`) in `richting` (`hoogste`/`laagste`), neem de
  eerste `aantal`. Bv. "speler met minste levensuren".
- `alle` → alle actieve spelers.

## Hoe verplaatsing-events gecontroleerd worden
"Verifieer beweging" kent **bij de controle** de levensuren toe (niet live), per speler op
basis van begin-snapshot → eindpositie (portaal-bewust). Legaal vooruit telt op; te
weinig/te veel/achteruit/niet-doelwit-dat-beweegt trekt af. Zou een speler onder 0 zakken,
dan blijft hij op 0 met **+1 sterfte**. Zie `docs/spel.md` en `docs/events.md`.

## Toekomstige verplaatsing-events (sjablonen)
- **Achterblijver vooruit**: `doelwit {selectie:rang, veld:levensuren, richting:laagste,
  aantal:enkel}`, `voorwaarde:min`, `getal:laag`.
- **Iedereen één vooruit**: `doelwit {selectie:alle}`, `voorwaarde:min`, `getal:enkel`.
- **Niemand mag bewegen**: `doelwit {type:geen}` + alle spelers worden als niet-doelwit
  gecontroleerd (elke beweging = BEWOOG mocht niet).

---

# Hoofdstuk 2 — Toestand

## Hoe een toestand-event in elkaar zit
Een toestand-event **kent iets toe** aan een speler of een uur via `gevolgen`:
- `commando` (`actie`-id) — stuurt een LED/buzzer-actie naar de betrokken palen.
- `score` (`delta`) — wijzigt levensuren van de doelwit-spelers (±, min 0).
- `effect` (`niveau: speler|uur`, `effect`, `duurRondes`) — plakt een tijdelijke tag.

Het doelwit kan `speler` of `uur` zijn. Toestand-events hebben doorgaans **geen**
beweging-voorwaarde. Met het optionele veld `max` begrens je hoeveel instanties van
hetzelfde event tegelijk actief mogen zijn (zo blijft het veld overzichtelijk).

> **LED-toestanden zijn effect-gedreven.** De centrale node "Sync toestanden + LEDs"
> leidt de LED-kleur af uit het actieve uur-effect (`portaal` → paars, `happy_hour` →
> goud) en zet de LED ook weer uit zodra het effect afloopt of het spel stopt. Toestand-
> events hebben dus normaal géén `commando`-gevolg nodig voor hun LED.

## Huidige events

### Portalen — "Een portaal opent tussen twee uren."
- **Werking**: kiest 2 willekeurige uren en opent er een portaal tussen. Beide palen
  krijgen een `portaal`-effect (uur-niveau) met een willekeurige duur (`duurRondes:
  "kort"` → 2–4 rondes); de centrale LED-node kleurt ze **continu paars**. De twee uren
  worden aan elkaar gekoppeld via `data.partner`.
- **Doelwit**: `type: uur`, `selectie: willekeurig`, `aantal: 2`.
- **Max**: `max: 1` — er is hooguit één portaal tegelijk op het veld.
- **Spelregel**: een speler die volgens de spelregels op een portaal-uur landt, mag
  (optioneel) naar het andere portaal-uur springen. Die sprong telt **niet** als stap
  en levert **0 levensuren** op; de stappen ervoor en erna tellen wel. Wie niet terug
  in de tijd mag, mag het portaal niet achteruit nemen. De controle ("Verifieer beweging")
  is portaal-bewust, zodat een legale sprong van een hoger naar een lager uur géén
  "TERUG IN TIJD"-foutcode geeft. Volledige scoring: `docs/spel.md`; afdwinging in flow 04.
- **Simulator**: de actieve paren worden gepubliceerd op `pof/portalen`; de simulator
  tekent een paarse verbindingslijn en laat je een speler die je op een portaal-uur
  loslaat, naar de partner teleporteren.

### Happy Hour — "x uren worden Happy Hour."
- **Werking**: kiest 1–3 willekeurige uren (`aantal: "laag"`) en plaatst er een
  `happy_hour`-effect op; de centrale LED-node kleurt die uren **goud**. De afroep zegt
  het aantal vooraan ("3 uren worden Happy Hour").
- **Doelwit**: `type: uur`, `selectie: willekeurig`, `aantal: "laag"`.
- **Max**: `max: 4` — tot 4 happy-hour-uren tegelijk.
- **Scoring (×2)**: eindigt een speler een verplaatsing **op** een happy-hour-uur, dan
  tellen de daarmee verdiende levensuren **dubbel** (flow 04, "Bereken levensuren"). Bv.
  3 uur vooruit eindigend op happy hour → +6. Zie `docs/spel.md`.

## Hoe het doelwit bepaald wordt
- **Uur-doelwit**: kandidaten = het actieve palen-veld. `willekeurig`/`rang`/`alle`
  zoals in hoofdstuk 1; bij `rang` is `veld` = `nummer` of `bezetting` (aantal spelers
  op die paal).
- **Speler-doelwit**: zoals hoofdstuk 1 (actieve spelers).

## Effecten: opslag, veroudering, weergave
- Opslag: `spelerEffecten[naam][]` (speler-niveau) of `bordStaat[uur].effecten[]`
  (uur-niveau). Elk effect: `{id, bron, instId, effect, naam, resterendeRondes, data}`.
  `bron` = het event-id, `instId` = één per afvuring (alle effecten van één event delen
  dezelfde `instId`, zodat ze samen als één instantie tellen).
- Veroudering: elke ronde `resterendeRondes − 1`; bij 0 verwijderd ("Verouder effecten").
- Weergave: de tabel "Actieve effecten" toont niveau, doel, effect en resterende rondes.
- **Max-engine**: "Kies event" telt vóór elke keuze de actieve instanties (distinct
  `instId` met `bron === id`) en slaat een event over zolang zijn `max` bereikt is.
- **Afgedwongen** effecten (rest is enkel zichtbaar/tag):
  - `mag_niet_bewegen` (speler) → de speler verdient geen levensuren door te bewegen
    (puntensysteem, flow 04).
  - `portaal` (uur) → een sprong tussen de twee gekoppelde portaal-uren levert 0
    levensuren op en telt niet als stap (flow 04). De LED wordt paars. Zie `docs/spel.md`.
  - `happy_hour` (uur) → een verplaatsing die op dit uur eindigt levert dubbele
    levensuren (flow 04). De LED wordt goud. Zie `docs/spel.md`.
  - De LED-kleur van `portaal`/`happy_hour` wordt centraal gezet door "Sync toestanden
    + LEDs" en gaat weer uit zodra het effect afloopt of het spel stopt.

## Hoe toestand-events gecontroleerd worden
Een toestand-event kent geen min/max-doelwit, dus tijdens zijn reactietijd moet **iedereen
stil blijven staan**: wie tóch beweegt, wordt bij de controle bestraft (−|verplaatsing|,
mogelijk een sterfte). De controle bevestigt verder de toekenning: het effect/score is
toegepast (zichtbaar in de effecten- en globale-stats-tabellen). Het happy-hour-×2-voordeel
wordt later geïnd door een verplaatsing-doelwit dat op een happy-hour-uur eindigt.

## Toekomstige toestand-events (sjablonen)
- **Strafkorting**: `gevolgen:[{type:score, delta:-3}]`, doelwit = rijkste speler.
- **Bevriezing**: `gevolgen:[{type:effect, niveau:speler, effect:"mag_niet_bewegen",
  duurRondes:2}]`.
- **Gevaarlijk uur**: `gevolgen:[{type:effect, niveau:uur, effect:"gevaarlijk",
  duurRondes:3}, {type:commando, actie:1}]` (rood + tag).

---

# Hoofdstuk 3 — Wereld

## Hoe een wereld-event in elkaar zit
Een wereld-event verandert iets voor **het hele spel** via `gevolgen` met
`effect`-`niveau: wereld`, of via een globale regelaanpassing. Het `doelwit` is
meestal `type: geen`.

## Huidige events
Nog geen wereld-events gedefinieerd (`[CONFIG] Wereld-events` is leeg).

## Wereld-effecten: opslag, veroudering, weergave
- Opslag: `wereldEffecten[]` (`{id, effect, naam, resterendeRondes, data}`).
- Veroudering: −1 per ronde, verwijderd bij 0.
- Weergave: tabel "Wereld-effecten".
- **Afgedwongen**:
  - `events_sneller` → de reactietijd van elk event wordt gehalveerd (min 1 s),
    afgedwongen in "Voer gevolg uit".

## Hoe wereld-events gecontroleerd worden
Wereld-events hebben geen per-speler beweging-controle. Hun effect is zichtbaar in de
wereld-tabel en in het gedrag van de engine (snellere events, gewijzigde regels) tot
`resterendeRondes` op 0 staat.

## Toekomstige wereld-events (sjablonen)
- **Versnelling**: `gevolgen:[{type:effect, niveau:wereld, effect:"events_sneller",
  duurRondes:3}]`.
- **Collectieve bonus/straf**: een wereld-event dat via een function alle spelers
  tegelijk levensuren geeft of afneemt.
- **Regelwijziging**: een wereld-event dat `pofRegels.maxVerplaatsing` tijdelijk
  aanpast.
