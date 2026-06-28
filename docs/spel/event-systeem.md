# Magnum Opus — Event-systeem & verplaatsingscontrole

> Leidend document voor hoe events bedoeld zijn en hoe de **verplaatsingscontrole** na elk
> event werkt. De belangrijkste sectie is "Verplaatsing = reeks atomaire acties": daar zit het
> concept waarop de portaal-logica eerder fout liep.
>
> **Consistent houden:** bij elke wijziging aan het event-systeem moeten dit document,
> `docs/spel/events.md` en `docs/spel/event-catalogus.md` samen kloppen.

---

## 1. Kernprincipe: scheid "wat gebeurde" van "wat mag"

Elk event beschrijven we met drie dingen:

- **Preconditie** — wat waar moet zijn *vóór* het event mag plaatsvinden.
- **Effect** — welke velden dit event *mag* wijzigen, en hoe. Wat hier niet staat, mag niet veranderen.
- **Invariant** — wat *altijd* waar moet blijven, ongeacht het event.

Twee regels die alles schaalbaar houden:

1. **Centraliseer de invarianten** — schrijf de algemene regels één keer, als checks die ná
   *elk* event draaien. Niet per event herhalen.
2. **Frame-check** — neem een snapshot vóór het event en controleer dat enkel de velden uit het
   effect-lijstje veranderd zijn (bv. een niet-geselecteerde speler die toch bewoog = bug/straf).

Een nieuw event toevoegen = preconditie + toegestane wijzigingen schrijven; het zware werk
(invarianten) staat al klaar.

---

## 2. State-model (wat de validator kent)

```
speler:   id, positie (paal/uur), levensuren, sterftes, (rol)
paal/uur: index, is_happy_hour (goud LED), portaal_partner (paars LED als gezet)
speelveld: voorwaartse_richting (klok loopt ROND), actieve_portalen [{a,b}], happy_hour_palen []
```

In Node-RED: `spelerStats` (levensuren=`totaalUren`, `sterftes`, `huidigePaal`), `spelerLocaties`
(settled paal per speler), `bordStaat[uur].effecten` (toestanden `portaal`/`happy_hour` met
`data.partner` en `resterendeRondes`), `palenActief` (de ring 1..24 in sim), `spelerEigenschappen`
(`{ naam: { kleur, jaar } }` voor groep-doelwitten, uit `[CONFIG] Speler-eigenschappen`).

**Groep-doelwit**: een event met `doelwit.type === "groep"` kiest via `veld` (`kleur`/`jaar`) één
waarde (willekeurig onder de actieve spelers, of vast via `waarde`) en richt zich op **alle** actieve
spelers met die waarde. De controle behandelt elk groepslid als doelwit; de afroep noemt enkel het
groep-label `veld: waarde` (prefix "een groep"), niet de individuele spelers.

**Ziekte**: `global.ziekeSpelers` (`{ naam: rondesOver }`) houdt de zieke spelers + resterende events
bij; **medicijn** is een uur-effect (`medicijn` in `bordStaat`, felroze LED) dat **niet** veroudert. Een
zieke loopt door de **normale** controle (geen vrijstelling): verdient **geen** levensuren, verliest wél
bij onwettige zetten. Genezen kan **enkel** bij een **wettelijke** zet (OK / OK (stil)) op een
medicijn-uur — "Verifieer beweging" zet die spelers in `global.pofGenezen`. De node "Ziekte-beheer" doet
elke ronde na de controle: genezen (uit `pofGenezen`), **medicijn-verbruik** (elk medicijn waar deze
ronde iemand genas verdwijnt — felroze LED uit, ook bij één genezer), aftellen voor **álle** resterende
zieken, **dood** bij 0 (uren 0 + sterfte), hartslag-waarschuwing vanaf ≤ 3, en opschoning van de
medicijnen als er geen zieken meer zijn. Zie `docs/spel/event-catalogus.md`.

**Tijdbom**: `global.tijdbomSpelers` (`{ naam: rondesOver }`) houdt de bom-spelers bij; `global.tijdbomOntmantelPalen`
zijn de gekozen palen-mét-drukknop waarop ontmanteld kan worden (uur-effect `tijdbom`, felrood/knipperend,
`ACTIE_TIJDBOM` 13). De node **"Knop-verwerking"** (op `plaatjes/data` `{paal,knop:1}`, werkt in **elke**
fase) probeert te ontmantelen wanneer een bom-speler op een ontmantel-paal staat: **dag** (7–18) 80%,
**nacht** (19–6) 50%. Slagen = bom weg; mislukken = iedereen op die paal verliest `uur` levensuren. De node
**"Tijdbom-beheer"** (elke ronde na de controle) telt af en laat de bom bij 0 ontploffen (= mislukking) en
publiceert `pof/tijdbom`. **Toestand-exclusiviteit**: events met `exclusiefGroep` (ziekte én tijdbom =
`"speler-toestand"`) worden niet op een speler gelegd die al zo'n toestand heeft, tenzij `global.toestandExclusief`
op `false` staat (Systeeminstellingen). De **`global.tempoFactor`** (Systeeminstellingen) schaalt de reactietijd.
Drukknop-palen staan in `global.drukknopPalen` (`[CONFIG] Drukknop-palen`, ook retained op `config/drukknoppen`).

**Tornado**: een **één-shot** toestand-event (uur-doelwit). "Voer gevolg uit" zet `global.tornadoActief`
(`[{center, randen:[a,b]}]`); "Sync toestanden + LEDs" overschrijft de center-LED (`ACTIE_TORNADO` 14,
donkergrijs) en de buururen (`ACTIE_TORNADO_RAND` 15, grijze pulse) — wint van een onderliggend effect.
Het uur-doelwit wordt gekozen met `event.minAfstand` (ring-afstand ≥ 3) zodat twee tornado's niet overlappen.
"Verifieer beweging" heeft een **tornado-tak**: spelers die op een rand-uur startten moeten op het center
eindigen, anders verliezen ze **al** hun levensuren (geen sterfte, status `WEGGEZOGEN`); daarna wordt
`tornadoActief` geleegd + een LED-rebuild geforceerd zodat de oorspronkelijke LED's terugkeren. Het
"Volgende events"-paneel toont `global.pofWachtrij`; een entry wegklikken (`sim/wachtrij-weg`) splice't die
index zodat het event niet voorkomt (de rij vult zich weer aan in "Bouw pof/status").

**Spel-tempo**: `global.spelTempoFactor` (start 1,0, **range 0,6–1,3**) wordt door de wereld-events
`sneller_events` (−0,1, min 0,6) en `trager_events` (+0,1, max 1,3) gestapt (gevolg `{type:"tempo", richting}`)
en vermenigvuldigt in "Voer gevolg uit" de reactietijd van elk volgend event — bovenop de test-`tempoFactor`.
Reset naar 1,0 bij Stop/Herstart. **Slechte aura**: events met `slechteAura: true` (Ziekte, Tijdbom) kiezen
hun speler-doelwit in "Kies event" **gewogen** naar regio (avond uur 20–6 ×1,10, middernacht uur 24 ×1,15,
dag ×1,00) wanneer `global.badAuraAan !== false` (Spelinstellingen-tab → `sim/spel-config`). Bovenop dat
regiogewicht telt de **valsspeel-aura** per speler: elke foute verplaatsing geeft `+1 valsspeelpunt` en
`+3% auraValsspeel` (in "Verifieer beweging"); het gewicht wordt `× (1 + auraValsspeel/100)`, dus
valsspelers worden vaker doelwit. `auraValsspeel` reset zodra de speler zelf zo'n event krijgt.

**Event-tiers**: elk event heeft een `tier` (keuze-gewicht common 50 / uncommon 25 / rare 15 / epic 8 /
legendary 2). De engine kiest gewogen (in "Bouw pof/status" voor de wachtrij en in "Kies event" als
fallback); per event te overschrijven via de events-tab (`sim/tiers-config` → `global.eventTiers`).
**Tijd-terug**: "Kies event" snapshot de volledige spelstaat (`global.pofSnapshots`); `sim/tijd-terug`
herstelt de laatste en herpubliceert states + `pof/herstel-posities`. **Middernacht gate-block**: een
doelwit dat tegen de dichte poort wordt tegengehouden (eindigt op de poort-paal, exact de ring-afstand
gelopen) wordt niet bestraft voor te weinig/ongeldige keuze (M3b). **Dramatische animaties** (nuke/oogst/
tornado) gaan als één retained `pof/animatie`-bericht naar de simulator (robuust); de firmware blijft op
de per-paal acties.

**NUKE** (wereld): bij de controle ontploft elke nog **gedetecteerde** speler (in `spelerLocaties`) →
uren 0 + sterfte; wie ontkomen is overleeft. Geen bewegings-straffen. Een nuke **wist de wereld**: ze ruimt
ook de lopende **ziekte-episode** (`ziekeSpelers` + medicijn-effecten) en alle **dienaars** op, zodat het veld
daarna schoon is (Ziekte-beheer herpubliceert dan lege `pof/ziekte`/`pof/dienaars`). Daarna zet "Verifieer
beweging" de engine in de fase **`regroup`** (`regroup_s` s, standaard 60) i.p.v. de normale aanloop; de
"Engine tick" telt die af en keert daarna terug naar `aanloop`/`wacht`. Reset (`Stop`/`Herstart`) wist
`ziekeSpelers`, `pofGenezen`, `dienaars` en publiceert lege `pof/ziekte`/`pof/dienaars`.

**Middernacht** (permanent mechanisme, geen event): globals `piDigits` (500 cijfers, Machin/BigInt),
`midnightIndex`/`midnightOpen`/`midnightRemaining` (poort-staat, **niet** gereset bij Stop — π loopt door),
`middernachtPaal` (= hoogste actieve paal), `dienaars` (`{geoogste: meester}`, wél gewist bij Stop). De node
"Middernacht" draait **één keer per event** (getriggerd door "Kies event"): ze schuift de π-fase op, zet de
poort open/dicht en publiceert `pof/middernacht`. Het mechanisme is **uitschakelbaar** via de simulator-checkbox
"Middernacht actief" (`sim/middernacht-config` → global `middernachtAan`): staat ze uit, dan zet de node
`middernachtActief=false` en is de hoogste paal een **gewoon uur** (geen poort-LED, geen oversteek-blokkade,
geen oogst, kiesbaar als uur-doelwit); de π-stand blijft bevroren en loopt verder zodra je weer inschakelt. Bij een **0**-cijfer worden alle spelers op de
middernacht-paal **geoogst** (uren 0 + sterfte + dienaar van de armste). In "Verifieer beweging": een speler
die bij een **dichte** poort **over de poort heen steekt** (de voorwaartse 24→1-wrap, `r.kruist`) verliest
**al zijn levensuren + 1 sterfte** → `MIDDERNACHT DICHT`; tot aan de poort lopen zonder oversteken mag wél. De
admin-knop "Middernacht-klok → start" (of "Reset ALLES") zet de π-stand terug naar de startstand. En een
**dienaar** verdient niets voor zichzelf — positieve `delta` gaat naar `stats[meester].totaalUren` (verlies
+ sterfte blijven bij de dienaar).

---

## 3. ⭐ Verplaatsing = reeks atomaire acties

Een verplaatsing is **niet** "speler van X naar Y", maar een **geordende reeks atomaire acties**:

### STAP (vooruit)
- Eén paal vooruit in de voorwaartse richting (klok loopt rond: na 24 → 1).
- Verbruikt **1** budget; levert **1 levensuur** (vóór happy-hour-×2).
- Mag **nooit** achteruit — harde regel op STAP-niveau.

### TELEPORT (portaal)
- Van het ene portaal-eindpunt naar het andere, enkel als dat portaal **actief** is.
- Verbruikt **0** budget; levert **0 levensuren**.
- **Richting-agnostisch**: 13→20 én 20→13 zijn beide geldig (een wormgat, geen stap in de tijd).
- **Optioneel** (een geldig pad mag uit louter STAPpen bestaan, ook al lag er een portaal).
- **Max 1× per portaal per verplaatsing** (geen pingpong).

### De fout die vermeden wordt
Richting **nooit** afleiden uit de netto begin/eind-verplaatsing. `eind < begin` is op zichzelf
**geen** bewijs van vals spel — een legale portaal-sprong kan de eindpaal lager maken. Beoordeel
**actie per actie**; de "niet achteruit"-regel geldt enkel op STAP, nooit op TELEPORT.

### Hoe het pad bekend is (implementatie)
We leiden het pad af uit de **settled paalwissels** (uit de locatiebepaling) tijdens de
reactietijd: een geordende reeks hops `[van,naar]` per speler (`global.pofPad`). Classificatie:
- `{van,naar}` = de twee eindpunten van een **actief portaal** → **TELEPORT** (0 stappen).
- anders: voorwaartse afstand `fd`, achterwaartse `bd` (ring); `fd ≤ bd` → `fd` STAPpen vooruit,
  anders `bd` STAPpen achteruit (= verboden).

> In de **simulatie** is dit deterministisch (drag = settled posities; sleep-op-portaal = de
> A→B-hop tussen de twee paarse palen = TELEPORT). In het **echte spel** zijn de paarse palen het
> portaal; een robuuster expliciet signaal (bv. drukknop) kan later toegevoegd worden. De
> logica-laag is correct gegeven correcte settled-input; de sensingkwaliteit is de ondergrens.

---

## 4. De evenement-cyclus

1. **Aanloop** — timer telt af.
2. **Event kiezen + tonen** — respecteer de `max`/`getal`-grenzen van het event.
3. **Doelwitten bekendmaken** — wie is geselecteerd (afroep: aantal + zelfst.nw + tekst).
4. **Reactietijd** — geselecteerde spelers bewegen; pad wordt opgenomen.
5. **Controle** — de verplaatsingscontrole draait (sectie 7): pad actie-per-actie + invarianten.
6. **Toestanden opschonen** — verlopen toestanden (`resterendeRondes ≤ 0`) verdwijnen; portalen/
   happy hour die nog actief zijn blijven.

Portalen en happy hour zijn **toestanden** (open in hun eigen event, blijven actief tot ze
aflopen of voor altijd als er geen einde is). De **levensduur** van een toestand komt uit
het event-veld **`duratie`** (op event-niveau): een vast getal, een `[min,max]`-bereik dat
willekeurig gerold wordt, of een preset (`kort`/`middel`/`lang`). Dat getal wordt
`resterendeRondes` en telt elke ronde af in stap 6.

---

## 5. Globale invarianten (na ELK event)

1. Geen speler heeft negatieve levensuren (clamp op 0; onder 0 → +1 sterfte).
2. Elke `speler.positie` is een bestaande paal.
3. **Frame-check**: geen niet-geselecteerde speler veranderde van positie of levensuren ongestraft.
4. Elke STAP ging vooruit (TELEPORT is de enige uitzondering; geen achteruit-events voorlopig).
5. Niemand verbruikte meer budget dan het event toestaat.

---

## 6. De events

### Event A — "maximum x vooruit" (`verplaatsing2`)
Geselecteerde spelers mogen tot `x` palen vooruit; minder mag, meer niet, achteruit niet.
- **Preconditie**: elke geselecteerde speler staat op een geldige paal.
- **Effect**: `positie` via geldige verplaatsing met `aantal_STAP ≤ x`; `levensuren += verdiend`.
- **Invariant**: `aantal_STAP ≤ x`; elke STAP vooruit.

### Event D — "x of y vooruit" (`of_verplaatsing`)
Geselecteerde spelers moeten **exact `x` óf exact `y`** palen vooruit; elk ander aantal is fout.
`x` rolt uit `getal` (laag, 1–3), `y` uit `getal2` (`[4,6]`); een `[min,max]`-bereik houdt `y > x`.
- **Preconditie**: elke geselecteerde speler staat op een geldige paal.
- **Effect**: `positie` via geldige verplaatsing met `aantal_STAP ∈ {x, y}`; `levensuren += verdiend`.
- **Invariant**: `aantal_STAP === x` of `aantal_STAP === y`; elke STAP vooruit. Anders ONGELDIGE KEUZE (−voor).

### Event B — "portalen" (`portalen`, toestand)
Twee palen worden paars; er ontstaat een portaal. Verplaatst zelf niemand.
- **Preconditie**: twee bestaande palen, nog niet aan een portaal gekoppeld.
- **Effect**: `actieve_portalen += {a,b}`; beide palen paars (LED actie 1).
- **Invariant**: een portaal koppelt precies twee verschillende palen. `max: 1`.

### Event C — "happy hour" (`happy_hour`, toestand)
Eén of meer uren worden goud; wie zijn verplaatsing op zo'n uur **eindigt** krijgt de levensuren
van die verplaatsing verdubbeld.
- **Preconditie**: bestaande paal/palen.
- **Effect**: `happy_hour_palen += index`; goud (LED actie 2).
- **Invariant**: beïnvloedt enkel de levensuren-berekening bij verplaatsing, niet budget/positie.
  `max: 2`.

---

## 7. Verplaatsingscontrole + scoring (Verifieer beweging)

Per speler, op basis van het opgenomen pad (`pofPad[speler]`):

1. Classificeer elke hop → `voor` (STAP vooruit), `achter` (STAP achteruit, verboden), TELEPORT
   (0, max 1×/portaal — anders ongeldig).
2. **Beoordeel + scoor** (Δ = levensuren toegekend/afgetrokken):

| Geval | Status | Δ |
|------|--------|---|
| doelwit, geldig (`achter=0`, `voor ≤ x`) | OK | **+voor** (×2 als eindpaal happy hour) |
| doelwit, `voor > x` | TE VEEL | **−(voor − x)** |
| doelwit `of`, `voor ∉ {x, y}` | ONGELDIGE KEUZE | **−voor** |
| doelwit, `achter > 0` | TERUG IN TIJD | **−achter** |
| doelwit, >1× zelfde portaal | ONGELDIGE TELEPORT | **−voor** |
| niet-(bewegings)doelwit dat bewoog | BEWOOG (mocht niet) | **−(voor+achter)** |
| stil blijven staan | OK (stil) | 0 |

3. **Sterfte**: zou Δ de levensuren onder 0 brengen → blijf op 0 en **+1 sterfte** (speler speelt
   door; legale winst geeft nooit een sterfte).
4. `mag_niet_bewegen`-effect → Δ = 0.
5. De controle meldt per speler **welke regel brak + waarden** (status + Δ) naar de tabel
   **Controle** en `pof/controle`. TELEPORTs tellen niet mee in `voor` (dus niet in de levensuren).

### Levensuren-berekening
```
basis = aantal STAP-acties (teleports tellen niet)
verdiend = (eindpaal happy hour) ? 2*basis : basis
```

---

## 8. Grenzen

- De logica is deterministisch correct **gegeven correcte settled-input**; in de simulatie volledig
  toetsbaar (geen RSSI-ruis).
- Op hardware is de **sensingkwaliteit de ondergrens**: slechte dwell-detectie = foute input.
- Sub-sensing valsspel (te snel om te settelen, eindigt legaal) kan ontsnappen; gedetecteerde
  tussenstappen worden wél bestraft.

---

## 9. Aannames (bevestigd)
1. Levensuren-basis = aantal STAP-acties (1 stap = 1 levensuur).
2. Klok loopt **rond** (na 24 → 1).
3. Max **1 teleport per portaal** per verplaatsing.
4. STAP achteruit altijd verboden; geen achteruit-events (voorlopig).
