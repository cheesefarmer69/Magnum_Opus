# Plates of Fate — events opstellen

> **Werking & verplaatsingscontrole** (preconditie/effect/invariant, STAP/TELEPORT, scoring):
> zie `docs/spel/event-systeem.md` (leidend). **Per-categorie overzicht** van de events:
> `docs/spel/event-catalogus.md`. Dit document is de technische **schema-referentie** voor het
> opstellen van een event-object. Houd deze drie consistent.

Events worden bewaard in `global.pofEvents`, gevuld door de `[CONFIG] <categorie>-events`
injects in Node-RED flow 06 (zie `pi/node-red/blokken/06_plates_of_fate/README.md`).
Eén event is één object in die array.

## Het standaardformaat

```js
{
  id: "uniek_id",            // VERPLICHT — unieke sleutel (kebab/lowercase)
  naam: "Mooie naam",        // VERPLICHT — weergavenaam voor dashboard en log
  categorie: "toestand",     // VERPLICHT — "verplaatsing" | "toestand" | "wereld" (soort event,
                             //   los van het doelwit: een toestand kan een speler- óf uur-doelwit hebben)
  tekst: "Wat klinkt.",      // VERPLICHT — voorgelezen tekst (naar audio/afspelen)
  reactietijd_s: 15,         // VERPLICHT — seconden reactietijd vóór de controle

  doelwit: {                 // VERPLICHT — wie/wat het event raakt (mag type "geen")
    type: "uur",             //   "speler" | "uur" | "groep" (gedeelde eigenschap) | "geen"
    selectie: "willekeurig", //   "willekeurig" (toeval) | "alle"
    aantal: "laag",          //   "enkel"|"laag"|"midden"|"hoog" of vast getal (niet bij groep)
    // alleen bij type "uur":
    vast: [9, 11],           //   OPTIONEEL — vaste uren (geen keuze)
    vastOpties: [[9,11], [4,20]], // OPTIONEEL — lijst gelijkwaardige uur-duo's; de engine trekt er
                             //   uniform één (wint van 'vast'); zie audioVoorOpties
    // alleen bij type "groep":
    veld: "willekeurig",     //   "kleur"|"jaar"|"maand"|"seizoen"|"pariteit"|"willekeurig"
    waarde: "rood"           //   OPTIONEEL — vaste groep-waarde; weglaten = willekeurig gekozen
  },

  getal: "midden",           // OPTIONEEL — rolt een getal en vult elke 'x' in de tekst in
  getal2: [4, 6],            // OPTIONEEL — tweede getal (vult elke 'y' in de tekst); optie of [min,max]
  voorwaarde: "min",         // OPTIONEEL — "min" | "max" | "of" | "geen" (beweging-controle)
  max: 1,                    // OPTIONEEL — max. aantal tegelijk actieve instanties (toestand)
  tier: "rare",              // OPTIONEEL — zeldzaamheid → keuze-gewicht: common 50 / uncommon 25 /
                             //   rare 15 / epic 8 / legendary 2 (default common). Per event aanpasbaar
                             //   in de simulator-events-tab (sim/tiers-config → global.eventTiers).
  minAfstand: 3,             // OPTIONEEL — minimale RING-afstand tussen gekozen uur-doelwitten
                             //   (bv. tornado: 3 zodat de centers + buururen nooit overlappen)
  minSpelerAfstand: 5,       // OPTIONEEL — minimale RING-afstand tussen twee gekozen SPELERS
                             //   (enkel bij aantal 2; bv. body-swap: 5 uren uit elkaar)
  slechteAura: true,         // OPTIONEEL — "slechte aura": negatief speler-event dat 's avonds (uur 20–6,
                             //   ×1,10) en op middernacht (uur 24, ×1,15) vaker een doelwit kiest, zodat de
                             //   dag veiliger is. Werkt enkel als de spelinstelling badAuraAan aan staat.
  exclusiefGroep: "speler-toestand", // OPTIONEEL — speler-toestand-groep; spelers in dezelfde groep
                             //   (bv. ziekte ⇄ tijdbom) krijgen dit event niet samen, tenzij de
                             //   systeeminstelling "toestand-exclusiviteit" uit staat (global.toestandExclusief)
  duratie: [2, 4],           // OPTIONEEL — hoelang de toestand blijft: getal | [min,max] | "kort"/"middel"/"lang"
  audioVoor: "id_voor.wav",  // OPTIONEEL — WAV vóór het getal in de afroep
  audioVoorOpties: [...],    // OPTIONEEL — één WAV per doelwit.vastOpties-entry (zelfde index)
  audioNa: "id_na.wav",      // OPTIONEEL — WAV ná het getal in de afroep
  audioAfgelopen: "id_afgelopen.wav", // OPTIONEEL — WAV (uit events/afgelopen/) die speelt zodra de
                             //   toestand/duratie verloopt, net vóór het volgende event → spelers
                             //   horen dat het effect niet meer geldt

  gevolgen: [                // VERPLICHT — array van één of meer gevolgen
    { type: "effect", niveau: "uur", effect: "portaal", data: {} }
  ]
}
```

### Veldreferentie

| Veld            | Verplicht | Wat het is / wat je invult |
|-----------------|-----------|-----------------------------|
| `id`            | ja        | Unieke sleutel (kebab/lowercase). |
| `naam`          | ja        | Weergavenaam (dashboard "Huidig event", debug, log). |
| `categorie`     | ja        | `verplaatsing` / `toestand` / `wereld` — soort event (los van `doelwit.type`); bepaalt de `[CONFIG]`-inject. |
| `tekst`         | ja        | Wat wordt voorgelezen. Een losse `x` wordt door `getal` vervangen. |
| `reactietijd_s` | ja        | Seconden reactietijd ná het voorlezen, vóór de controle. **Conventie**: verplaatsing-events standaard **20 s** (denktijd voor de route); toestand-events die onmiddellijk iets zetten (portaal/happy hour/ziekte/tijdbom) **10 s**, live overschrijfbaar via `global.reactieToestand` tenzij het event `reactieVast: true` zet. Op hardware klemt de **sensing-vloer** (SP6) alles op ~7 s. |
| `doelwit`       | ja        | Wie/wat geraakt wordt — object met `type`, `selectie`, `aantal`. Bij `type: "uur"` optioneel `vast` (vaste uren) of `vastOpties` (lijst gelijkwaardige uur-verzamelingen, uniform getrokken — wint van `vast`). Bij `type: "groep"` ook `veld` (`kleur`/`jaar`/`maand`/`seizoen`/`pariteit`/`willekeurig`) en optioneel `waarde` (vaste groepwaarde; weglaten = willekeurig). Enkel `veld: "willekeurig"` kan ~15 % v/d tijd een **tweede** groep toevoegen. |
| `getal`         | nee       | Optie/getal dat `x` in de tekst invult én de controle-waarde bepaalt. |
| `getal2`        | nee       | Tweede optie/getal dat `y` in de tekst invult (bv. de tweede keuze bij `voorwaarde: "of"`). Mag een optie (`laag`/…) of een `[min,max]`-bereik zijn. |
| `voorwaarde`    | nee       | `min` / `max` / `of` / `geen` — beweging-controle na de reactietijd. Bij `of`: geldig als `voor` exact `x` óf exact `y` is. |
| `max`           | nee       | Hoeveel instanties van dít event tegelijk actief mogen zijn (toestand). |
| `duratie`       | nee       | Hoelang de toestand blijft (events/rondes): vast getal, `[min,max]`-bereik (willekeurig), of preset `kort`/`middel`/`lang`. Overschrijft per-gevolg `duurRondes`. Bij `ziekte`: aantal events dat een zieke heeft. |
| `fase`          | nee       | `middag` (default) / `avond` / `beide` — in welk spel het event verschijnt. In `avondModus` toont/kiest de engine enkel `avond`/`beide`; anders verdwijnt `avond`. Zie `docs/spel/avondspel.md`. |
| `minSpelerAfstand` | nee    | Minimale RING-afstand tussen twee gekozen **spelers** (enkel bij `aantal: 2`). Body-swap gebruikt **5**. Geen geldig paar → gewone steekproef + `node.warn`. |
| `minAfstand`    | nee       | Minimale RING-afstand tussen gekozen **uur**-doelwitten. Tornado gebruikt **3**, portalen **6**. Bij een uur-effect worden bovendien palen uitgesloten die dat effect al dragen (twee portalen delen nooit een paal). |
| `regroup_s`     | nee       | Enkel bij het NUKE-event: seconden regroup-pauze ná de ontploffing vóór het spel verdergaat (default **45**). Ook in `regroup` is bewegen **niet** vrij (V10) — wie terugloopt, betaalt bij de volgende controle. |
| `escape_s`      | nee       | Enkel bij het NUKE-event: hoe lang (s) een beacon **niet meer vers gezien** mag zijn om als **ontsnapt** te gelden (default 4). Zorg dat `reactietijd_s ≥ escape_s + 2` — anders kan de ~1 s-prune niet op tijd opschonen. |
| `audioVoor` / `audioNa` | nee | WAV-bestandsnamen voor de afroep (knip-en-plak rond het getal). |
| `audioVoorOpties` | nee  | Eén WAV per `doelwit.vastOpties`-entry (**zelfde index**, even lang). De engine speelt de clip van de gekozen optie i.p.v. `audioVoor`. |
| `gevolgen`      | ja        | Eén of meer gevolgen (`commando` / `score` / `effect` / `geen`). |

---

## Uitleg per veld

### `categorie`

Bepaalt het soort event én in welke config-inject het hoort:

- **`verplaatsing`** — een verplaatsing-event: gekozen spelers moeten binnen de reactietijd aan
  een beweging-`voorwaarde` voldoen. Meestal `gevolgen: [{type:"geen"}]`.
- **`toestand`** — kent iets toe aan een speler of uur (een blijvend `effect`, een `score`,
  of een `commando`). Bv. portaal of happy hour.
- **`wereld`** — verandert iets voor het hele spel (meestal een wereld-`effect`).

### `doelwit` — wie/wat wordt geraakt

| `type`   | Betekenis | `selectie` | `aantal` |
|----------|-----------|------------|----------|
| `speler` | kiest spelers | `willekeurig` / `alle` | aantal spelers |
| `uur`    | kiest palen/uren | `willekeurig` / `alle` | aantal uren |
| `groep`  | kiest alle spelers met een gedeelde eigenschap | `willekeurig` (+ `veld`, optioneel `waarde`) | — |
| `geen`   | raakt niemand specifiek (wereld-events) | — | — |

- **`willekeurig`** — een steekproef van `aantal` spelers/uren (zonder terugleggen).
- **`alle`** — alle actieve spelers / alle actieve palen.
- **Actieve spelers** = enkel spelers met een bekende positie (`spelerLocaties`) en niet
  gepauzeerd. **Actieve palen** = `palenActief` (in simulatiemodus 1..24, anders `paaltjesLijst`).

#### Groep-doelwit (`type: "groep"`)

Voor minder exclusieve events bij veel spelers richt een groep-event zich op **alle actieve
spelers die een eigenschap delen**:

- **`veld`** — de eigenschap-kolom om op te groeperen: `"kleur"`, `"jaar"`, `"maand"`, `"seizoen"`
  (zie `docs/spel/spelers.md`) of het virtuele `"pariteit"` (even/oneven **startuur**, uit `spelerLocaties`).
  Met `"willekeurig"` (of weglaten) kiest de engine **per afvuring** willekeurig uit de vier
  eigenschap-velden — en enkel dán is er ~15 % kans op een **tweede** groep erbij (WE3).
- **`selectie: "willekeurig"`** — de engine kiest één **willekeurige waarde** die onder de actieve
  spelers voorkomt (bv. `kleur` → `rood`/`zwart`/`blauw`). Met optioneel **`waarde`** (bv. `"rood"`)
  zet je de groep vast.
- Het **doelwit** = alle actieve spelers met die waarde; zij worden allemaal als doelwit
  gecontroleerd (niet-leden moeten stil blijven).
- **Afroep**: de prefix is **"een groep"** (i.p.v. een aantal), gevolgd door de event-tekst en het
  groep-label **`veld: waarde`** (bv. "een groep … maximum 3 uur vooruit. kleur: rood"). De
  individuele leden worden **niet** opgesomd.
- Eigenschappen komen uit `global.spelerEigenschappen` (`{ naam: { kleur, jaar, maand, seizoen } }`),
  gevuld door de `[CONFIG] Speler-eigenschappen`-inject.
- **Identiteitscrisis**: zolang die loopt bepaalt je **luisternaam** je `kleur`; `jaar`, `maand`,
  `seizoen` en `pariteit` blijven van jezelf. Zie `event-catalogus.md`.

### Opties `enkel` / `laag` / `midden` / `hoog`

Zowel `getal` als `doelwit.aantal` mogen een optie zijn, maar ze worden **verschillend** verwerkt:

**`getal` (het getal in de tekst)** — vast bereik, gerold met `rol()`:

| Optie    | Bereik (`getal`) |
|----------|------------------|
| `enkel`  | 1     |
| `laag`   | 1 – 3 |
| `midden` | 1 – 6 |
| `hoog`   | 3 – 10 |

**`doelwit.aantal` (aantal doelwitten)** — groeit **sub-lineair** met het veld (doelwit-dichtheid, G3).
Het aantal schaalt met **√N** (N = actieve, niet-gepauzeerde spelers), gestuurd door de globale knob
`global.doelwitDichtheid` (default **0,25** = neutraal, dashboard-instelbaar — Bediening → "Spelbalans"):

```
aantal = clamp( round( mult × √N × (dichtheid / 0,25) ), 1, min(N, 6) )
```

| Optie    | `mult` | N = 8 | N = 16 | N = 24 | N = 31 |
|----------|--------|-------|--------|--------|--------|
| `enkel`  | —      | 1 | 1 | 1 | 1 |
| `laag`   | 0,35   | 1 | 1 | 2 | **2** |
| `midden` | 0,55   | 2 | 2 | 3 | **3** |
| `hoog`   | 0,90   | 3 | 4 | 4 | **5** |

**Jitter (O5):** ná de √N-formule en **vóór** de clamp krijgt het aantal een **±1-stap**: **25 %** kans −1,
**25 %** kans +1, **50 %** ongewijzigd. De getallen in de tabel zijn dus **richtwaarden** — bij 31 spelers
varieert `laag`/`midden`/`hoog` rond 2/3/5 (met jitter ≈ 1–3 / 2–4 / 4–6) i.p.v. elke keer exact hetzelfde.
De formule zelf en de knob blijven ongewijzigd.

Waarom sub-lineair: een lineaire fractie van N liet het veld **verzadigen** — bij 31 spelers raakte
`laag` er al 5 en kleurde happy hour 5 van de 24 uren goud (21 % van de ring). Met √N groeit het aantal
nog steeds mee, maar vlakt het af, zodat een toestand-event zeldzaam blijft aanvoelen. De harde cap
is **6**. **Uitzonderingen (ongewijzigd):** een **vast getal** (`portalen` 2, `tweeling` 2, `bodyswap` 2),
een `[min,max]`-**array** (`tornado` `[1,2]`, via `rol()`), `vast`/`vastOpties` (`bomaanslag`) en
`selectie:"alle"` schalen **niet**. **Groep-events** hebben helemaal geen `aantal` (de omvang is emergent)
en worden bovendien **zwaarder gewogen** bij veel spelers (vanaf N > 15).

### Getallen in de tekst (`getal` → `x`)

Zet je `getal`, dan rolt de engine één getal en vervangt elke losse `x` in `tekst` erdoor
(eenmalig, net vóór dit event). Voorbeeld: `tekst: "Minimum x uur vooruit."` met
`getal:"midden"` wordt bv. "Minimum 5 uur vooruit." Datzelfde getal is meteen de waarde
waartegen de `voorwaarde` controleert.

### Beweging-controle (`voorwaarde`) en scoring

Levensuren worden **pas bij de controle** toegekend (niet live), op basis van het **opgenomen
pad** (geordende STAP/TELEPORT-acties), **niet** een netto begin/eind-vergelijking. Een STAP
telt 1 (nooit achteruit); een TELEPORT (sprong tussen twee actieve portaal-palen) telt 0 en
wordt niet op richting gecontroleerd. Bij een verplaatsing-event (`voorwaarde` min/max) mag enkel
het doelwit bewegen; anderen moeten stil blijven. `voor` = aantal STAP vooruit, `x` = `getal`.
Δ = de toegekende/afgetrokken levensuren:

| Geval | Status | Δ levensuren |
|-------|--------|--------------|
| doelwit `max`, `voor ≤ x` (geldig) | OK | **+voor** (×2 op happy-hour-eindpaal) |
| doelwit `max`, `voor > x` | TE VEEL | **max(0, x − (voor − x))** |
| doelwit `min`, `voor < x` | TE WEINIG | **max(0, voor − (x − voor))** |
| doelwit `of`, `voor ≠ x` én `voor ≠ y` | ONGELDIGE KEUZE | **max(0, voor − afstand tot dichtste geldige)** |
| doelwit, achterwaartse STAP | TERUG IN TIJD | **max(0, voor − achter)** |
| doelwit, >1× zelfde portaal | ONGELDIGE TELEPORT | **0** |
| niet-doelwit dat beweegt | BEWOOG (mocht niet) | **0** |
| gepauzeerde speler | GEPAUZEERD | 0 (niet gescoord) |
| stil blijven staan | OK (stil) | 0 |

> **Proportioneel model (V11, juli 2026):** valsspelen kost **geen** levensuren meer; je **verdient minder** naarmate je verder afwijkt, met vloer **0** (`Δ = max(0, legaalBasis − overtreding)`). Bv. max 5, bewoog 8 → +2; bewoog 12 → +0. Nooit negatief, dus **geen sterfte door valsspel**. Dodelijke straffen (middernacht/nuke/tornado/bom/ziekte) staan los. Zie `docs/invarianten.md` §2.

Een foute bewegingszet brengt een speler **nooit onder 0** (Δ ≥ 0). De aparte dodelijke mechanismen kunnen dat wél: dan blijft hij op 0 met **+1 sterfte** (hij speelt door).
Doordat de TELEPORT niet als STAP telt en niet op richting wordt gecontroleerd, geeft een legale
portaal-sprong (ook naar een lager uur) géén "TERUG IN TIJD". Volledige uitleg: `docs/spel/event-systeem.md`.

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
| `ziekte`   | —                                         | Maakt de doelwit-spelers **ziek** (`duratie` events) en plaatst **medicijn** op evenveel vrije uren. Zie hieronder. |
| `nuke`     | —                                         | Wereld-event: na de aftelklok (`reactietijd_s`) ontploft iedereen die nog **gedetecteerd** is (uren 0 + sterfte); wie zijn beacon > `escape_s` (default 4 s) niet meer laat zien is **ontsnapt** (VEILIG). Daarna een **regroup**-pauze van `regroup_s` s. Zie hieronder. |
| `tijdbom`  | —                                         | Maakt de doelwit-spelers een **tikkende tijdbom** (`duratie` events) + kiest evenveel **ontmantel-palen** (palen met een drukknop). Ontmantelen = de knop op zo'n paal indrukken (dag 80% / nacht 50%). Mislukken of ontploffen → iedereen op die paal verliest `uur` levensuren. Zie hieronder. |
| `tornado`  | —                                         | **Tornado** op de doelwit-uren (1–2 centers, `minAfstand` houdt ze uit elkaar): spelers op de twee **aanliggende** uren worden naar het center gezogen. Wie niet meebeweegt → **alle** levensuren kwijt (geen sterfte). Zie hieronder. |
| `bom`      | —                                         | **Bomaanslag** op de doelwit-uren: tijdens `reactietijd_s` een waarschuwing (rode tik-LED + zoemer) op die uren; bij de controle ontploft de bom — wie **dan** op een doel-uur staat verliest `uur` levensuren (vluchten mag, geen bewegingsstraf). Witte flikker (OOGST-strobe). De afroep-clip komt uit `audioVoorOpties` van de gekozen optie. Gebruik `doelwit:{type:"uur","vastOpties":[[9,11],[4,20],[6,7],[6,9]]}` → elk duo **25 %**. |
| `tempo`    | `richting: "sneller"\|"trager"`           | Wereld-event: schaalt het **spel-tempo** (`global.spelTempoFactor`) dat de reactietijd van volgende events vermenigvuldigt. `sneller` −0,1 (min **0,6**), `trager` +0,1 (max **1,3**). Start 1,0; reset naar 1,0 bij Stop. |
| `onmiddellijke_dood` | —                             | **Avondspel-gimmick.** Loot een slachtoffer (gewicht = `sterftes + valsspeelpunten` per niet-gestorven speler) via een cirkelende paarsrode ring-animatie; zet het op 0 + sterfte + `gestorven`. Enkel zinvol met `fase:"avond"`. Zie `docs/spel/avondspel.md`. |
| `geen`     | —                                         | Geen neveneffect. Gebruik dit voor pure beweging-opdrachten (met `voorwaarde`). |

`actie`-waarden (commando): `0` uit · `1` portaal (paars) · `2` happy hour (goud) ·
`3` buzzer-piep · `4` medicijn (felroze) · `5/6/7` ziekte-waarschuwing 3/2/1 hartslagen
(zie `docs/protocol.md`).

### Ziekte-gevolg (`type: "ziekte"`)

Een `ziekte`-gevolg start een **ziekte-episode**:
- De doelwit-spelers worden **ziek** voor `duratie` events (standaard 10).
- Evenveel **vrije uren** (palen zonder actief uur-effect) krijgen een `medicijn`-effect → **felroze** LED.
- Een zieke speler doorloopt de **normale** verplaatsingscontrole (geen vrijstelling): hij verdient
  **geen** levensuren, maar **verliest** ze bij onwettige zetten. Hij geneest **enkel** als hij via een
  **wettelijke** zet (OK / OK (stil)) op een medicijn-uur eindigt — gewoon naar een medicijn wandelen
  terwijl je niet mocht bewegen geneest **niet** (en geeft "BEWOOG (mocht niet)").
  Komen twee zieken samen op één medicijn-uur, dan verdwijnt dat
  medicijn. Na `duratie` events zonder medicijn **sterft** de speler (levensuren → 0, +1 sterfte).
  Vanaf nog 3 events te gaan klinkt elke ronde een hartslag-waarschuwing op zijn uur (3/2/1 slagen).
  Volledige lifecycle: `docs/spel/event-catalogus.md` en `docs/spel/event-systeem.md`.

> **LED-toestanden hoef je normaal niet met een `commando` te zetten.** De centrale node
> "Sync toestanden + LEDs" leidt de LED-kleur rechtstreeks af uit het actieve `effect`
> (`portaal` → paars, `happy_hour` → goud) en zet de LED ook weer uit als het effect
> afloopt. Een `commando`-gevolg is dus enkel voor losse, eenmalige LED/zoemer-acties.

### Blijvende effecten (`type: "effect"`)

Een blijvend effect blijft een aantal rondes actief (één ronde = één event) en loopt
daarna vanzelf af. De duur komt bij voorkeur van het **event-veld `duratie`** (getal,
`[min,max]`-bereik, of preset `kort` 2–4 / `middel` 4–7 / `lang` 7–12); zonder `duratie`
valt de engine terug op een per-gevolg `duurRondes`. Opslag in één van drie registers:

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

---

## Stap voor stap een nieuw event

1. **Kies de categorie** (`verplaatsing` / `toestand` / `wereld`) — bepaalt de config-inject.
2. **Schrijf `naam` en `tekst`** — de tekst is wat de spelers horen.
3. **Bepaal het doelwit** — `type` + `selectie` (`willekeurig`/`alle`) + `aantal`.
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
5. **Settle-grace** — korte `grace`-fase (`pofSettleGrace`, default 3 s; 0 = uit) zodat trage
   paalwissels nog settelen vóór de controle. Zie `docs/spel/event-systeem.md §4`.
6. **Controle** — als `voorwaarde` gezet is, wordt elke speler gecontroleerd.

## Voorbeelden

> **Verplaatsing = alleen groepen.** Verplaatsing-events hebben altijd `doelwit.type: "groep"`.
> De oude individuele speler-doelwit varianten (`verplaatsing2` en `of_verplaatsing`) zijn
> **verwijderd**; enkel de twee groep-events hieronder blijven.

### Groep-verplaatsing-event (doelwit = groep, max + getal + controle)

```js
{ id:"groep_verplaatsing", naam:"Groep-verplaatsing", categorie:"verplaatsing",
  tekst:"maximum x uur vooruit.", reactietijd_s:20,
  doelwit:{ type:"groep", veld:"willekeurig", selectie:"willekeurig" },
  getal:"midden", voorwaarde:"max",
  audioVoor:"groep_verplaatsing_voor.wav", audioNa:"groep_verplaatsing_na.wav",
  gevolgen:[{ type:"geen" }] }
```
Kiest één willekeurige groep onder de actieve spelers en richt zich op **alle** spelers in die
groep: zij mogen **hoogstens** `x` STAPpen vooruit (minder mag, achteruit niet; een portaal-sprong
telt 0). Elk teveel is **TE VEEL** → **max(0, x − (voor − x))** (proportioneel, vloer 0); niet-leden moeten stil blijven. Met
`veld:"willekeurig"` kiest de engine per afvuring een van **kleur / jaar / maand / seizoen**; afroep bv.
"een groep … maximum 3 uur vooruit. kleur: rood" of "… seizoen: winter". Vastzetten kan met
`veld:"kleur"`, `"jaar"`, `"maand"`, `"seizoen"` of `"pariteit"`.

### Groep-of-verplaatsing-event (doelwit = groep, keuze tussen twee getallen)

```js
{ id:"groep_of_verplaatsing", naam:"Groep-of-verplaatsing", categorie:"verplaatsing",
  tekst:"x of y uur vooruit.", reactietijd_s:20,
  doelwit:{ type:"groep", veld:"willekeurig", selectie:"willekeurig" },
  getal:"laag", getal2:[4,6], voorwaarde:"of",
  audioVoor:"groep_of_verplaatsing_voor.wav", audioNa:"groep_of_verplaatsing_na.wav",
  gevolgen:[{ type:"geen" }] }
```
Kiest één willekeurige groep (kleur/jaar/maand/seizoen) en richt zich op **alle** spelers in die groep: zij
moeten **exact `x` óf exact `y`** STAPpen vooruit zetten. `x` rolt uit `getal:"laag"` (1–3), `y` uit
`getal2:[4,6]` — het `[min,max]`-bereik houdt `y` gegarandeerd boven `x`. Elk ander aantal (geen
achterstap) is **ONGELDIGE KEUZE** → **−voor**; niet-leden moeten stil blijven. Een portaal-sprong
telt 0 STAPpen.

### Toestand-event (portaal: blijvend effect, max, paar-koppeling)

```js
{ id:"portalen", naam:"Portalen", categorie:"toestand",
  tekst:"Een portaal opent tussen twee uren.", reactietijd_s:5, max:1, duratie:[3,8],
  doelwit:{ type:"uur", selectie:"willekeurig", aantal:2 },
  audioVoor:"portalen_voor.wav", audioNa:"portalen_na.wav",
  gevolgen:[ { type:"effect", niveau:"uur", effect:"portaal", data:{} } ] }
```
Kiest 2 willekeurige uren; elk krijgt een `portaal`-effect met `data.partner` = het andere
uur. De LED's worden paars via de centrale LED-node (geen `commando` nodig). `max: 1` houdt
het bij één portaal tegelijk. Een sprong tussen de twee portaal-uren geeft **0 levensuren**
(zie `docs/spel/spel.md`).

### Toestand-event (happy hour: ×2-scoring)

```js
{ id:"happy_hour", naam:"Happy Hour", categorie:"toestand",
  tekst:"worden Happy Hour.", reactietijd_s:5, max:1, duratie:[3,6],
  doelwit:{ type:"uur", selectie:"willekeurig", aantal:"laag" },
  audioVoor:"happy_hour_voor.wav", audioNa:"happy_hour_na.wav",
  gevolgen:[ { type:"effect", niveau:"uur", effect:"happy_hour", data:{} } ] }
```
Kiest 1–3 willekeurige uren (afroep: "3 uren worden Happy Hour"). Die uren worden goud.
Eindigt een speler een verplaatsing op een happy-hour-uur, dan tellen de verdiende
levensuren dubbel (zie `docs/spel/spel.md`). `max: 1` houdt het bij één happy-hour-**episode**
tegelijk — die ene afvuring kleurt wel meerdere uren tegelijk goud (`aantal`, dichtheid-geschaald).

### Toestand-event (score)

```js
{ id:"kosmische_gift", naam:"Kosmische gift", categorie:"toestand",
  tekst:"Een gulle ster schenkt een willekeurige speler een gift.",
  reactietijd_s:5,
  doelwit:{ type:"speler", selectie:"willekeurig", aantal:"enkel" },
  gevolgen:[ { type:"score", delta:3 } ] }
```

### Toestand-event (ziekte: zieke spelers + medicijn-palen)

```js
{ id:"ziekte", naam:"Ziekte", categorie:"toestand",
  tekst:"worden ziek.", reactietijd_s:5, max:1, duratie:10,
  doelwit:{ type:"speler", selectie:"willekeurig", aantal:"laag" },
  audioVoor:"ziekte_voor.wav", audioNa:"ziekte_na.wav",
  gevolgen:[ { type:"ziekte" } ] }
```
Kiest 1–3 spelers (afroep: "3 spelers worden ziek") en maakt ze ziek; evenveel vrije uren worden
felroze (medicijn). De zieken hebben `duratie` (10) events om een medicijn-uur te bereiken, anders
sterven ze. `max: 1` houdt het bij één ziekte-episode tegelijk (de medicijnen blijven actief tot de
episode voorbij is). Volledige uitleg: `docs/spel/event-catalogus.md`.

### Toestand-event (tijdbom: bom-spelers + ontmantel-palen via drukknop)

```js
{ id:"tijdbom_speler", naam:"Tijdbom", categorie:"toestand",
  tekst:"worden een tijdbom.", reactietijd_s:5, max:1, duratie:10,
  doelwit:{ type:"speler", selectie:"willekeurig", aantal:"laag" },
  exclusiefGroep:"speler-toestand",
  audioVoor:"tijdbom_voor.wav", audioNa:"tijdbom_na.wav",
  gevolgen:[ { type:"tijdbom" } ] }
```
Kiest 1–3 spelers (afroep: "3 spelers worden een tijdbom") en maakt ze een tikkende tijdbom met
`duratie` (10) events op de klok. Het event kiest evenveel **ontmantel-palen** als bommen uit de
palen met een **drukknop** (`config/drukknoppen`); die palen knipperen rood (actie 13). Een bom-speler
ontmantelt door op zo'n paal de **knop** in te drukken: in de **dag** (uren 7–18) lukt dat met **80%**,
in de **nacht** (19–6) met **50%**. Lukt het → bom weg, geen gevolgen. Mislukt het, óf loopt de klok af
(ontploffing) → **iedere** speler op die paal verliest het aantal levensuren gelijk aan het **uur** waar
ze staan (uur 7 → −7). `exclusiefGroep:"speler-toestand"` houdt tijdbom en ziekte van dezelfde speler
weg (tenzij uitgezet in Systeeminstellingen). Volledige uitleg: `docs/spel/event-catalogus.md`.

### Toestand-event (tornado: zuigt aanliggende uren naar het midden)

```js
{ id:"tornado", naam:"Tornado", categorie:"toestand",
  tekst:"worden door een tornado getroffen.", reactietijd_s:20, max:1, duratie:1,
  doelwit:{ type:"uur", selectie:"willekeurig", aantal:[1,2] },
  minAfstand:3,
  audioVoor:"tornado_voor.wav", audioNa:"tornado_na.wav",
  gevolgen:[ { type:"tornado" } ] }
```
Kiest 1–2 uren als **tornado-center** (donkergrijze LED); `minAfstand:3` houdt twee tornado's volledig
gescheiden. De twee **aanliggende** uren pulsen traag grijs en worden naar het center gezogen: spelers
daar **moeten** binnen de reactietijd naar het center bewegen, anders zijn ze **al** hun levensuren kwijt
(geen sterfte). De tornado mag de LED van een onderliggend effect tijdelijk overschrijven; na het event
(`duratie:1`, opgeruimd bij de controle) keren de LED's terug. Volledige uitleg: `docs/spel/event-catalogus.md`.

### Wereld-event (tempo: spel sneller / trager)

```js
{ id:"sneller_events", naam:"Sneller", categorie:"wereld",
  tekst:"De tijd versnelt: events komen sneller.", reactietijd_s:5,
  doelwit:{ type:"geen" }, audioVoor:"sneller_events.wav",
  gevolgen:[ { type:"tempo", richting:"sneller" } ] }
```
Stapt `global.spelTempoFactor` met **−0,1** (min **0,6**); het analoge `trager_events` (`richting:"trager"`)
met **+0,1** (max **1,3**). Die factor vermenigvuldigt de reactietijd van elk volgend event (verplaatsing 20
→ ×0,9 = 18 s, enz.), bovenop de test-`tempo` uit de Systeeminstellingen. Reset naar 1,0 bij Stop.

### Wereld-event (blijvend effect)

```js
{ id:"tijdslot", naam:"Tijdslot", categorie:"wereld",
  tekst:"De tijd verstrakt: events komen sneller.", reactietijd_s:20,
  doelwit:{ type:"geen" },
  gevolgen:[ { type:"effect", niveau:"wereld", effect:"events_sneller", duurRondes:3, data:{} } ] }
```

> **Heeft een wereld-event een doelwit nodig?** Het `doelwit`-object is in het schema **verplicht**,
> maar bij `categorie:"wereld"` zet je `type:"geen"` (raakt niemand specifiek). Een wereld-event geldt
> voor het hele spel via zijn **gevolg**, niet via doelwit-selectie — er worden dus geen spelers/uren
> gekozen of afgeroepen.

### Wereld-event (NUKE: ontploffing + regroup)

```js
{ id:"nuke", naam:"Nuke", categorie:"wereld", tekst:"Nuke.",
  reactietijd_s:16, escape_s:4, regroup_s:60, max:1, doelwit:{ type:"geen" },
  audioVoor:"nuke.wav", gevolgen:[ { type:"nuke" } ] }
```
Speelt "NUKE" af + een **aftelklok** van `reactietijd_s` (16 s, aanpasbaar) om weg te lopen. Bij de
controle ontploft **elke speler die nog gedetecteerd is** (in `spelerLocaties`): levensuren → 0 **en
+1 sterfte**. Wie **ontkomen** is, overleeft. Daarna een **regroup**-pauze van `regroup_s` s (60,
aanpasbaar) vóór het spel verdergaat. `regroup_s` en `escape_s` zijn optionele velden; de fase heet
`regroup` in `pof/status`.

> **Ontsnappen op hardware (`escape_s`).** `spelerLocaties` wordt normaal nooit opgeschoond (een beacon
> die stilvalt blijft als ghost staan), dus zonder ingreep zou `loc[naam] != null` altijd waar zijn en
> kon niemand ontsnappen. **Enkel tijdens een nuke** (`nukeActief`, niet in sim) haalt `Evalueer
> spelstatus` spelers die > `escape_s` niet meer **vers gezien** zijn (via `status_lastSeenMac`) uit
> `spelerLocaties` — je ziet vluchters live van de radar verdwijnen. Bij de controle geldt zo'n speler dan
> als **VEILIG (ontkomen)**. Na de nuke stopt de prune en is `spelerLocaties` weer accumulerend (normaal).
> In de **simulator** werkt ontsnappen al via `Sim directe locatie` (integrale herschrijving), dus daar is
> de prune uitgeschakeld.
