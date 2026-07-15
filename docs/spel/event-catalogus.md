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
  - `aantal`: een **vast getal** / `[min,max]`-array / `vast`(`Opties`) (schalen niet), of een **optie**
    die **sub-lineair** met het veld meegroeit (**doelwit-dichtheid**, G3):
    `aantal = clamp(round(mult × √N × (dichtheid / 0,25)), 1, min(N, 6))` met `mult` **0,35** (`laag`) /
    **0,55** (`midden`) / **0,90** (`hoog`), en `enkel` = altijd 1. `global.doelwitDichtheid` (default
    **0,25**) is de globale knob. **Jitter (O5):** ná de √N-formule en vóór de clamp wordt er een
    **±1-stap** bijgeteld — **25 %** kans −1, **25 %** kans +1, **50 %** ongewijzigd — zodat het aantal
    doelwitten per afvuring licht varieert i.p.v. elke keer exact hetzelfde te zijn. Bij 31 spelers geeft
    dat rond **2 / 3 / 5** (met jitter ≈ 1–3 / 2–4 / 4–6); de vroegere lineaire formule gaf 5 / 8 / 10 en
    liet het veld verzadigen. Groep-events negeren `aantal` (emergente omvang) en wegen zwaarder bij veel spelers.
- **Bewegen mag enkel wanneer een event het zegt.** Buiten een event (fases `aanloop`/`wacht`) is elke
  paalwissel **vrij wandelen**: bij de eerstvolgende controle vervalt je winst (`delta → 0`) en krijg je
  +1 valsspeelpunt (`… | VRIJ GEWANDELD`). Géén levensuren-verlies, géén sterfte. Een **god-punt** vergeeft
  het. Er is **geen enkele vrije fase** — ook niet de `regroup` na een nuke. Zie invariant **V10**.
- **Groep-doelwit** (`type: "groep"`): kiest via `veld` (`kleur`/`jaar`/`maand`/`seizoen`/`pariteit`, of
  `willekeurig` = engine kiest er per afvuring één) één willekeurige waarde die onder de actieve spelers
  voorkomt (of een vaste `waarde`) en richt het event op **alle** actieve spelers met die waarde.
  Enkel bij `veld: "willekeurig"` (en ≥ 4 spelers) is er ~15 % kans op een **tweede** groep erbij;
  een vast `veld` (bv. etenstijd = `kleur`) geeft altijd precies één groep.
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
- **Woosh-signatuur (wereld-events)**: enkel bij `categorie === "wereld"` speelt **direct ná het aftellen
  en vóór de afroep** een korte sting `sound-effect/wereld-events/woosh.wav` — een herkenbaar "er komt een
  groot event aan"-geluid. Andere categorieën (verplaatsing/toestand) krijgen die woosh niet. (WAV nog op te
  nemen; de audio-player slaat ontbrekende bestanden stil over.)
- **Max** (`max`): begrenst hoeveel instanties van hetzelfde toestand-event tegelijk
  op het veld mogen staan (zie hoofdstuk 2).
- **Netto-verplaatsing**: het verschil tussen begin- en eindpaal op de 24-uur ring,
  als kortste **signed** afstand: positief = vooruit, negatief = achteruit.
  De beginposities worden vastgelegd op het moment dat het event valt.

---

# Hoofdstuk 1 — Verplaatsing

## Hoe een verplaatsing-event in elkaar zit
Een verplaatsing-event kiest een **groep** spelers (gedeelde eigenschap: kleur, jaar, maand of seizoen) die
binnen de reactietijd aan een beweging-**voorwaarde** moeten voldoen. Spelers die géén doelwit
zijn, moeten stil blijven staan. Het event heeft meestal `gevolgen: [{type:"geen"}]` — de
"straf/beloning" zit in het puntensysteem (levensuren, toegekend bij de controle), niet in een
LED-commando.

> **Alleen groep-doelwitten.** Verplaatsing-events richten zich uitsluitend op een **groep**
> (`doelwit.type: "groep"`). De vroegere varianten met een individueel speler-doelwit
> (`verplaatsingMax`/`verplaatsing2` en `of_verplaatsing`) zijn **verwijderd** — bij veel spelers
> is een groep-afroep minder repetitief. De regels/scoring zijn ongewijzigd; enkel het doelwit is
> nu altijd een groep.

| Veld | Rol |
|------|-----|
| `categorie` | `"verplaatsing"` |
| `voorwaarde` | `"max"` (hoogstens x vooruit) of `"of"` (exact x óf exact y vooruit) |
| `getal` | rolt `x` (bv. `midden` → 1–6) en vult het in de tekst in |
| `getal2` | rolt `y` (tweede keuze bij `voorwaarde: "of"`); optie of `[min,max]`-bereik |
| `doelwit` | type `groep` (`veld: kleur`/`jaar`/`maand`/`seizoen`/`pariteit`/`willekeurig`) — welke groep moet bewegen |

> Controle/scoring is **pad-gebaseerd** (STAP/TELEPORT, actie-per-actie) — zie
> `docs/spel/event-systeem.md`. `voor` = aantal STAP vooruit, `x` = budget. Een TELEPORT (sprong
> tussen twee actieve portaal-palen) telt 0 stappen.

## Overzicht (gestructureerd)

### Groep-verplaatsing — "maximum x uur vooruit."
- **Tier:** common
- **Uitleg:** Een hele groep (kleur, jaar, maand of seizoen) mag hoogstens `x` STAPpen vooruit; niet-leden blijven stil. Levensuren worden bij de controle toegekend.
- **Max:** —
- **Audio (opkomst):** `maximum.wav` + getal + `uur_vooruit.wav`
- **Audio (weggaan):** —

### Groep-of-verplaatsing — "x of y uur vooruit."
- **Tier:** common
- **Uitleg:** Een hele groep moet exact `x` óf exact `y` STAPpen vooruit zetten.
- **Max:** —
- **Audio (opkomst):** getal + `uur_vooruit.wav` (geen aparte audioVoor)
- **Audio (weggaan):** —

## Huidige events

### Groep-verplaatsing — "maximum x uur vooruit." (groep-doelwit)
- **Werking**: de engine kiest per afvuring een dimensie (`kleur`, `jaar`, `maand` of `seizoen`, want `veld: willekeurig`),
  dan één willekeurige waarde daarvan (bv. `kleur: rood` of `jaar: eerste`) onder de actieve spelers,
  en richt zich op **alle** spelers met die waarde; zij mogen **hoogstens** `x` STAPpen vooruit
  (minder mag, achteruit niet). Een portaal-sprong telt 0.
- **Doelwit**: `type: groep`, `veld: willekeurig` (kleur/jaar/maand/seizoen), `selectie: willekeurig`. `x` = `getal: midden`.
- **Afroep**: "een groep … maximum x uur vooruit. kleur: rood" / "… jaar: eerste" — geen individuele namen.
- **Controle** — levensuren-Δ (per groepslid; niet-leden moeten stil blijven):
  - `voor ≤ x` (geen achterstap) → **OK**, +voor (×2 op happy-hour-eindpaal)
  - `voor > x` → **TE VEEL**, max(0, x − (voor − x)) — proportioneel, vloer 0
  - achterwaartse STAP → **TERUG IN TIJD**, max(0, voor − achter)
  - >1× zelfde portaal → **ONGELDIGE TELEPORT**, 0
  - niet-doelwit dat beweegt → **BEWOOG (mocht niet)**, 0
  - valsspelen kost geen levensuren en geeft **geen sterfte** (V11); dodelijke straffen staan los

### Groep-of-verplaatsing — "x of y uur vooruit." (groep-doelwit)
- **Werking**: kiest per afvuring een dimensie (`kleur`/`jaar`/`maand`/`seizoen`, `veld: willekeurig`) en daarvan één
  willekeurige groep onder de actieve spelers; die spelers moeten **exact `x` óf exact `y`** STAPpen
  vooruit zetten. `x` rolt uit `getal: laag` (1–3), `y` uit `getal2: [4,6]` — het `[min,max]`-bereik
  houdt `y` gegarandeerd boven `x`. Een portaal-sprong telt 0.
- **Doelwit**: `type: groep`, `veld: willekeurig` (kleur/jaar/maand/seizoen), `selectie: willekeurig`.
- **Afroep**: "een groep … x of y uur vooruit. kleur: rood" / "… seizoen: winter" — geen individuele namen.
- **Controle** — levensuren-Δ (per groepslid; niet-leden moeten stil blijven):
  - `voor === x` of `voor === y` (geen achterstap) → **OK**, +voor (×2 op happy-hour-eindpaal)
  - `voor ≠ x` én `voor ≠ y` (geen achterstap) → **ONGELDIGE KEUZE**, max(0, voor − afstand tot dichtste geldige)
  - achterwaartse STAP → **TERUG IN TIJD**, max(0, voor − achter)
  - niet-doelwit dat beweegt → **BEWOOG (mocht niet)**, 0
  - valsspelen kost geen levensuren en geeft **geen sterfte** (V11); dodelijke straffen staan los

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

## Overzicht (gestructureerd)

### Portalen — "Een portaal opent tussen twee uren."
- **Tier:** uncommon
- **Uitleg:** Opent een portaal tussen 2 willekeurige uren die **minstens 6 uren uit elkaar** liggen (paars); een sprong ertussen telt 0 stappen en levert 0 levensuren. Twee portalen delen nooit een paal.
- **Max:** 2
- **Audio (opkomst):** `een_portaal_opent_tussen_twee_uren.wav`
- **Audio (weggaan):** `portaal_gesloten.wav`

### Happy Hour — "worden Happy Hour."
- **Tier:** uncommon
- **Uitleg:** 1–3 uren worden goud; een verplaatsing die op zo'n uur eindigt levert dubbele levensuren.
- **Max:** 1
- **Audio (opkomst):** `worden_happy_hour.wav`
- **Audio (weggaan):** `happy_hour_voorbij.wav`

### Ziekte — "worden ziek."
- **Tier:** rare
- **Uitleg:** 1–3 spelers worden ziek; genezen kan enkel via een wettelijke zet op een medicijn-uur, anders sterven ze na `duratie` events.
- **Max:** 1
- **Audio (opkomst):** `worden_ziek.wav`
- **Audio (weggaan):** —

### Tijdbom — "worden een tijdbom."
- **Tier:** rare
- **Uitleg:** 1–3 spelers worden een tikkende bom; ontmantelen via een drukknop-paal (dag 80% / nacht 50%), anders ontploft ze (iedereen op de paal verliest `uur` levensuren).
- **Max:** 1
- **Audio (opkomst):** `worden_een_tijdbom.wav`
- **Audio (weggaan):** —

### Tornado — "worden getroffen door een tornado."
- **Tier:** epic
- **Uitleg:** 1–2 uren worden tornado-center; spelers op de aanliggende uren moeten mee naar het center, anders zijn ze al hun levensuren kwijt (geen sterfte).
- **Max:** 1
- **Audio (opkomst):** `worden_getroffen_door_een_tornado.wav`
- **Audio (weggaan):** —

### Etenstijd — "Een wolf zal jagen op zijn schaapjes."
- **Tier:** epic
- **Uitleg:** Precies één **kleur**-groep wordt schaapjes; een wolf uit de **laagste 5 van het globale klassement** (buiten de groep) steelt levensuren telkens hij op hetzelfde uur eindigt als een schaap — maar **alleen na een legale eigen zet**. Schapen die de 15 rondes ongevangen overleven, krijgen **+5 levensuren**.
- **Max:** 1
- **Audio (opkomst):** `etenstijd.wav` (nog opnemen)
- **Audio (weggaan):** `etenstijd_voorbij.wav` (nog opnemen)

### Tweeling — "2 spelers worden een tweeling."
- **Tier:** epic
- **Uitleg:** Koppelt 2 spelers; je verdient enkel levensuren als je partner deze ronde óók legaal bewoog. Sterft er één, dan sterft de ander mee (behalve bij een nuke). Samen op hetzelfde uur eindigen heft de vloek op.
- **Max:** 4
- **Audio (opkomst):** `tweeling.wav` (nog opnemen)
- **Audio (weggaan):** — (eindigt op een dood, geen afloop-cue)

## Huidige events

### Portalen — "Een portaal opent tussen twee uren."
- **Werking**: kiest 2 willekeurige uren die **minstens 6 uren uit elkaar** liggen (`minAfstand: 6` —
  een sprong van 2 uur is geen sprong) en opent er een portaal tussen. Beide palen
  krijgen een `portaal`-effect (uur-niveau) met een willekeurige duur (`duratie: [3,8]` →
  3–8 events); de centrale LED-node kleurt ze **continu paars**. De twee uren worden aan
  elkaar gekoppeld via `data.partner`.
- **Doelwit**: `type: uur`, `selectie: willekeurig`, `aantal: 2`, `minAfstand: 6`.
- **Max**: `max: 2` — er kunnen **twee** portalen tegelijk open staan. "Kies event" sluit palen uit die
  het `portaal`-effect al dragen, dus twee portalen **delen nooit een paal**.
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
- **Max**: `max: 1` — hooguit één happy-hour-episode tegelijk; de afvuring zelf kleurt meerdere
  uren tegelijk goud (`aantal: "laag"`, dichtheid-geschaald).
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
  - **Geen medicijnen meer, wél zieken (Z9)**: staat er na het medicijn-verbruik géén enkel medicijn meer op
    het bord terwijl er nog zieken zijn, dan is genezing onmogelijk → **alle** zieken sterven onmiddellijk
    (uren 0 + 1 sterfte, ongeacht hun teller), hun tweelingen sterven mee, en de box roept om:
    *"Alle zieken zijn gestorven."* (`events/afgelopen/alle_zieken_gestorven.wav`).
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
  - **Slaagt** → bom verdwijnt, geen gevolgen. De ingedrukte paal flitst **blauw** (actie 16 modus 0,
    `r0 g0 b255`); omdat actie 16 geen firmware-timeout heeft, stuurt Node-RED na ~1 s een `ACTIE_NIETS`
    (actie 0) om de flits te wissen. De box roept: `tijdbom_ontmanteld.wav`.
  - **Mislukt** → **iedere** speler op die paal verliest het aantal levensuren gelijk aan het **uur**
    (mislukking op uur 7 → −7 voor iedereen daar; onder 0 → 0 + sterfte). De bom(men) op die paal zijn verbruikt.
    De paal krijgt een **rode ontploffing + sirene** (actie 24 `ACTIE_ONTPLOFFING`) + `tijdbom_ontploft.wav`.
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

### Etenstijd — "Een wolf zal jagen op zijn schaapjes." (wolf vs. schapen-groep)
- **Afroep (juli 2026):** géén "Groep …"-afroep vooraf. De wolf wordt al in **"Kies doelwit"** gekozen
  (vóór de reveal, opgeslagen in `global.etenstijdWolf`; "Voer gevolg uit" hergebruikt die keuze), en het
  doelwit wordt afgeroepen in de volgorde **"wolf: <naam>"** dan **"schaapjes: groep (kleur)"**
  (`prefix/wolf.wav` + `spelers/<naam>.wav` + `prefix/schaapjes.wav` + `groepen/kleur/kleur_<w>.wav`).
- **Werking**: kiest **precies één groep van het type `kleur`** als **schaapjes** + één **wolf**.
  Zolang de toestand loopt (`duratie: 15`
  rondes) **jaagt** de wolf: telkens hij **bij de controle** op **hetzelfde uur** eindigt als een schaapje,
  **steelt** hij van dat schaap **`min(uur, schaap-levensuren)`** levensuren (op uur 20 → tot 20, maar nooit
  meer dan het schaap heeft). Het **schaap** krijgt **+1 sterfte**, de **wolf** krijgt de buit erbij. Een
  schaap kan **maar één keer** gevangen worden (`gevangen`-lijst). De vangst is zichtbaar in de controle-tabel
  (`… | GEVANGEN DOOR WOLF (-N uur, +1 sterfte)` en `… | WOLF VING k (+N uur)`).
- **De wolf is een underdog (ET1b)**: hij wordt gekozen uit de **laagste 5 van het globale klassement**
  (`globaleStats[n].totaalUren` oplopend; gelijkspel op plek 5 telt mee), onder de actieve spelers **buiten**
  de schapengroep. Binnen die bodem-5 wint de **beste aura** (laagste `auraValsspeel`); gelijkspel →
  willekeurig. Zijn er geen niet-schapen, dan komen alle actieve spelers in aanmerking.
- **De wolf mag niet vrij bewegen (ET2b)**: de vangst telt **alleen** als de wolf zijn eigen zet deze ronde
  legaal speelde (basis-status begint met `OK`) **én** hij niet vrij wandelde. Anders geen vangst en zijn rij
  krijgt `… | WOLF MISTE (illegale zet)` bovenop de gewone `BEWOOG (mocht niet)`. Hij jaagt dus door mee te
  lopen als hij ín de afgeroepen groep zit, of door stil te staan tot een schaap naar hém wordt gestuurd.
  Tijdens een **nuke**-controle vangt hij nooit (de nuke-tak keert vroeg terug).
- **Overlevers-bonus (ET4)**: loopt de episode af, dan krijgt elk schaap dat **nooit** gevangen werd en nog
  actief is **+5 levensuren** ("Verouder effecten"). Bij Stop vervalt de bonus. De wolf houdt zijn buit.
- **Doelwit**: `type: "groep"`, **`veld: "kleur"`** — dus nooit een jaar-/maand-/seizoen-groep, en nooit
  twee groepen tegelijk (de ~15 %-tweede-groep-regel WE3 geldt enkel bij `veld: "willekeurig"`).
  **Gevolg**: `{type:"etenstijd"}`. `tier: epic`, `max: 1`.
- **Toestand**: `global.etenstijd = {wolf, schapen[], gevangen[], over}`; een `wereldEffecten`-effect telt de
  15 rondes af. Bij afloop ("Verouder effecten") en bij Stop/Herstart → `global.etenstijd = null`. De wolf
  staat zichtbaar in de wereld-effecten-tabel (`Etenstijd (wolf: <naam>)`).
- **Audio**: `events/toestanden/etenstijd.wav` (afroep) + `events/afgelopen/etenstijd_voorbij.wav`
  ("de wolf is voldaan", bij afloop) — beide WAV's nog **opnemen**.

### Tweeling — "2 spelers worden een tweeling." (gekoppeld bewegen)
- **Werking (TW2)**: koppelt **2 spelers**. Vanaf dan verdien je **enkel levensuren als je tweeling deze ronde
  óók bewoog én zijn zet legaal was** (basis-status begint met `OK`, geen vrij wandelen). Zo niet, dan wordt
  de zojuist toegekende winst **teruggedraaid** (`… | TWEELING (geen winst: <partner> bewoog niet legaal mee,
  -N uur)`) — ook winst die via een dienaar naar zijn **meester** ging. **Verlies wordt nooit teruggedraaid**
  en er valt **geen sterfte**. De oude "asymmetrisch → beiden alles kwijt"-straf bestaat **niet meer**.
- **Dood-propagatie (TW3)**: krijgt één tweeling een sterfte (beweging, middernacht-oversteek,
  **middernacht-oogst**, **wolf-vangst**, **ziekte-dood**), dan sterft de andere mee (uren 0 + 1 sterfte) en de
  **band verbreekt**. Dit loopt via één gedeelde helper `tweelingDood` in `pi/node-red/settings.js`, zodat de
  oogst en de ziekte-dood exact hetzelfde doen als de controle-tak. Tornado en bom geven **geen** sterfte.
- **Een nuke spaart de band (TW5)**: iedereen ontplofte tegelijk, niemand trok de ander mee. Na de nuke zijn
  de tweelingen nog steeds gekoppeld.
- **De vloek opheffen (TW6)**: eindigen beide tweelingen bij een controle op **hetzelfde uur**, dan breekt de
  band (`… | TWEELING VERBROKEN (samen op uur N)`). **Geen beloning** — en omdat de TW2-clawback eerst draait,
  kost samenkomen meestal een ronde winst. Dat is de bedoelde opoffering.
- **Grenzen**: **max 4** tweelingen tegelijk (`max: 4`, geteld via een niet-verouderend `wereldEffecten`-
  effect per paar). Wie al een tweeling is, kan **geen tweede** tweeling worden (uitgesloten bij de
  doelwitkeuze in "Kies event").
- **Doelwit**: `type: "speler"`, `aantal: 2`. **Gevolg**: `{type:"tweeling"}`. **Geen duratie** (persistent
  tot dood/spel-einde; het paar-effect veroudert niet, zoals medicijn).
- **Toestand**: `global.tweelingen = [{a, b, inst}, …]`. Gereset bij Stop/Herstart. De controle-tabel toont
  `… | TWEELING (geen winst: <partner> bewoog niet legaal mee, -N uur)`, `… | TWEELING STERFT MEE` of
  `… | TWEELING VERBROKEN (samen op uur N)`.
- **Audio**: `events/toestanden/tweeling.wav` (afroep "2 spelers …") — nog **opnemen**. Geen afloop-cue
  (eindigt op een dood, niet op duratie).

### Tempo-events — "Sneller" / "Trager" (wereld)
- **Werking**: `sneller_events` en `trager_events` (wereld, `doelwit: geen`) stappen `global.spelTempoFactor`,
  die de **reactietijd** van elk volgend event schaalt. Sneller: **−0,1** per keer (min **0,6**); trager:
  **+0,1** per keer (max **1,3**). Start 1,0; reset naar 1,0 bij Stop. Bv. een verplaatsing-event (20 s) bij
  tempo 0,8 → 16 s reactietijd. `gevolgen: [{type:"tempo", richting:"sneller"|"trager"}]`.

### Event-tiers (zeldzaamheid → keuze-kans)
- Elk event heeft een **`tier`** met een keuze-**gewicht**: `common` 50 · `uncommon` 25 · `rare` 15 ·
  `epic` 8 · `legendary` 2 (default `common`). De engine kiest events **gewogen** (in "Bouw pof/status"
  voor de wachtrij en in "Kies event" als fallback), zodat ingrijpende events zeldzaam blijven en het spel
  niet wild heen en weer geslingerd wordt. Standaard (zoals in de `[CONFIG]`-injects): verplaatsing-events
  `common`; portalen/happy_hour `uncommon`; ziekte/tijdbom/bomaanslag/max_per_uur/polonaise `rare`;
  sneller/trager/tornado/etenstijd/tweeling/identiteitscrisis/**tijdreizen** `epic`; nuke/onmiddellijke_dood `legendary`. Per event aanpasbaar
  via de **events-tab** in de simulator (`sim/tiers-config` → `global.eventTiers`).

### Slechte aura (geen event — een eigenschap van negatieve events)
- **Werking**: events met `slechteAura: true` (nu **Ziekte** en **Tijdbom**) kiezen hun speler-doelwit
  **gewogen** naar de regio: een kandidaat in de **avond** (uur 20–23 of 1–6) krijgt **×1,10** kans, op
  **middernacht** (uur 24) **×1,15**, overdag (uur 7–19) ×1,00. Zo is het overdag veiliger om te verblijven.
  Aan/uit via de **Spelinstellingen**-tab (`global.badAuraAan`, `sim/spel-config`). Uur-events en `geen`-doelwit
  (Nuke) vallen erbuiten.
- **Valsspeel-aura** (per speler): elke **foute verplaatsing** bij de controle (TE VEEL, TE WEINIG,
  ONGELDIGE KEUZE, TERUG IN TIJD, BEWOOG (mocht niet), ONGELDIGE TELEPORT, MIDDERNACHT DICHT) telt als
  **valsspelen** → `+1 valsspeelpunt` en **+3% slechte-aura** (`spelerStats[n].auraValsspeel`). Dat
  percentage wordt **bovenop** het regiogewicht vermenigvuldigd (`gewicht × (1 + auraValsspeel/100)`), dus
  wie vaker vals speelt wordt **relatief vaker** het doelwit van een slechte-aura-event. De opgebouwde aura
  **reset naar 0** zodra die speler zélf door een slechte-aura-event (Ziekte/Tijdbom) getroffen wordt — de
  schuld is dan "afbetaald". Valsspeelpunten zelf blijven staan en tellen mee in de globale eindstand.

### God-punten (dynamiek — "ongestraft vals spelen")
- **Verdienen (pas NÁ het spel)**: wie zijn **doel** haalde, krijgt bij **Stop** **+2 god-punten** (in
  `transferStats()` in "Spel aan/uit", achter de `godAward`-latch). Tijdens de lopende partij stijgt je saldo
  dus **nooit**: een vers verdiend punt kan geen schild zijn in de partij waarin je je doel haalde — anders
  beloont het eindspel precies de speler die stilstaat. Het saldo `godPunten` is **persistent** over spellen
  heen — je spaart ze op en geeft ze later uit. Reset enkel via de beheer-wis.
- **Gebruiken (automatisch)**: bij een **foute verplaatsing** (dezelfde set als de valsspeel-aura, incl.
  middernacht-oversteek bij dichte poort) **of bij vrij wandelen** (V10) verbruikt de engine **automatisch
  1 god-punt** als het saldo > 0 — hoogstens één punt per controle, ook als beide fouten samen vielen.
  Gevolg: **geen levensuren-verlies, geen sterfte**, de status krijgt `… [GOD-PUNT]`, en het telt **niet**
  als valsspelen (geen valsspeelpunt, geen aura). Zo kun je "ongestraft vals spelen": je verplaatsing geldt
  altijd, je mag middernacht oversteken in gesloten toestand, en een **ziek** persoon mag zo naar een
  medicijn-paal lopen en **genezen**. Heb je geen god-punten meer, dan geldt de normale straf + valsspeel.
- Saldo zichtbaar in de dashboard-tabel "Vals-spelen & God-punten" (huidig spel) en de globale tabel.

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

## Overzicht (gestructureerd)

### Nuke — "Nuke."
- **Tier:** legendary
- **Uitleg:** Aftelklok om weg te lopen; wie bij de controle nog gedetecteerd is verliest alles + 1 sterfte. De arena bouwt op naar een **verblindende ontploffing** (wit → rood-strobe → nagloed). Daarna een regroup-pauze van **30 s**.
- **Max:** 1
- **Audio (opkomst):** `nuke.wav`
- **Audio (weggaan):** —

### Sneller — "events komen sneller."
- **Tier:** epic
- **Uitleg:** Verlaagt `spelTempoFactor` met −0,1 (min 0,6); elk volgend event krijgt een kortere reactietijd.
- **Max:** —
- **Audio (opkomst):** `events_komen_sneller.wav`
- **Audio (weggaan):** —

### Trager — "events komen trager."
- **Tier:** epic
- **Uitleg:** Verhoogt `spelTempoFactor` met +0,1 (max 1,3); elk volgend event krijgt een langere reactietijd.
- **Max:** —
- **Audio (opkomst):** `events_komen_trager.wav`
- **Audio (weggaan):** —

### Bomaanslag — "Een bomaanslag vind plaats op uur a en b."
- **Tier:** rare
- **Uitleg:** Gelokaliseerde bom op één van **vier** vaste uur-duo's (elk 25 %): **9+11**, **4+20**, **6+7**, **6+9**. Wie er bij de controle staat verliest `uur` levensuren (vluchten mag).
- **Max:** —
- **Audio (opkomst):** één clip per duo (`audioVoorOpties`): `een_bomaanslag_vind_plaats_op_uur_9_en_11.wav` (bestaat) + `..._4_en_20.wav`, `..._6_en_7.wav`, `..._6_en_9.wav` (nog opnemen)
- **Audio (weggaan):** —

### Identiteitscrisis — "Alle spelers krijgen een identiteitscrisis."
- **Tier:** epic
- **Uitleg:** Schuift de luisternamen één alfabetische stap door (cyclisch): elke actieve speler luistert nu naar de naam van een andere speler. Duurt **vast 10** rondes.
- **Max:** 1
- **Audio (opkomst):** `identiteitscrisis.wav`
- **Audio (weggaan):** `identiteitscrisis_voorbij.wav`

### Tijdreizen wordt toegestaan — "Tijdreizen zal worden toegestaan."
- **Tier:** epic
- **Uitleg:** Zolang de toestand loopt (10–15 rondes) mag iedereen ook achteruit in de tijd zonder straf (behalve de middernacht-poort).
- **Max:** 1
- **Audio (opkomst):** `tijdreizen.wav` (nog opnemen)
- **Audio (weggaan):** `tijdreizen_voorbij.wav` (nog opnemen)

## Huidige events

### Nuke — "Nuke." (ontploffing + regroup)
- **Werking**: speelt "NUKE" af + een **aftelklok** (`reactietijd_s`, standaard 16 s, aanpasbaar) om
  weg te lopen. Bij de controle ontploft **elke speler die nog gedetecteerd is** (in `spelerLocaties`):
  **levensuren → 0 en +1 sterfte**. Wie **ontkomen** is, overleeft als "VEILIG (ontkomen)".
- **Ontsnappen op hardware** (`escape_s`, default 4 s): `spelerLocaties` wordt normaal nooit opgeschoond,
  dus zonder ingreep zou `loc[naam] != null` altijd waar zijn en kon niemand ontsnappen. **Enkel tijdens
  de nuke** (`nukeActief`, niet in sim) haalt `Evalueer spelstatus` spelers die > `escape_s` niet meer
  **vers gezien** zijn (via `status_lastSeenMac`) uit `spelerLocaties` — vluchters verdwijnen live van de
  radar. Na de nuke stopt de prune (weer accumulerend, normaal). In de **simulator** regelt `Sim directe
  locatie` dit al (prune uitgeschakeld). **Vereiste**: `reactietijd_s ≥ escape_s + 2` (de prune loopt op
  ~1 s-cadans). Zie `docs/invarianten.md §4c` (N1/N7).
- **Doelwit**: `type: geen`. **Gevolg**: `{type:"nuke"}`. `max: 1`.
- **Lichtshow (tijd-gefaseerd)**: de ACTIE_NUKE-animatie loopt in de slave **op zijn eigen klok** vanaf
  `actieStartMs`, in drie fasen: **0–16 s** = opbouwend radioactief geel↔groen (de "ademhaling" versnelt
  naarmate de knal nadert), **16–19 s** = de **ontploffing** (verblindende witte flits → felle wit/rode
  strobe), **≥19 s** = uitdovende rode nagloed. Het geldt voor de hele arena (actie 8), **behalve de
  middernacht-poort-paal** (de hoogste paal) — die houdt zijn poort-status. Om die firmware-klok te laten
  doorlopen en de escalatie zichtbaar te houden zet Node-RED bij het nuke-resolve een **`nukeNaglow`**-vlag
  (i.p.v. de LED's meteen te herbouwen): de ring blijft de hele regroup op actie 8 en wordt **niet
  opnieuw verzonden**; pas aan het **einde** van de regroup wist `Engine tick` de vlag via
  `paalLedForceRebuild`, waarna de palen netjes uitgaan.
- **Regroup**: na de ontploffing een pauze van `regroup_s` s (standaard **30**, aanpasbaar) — de engine
  staat in fase `regroup` en gaat daarna terug naar de normale aanloop; spelers mogen ná de 30 s terug naar
  hun vorige toestand. Geen bewegings-straffen tijdens
  een NUKE (iedereen mág vluchten). **In `regroup` is bewegen niet vrij** (V10): wie terugloopt betaalt bij de
  volgende controle 0 winst + 1 valsspeelpunt. Levensuren kost het niet — hij staat toch op 0. Een nuke
  breekt bovendien **geen tweelingbanden** (TW5) en de wolf vangt niet in een nuke-controle.
- **Wereld-wis**: een nuke ruimt bij de controle ook de lopende **ziekte-episode** (zieke spelers +
  medicijn-uren) en alle **dienaars** op; het veld is daarna schoon. Zieken/medicijnen/dienaars overleven
  dus geen nuke. (De "willekeurige zieken" die je tijdens een spel ziet, komen van het normale
  **ziekte-event**, niet van de nuke.)
- **Simulator**: een "Out"-knop (en het buiten het veld slepen van een bolletje) zet spelers op **"uit"**
  → ze worden niet meer gepubliceerd → veilig. Bij de ontploffing flitst het veld kort rood/geel.

### Bomaanslag — "Een bomaanslag vind plaats op uur a en b." (vier vaste duo's)
- **Werking**: een gelokaliseerde bom op **één van vier** vaste uur-duo's, elk met **25 % kans**:

  | Duo | Verlies |
  |-----|---------|
  | **9 en 11** | −9 / −11 levensuren |
  | **4 en 20** | −4 / −20 levensuren |
  | **6 en 7** | −6 / −7 levensuren |
  | **6 en 9** | −6 / −9 levensuren |

  Tijdens `reactietijd_s` (3 s; op hardware opgetrokken tot de sensing-vloer van ~7 s, SP6) tikken die
  uren rood (LED 13) + zoemer-piep. Bij de controle ontploft de bom: wie **op dat moment** op een doel-uur
  staat verliest **`uur`** levensuren. Vluchten mag (geen bewegingsstraf, zoals NUKE).
- **Doelwit**: `type: "uur"`, **`vastOpties: [[9,11],[4,20],[6,7],[6,9]]`** (uniform getrokken).
  **Gevolg**: `{type:"bom"}`.
- **Lichtshow**: bij de ontploffing een **korte witte flikker** (OOGST-strobe, actie 11) op de twee uren;
  het venster is **~1,2 s** (juli 2026, was 3 s) zodat de LED's **niet de volgende event-ronde in blijven
  branden** — een bom is één event, daarna gaan de LED's uit (via de Sync-rebuild, actie 0).
- **Audio**: **één afroep-clip per duo**, parallel aan `vastOpties` in `audioVoorOpties`. Bestaat al:
  `events/wereld-events/een_bomaanslag_vind_plaats_op_uur_9_en_11.wav`. Nog **opnemen**:
  `..._4_en_20.wav`, `..._6_en_7.wav`, `..._6_en_9.wav`.

### Identiteitscrisis — "Alle spelers krijgen een identiteitscrisis." (luisternamen verschuiven)
- **Werking**: schuift de **luisternamen** (`global.luisterNaam`) één **alfabetische** stap door,
  **cyclisch** over alle actieve (niet-gepauzeerde) spelers: wie alfabetisch als eerste komt, luistert
  voortaan naar de tweede naam, enz., en de laatste naar de eerste. Roept een event of doelwit dus een
  naam af, dan geldt die voor de speler die nú die luisternaam draagt — spelers moeten uitkijken naar
  de naam van iemand anders. De toestand loopt `duratie: 10` rondes (vast, was `[7, 15]`); bij afloop keren de
  luisternamen terug naar normaal.
- **Scope**: de crisis raakt **alleen je luisternaam en je kleur**. Draag je de naam van iemand anders, dan
  hoor je ook bij **zijn kleur-groep**. Je `jaar`, `maand`, `seizoen` en `pariteit` blijven onveranderd van
  jezelf — die groepen zijn dus altijd betrouwbaar.
- **Gelijke voornamen**: de cyclische verschuiving sorteert met `localeCompare(nl-BE)` op de **volledige**
  sleutel in `spelerEigenschappen`, en die bevat de achternaam waar nodig (`"Alix Blond"` < `"Alix Bruin"`,
  `"Marie DM"` < `"Marie Smet"`). Er is dus nooit dubbelzinnigheid.
- **Doelwit**: `type: geen`. **Gevolg**: `{type:"identiteitscrisis"}`. `max: 1`. De verschoven namen
  staan in `global.luisterNaam` (gezet in "Voer gevolg uit", afgeteld via een `wereldEffecten`-effect,
  teruggezet bij het verlopen van de toestand en bij Stop/Herstart).
- **Audio**: `events/wereld-events/identiteitscrisis.wav` (afroep) +
  `events/afgelopen/identiteitscrisis_voorbij.wav` (bij afloop).

### Tijdreizen — "Tijdreizen zal worden toegestaan." (tijdelijke regelwijziging)
- **Werking**: zolang de toestand loopt (`duratie [10, 15]` rondes) mag **iedereen** bij het verplaatsen
  **zowel voor- als achteruit** in de tijd gaan. Een achterwaartse zet (bv. van uur 5 naar uur 2) wordt
  **niet meer bestraft** als "TERUG IN TIJD": de afgelegde stappen tellen mee als **geldige beweging**
  (`stappen = voor + achter`) en moeten nog steeds aan de event-voorwaarde voldoen (min/max/of). Je moet
  dus nog steeds **mogen** bewegen (een niet-doelwit blijft stil staan) en het **aantal stappen moet
  kloppen**.
- **Kies één richting**: **pendelen mag niet**. Bevat je pad zówel voorwaartse als achterwaartse stappen,
  dan is de zet fout → status **`PENDELEN`**, `delta = 0` + valsspeelpunt. Heen én terug lopen om stappen
  te sprokkelen werkt dus niet.
- **Uitzondering — middernacht**: de **poort** mag enkel **mee met de tijd** (voorwaarts) doorkruist
  worden. Een **achterwaartse** middernacht-oversteek (de 1→24-wrap) blijft verboden en wordt nog steeds
  bestraft als "TERUG IN TIJD". Tijdreizen **opent** de middernachtpoort niet — een dichte poort blijft
  een dichte poort (voorwaartse oversteek = `MIDDERNACHT DICHT`).
- **Doelwit**: `type: geen`. **Gevolg**: `{type:"tijdreizen"}`. `max: 1`. Globale vlag
  `global.tijdreizenActief` (gezet in "Voer gevolg uit", afgeteld via een `wereldEffecten`-effect, terug
  op `false` in "Verouder effecten" en bij Stop/Herstart).
- **Audio**: `events/wereld-events/tijdreizen.wav` (afroep) + `events/afgelopen/tijdreizen_voorbij.wav`
  (bij afloop) — beide WAV's moet je nog **opnemen**.

## Nieuwe events & regels (juli 2026)

### Body-swap — "Twee spelers wisselen van plaats." (toestand)
- **Werking**: 2 spelers die **minstens 5 uren uit elkaar** staan zijn doelwit. **Correct** = beide eindigen
  op elkaars **startpaal** (`voorwaarde:"wissel"`, gescoord in `Verifieer beweging`). **Geen beloning**
  (delta 0). Wie **niet correct** wisselt = **foute zet** (`NIET GEWISSELD` → valsspeelpunt + aura,
  in `_VALS_FOUT`).
- **Elke route is legaal**: voor de twee doelwitten telt **enkel de eindpositie**. Ze mogen achteruit lopen,
  de **dichte middernachtpoort** oversteken, en negeren `MIDDERNACHT STIL`, `MAX/UUR`, polonaise en de
  vrij-wandel-straf (V10). Niet-doelwitten worden gewoon normaal gescoord. Zie invarianten BS1–BS3.
- **Config**: `tier:"rare"`, `reactietijd_s:15` + `reactieVast:true` (blijft 15 s ondanks de toestand-basis van
  10 s, item 10), `doelwit:{type:"speler",aantal:2}`, **`minSpelerAfstand:5`**, `gevolgen:[{type:"geen"}]`.
- **LED-cue**: tijdens de reactietijd laat "Doelwit reveal" de **twee (start)palen oranje kaarsvlammen**
  (actie 16 modus 1, `r255 g140 b0`), zodat spelers zien **tussen welke twee palen** de wissel loopt. Bij de
  controle zet "Verifieer beweging" `paalLedForceRebuild` → Sync wist de flikkering weer.
- **Audio**: `body_swap.wav` (afroep) — **nog opnemen**.

### Verplaatsing (iedereen) — "Iedereen maximum 5 uur vooruit." (verplaatsing)
- **Werking**: `doelwit:{type:"speler",selectie:"alle"}`, `voorwaarde:"max"`, `getal:[1,5]` — de engine rolt
  per afvuring een getal **tussen 1 en 5** en roept het af ("Iedereen maximum 3 uur vooruit"). Gewone scoring. **Elk bewogen uur maakt de aura 5% slechter** (`auraPerUur:5` → `auraValsspeel
  += 5 × voor` in `Verifieer beweging`; blijvend). Hoe verder je gaat, hoe vaker je later doelwit wordt van
  slechte-aura-events. `tier:"rare"`.
- **`geenDoelwitAfroep:true` (O1)**: omdat het doelwit **iedereen** is, wordt de afroep **niet** per speler
  opgesomd. (a) Er speelt **geen** getal-prefix `getallen/<N>.wav` (die zou bij 31 spelers naar het
  onbestaande `getallen/31.wav` zoeken) — in plaats daarvan één `prefix/iedereen.wav`; (b) de 31 losse
  naam-clips worden **niet** afgespeeld; (c) de reveal toont één regel **"Iedereen"** i.p.v. 31 namen één
  voor één op te lijsten (dat kostte ~25 s vóór de reactietijd kon starten). Afroep =
  `iedereen.wav → maximum.wav → getallen/<getal>.wav → uur_vooruit.wav`.

### Maximaal aantal per uur — "Vanaf nu maximaal x spelers per uur." (wereld)
- **Werking**: rolt X uit **4–6** (`getal:[4,6]`, = het afroepgetal), zet `global.maxPerUur=X`. Tijdelijk
  (`duratie:[10,15]`). Zolang actief: telt een uur bij de controle **meer dan X** spelers, dan verliest
  **iedereen** op dat uur het **overschot** aan levensuren — bij X=5 en 7 spelers → `7 − 5 = −2` voor elk.
  Onder 0 → 0 + een sterfte (zoals bom/tijdbom); een sterfte laat de eventuele **tweeling mee sterven**.
- **Ook door een tornado (juli 2026):** de straf wordt óók toegepast in de **tornado-tak** — een tornado die
  spelers op één center samenzuigt tot boven X straft dus net zo goed. Logica: helper `_strafOvervol` in
  `Verifieer beweging` (aangeroepen in de normale controle én de tornado-tak); gezet in `Voer gevolg uit`
  (gevolg `max_per_uur`), opgeruimd in `Verouder effecten`. (De oude zachte "geen winst volgende ronde"-vlag
  is vervangen door dit directe verlies.) `tier:"rare"`.
- **Audio**: `max_per_uur.wav` + `max_per_uur_voorbij.wav` — **nog opnemen**.

### Polonaise — "De polonaise begint." (wereld)
- **Werking**: duurt **10 verplaatsings-events** (eigen teller `global.polonaiseTeller`, afgeteld in
  `Verifieer beweging`; niet-verouderend wereld-effect). Bij een **verplaatsings-event**: vertrek met **≥ 3**
  spelers van hetzelfde uur → gewone beweging **+ 1 levensuur per medevertrekker boven 3** (`+ (M − 3)`, dus
  4 vertrekkers = +1).
  Vertrek met **< 3** = **foute zet** (`TE WEINIG SAMEN` → valsspeelpunt + aura). `max:1`, `tier:"rare"`.
- **Audio**: `polonaise.wav` + `polonaise_voorbij.wav` — **nog opnemen** (afloop-cue nog niet gewired).

### De reactietijd wordt Pools — "De reactietijd wordt Pools." (wereld)
- **Werking**: `duratie:7`, `max:1`, `tier:"epic"`, `gevolg:{type:"reactie_pools"}`. Zolang de toestand
  loopt speelt een **Poolse achtergrondsong** (`muziek/reactie_pools.wav`, ~120 s) **tijdens de
  reactietijd** van elk event; hij **pauzeert** wanneer het volgende event begint en **hervat op
  dezelfde positie** bij de volgende reactietijd. Geen spelregel-effect — puur sfeer.
- **Mechaniek**: `Voer gevolg uit` zet `global.poolsActief=true`; de nieuwe node **"Poolse-muziek-tick"**
  (gevoed door de 1 s-ticker) stuurt op de fase-randen `play`/`resume`/`pause` naar het bestuurbare
  audiokanaal **`audio/muziek`** (zie `docs/protocol.md`). Bij het verlopen (`Verouder effecten`) →
  `poolsActief=false` → de tick stuurt `stop`. `resetPartij` wist `poolsActief`/`poolsGestart`.
- **Audio**: `muziek/reactie_pools.wav` (geplaatst). Aparte gesproken afroep (`audioVoor`) nog **op te nemen**.

### Onmiddellijke dood — "Een speler zal onmiddellijk sterven." (wereld, cutscene)
- **Werking**: `fase:"avond"`, `max:1`, `tier:"legendary"`, `gevolg:{type:"onmiddellijke_dood"}`. Kiest een
  slachtoffer via een **loterij** (gewicht = `sterftes + valsspeelpunten`; 0 = immuun) en speelt een
  **cutscene** af (node `dood_anim_fn1`). De engine gaat naar fase **`cutscene`** (Engine tick bevroren →
  geen andere events/verplaatsingen; RF vrij) tot de cutscene klaar is.
- **Cutscene**: de 24 s-track `events/wereld-events/onmiddellijke_dood.wav` start op het kanaal `audio/muziek`;
  tegelijk springt een **rode lamp** (`actie 10`) **paal-per-paal** rond (~6/s, fire-and-forget), landt na
  hele rondes **exact op het uur van het slachtoffer** (14–20 s totaal). Bij de landing wordt de track
  **afgekapt** (`audio/muziek stop`) en **direct** `events/wereld-events/eliminatie.wav` gespeeld; het
  slachtoffer sterft (`totaalUren=0`, `+1 sterfte`, `gestorven=true`) en de engine hervat. De vloeiende
  versie loopt in de simulator via `pof/dood-anim`.
- **Audio**: `onmiddellijke_dood.wav` (24 s cutscene-track, vervangt de afroep → `audioVoor:""`) +
  `eliminatie.wav` (geplaatst).

### Pariteit-verplaatsing — "maximum x uur vooruit." op even/oneven uur (verplaatsing)
- **Werking**: als de bestaande groep-verplaatsing, maar de groep = spelers op een **even** óf **oneven**
  **startuur** (`doelwit:{type:"groep",veld:"pariteit"}`; `Kies event` bepaalt de leden uit `spelerLocaties`,
  niet uit `spelerEigenschappen`). Label/afroep: `uur: even` / `uur: oneven`. Gewone scoring; middernacht-regels
  gelden. `tier:"uncommon"`.
- **LED-cue**: tijdens de afroep pulseert "Doelwit reveal" **alle even (of oneven) palen 2 s wit**
  (actie 16 modus 0), afhankelijk van de gekozen groep (even/oneven); daarna `paalLedForceRebuild` → Sync
  herstelt de echte toestand. Zo zien spelers meteen welke pariteit aan de beurt is.
- **Audio**: `groepen/uur/even.wav` + `groepen/uur/oneven.wav` — **nog opnemen**.

### Twee groepen tegelijk (item 11, geen apart event)
- Bij **elk** groep-event (willekeurig veld) is er **~15% kans** dat er een **tweede** groep uit een (mogelijk)
  ander veld bij komt; het doelwit = de **unie** (bv. "kleur rood **en** jaar eerste"). Overlap telt één keer;
  bij < 4 actieve spelers of een vast veld gebeurt het niet. Alleen `Kies event`/reveal-label/audio wijzigen —
  de scoring behandelt `doelwit` al als platte set.
- **Audio**: `prefix/groepen.wav` (meervoud) + `woorden/en.wav` (verbinding) — **nog opnemen**.

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
