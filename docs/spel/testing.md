# Magnum Opus — Testscenario's per event en mechanisme

Dit document is de **checklist voor de testdag**: per event, per permanent mechanisme en per
minigame staat hier wat je opzet, wat je doet, en wat er dan **op het veld** en in de
**controle-tabel** hoort te gebeuren. Ontbreekt het verwachte gedrag, dan is dat een bug — niet
een regel die je ter plekke herinterpreteert.

> **Normatief blijven** `docs/invarianten.md` (wat altijd waar moet zijn), `docs/protocol.md`
> (het communicatiecontract) en `docs/spel/event-systeem.md` (leidend voor de spelregels).
> Dit document beschrijft alleen hoe je die regels **aftoetst**.
> Voor de **hardwareketen** (paal → master → bridge → Node-RED → audio) hoort `docs/handboek/02-testprocedure.md`
> (T1–T13); dat wordt hier niet herhaald.

Notatie per scenario: **Opzet → Doe → Verwacht in het veld → Verwacht in de controle-tabel → Faalsignaal**.

---

## 1. Hoe je test

### 1a. Simulator (geen hardware nodig)

Zet `serial-bridge` stil (`docker stop serial-bridge`), open `http://<pi>:1880/sim/`, kies modus
**Simulatie** en zet het **24-uur-veld** aan. Sleep spelers over de ring; een drag is een *settled*
paalwissel, dus wat jij sleept is exact wat de engine ziet (geen RSSI-ruis).

Handige knoppen:

- **Manueel-modus** — jij bepaalt wanneer het event valt ("Volgende event") en wanneer de controle
  draait ("Controle"). Zonder tijdsdruk. Er is dan **geen** `grace`-fase.
- **"→ wachtrij"** op een event-kaart — dwingt dat event als eerstvolgende af. Zo test je een
  `legendary` event zonder honderd rondes te wachten.
- **Tijd terug (↶)** — herstelt de vorige spelstaat. Handig om één scenario meermaals te draaien.
- **Systeem-/Spelinstellingen** — middernacht uit, toestand-exclusiviteit uit, tempo, slechte aura,
  thuisbank.

### 1b. AI-testharnas (regressie)

`tools/speltest/` draait een onafhankelijk **orakel** naast de echte Node-RED-engine en meldt elke
afwijking als bugkandidaat. Eén ronde: `python -m tools.speltest.game_driver next` → `move` →
`verify`. Batch: `runner --strategie all --rondes 40 --report out/`.

> ⚠️ Het harnas dekt **alleen het PoF-middagspel**. Klokslag, Infected, het avondspel en de
> drukknop-modus test je met de hand.
>
> ⚠️ **Bekende schuld**: `tools/speltest/oracle.py` (~regel 188) hanteert nog het **oude** strafmodel
> van vóór V11 (`BEWOOG` = `−(voor+achter)`). Het orakel meldt daar dus vals-positieve mismatches
> tot dat is bijgewerkt. Ook de fase-2-regels (wolf-legaliteit, tweeling-clawback, `OK (poort blokkeert)`)
> kent het orakel nog niet.

### 1c. Waar je kijkt

| Bron | Wat je eruit haalt |
|------|--------------------|
| Dashboard **Bediening** → tabel *Controle* | status + Δ per speler, de rij waarin alle suffixen staan |
| Dashboard **Bediening** → *Actieve effecten* / *Wereld-effecten* | portalen, medicijnen, tweelingen, etenstijd, tornado |
| MQTT `pof/controle` | machine-leesbare `{speler, status, verplaatst, delta, tag}` |
| MQTT `pof/status` | fase, huidig event, doelwit, teller, wachtrij |
| Node-RED **debug**-paneel | de `node.warn`-regels (oogst, ziekte-dood, wolf, tweelingDood ontbreekt) |
| Dashboard **Leaderbord** (`/leaderbord`) | cumulatieve stand, groot |

---

## 2. Verplaatsing-events (4)

Gemeenschappelijk: alleen het **doelwit** mag bewegen; alle anderen krijgen `BEWOOG (mocht niet)`
(0 uren, geen sterfte — V11). Een portaal-sprong telt **0** stappen.

### 2.1 Groep-verplaatsing (`groep_verplaatsing`) — "maximum x uur vooruit"

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Event valt op `kleur: rood`, x = 4 | Een rode speler loopt 3 vooruit | `OK`, **+3** levensuren |
| B | idem | Een rode speler loopt 6 vooruit | `TE VEEL`, Δ = `max(0, 4 − (6−4))` = **+2**, +1 valsspeelpunt |
| C | idem | Een rode speler blijft staan | `OK`, **0** — stilstaan mag bij een max-event |
| D | idem | Een **blauwe** speler loopt 1 vooruit | `BEWOOG (mocht niet)`, **0**, +1 valsspeelpunt |
| E | idem, happy hour op de eindpaal | Rode speler loopt 3 naar het gouden uur | `OK (happy hour x2)`, **+6** |

**Faalsignaal:** een negatieve Δ. Valsspelen mag **nooit** levensuren kosten (V11).

### 2.2 Groep-of-verplaatsing (`groep_of_verplaatsing`) — "x of y uur vooruit"

Stilstaan is hier **fout** (0 ∉ {x,y}).

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | x = 2, y = 5 | Groepslid loopt exact 5 | `OK`, **+5** |
| B | idem | Groepslid loopt 3 | `ONGELDIGE KEUZE`, Δ = `max(0, 3 − 1)` = **+2** |
| C | idem | Groepslid blijft staan | `ONGELDIGE KEUZE`, Δ = `max(0, 0 − 2)` = **0** |

### 2.3 Verplaatsing (iedereen) (`verplaatsing_iedereen`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Forceer het event **10×** | Noteer het afgeroepen getal | Altijd **1..5**; alle vijf waarden komen voor (`getal: [1,5]`) |
| B | x = 3 | Iedereen loopt 3 | Iedereen `OK`, **+3** |
| C | idem | Speler loopt 2 | `OK`, **+2** (max-event) |
| D | idem | Speler loopt 3 | `auraValsspeel` **+15 %** (`auraPerUur: 5` × 3 uren) |

**Kijk in het veld:** *iedereen* is doelwit, dus er is geen `BEWOOG` mogelijk.

### 2.4 Pariteit-verplaatsing (`verplaatsing_pariteit`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Spelers op uur 3, 4, 7, 8 | Event valt, label `uur: oneven` | Doelwit = spelers op **3 en 7** (hun **startuur**) |
| B | idem | Speler van uur 4 loopt mee | `BEWOOG (mocht niet)` |
| C | Zet iemand tijdens de reveal van 3 naar 4 | | Hij blijft doelwit: de pariteit is op het **startuur** bepaald |

---

## 3. Toestand-events (8)

### 3.1 Portalen (`portalen`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Forceer het event | Meet de ring-afstand tussen de twee paarse palen | Altijd **≥ 6 uren** (`minAfstand: 6`) |
| B | Eén portaal open | Forceer het event nog eens | Tweede portaal opent; de vier palen zijn **allemaal verschillend** |
| C | Twee portalen open | Forceer een derde keer | Event wordt **niet** gekozen (`max: 2`) |
| D | Portaal 13↔20, doelwit-event max 5 | Loop 13 → **teleport** 20 → 2 stappen | `OK`, **+2** (de sprong telt 0), **geen** "TERUG IN TIJD" |
| E | idem | Teleporteer **twee keer** door hetzelfde portaal | `ONGELDIGE TELEPORT`, **0** |
| F | Dichte middernachtpoort | Teleporteer naar uur 1 | Toegestaan — een portaal-hop is geen lopen (M3) |

**In het veld:** beide palen **continu paars**; na `duratie` (3–8 events) doven ze en speelt
`portaal_gesloten.wav`.

### 3.2 Happy Hour (`happy_hour`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | 31 spelers actief | Forceer het event | **2** gouden uren (`aantal: laag`, √N-curve — EV6) |
| B | 8 spelers actief | Forceer het event | **1** goud uur |
| C | Goud op uur 9 | Doelwit eindigt op uur 9 na 3 stappen | `OK (happy hour x2)`, **+6** |
| D | idem | Doelwit **passeert** uur 9 en eindigt op 10 | **+3** — alleen de **eindpaal** telt |

### 3.3 Ziekte (`ziekte`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Forceer het event | Tel de zieken en de medicijn-palen | Even veel; medicijn-palen **felroze** |
| B | Zieke is bewegings-doelwit, medicijn 3 vooruit | Loop legaal naar de medicijn-paal | `GENEZEN`; het medicijn **verdwijnt** (Z3) |
| C | Zieke is **geen** doelwit, medicijn 2 vooruit | Wandel er tijdens de aanloop heen, sta stil in het event | `OK (stil) | VRIJ GEWANDELD (0 uur) → GENEZEN`, **+1 valsspeelpunt**, **geen** uren-verlies |
| D | Zieke, `rondesOver = 1`, geen medicijn bereikbaar | Laat de ronde verstrijken | **Dood**: uren 0 + 1 sterfte, `node.warn` |
| E | Zieke, `rondesOver ≤ 3` | Wacht een ronde | **Hartslag-piep** (3/2/1) op zijn uur (acties 5/6/7) |
| F | **Z9** — 2 zieken, verwijder het laatste medicijn (Admin → "Reset paal → rust") | Wacht een ronde | **Beide zieken sterven meteen** (uren 0 + sterfte), box roept *"Alle zieken zijn gestorven."* |
| G | Zieke heeft een **tweeling** en sterft | | De tweeling sterft mee, band breekt (TW3) |

### 3.4 Tijdbom (`tijdbom_speler`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Forceer het event | Kijk waar de ontmantel-palen liggen | Enkel op palen **met een drukknop** (`config/drukknoppen`) |
| B | Bom-speler staat op ontmantel-paal, **dag** (uur 7–18) | Druk de knop | **80 %** kans: bom weg, groene flits (actie 22) |
| C | idem, **nacht** (uur 19–6) | Druk de knop | **50 %** kans; bij falen: rode flits (23), **iedereen op die paal** verliest `uur` levensuren |
| D | Bom loopt af (`rondesOver = 0`) | | Ontploft = identiek aan een mislukte ontmanteling |
| E | Toestand-exclusiviteit **aan** | Forceer ziekte + tijdbom | Nooit dezelfde speler in beide |

### 3.5 Tornado (`tornado`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Forceer het event, 2 centers | Meet de ring-afstand | **≥ 3** (`minAfstand`), buururen overlappen nooit |
| B | Speler startte op een **buur-uur** | Hij loopt naar het center | `GEVOLGD`, Δ 0 |
| C | idem | Hij blijft staan | `WEGGEZOGEN`: **alle** levensuren kwijt, **géén sterfte** |
| D | Speler stond er niet naast | Hij beweegt niet | `OK`, geen straf |
| E | Na de controle | Kijk naar de LED's | Center + randen **terug naar hun oude kleur** (één-shot) |
| F | Tornado-slachtoffer heeft een tweeling | | **Niets** propageert — een tornado geeft geen sterfte |

### 3.6 Etenstijd (`etenstijd`) — de wolf

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Forceer het event | Lees het groep-label af | Altijd **precies één** groep, altijd `kleur: …` (nooit jaar/maand/seizoen, nooit twee) |
| B | Speler-klassement 0..190 uur | Forceer het event 10× | De wolf zit **altijd** in de laagste 5 van `globaleStats.totaalUren`, buiten de kleur-groep |
| C | Wolf is **niet** afgeroepen | Laat hem naar een schaap lopen | `BEWOOG (mocht niet) \| WOLF MISTE (illegale zet)` — **geen vangst**, schaap houdt zijn uren |
| D | Wolf staat stil, schaap wordt naar hem toe gestuurd | Controle | `OK (stil) \| WOLF VING 1 (+N uur)`; schaap `GEVANGEN DOOR WOLF (−N uur, +1 sterfte)` |
| E | Wolf zit **in** de afgeroepen groep en loopt legaal naar een schaap | Controle | Vangst telt |
| F | Wolf wandelde tijdens de aanloop naar zijn schaap | Controle | `VRIJ GEWANDELD` + **WOLF MISTE** — geen vangst |
| G | Zelfde schaap, tweede ronde op hetzelfde uur | Controle | **Geen** tweede vangst (`gevangen`-lijst) |
| H | Buit-grens: wolf op uur 20, schaap heeft 6 uren | Vangst | Buit = `min(20, 6)` = **6** |
| I | Laat de 15 rondes aflopen | | Elk **niet-gevangen** schaap krijgt **+5 levensuren**; `etenstijd_voorbij.wav` speelt |
| J | Nuke valt terwijl etenstijd loopt | | **Geen** vangst in die controle |
| K | Schaap met tweeling wordt gevangen | | Tweeling sterft mee (het schaap kreeg een sterfte) |

### 3.7 Tweeling (`tweeling`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Forceer 4× | 5e keer forceren | Event wordt **niet** gekozen (`max: 4`); niemand zit in twee paren |
| B | Ann + Bo tweeling, beide doelwit | Beide lopen legaal | **Beide** krijgen hun winst |
| C | Ann doelwit (loopt 3), Bo staat stil | Controle | Ann: `OK (+3) \| TWEELING (geen winst: Bo bewoog niet legaal mee, −3 uur)` → netto **0**. Bo behoudt zijn uren. **Geen sterfte.** |
| D | Ann loopt legaal, Bo beweegt illegaal | Controle | Idem: Ann netto 0 |
| E | Ann en Bo eindigen op **hetzelfde uur** | Controle | `TWEELING VERBROKEN (samen op uur N)`, band weg uit `tweelingen` én uit de wereld-effecten. Geen beloning |
| F | Ann steekt de **dichte** poort over | Controle | Ann sterft; Bo `TWEELING STERFT MEE` (uren 0 + sterfte); band breekt |
| G | **Nuke** valt | Controle | Beiden ontploffen, maar de **band blijft** (TW5) — check *Wereld-effecten*: `Tweeling: Ann & Bo` staat er nog |
| H | **Middernacht-oogst** raakt Ann | | Bo sterft mee, band breekt (via `tweelingDood`) |
| I | Ann is een **dienaar** van Baas, loopt legaal, Bo staat stil | Controle | De winst die naar **Baas** ging wordt weer bij **Baas** afgetrokken |

**Faalsignaal:** zie je nog `TWEELING UIT SYNC (-alle uren)`, dan draait er oude code.

### 3.8 Body-swap (`bodyswap`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Forceer het event | Meet de ring-afstand tussen de twee doelwitten | **≥ 5 uren** (`minSpelerAfstand: 5`) |
| B | Alle spelers < 5 uur uit elkaar | Forceer | `node.warn` + gewone steekproef; het event valt wél |
| C | Ann op 22, Bo op 4 | Beide eindigen op elkaars startpaal | `OK (gewisseld)`, Δ 0 voor beiden |
| D | Dichte middernachtpoort ertussen | Ann steekt over om te swappen | **Geen** `MIDDERNACHT DICHT`, geen sterfte — de route is vrij (BS2) |
| E | Bo loopt achteruit om te swappen | | Toegestaan, geen `TERUG IN TIJD` |
| F | Ann eindigt ergens anders | | `NIET GEWISSELD`, 0 + valsspeelpunt |

---

## 4. Wereld-events (9)

### 4.1 Nuke (`nuke`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Forceer het event | Kijk naar het veld | Alle palen pulseren **groen↔geel**, behalve de middernacht-poort-paal |
| B | Sim | Sleep een speler het veld **uit** vóór de knal | `VEILIG (ontkomen)` |
| C | Hardware | Loop ≥ 4 s (`escape_s`) buiten bereik | Idem; check `reactietijd_s ≥ escape_s + 2` |
| D | Blijf staan | | `ONTPLOFT`: uren 0 + 1 sterfte |
| E | Na de knal | Kijk naar de timer | `Regroup: 45s` |
| F | **Tijdens de regroup** loop je terug het veld in | Volgende controle | `VRIJ GEWANDELD` (0 winst + valsspeelpunt) — regroup is **géén** vrije fase |
| G | Ziekte + tijdbom + dienaars actief | Nuke | Alles gewist; `pof/ziekte` en `pof/dienaars` leeg |
| H | Tweeling actief | Nuke | Band **blijft** (TW5/N8) |
| I | Etenstijd actief | Nuke | Wolf vangt **niet** in die controle |

### 4.2 Sneller / Trager (`sneller_events`, `trager_events`)

| # | Doe | Verwacht |
|---|-----|----------|
| A | Forceer `sneller` | `pof/status.spelTempo` −0,1, ondergrens **0,6** |
| B | Forceer `trager` | +0,1, bovengrens **1,3** |
| C | Zet het tempo op 0,6 en kijk naar een 20 s-event op hardware | Reactietijd klemt op de **sensing-vloer** ~7 s (SP6) + `node.warn` |
| D | Stop het spel | Tempo terug op **1,0** |

### 4.3 Bomaanslag (`bomaanslag`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Forceer 20× | Noteer de duo's | Enkel **9+11 / 4+20 / 6+7 / 6+9**, elk ~25 % |
| B | | Luister | Per duo een **eigen** afroep-clip (`audioVoorOpties`) |
| C | Sta op uur 20 bij duo 4+20 | Blijf staan | `GERAAKT (−20 uur)` — het **uurnummer** is de schade |
| D | Vlucht tijdens de reactietijd | | `VEILIG`, **geen** bewegingsstraf |
| E | Tijdens de reactietijd | Kijk naar de twee palen | Rode tik-LED (13) + zoemer-piep |
| F | Bom-slachtoffer heeft een tweeling | | **Niets** propageert — de bom geeft **geen sterfte** |

### 4.4 Identiteitscrisis (`identiteitscrisis`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Forceer het event | Lees `global.luisterNaam` | Elke speler luistert naar de **volgende** naam (alfabetisch, cyclisch) |
| B | Ann is rood, luistert naar Bo (blauw) | Groep-event `kleur: blauw` | Ann **zit erin** |
| C | idem | Groep-event `jaar: <Anns jaar>` | Ann zit erin — **jaar verschuift niet** |
| D | idem | Groep-event `maand` / `seizoen` | Ann volgt haar **eigen** maand/seizoen |
| E | *Alix Blond* en *Alix Bruin* | Kijk naar de volgorde | Sortering op de **volledige** naam: Blond vóór Bruin |
| F | Laat de duratie aflopen | | `luisterNaam` leeg, afloop-audio |

### 4.5 Tijdreizen (`tijdreizen`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Actief, doelwit max 5 | Loop 2 **achteruit** | `OK`, **+2** — achteruit telt mee |
| B | idem | Loop 2 vooruit **en** 1 achteruit | `PENDELEN`, **0** + valsspeelpunt — kies één richting |
| C | idem | Steek de middernachtpoort **achterwaarts** over (1→24) | `TERUG IN TIJD` — blijft verboden |
| D | Dichte poort | Steek voorwaarts over | `MIDDERNACHT DICHT` — tijdreizen opent de poort niet |
| E | Duratie afgelopen | Loop achteruit | Weer `TERUG IN TIJD` |

### 4.6 Onmiddellijke dood (`onmiddellijke_dood`, avond)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | `avondModus` uit | Kijk in de events-tab | Event is **onzichtbaar** (`fase: avond`) |
| B | Avond aan, iedereen 0 sterftes én 0 valsspeelpunten | Forceer | Uniforme loting |
| C | Eén speler met veel sterftes + valsspeelpunten | Forceer | Hij wordt **veel vaker** geloot (gewicht = `sterftes + valsspeelpunten`) |
| D | Stop het spel tijdens de dood-animatie | | Geen na-vuur (`pofGeneration`-token) |

### 4.7 Maximaal per uur (`max_per_uur`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | X = 4, 5 spelers eindigen op uur 7 | Volgende controle | Wie op 7 aankwam of van 7 wegging, verdient **0** (`… MAX/UUR`), **geen sterfte** |
| B | Duratie afgelopen | | `maxPerUur` weg, vlaggen leeg |

### 4.8 Polonaise (`polonaise`)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Actief, 5 spelers vertrekken van uur 3 | Verplaatsings-event | `OK (polonaise +1)`: `+voor + (5−4)` |
| B | 2 spelers vertrekken samen | | `TE WEINIG SAMEN`, 0 + valsspeelpunt |
| C | 10 **verplaatsings**-events verder | | Polonaise stopt; toestand/wereld-events tellen **niet** mee in de teller |

### 4.9 Twee groepen tegelijk (WE3)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | ≥ 4 spelers, groep-event met `veld: willekeurig` | Forceer 40× | ~15 % van de keren **twee** groepen, label `veld: waarde + veld: waarde` |
| B | `etenstijd` (vast `veld: kleur`) | Forceer 20× | **Nooit** twee groepen |
| C | Speler zit in beide groepen | | Telt **één** keer als doelwit |

---

## 5. Permanente mechanismen

### 5.1 Vrij wandelen (V10)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Spel loopt, fase `aanloop` | Sleep een speler 2 palen | Volgende controle: `… \| VRIJ GEWANDELD (0 uur)`, **totaalUren onveranderd**, +1 valsspeelpunt |
| B | Dezelfde speler heeft ≥ 1 god-punt | Idem | `… \| VRIJ GEWANDELD [GOD-PUNT]`, saldo −1, **winst blijft**, geen valsspeelpunt |
| C | Speler wandelt vrij **én** speelt fout in het event, 1 god-punt | | Hoogstens **één** punt op |
| D | Fase `regroup` (na een nuke) | Sleep een speler | Wordt óók bestraft — geen vrije fase |
| E | Spel **gestopt** | Sleep spelers rond | Er wordt **niets** opgenomen |
| F | Klokslag of Infected actief | Sleep spelers rond | Niets opgenomen (`pofActief` is false) |
| G | Direct na de controle (< `pofSettleGrace`) settelt een late hop | | **Niet** bestraft (`pofVrijVanaf`-genade) |
| H | Tijd terug (↶) | | De herstel-posities lokken **geen** straf uit |

### 5.2 Middernachtpoort

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Poort **open** (wit) | Loop 24 → 1 | Gewoon `OK` |
| B | Poort **dicht** (rood), niet-doelwit | Loop 24 → 1 | `MIDDERNACHT DICHT`: **alle** uren kwijt + 1 sterfte |
| C | Dicht, tussen events | Loop 24 → 1 | De **live poort-bewaker** straft; **niet** nog eens bij de controle (`mnGestraft`) |
| D | Dicht, doelwit op uur 22, max 5 | Loop 2 (tot 24) | **Gate-block**: `OK`, +2 — geen "te weinig" (M3b) |
| E | Dicht, doelwit staat **op** uur 24, max 5 | Blijf staan (kan niet anders) | `OK (poort blokkeert)`, Δ **0**, geen valsspeelpunt, **geen** −1 (**M10**) |
| F | idem, maar met een `min 3`- of `of 3/6`-event | | Ook `OK (poort blokkeert)` |
| G | Dicht, **niet**-doelwit staat op uur 24 | Blijf staan | `… \| MIDDERNACHT STIL (-1)` — −1 levensuur per ronde (M9) |
| H | Dicht, portaal naar uur 1 | Teleporteer | Toegestaan |
| I | π-cijfer **0** | Sta op uur 24 | **Oogst**: uren 0 + sterfte, je wordt **dienaar** van de armste vrije speler; hele ring toont de oogst-animatie |
| J | Geoogste heeft een tweeling | | Tweeling sterft mee, band breekt |
| K | Stop + Start | Kijk naar de π-stand | **Loopt door** (M1) — enkel de admin-knop zet hem terug |

### 5.3 Dienaar / meester

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Ann is dienaar van Baas | Ann loopt legaal 3 | `… -> dient Baas (+3)`; **Baas** krijgt +3, Ann niets |
| B | idem | Baas steekt de dichte poort over | Baas verliest **alles**, óók de van Ann gekregen uren (ze zijn verliesbaar) |
| C | Ann verliest uren / sterft | | Verlies + sterfte blijven **bij Ann** |
| D | Nuke of Stop | | `dienaars` leeg |

### 5.4 God-punten

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Speler haalt zijn doel tijdens een lopende partij | Kijk naar `godPunten` | **Ongewijzigd** — punten komen pas bij Stop |
| B | Zet de partij op **uit** | | `+2` verschijnt, `godAward` gereset |
| C | Speler met 1 punt speelt fout | | `… [GOD-PUNT]`, saldo −1, geen valsspeelpunt, **geen** sterfte |
| D | Zieke met god-punt loopt fout naar een medicijn-paal | | Hij **geneest** toch |
| E | Beheer-wis | | Saldo terug op 0 |

### 5.5 Doelen + goal-lock

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Doel "verplaats 5 uur" | Loop 3 legaal + 4 met `TE VEEL` | `verplaatstSpel` = **3** — enkel schone `OK`-zetten tellen (S7) |
| B | Vrij gewandeld + daarna legaal | | De vrij-gewandelde ronde telt **niet** mee |
| C | Doel gehaald met 20 uren | Verlies daarna 15 uren | Bij Stop: `globaleStats` kreeg **20** bij het lockmoment en **max(0, 5 − 20) = 0** erbij → nooit dubbel, nooit minder (D8) |
| D | Doel "inhalen" | Loop je rivaal **lopend** voorbij | `doelBereikt` latcht |
| E | idem | Passeer hem via een **teleport** | Telt **niet** |

### 5.6 Thuisbank (optioneel, standaard uit)

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Aan, startuur = 2, speler heeft 10 uren | Loop de ring rond en land exact op uur 2 | `… \| GESTORT (+N uur globaal)`; `spelerStats.totaalUren` → **0**; Leaderbord stijgt |
| B | idem, maar **ziek** of met een **tijdbom** | Land op uur 2 | `… \| THUIS (geblokkeerd: toestand)` — niets gestort |
| C | Speler stond al op zijn startuur en beweegt niet | | **Geen** storting (hij moet er *aankomen*) |
| D | Uit (default) | Land op je startuur | Niets bijzonders |

### 5.7 Slechte aura + valsspeel-aura

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | `badAura` aan | Forceer ziekte 50× met spelers verspreid | Uren 20–6 worden ~10 % vaker geraakt, uur 24 ~15 % |
| B | Speler met 5 valsspeelpunten (+15 % aura) | Forceer ziekte | Hij wordt relatief vaker geraakt |
| C | Diezelfde speler wórdt ziek | | Zijn `auraValsspeel` **reset naar 0**; `valsspeelpunten` blijft |
| D | `badAura` uit | | Uniforme loting |

### 5.8 Doelwit-dichtheid (EV6)

Forceer `happy_hour` (`aantal: laag`) bij verschillende spelersaantallen, met de knob op 25 %:

| N | laag | midden | hoog |
|---|------|--------|------|
| 8 | 1 | 2 | 3 |
| 16 | 1 | 2 | 4 |
| 24 | 2 | 3 | 4 |
| 31 | **2** | **3** | **5** |

Cap = **6**. Schuif de knob naar 50 % → de aantallen verdubbelen ruwweg. Vaste getallen
(`portalen` 2, `tweeling` 2, `bodyswap` 2), `[min,max]`-arrays (`tornado`), `vastOpties`
(`bomaanslag`) en `selectie: "alle"` schalen **niet**.

### 5.9 Avondspel

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Avond aan, midden in een partij | | Stats blijven behouden (AV1) |
| B | Loop legaal 3 | | **−3** levensuren; `totaalUren` mag **negatief** worden, **geen** sterfte |
| C | Speler met `gestorven: true` | Loop | Floort op 0 |
| D | Events-tab | | Enkel `avond`/`beide` |

---

## 6. Minigames

Geen orakel — dit test je met de hand. Beide draaien in **overdrive-sensing** (`venster` 2500 ms,
`grace` 1500 ms, scan 400 ms); check na afloop dat `locParams` terug op `locParamsNormaal` staat.

### 6.1 Klokslag

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Team A alleen op uur 3 | Wacht | Inname duurt `H = max(3, min_inname_s)` = **5 s** (G8-vloer) |
| B | Team A alleen op uur 18 | Wacht | `H` = **18 s** |
| C | Team A heeft 3 spelers meer dan B | | `snelheid` = `1,0 + 0,1 × min(voorsprong−1, 3)` = **1,3** (cap) |
| D | Uur is eigendom van B, A staat erop | | `P` daalt; volledige overname kost **2H** |
| E | Gelijkspel of leeg | | `P` verval **1/s** (`verval_per_sec`) |
| F | A verlaat een uur dat hij bezit | | Paal blijft **vergrendeld** (sticky) |
| G | Speeltijd = 0 | | Score = **aantal bezeten uren**; gelijkspel → **som van de uurnummers** |
| H | LED's | | Rust = ademend dim wit; inname = kaarsflikker in de controller-kleur (∝ `P/H`); bezit = constant fel; vergrendeld = bevroren |
| I | Gepauzeerde speler | | Telt **niet** mee (S8b/EV3) |

### 6.2 Infected

| # | Opzet | Doe | Verwacht |
|---|-------|-----|----------|
| A | Start | | Eén willekeurige **patiënt 0**; zijn paal wordt **constant rood** |
| B | 1 besmet | Sta 5 s op zijn uur | Besmet (`drempel = 5 + floor(1/2)` = 5 s) |
| C | 6 besmet | Sta op zijn uur | Drempel = `5 + 3` = **8 s** |
| D | Sta 3 s, loop weg, kom terug | | Teller **reset** — opnieuw vanaf 0 |
| E | Precies **5** besmet | | **2 bestrijders** worden gekozen uit de gezonden; hun paal wordt **blauw**, 60 s immuun |
| F | Sta op een **blauwe** paal met een besmette | | Je raakt **niet** besmet; teller reset |
| G | 60 s later | | **2 nieuwe** bestrijders (de vorige twee bij voorkeur uitgesloten) |
| H | Zak terug onder 5 besmet | | **Geen** bestrijders meer |
| I | Nog 3 gezonden over (bij > 3 starters) | | Die 3 **winnen**, fase `klaar` |
| J | Stop | | Alle LED's uit, `infected/status` opgeruimd |

---

## 7. Hardware

Voor de hele keten — slave solo, LED + zoemer, batterij, detectie, spelstatus GO, commando-keten,
audio, drukknoppen, scan-duur, simulator-smoke, mini-spel, testharnas en noodherstel — zie
[`docs/handboek/02-testprocedure.md`](../handboek/02-testprocedure.md) (**T1 t/m T13**).

Twee aandachtspunten die de bovenstaande scenario's op hardware kunnen laten falen zonder dat er
een bug is:

1. **Sensing-vloer (SP6).** Elke reactietijd wordt op hardware opgetrokken tot ~7 s. Een event met
   `reactietijd_s: 3` (bomaanslag) is in de simulator 3 s en op het veld 7 s. `node.warn` meldt het klemmen.
2. **Settle-latentie.** Een paalwissel is pas "echt" na `(minSamples + switchSamples)` scans. Wie in
   de laatste seconde van de reactietijd nog loopt, landt in de `grace`-fase — en anders in het
   *volgende* event als `VRIJ GEWANDELD`. Verlaag `pofSettleGrace` niet zomaar.
