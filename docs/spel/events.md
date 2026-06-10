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
  categorie: "toestand",     // VERPLICHT — "speler" | "toestand" | "wereld"
  tekst: "Wat klinkt.",      // VERPLICHT — voorgelezen tekst (naar audio/afspelen)
  reactietijd_s: 15,         // VERPLICHT — seconden reactietijd vóór de controle

  doelwit: {                 // VERPLICHT — wie/wat het event raakt (mag type "geen")
    type: "uur",             //   "speler" | "uur" | "groep" (gedeelde eigenschap) | "geen"
    selectie: "willekeurig", //   "willekeurig" (toeval) | "alle"
    aantal: "laag",          //   "enkel"|"laag"|"midden"|"hoog" of vast getal (niet bij groep)
    // alleen bij type "groep":
    veld: "willekeurig",     //   "kleur" | "jaar" | "willekeurig" (engine kiest kleur of jaar)
    waarde: "rood"           //   OPTIONEEL — vaste groep-waarde; weglaten = willekeurig gekozen
  },

  getal: "midden",           // OPTIONEEL — rolt een getal en vult elke 'x' in de tekst in
  getal2: [4, 6],            // OPTIONEEL — tweede getal (vult elke 'y' in de tekst); optie of [min,max]
  voorwaarde: "min",         // OPTIONEEL — "min" | "max" | "of" | "geen" (beweging-controle)
  max: 1,                    // OPTIONEEL — max. aantal tegelijk actieve instanties (toestand)
  duratie: [2, 4],           // OPTIONEEL — hoelang de toestand blijft: getal | [min,max] | "kort"/"middel"/"lang"
  audioVoor: "id_voor.wav",  // OPTIONEEL — WAV vóór het getal in de afroep
  audioNa: "id_na.wav",      // OPTIONEEL — WAV ná het getal in de afroep

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
| `categorie`     | ja        | `speler` / `toestand` / `wereld` — bepaalt in welke `[CONFIG]`-inject het hoort. |
| `tekst`         | ja        | Wat wordt voorgelezen. Een losse `x` wordt door `getal` vervangen. |
| `reactietijd_s` | ja        | Seconden reactietijd ná het voorlezen, vóór de controle. |
| `doelwit`       | ja        | Wie/wat geraakt wordt — object met `type`, `selectie`, `aantal`. Bij `type: "groep"` ook `veld` (`"kleur"`/`"jaar"`/`"willekeurig"`) en optioneel `waarde` (vaste groepwaarde; weglaten = willekeurig). |
| `getal`         | nee       | Optie/getal dat `x` in de tekst invult én de controle-waarde bepaalt. |
| `getal2`        | nee       | Tweede optie/getal dat `y` in de tekst invult (bv. de tweede keuze bij `voorwaarde: "of"`). Mag een optie (`laag`/…) of een `[min,max]`-bereik zijn. |
| `voorwaarde`    | nee       | `min` / `max` / `of` / `geen` — beweging-controle na de reactietijd. Bij `of`: geldig als `voor` exact `x` óf exact `y` is. |
| `max`           | nee       | Hoeveel instanties van dít event tegelijk actief mogen zijn (toestand). |
| `duratie`       | nee       | Hoelang de toestand blijft (events/rondes): vast getal, `[min,max]`-bereik (willekeurig), of preset `kort`/`middel`/`lang`. Overschrijft per-gevolg `duurRondes`. Bij `ziekte`: aantal events dat een zieke heeft. |
| `regroup_s`     | nee       | Enkel bij het NUKE-event: seconden regroup-pauze ná de ontploffing vóór het spel verdergaat. |
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

- **`veld`** — de eigenschap-kolom om op te groeperen: `"kleur"` of `"jaar"` (zie `docs/spel/spelers.md`).
  Met `"willekeurig"` (of weglaten) kiest de engine **per afvuring** willekeurig tussen kleur en jaar.
- **`selectie: "willekeurig"`** — de engine kiest één **willekeurige waarde** die onder de actieve
  spelers voorkomt (bv. `kleur` → `rood`/`zwart`/`blauw`). Met optioneel **`waarde`** (bv. `"rood"`)
  zet je de groep vast.
- Het **doelwit** = alle actieve spelers met die waarde; zij worden allemaal als doelwit
  gecontroleerd (niet-leden moeten stil blijven).
- **Afroep**: de prefix is **"een groep"** (i.p.v. een aantal), gevolgd door de event-tekst en het
  groep-label **`veld: waarde`** (bv. "een groep … maximum 3 uur vooruit. kleur: rood"). De
  individuele leden worden **niet** opgesomd.
- Eigenschappen komen uit `global.spelerEigenschappen` (`{ naam: { kleur, jaar } }`), gevuld door de
  `[CONFIG] Speler-eigenschappen`-inject.

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

Levensuren worden **pas bij de controle** toegekend (niet live), op basis van het **opgenomen
pad** (geordende STAP/TELEPORT-acties), **niet** een netto begin/eind-vergelijking. Een STAP
telt 1 (nooit achteruit); een TELEPORT (sprong tussen twee actieve portaal-palen) telt 0 en
wordt niet op richting gecontroleerd. Bij een verplaatsing-event (`voorwaarde` min/max) mag enkel
het doelwit bewegen; anderen moeten stil blijven. `voor` = aantal STAP vooruit, `x` = `getal`.
Δ = de toegekende/afgetrokken levensuren:

| Geval | Status | Δ levensuren |
|-------|--------|--------------|
| doelwit `max`, `voor ≤ x` (geldig) | OK | **+voor** (×2 op happy-hour-eindpaal) |
| doelwit `max`, `voor > x` | TE VEEL | **−(voor − x)** |
| doelwit `min`, `voor < x` | TE WEINIG | **−voor** |
| doelwit `of`, `voor ≠ x` én `voor ≠ y` | ONGELDIGE KEUZE | **−voor** |
| doelwit, achterwaartse STAP | TERUG IN TIJD | **−achter** |
| doelwit, >1× zelfde portaal | ONGELDIGE TELEPORT | **−voor** |
| niet-doelwit dat beweegt | BEWOOG (mocht niet) | **−(voor+achter)** |
| stil blijven staan | OK (stil) | 0 |

Zou Δ een speler **onder 0** brengen, dan blijft hij op 0 met **+1 sterfte** (hij speelt door).
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
| `nuke`     | —                                         | Wereld-event: na de aftelklok (`reactietijd_s`) ontploft iedereen die nog **gedetecteerd** is (uren 0 + sterfte); daarna een **regroup**-pauze van `regroup_s` s. Zie hieronder. |
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

1. **Kies de categorie** (`speler` / `toestand` / `wereld`) — bepaalt de config-inject.
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
5. **Controle** — als `voorwaarde` gezet is, wordt elke speler gecontroleerd.

## Voorbeelden

### Verplaatsing-event (speler, max + getal + controle) — Event A

```js
{ id:"verplaatsing2", naam:"verplaatsingMax", categorie:"speler",
  tekst:"Maximum x uur.", reactietijd_s:15,
  doelwit:{ type:"speler", selectie:"willekeurig", aantal:"laag" },
  getal:"midden", voorwaarde:"max", gevolgen:[{ type:"geen" }] }
```
Kiest 1–3 spelers, rolt een getal in plaats van `x`, en controleert na de reactietijd of die
spelers **hoogstens** dat aantal STAPpen vooruit zetten (een portaal-sprong telt 0). Niet-gekozen
spelers moeten stil blijven. (Het oude `verplaatsingMin`-event is verwijderd.)

### Of-verplaatsing-event (speler, keuze tussen twee getallen) — Event D

```js
{ id:"of_verplaatsing", naam:"Of-verplaatsing", categorie:"speler",
  tekst:"x of y uur vooruit.", reactietijd_s:15,
  doelwit:{ type:"speler", selectie:"willekeurig", aantal:"midden" },
  getal:"laag", getal2:[4,6], voorwaarde:"of", gevolgen:[{ type:"geen" }] }
```
Rolt twee getallen: `x` uit `getal:"laag"` (1–3) en `y` uit `getal2:[4,6]` (4–6), allebei
ingevuld in de tekst (bv. "2 of 5 uur vooruit."). De gekozen spelers moeten **exact `x` óf exact
`y`** STAPpen vooruit zetten. Elk ander aantal (geen achterstap) is **ONGELDIGE KEUZE** → **−voor**.
Een `[min,max]`-bereik voor `getal2` houdt `y` gegarandeerd boven `x` (de optie `midden` rolt 1–6
en zou `y ≤ x` kunnen maken). Een portaal-sprong telt 0 STAPpen.

### Groep-verplaatsing-event (speler, doelwit = groep)

```js
{ id:"groep_verplaatsing", naam:"Groep-verplaatsing", categorie:"speler",
  tekst:"maximum x uur vooruit.", reactietijd_s:20,
  doelwit:{ type:"groep", veld:"willekeurig", selectie:"willekeurig" },
  getal:"midden", voorwaarde:"max",
  audioVoor:"groep_verplaatsing_voor.wav", audioNa:"groep_verplaatsing_na.wav",
  gevolgen:[{ type:"geen" }] }
```
Kiest één willekeurige groep onder de actieve spelers en richt zich op **alle** spelers in die
groep: zij mogen hoogstens `x` STAPpen vooruit (zelfde `max`-controle als `verplaatsing2`).
Met `veld:"willekeurig"` kiest de engine per afvuring tussen **kleur** en **jaar**; afroep bv.
"een groep … maximum 3 uur vooruit. kleur: rood" of "… jaar: eerste". Vastzetten kan met
`veld:"kleur"` of `veld:"jaar"`.

### Groep-of-verplaatsing-event (speler, doelwit = groep, keuze tussen twee getallen)

```js
{ id:"groep_of_verplaatsing", naam:"Groep-of-verplaatsing", categorie:"speler",
  tekst:"x of y uur vooruit.", reactietijd_s:20,
  doelwit:{ type:"groep", veld:"willekeurig", selectie:"willekeurig" },
  getal:"laag", getal2:[4,6], voorwaarde:"of",
  audioVoor:"groep_of_verplaatsing_voor.wav", audioNa:"groep_of_verplaatsing_na.wav",
  gevolgen:[{ type:"geen" }] }
```
Groep-variant van `of_verplaatsing`. Kiest één willekeurige **kleur** en richt zich op **alle**
spelers met die kleur: zij moeten **exact `x` óf exact `y`** STAPpen vooruit zetten.
`x` rolt uit `getal:"laag"` (1–3), `y` uit `getal2:[4,6]`. Controle identiek aan `of_verplaatsing`
maar voor elk groepslid; niet-leden moeten stil blijven.

### Toestand-event (portaal: blijvend effect, max, paar-koppeling)

```js
{ id:"portalen", naam:"Portalen", categorie:"toestand",
  tekst:"Een portaal opent tussen twee uren.", reactietijd_s:15, max:1, duratie:[2,4],
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
  tekst:"worden Happy Hour.", reactietijd_s:15, max:4, duratie:[3,6],
  doelwit:{ type:"uur", selectie:"willekeurig", aantal:"laag" },
  audioVoor:"happy_hour_voor.wav", audioNa:"happy_hour_na.wav",
  gevolgen:[ { type:"effect", niveau:"uur", effect:"happy_hour", data:{} } ] }
```
Kiest 1–3 willekeurige uren (afroep: "3 uren worden Happy Hour"). Die uren worden goud.
Eindigt een speler een verplaatsing op een happy-hour-uur, dan tellen de verdiende
levensuren dubbel (zie `docs/spel/spel.md`). `max: 4` laat tot 4 happy-hour-uren tegelijk toe.

### Toestand-event (score)

```js
{ id:"kosmische_gift", naam:"Kosmische gift", categorie:"toestand",
  tekst:"Een gulle ster schenkt een willekeurige speler een gift.",
  reactietijd_s:15,
  doelwit:{ type:"speler", selectie:"willekeurig", aantal:"enkel" },
  gevolgen:[ { type:"score", delta:3 } ] }
```

### Toestand-event (ziekte: zieke spelers + medicijn-palen)

```js
{ id:"ziekte", naam:"Ziekte", categorie:"toestand",
  tekst:"worden ziek.", reactietijd_s:15, max:1, duratie:10,
  doelwit:{ type:"speler", selectie:"willekeurig", aantal:"laag" },
  audioVoor:"ziekte_voor.wav", audioNa:"ziekte_na.wav",
  gevolgen:[ { type:"ziekte" } ] }
```
Kiest 1–3 spelers (afroep: "3 spelers worden ziek") en maakt ze ziek; evenveel vrije uren worden
felroze (medicijn). De zieken hebben `duratie` (10) events om een medicijn-uur te bereiken, anders
sterven ze. `max: 1` houdt het bij één ziekte-episode tegelijk (de medicijnen blijven actief tot de
episode voorbij is). Volledige uitleg: `docs/spel/event-catalogus.md`.

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
  reactietijd_s:5, regroup_s:60, max:1, doelwit:{ type:"geen" },
  audioVoor:"nuke.wav", gevolgen:[ { type:"nuke" } ] }
```
Speelt "NUKE" af + een **aftelklok** van `reactietijd_s` (5 s, aanpasbaar) om weg te lopen. Bij de
controle ontploft **elke speler die nog gedetecteerd is** (RSSI boven de vloer / binnen het veld):
levensuren → 0 **en +1 sterfte**. Wie **ontkomen** is (onder de RSSI-vloer / buiten het veld → uit
`spelerLocaties`) overleeft. Daarna een **regroup**-pauze van `regroup_s` s (60, aanpasbaar) vóór het
spel verdergaat. `regroup_s` is een nieuw, optioneel veld; de fase heet `regroup` in `pof/status`.
