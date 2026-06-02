# Plates-of-Fate — event-catalogus

Dit document beschrijft **per categorie** elk event van het spel: hoe het in elkaar
zit, wie of wat de doelwitten zijn (en hoe die bepaald worden), en hoe het event na
afloop gecontroleerd wordt. De drie categorieën zijn:

1. **Verplaatsing** — over de spelers die mogen/moeten bewegen.
2. **Toestand** — wat er precies aan een speler of uur wordt toegekend.
3. **Wereld** — wat er in het hele spel gezamenlijk verandert.

> Voor het veld-voor-veld JSON-schema van een event-object: zie `docs/events.md`.
> Voor de spelregels rond levensuren/achterstand: zie `docs/spel.md`.

## Begrippen (gelden voor alle categorieën)

- **Doelwit**: wie/wat het event raakt. Bepaald door het `doelwit`-object:
  - `selectie`: `willekeurig` (steekproef), `rang` (gesorteerd op een veld), of `alle`.
  - `veld` (bij `rang`): spelers → `levensuren`/`achterstand`; uren → `nummer`/`bezetting`.
  - `richting` (bij `rang`): `hoogste` of `laagste`.
  - `aantal`: vast getal, of optie `enkel`=1, `laag`=1–3, `midden`=1–6, `hoog`=3–10.
- **Actieve spelers**: alleen spelers met een bekende positie (`spelerLocaties`) en
  niet gepauzeerd komen in aanmerking als doelwit. Verwijderde of niet-aanwezige
  spelers worden nooit gekozen.
- **Reactietijd** (`reactietijd_s`): tijd waarin spelers mogen reageren vóór de
  controle. Wereld-effect `events_sneller` halveert deze.
- **Netto-verplaatsing**: het verschil tussen begin- en eindpaal op de 24-uur ring,
  als kortste **signed** afstand: positief = vooruit, negatief = achteruit.
  De beginposities worden vastgelegd op het moment dat het event valt.

---

# Hoofdstuk 1 — Verplaatsing

## Hoe een verplaatsing-event in elkaar zit
Een verplaatsing-event kiest doelwit-**spelers** die binnen de reactietijd aan een
beweging-**voorwaarde** moeten voldoen. Spelers die géén doelwit zijn, moeten stil
blijven staan. Het event heeft meestal `gevolgen: [{type:"geen"}]` — de "straf/beloning"
zit in het puntensysteem (levensuren/achterstand), niet in een LED-commando.

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
- **Controle** (na reactietijd, per doelwit-speler):
  - `netto < 0` → **TERUG IN TIJD** (foutcode)
  - `netto ≥ x` → **OK**
  - `0 ≤ netto < x` → **TE WEINIG** (foutcode; ook stilstaan = netto 0)
  - niet-doelwit dat toch beweegt → **BEWOOG (mocht niet)** (foutcode)

### verplaatsingMax — "Maximum x uur."
- **Werking**: de gekozen spelers mogen hoogstens `x` uur vooruit.
- **Doelwit**: identiek aan verplaatsingMin.
- **Controle**:
  - `netto < 0` → **TERUG IN TIJD**
  - `0 ≤ netto ≤ x` → **OK** (ook stilstaan)
  - `netto > x` → **TE VEEL**
  - niet-doelwit dat beweegt → **BEWOOG (mocht niet)**

## Hoe het doelwit bepaald wordt
De kandidaten zijn de **actieve, niet-gepauzeerde** spelers. Daarna:
- `willekeurig` → `aantal` spelers via steekproef (zonder terugleggen).
- `rang` → sorteer op `veld` (`levensuren` of `achterstand`) in `richting`
  (`hoogste`/`laagste`), neem de eerste `aantal`. Bv. "speler met minste levensuren".
- `alle` → alle actieve spelers.

## Hoe verplaatsing-events gecontroleerd worden
"Verifieer beweging" berekent per speler de netto-verplaatsing tussen de
begin-snapshot en de eindpositie, en kent een status toe (zie boven). Alleen échte
overtredingen (TE WEINIG / TE VEEL / TERUG IN TIJD / BEWOOG mocht niet) verschijnen
als foutcode; een correcte ronde geeft "Controle OK".

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
beweging-voorwaarde.

## Huidige events

### test_uur_groen — "Eén willekeurig uur wordt groen."
- **Werking**: kiest 1 uur en maakt dat groen; bij het afroepen van het uur klinkt
  de buzzer-piep (slave-actie 23) op die paal.
- **Doelwit**: `type: uur`, `selectie: willekeurig`, `aantal: enkel` (1 uur uit het
  actieve veld — in simulatiemodus 1..24, anders `paaltjesLijst`).
- **Gevolg**: `commando` actie `2` (groen) naar het gekozen uur.
- **Controle**: geen beweging-controle. De controle-tabel toont de toegekende tags;
  voor uur-events is "geslaagd" = het commando is verstuurd.

## Hoe het doelwit bepaald wordt
- **Uur-doelwit**: kandidaten = het actieve palen-veld. `willekeurig`/`rang`/`alle`
  zoals in hoofdstuk 1; bij `rang` is `veld` = `nummer` of `bezetting` (aantal spelers
  op die paal).
- **Speler-doelwit**: zoals hoofdstuk 1 (actieve spelers).

## Effecten: opslag, veroudering, weergave
- Opslag: `spelerEffecten[naam][]` (speler-niveau) of `bordStaat[uur].effecten[]`
  (uur-niveau). Elk effect: `{id, effect, naam, resterendeRondes, data}`.
- Veroudering: elke ronde `resterendeRondes − 1`; bij 0 verwijderd ("Verouder effecten").
- Weergave: de tabel "Actieve effecten" toont niveau, doel, effect en resterende rondes.
- **Afgedwongen** effecten (rest is enkel zichtbaar/tag):
  - `mag_niet_bewegen` (speler) → de speler verdient geen levensuren door te bewegen
    (puntensysteem, flow 04).

## Hoe toestand-events gecontroleerd worden
Geen beweging-controle. De controle bevestigt de toekenning: het LED/buzzer-commando
is verstuurd naar de juiste palen, en/of het effect/score is toegepast (zichtbaar in
de effecten-tabel en de levensuren-tabel).

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
- **Regels** (`pofRegels`, globaal): `maxVerplaatsing` begrenst hoeveel uur een speler
  per ronde vooruit kan scoren; overschrijding telt als achterstand i.p.v. beloning
  (puntensysteem, flow 04).

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
