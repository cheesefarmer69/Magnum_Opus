# Plates-of-Fate — event-catalogus

Dit document beschrijft **per categorie** elk event van het spel: hoe het in elkaar
zit, wie of wat de doelwitten zijn (en hoe die bepaald worden), en hoe het event na
afloop gecontroleerd wordt. De drie categorieën zijn:

1. **Verplaatsing** — over de spelers die mogen/moeten bewegen.
2. **Toestand** — wat er precies aan een speler of uur wordt toegekend.
3. **Wereld** — wat er in het hele spel gezamenlijk verandert.

> Voor het veld-voor-veld JSON-schema van een event-object: zie `docs/spel/events.md`.
> Voor de spelregels rond levensuren/sterftes: zie `docs/spel/spel.md`.

## Begrippen (gelden voor alle categorieën)

- **Doelwit**: wie/wat het event raakt. Bepaald door het `doelwit`-object:
  - `type`: `speler`, `uur`, `groep` of `geen`.
  - `selectie`: `willekeurig` (steekproef) of `alle`.
  - `aantal`: vast getal, of optie `enkel`=1, `laag`=1–3, `midden`=1–6, `hoog`=3–10.
- **Groep-doelwit** (`type: "groep"`): kiest via `veld` (`kleur`/`jaar`, of `willekeurig` = engine
  kiest per afvuring kleur of jaar) één willekeurige waarde die onder de actieve spelers voorkomt
  (of een vaste `waarde`) en richt het event op **alle** actieve spelers met die waarde.
  Afroep-prefix "een groep"; label `veld: waarde`. Bron: `spelerEigenschappen` (zie `docs/spel/spelers.md`).
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
| `voorwaarde` | `"min"` (minstens x vooruit), `"max"` (hoogstens x vooruit) of `"of"` (exact x óf exact y vooruit) |
| `getal` | rolt `x` (bv. `midden` → 1–6) en vult het in de tekst in |
| `getal2` | rolt `y` (tweede keuze bij `voorwaarde: "of"`); optie of `[min,max]`-bereik |
| `doelwit` | type `speler` — wie moet bewegen |

> Controle/scoring is **pad-gebaseerd** (STAP/TELEPORT, actie-per-actie) — zie
> `docs/spel/event-systeem.md`. `voor` = aantal STAP vooruit, `x` = budget. Een TELEPORT (sprong
> tussen twee actieve portaal-palen) telt 0 stappen.

## Huidige events

### verplaatsingMax — "Maximum x uur." (Event A)
- **Werking**: de gekozen spelers mogen **hoogstens** `x` STAPpen vooruit (minder mag,
  achteruit niet). Een portaal-sprong telt 0.
- **Doelwit**: `type: speler`, `selectie: willekeurig`, `aantal: laag` (1–3 actieve spelers).
  `x` = `getal: midden`.
- **Controle** — levensuren-Δ:
  - `voor ≤ x` (geen achterstap) → **OK**, +voor (×2 op happy-hour-eindpaal)
  - `voor > x` → **TE VEEL**, −(voor − x)
  - achterwaartse STAP → **TERUG IN TIJD**, −achter
  - >1× zelfde portaal → **ONGELDIGE TELEPORT**, −voor
  - niet-doelwit dat beweegt → **BEWOOG (mocht niet)**, −(voor+achter)
  - onder 0 → 0 levensuren + **1 sterfte**

> Het oude `verplaatsingMin`-event is verwijderd (de "nooit achteruit"-regel zit nu in de
> STAP-definitie; een aparte minimum-variant is niet nodig).

### Of-verplaatsing — "x of y uur vooruit." (Event D)
- **Werking**: de gekozen spelers krijgen **twee** keuzes en moeten **exact `x` óf exact `y`**
  STAPpen vooruit zetten. `x` rolt uit `getal: "laag"` (1–3), `y` uit `getal2: [4, 6]` (4–6) — het
  `[min,max]`-bereik houdt `y` gegarandeerd boven `x`. Een portaal-sprong telt 0.
- **Doelwit**: `type: speler`, `selectie: willekeurig`, `aantal: midden` (1–6 actieve spelers).
- **Controle** — levensuren-Δ:
  - `voor === x` of `voor === y` (geen achterstap) → **OK**, +voor (×2 op happy-hour-eindpaal)
  - `voor ≠ x` én `voor ≠ y` (geen achterstap) → **ONGELDIGE KEUZE**, −voor
  - achterwaartse STAP → **TERUG IN TIJD**, −achter
  - niet-doelwit dat beweegt → **BEWOOG (mocht niet)**, −(voor+achter)
  - onder 0 → 0 levensuren + **1 sterfte**

### Groep-verplaatsing — "maximum x uur vooruit." (groep-doelwit)
- **Werking**: de engine kiest per afvuring een dimensie (`kleur` of `jaar`, want `veld: willekeurig`),
  dan één willekeurige waarde daarvan (bv. `kleur: rood` of `jaar: eerste`) onder de actieve spelers,
  en richt zich op **alle** spelers met die waarde; zij mogen hoogstens `x` STAPpen vooruit
  (`voorwaarde: max`, zelfde controle als `verplaatsingMax`).
- **Doelwit**: `type: groep`, `veld: willekeurig` (kleur of jaar), `selectie: willekeurig`. `x` = `getal: midden`.
- **Afroep**: "een groep … maximum x uur vooruit. kleur: rood" / "… jaar: eerste" — geen individuele namen.
- **Controle**: identiek aan `verplaatsingMax`, maar voor elk groepslid; niet-leden moeten stil blijven.

### Groep-of-verplaatsing — "x of y uur vooruit." (groep-doelwit, Event D-variant)
- **Werking**: kiest per afvuring een dimensie (`kleur` of `jaar`, `veld: willekeurig`) en daarvan één
  willekeurige groep onder de actieve spelers; die spelers moeten **exact `x` óf exact `y`** STAPpen
  vooruit zetten. `x` rolt uit `getal: laag` (1–3), `y` uit `getal2: [4,6]`. Zelfde controle als `of_verplaatsing`.
- **Doelwit**: `type: groep`, `veld: willekeurig` (kleur of jaar), `selectie: willekeurig`.
- **Afroep**: "een groep … x of y uur vooruit. kleur: rood" / "… jaar: tweede" — geen individuele namen.
- **Controle**: identiek aan `of_verplaatsing`, maar voor elk groepslid; niet-leden moeten stil blijven.

## Hoe het doelwit bepaald wordt
De kandidaten zijn de **actieve, niet-gepauzeerde** spelers. Daarna:
- `willekeurig` → `aantal` spelers via steekproef (zonder terugleggen).
- `alle` → alle actieve spelers.
- `groep` → dimensie `veld` (`kleur`/`jaar`, of `willekeurig`) en daarvan één waarde gekozen (willekeurig of vast via `waarde`); doelwit
  = alle actieve spelers met die waarde. Eigenschappen uit `spelerEigenschappen` (`docs/spel/spelers.md`).

## Hoe verplaatsing-events gecontroleerd worden
"Verifieer beweging" kent **bij de controle** de levensuren toe (niet live), per speler op
basis van begin-snapshot → eindpositie (portaal-bewust). Legaal vooruit telt op; te
weinig/te veel/achteruit/niet-doelwit-dat-beweegt trekt af. Zou een speler onder 0 zakken,
dan blijft hij op 0 met **+1 sterfte**. Zie `docs/spel/spel.md` en `docs/spel/events.md`.

## Toekomstige verplaatsing-events (sjablonen)
- **Eén vooruit (toeval)**: `doelwit {selectie:willekeurig, aantal:enkel}`,
  `voorwaarde:min`, `getal:laag`.
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
  krijgen een `portaal`-effect (uur-niveau) met een willekeurige duur (`duratie: [3,8]` →
  3–8 events); de centrale LED-node kleurt ze **continu paars**. De twee uren worden aan
  elkaar gekoppeld via `data.partner`.
- **Doelwit**: `type: uur`, `selectie: willekeurig`, `aantal: 2`.
- **Max**: `max: 1` — er is hooguit één portaal tegelijk op het veld.
- **Spelregel**: een speler die volgens de spelregels op een portaal-uur landt, mag
  (optioneel) naar het andere portaal-uur springen. Die sprong telt **niet** als stap
  en levert **0 levensuren** op; de stappen ervoor en erna tellen wel. Wie niet terug
  in de tijd mag, mag het portaal niet achteruit nemen. De controle ("Verifieer beweging")
  is portaal-bewust, zodat een legale sprong van een hoger naar een lager uur géén
  "TERUG IN TIJD"-foutcode geeft. Volledige scoring: `docs/spel/spel.md`; afdwinging in flow 04.
- **Simulator**: de actieve paren worden gepubliceerd op `pof/portalen`; de simulator
  tekent een paarse verbindingslijn en laat je een speler die je op een portaal-uur
  loslaat, naar de partner teleporteren.

### Happy Hour — "x uren worden Happy Hour."
- **Werking**: kiest 1–3 willekeurige uren (`aantal: "laag"`) en plaatst er een
  `happy_hour`-effect op; de centrale LED-node kleurt die uren **goud**. De afroep zegt
  het aantal vooraan ("3 uren worden Happy Hour").
- **Doelwit**: `type: uur`, `selectie: willekeurig`, `aantal: "laag"`.
- **Max**: `max: 2` — tot 2 happy-hour-uren tegelijk.
- **Scoring (×2)**: eindigt een speler een verplaatsing **op** een happy-hour-uur, dan
  tellen de daarmee verdiende levensuren **dubbel** (flow 04, "Bereken levensuren"). Bv.
  3 uur vooruit eindigend op happy hour → +6. Zie `docs/spel/spel.md`.

### Ziekte — "worden ziek." (zieke spelers + medicijn-palen)
- **Werking**: kiest 1–3 spelers (`aantal: laag`) die **ziek** worden, en plaatst **medicijn** op
  evenveel **vrije uren** (palen zonder actief uur-effect) → **felroze** LED (`ACTIE_MEDICIJN`, 4).
  De zieke spelers worden als gewone speler-doelwitten één voor één afgeroepen ("3 spelers worden ziek").
- **Doelwit**: `type: speler`, `selectie: willekeurig`, `aantal: laag`. `gevolgen: [{type:"ziekte"}]`.
- **Max / duratie**: `max: 1` (één episode tegelijk); `duratie: 10` = aantal events dat een zieke heeft.
- **Lifecycle** (beheerd door de node "Ziekte-beheer", elke ronde na de controle):
  - Een zieke speler doorloopt de **normale** verplaatsingscontrole (géén vrijstelling): verdient **geen**
    levensuren, maar **verliest** ze bij onwettige zetten en krijgt "BEWOOG (mocht niet)" als hij beweegt
    terwijl hij geen bewegings-doelwit is.
  - Genezen kan **enkel** als de zet **wettelijk** was (status OK / OK (stil)) **én** hij op een
    **medicijn-uur** eindigt → **GENEZEN** (verdient daarna weer). Gewoon naar een medicijn wandelen
    terwijl het niet mocht, geneest **niet**. Zodra **iemand** via een wettelijke zet op een
    medicijn-uur geneest, **verdwijnt** dat medicijn (na de controle; felroze LED uit) — ook bij één
    genezer, zodat hetzelfde medicijn niet daarna nog andere zieken kan genezen.
  - Niet-genezen zieken tellen elke ronde af; bij **0** → **dood**: levensuren → 0 **en +1 sterfte**.
  - Vanaf nog **3** events: elke ronde een **hartslag-waarschuwing** op het uur van de speler —
    ziekenhuis-monitor-piep + **3/2/1** hartslagen (`ACTIE_ZIEK_W3/W2/W1`, 5/6/7).
  - Zijn **alle** zieken genezen of dood → alle medicijn-palen worden **gedeactiveerd** (vrij voor
    andere events). De actieve zieken worden gepubliceerd op `pof/ziekte` voor de simulator.

### Tijdbom — "worden een tijdbom." (bom-spelers + ontmantel-palen via drukknop)
- **Werking**: kiest 1–3 spelers (`aantal: laag`) die een **tikkende tijdbom** worden (afroep: "3 spelers
  worden een tijdbom"), met een aftelklok van **`duratie` (10)** events — net als ziekte. Het event kiest
  evenveel **ontmantel-palen** als bommen uit de palen met een **drukknop** (`global.drukknopPalen`,
  zie `config/drukknoppen`); die palen knipperen rood (`ACTIE_TIJDBOM`, 13).
- **Doelwit**: `type: speler`, `selectie: willekeurig`, `aantal: laag`. `gevolgen: [{type:"tijdbom"}]`.
  `exclusiefGroep: "speler-toestand"` → niet samen met ziekte op één speler (uitschakelbaar in Systeeminstellingen).
- **Max / duratie**: `max: 1` (één episode tegelijk); `duratie: 10`.
- **Ontmanteling** (knop werkt op **elk** moment, in elke event-fase — node "Knop-verwerking"):
  - Een bom-speler die op een gekozen **ontmantel-paal** staat en die knop (laat) indrukken, probeert te
    ontmantelen. Kans op slagen: **dag** (uren **7–18**) = **80%**, **nacht** (uren **19–6**) = **50%**.
  - **Slaagt** → bom verdwijnt, geen gevolgen.
  - **Mislukt** → **iedere** speler op die paal verliest het aantal levensuren gelijk aan het **uur**
    (mislukking op uur 7 → −7 voor iedereen daar; onder 0 → 0 + sterfte). De bom(men) op die paal zijn verbruikt.
- **Ontploffing** (node "Tijdbom-beheer", elke ronde na de controle): tikt elke ronde af; bij **0**
  ontploft de bom = **hetzelfde als een mislukte ontmanteling** (iedereen op de paal van de bom-speler
  verliest `uur` levensuren). Zijn er geen bommen meer → de ontmantel-palen gaan uit. De stand wordt
  gepubliceerd op `pof/tijdbom` (bom-spelers + ontmantel-palen) voor de simulator (💣-badge + knoppen-paneel).

### Tornado — "worden door een tornado getroffen." (zuigt aanliggende uren naar het midden)
- **Werking**: kiest **1–2** uren als tornado-**center** (afroep: "2 uren ..."). `minAfstand: 3` zorgt dat
  twee tornado's (center + buururen) **nooit overlappen**. Het center krijgt een **donkergrijze** LED
  (`ACTIE_TORNADO`, 14); de twee **aanliggende** uren een **trage grijze pulse** (`ACTIE_TORNADO_RAND`, 15).
- **Doelwit**: `type: uur`, `selectie: willekeurig`, `aantal: [1,2]`, `minAfstand: 3`. `gevolgen: [{type:"tornado"}]`.
- **Mechaniek**: de tornado **zuigt** iedereen op de twee aanliggende uren naar het center. Spelers daar
  **moeten** binnen de reactietijd (20 s) naar het center bewegen. Bij de controle:
  - op het center geëindigd → **GEVOLGD** (geen winst/verlies);
  - niet gevolgd → **WEGGEZOGEN**: **alle** levensuren kwijt (**geen** sterfte).
  - Spelers die niet op een aanliggend uur stonden, blijven ongemoeid.
- **LED-override**: de tornado mag de LED van een onderliggend effect (portaal/happy/...) **tijdelijk**
  overschrijven. Het is een **één-shot** (`duratie: 1`): bij de controle wordt `tornadoActief` geleegd en
  een LED-rebuild geforceerd → de palen keren terug naar hun oorspronkelijke staat.

### Tempo-events — "Sneller" / "Trager" (wereld)
- **Werking**: `sneller_events` en `trager_events` (wereld, `doelwit: geen`) stappen `global.spelTempoFactor`,
  die de **reactietijd** van elk volgend event schaalt. Sneller: **−0,1** per keer (min **0,6**); trager:
  **+0,1** per keer (max **1,3**). Start 1,0; reset naar 1,0 bij Stop. Bv. een verplaatsing-event (20 s) bij
  tempo 0,8 → 16 s reactietijd. `gevolgen: [{type:"tempo", richting:"sneller"|"trager"}]`.

### Event-tiers (zeldzaamheid → keuze-kans)
- Elk event heeft een **`tier`** met een keuze-**gewicht**: `common` 50 · `uncommon` 25 · `rare` 15 ·
  `epic` 8 · `legendary` 2 (default `common`). De engine kiest events **gewogen** (in "Bouw pof/status"
  voor de wachtrij en in "Kies event" als fallback), zodat ingrijpende events zeldzaam blijven en het spel
  niet wild heen en weer geslingerd wordt. Standaard: verplaatsing-events `common`; portalen/happy_hour/
  sneller/trager `uncommon`; ziekte/tijdbom `rare`; tornado `epic`; nuke `legendary`. Per event aanpasbaar
  via de **events-tab** in de simulator (`sim/tiers-config` → `global.eventTiers`).

### Slechte aura (geen event — een eigenschap van negatieve events)
- **Werking**: events met `slechteAura: true` (nu **Ziekte** en **Tijdbom**) kiezen hun speler-doelwit
  **gewogen** naar de regio: een kandidaat in de **avond** (uur 20–23 of 1–6) krijgt **×1,10** kans, op
  **middernacht** (uur 24) **×1,15**, overdag (uur 7–19) ×1,00. Zo is het overdag veiliger om te verblijven.
  Aan/uit via de **Spelinstellingen**-tab (`global.badAuraAan`, `sim/spel-config`). Uur-events en `geen`-doelwit
  (Nuke) vallen erbuiten.

## Hoe het doelwit bepaald wordt
- **Uur-doelwit**: kandidaten = het actieve palen-veld. `willekeurig`/`alle`
  zoals in hoofdstuk 1.
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
    levensuren op en telt niet als stap (flow 04). De LED wordt paars. Zie `docs/spel/spel.md`.
  - `happy_hour` (uur) → een verplaatsing die op dit uur eindigt levert dubbele
    levensuren (flow 04). De LED wordt goud. Zie `docs/spel/spel.md`.
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
**altijd `type: geen`** (een wereld-event kiest geen spelers/uren en roept geen doelwit af).

## Huidige events

### Nuke — "Nuke." (ontploffing + regroup)
- **Werking**: speelt "NUKE" af + een **aftelklok** (`reactietijd_s`, standaard 5 s, aanpasbaar) om
  weg te lopen. Bij de controle ontploft **elke speler die nog gedetecteerd is** — in het echte spel:
  RSSI boven de vloer; in de simulator: binnen het veld (in `spelerLocaties`). Gevolg: **levensuren → 0
  en +1 sterfte**. Wie **ontkomen** is (onder de RSSI-vloer / buiten het veld → weg uit
  `spelerLocaties`) overleeft als "VEILIG".
- **Doelwit**: `type: geen`. **Gevolg**: `{type:"nuke"}`. `max: 1`.
- **Lichtshow**: tijdens het aftellen kleurt de hele arena groen↔geel (actie 8), **behalve de
  middernacht-poort-paal** (de hoogste paal) — die houdt zijn poort-status. Na de ontploffing gaan
  alle nuke-palen weer netjes uit (geen blijvend-groene palen).
- **Regroup**: na de ontploffing een pauze van `regroup_s` s (standaard 60, aanpasbaar) — de engine
  staat in fase `regroup` en gaat daarna terug naar de normale aanloop. Geen bewegings-straffen tijdens
  een NUKE (iedereen mág vluchten).
- **Wereld-wis**: een nuke ruimt bij de controle ook de lopende **ziekte-episode** (zieke spelers +
  medicijn-uren) en alle **dienaars** op; het veld is daarna schoon. Zieken/medicijnen/dienaars overleven
  dus geen nuke. (De "willekeurige zieken" die je tijdens een spel ziet, komen van het normale
  **ziekte-event**, niet van de nuke.)
- **Simulator**: een "Out"-knop (en het buiten het veld slepen van een bolletje) zet spelers op **"uit"**
  → ze worden niet meer gepubliceerd → veilig. Bij de ontploffing flitst het veld kort rood/geel.

### Bomaanslag — "Een bomaanslag op uur 9 en 11!" (vaste uren)
- **Werking**: een gelokaliseerde bom op de **vaste** uren **9 en 11**. Tijdens `reactietijd_s` (3 s) een
  waarschuwing op die uren (rode tik-LED + zoemer-piep) + het ontploffingsgeluid (`audioVoor`). Bij de
  controle ontploft de bom: wie **op dat moment** op uur 9 of 11 staat verliest **`uur`** levensuren
  (uur 9 → −9, uur 11 → −11). Vluchten tijdens de 3 s mag (geen bewegingsstraf, zoals NUKE).
- **Doelwit**: `type: "uur"`, **`vast: [9, 11]`** (altijd diezelfde uren). **Gevolg**: `{type:"bom"}`.
- **Lichtshow**: bij de ontploffing een **witte flikker** (OOGST-strobe, actie 11) op uur 9 en 11; daarna
  gaan ze weer uit (via een Sync-rebuild).
- **Audio**: centraal ontploffingsgeluid via **`events/bomaanslag.wav`** op de audio-player — dit
  WAV-bestand moet je nog toevoegen in `pi/audio-player/audio/events/`.

## Middernacht (permanent mechanisme, géén afroepbaar event)
Middernacht is **geen** event in `pofEvents` maar een **continu** mechanisme (node "Middernacht", draait per
event). De poort op de hoogste paal volgt de cijfers van **π**: open (zacht witte LED) / dicht (rode LED),
fase-duur = het π-cijfer, start open. Bij een **dichte** poort mag je middernacht niet **oversteken**:
wie tóch over de poort heen stapt (de 24→1-wrap) verliest **al zijn levensuren + 1 sterfte**
(`MIDDERNACHT DICHT`); tot aan de poort lopen zonder oversteken mag wél. Een **0** in π = **oogst**: spelers op de middernacht-paal sterven en worden
**dienaar** van de armste speler (hun winst gaat voortaan naar die meester). Volledige regels:
`docs/spel/spel.md` en `docs/invarianten.md` §4d. De simulator heeft een vast **Middernacht-paneel**
(linksboven, onder de broker-balk; uitklapbaar, max ~1/4 schermhoogte) met de π-index, open/dicht, events
tot de oogst, plus een **"Actief"-checkbox** om het mechanisme aan/uit te zetten; dienaren staan in het
speler-menu. Zet je de checkbox uit, dan is de hoogste paal een gewoon uur (`sim/middernacht-config` →
`middernachtAan`). Het dashboard heeft een **Dienaren**-tabel + middernacht-status.

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
