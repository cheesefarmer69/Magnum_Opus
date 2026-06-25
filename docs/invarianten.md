# Magnum Opus — Invarianten

Een **invariant** is een eigenschap die **altijd** waar moet zijn, ongeacht welk event
speelt, welke actie een speler doet, of welke toestand het systeem heeft. Dit document
bundelt alle invarianten van het systeem op één plek.

> Gerelateerde specs: `docs/spel/event-systeem.md` (leidend voor spelregels),
> `docs/protocol.md` (communicatieprotocol), `Design_rules.md` (ontwerp- en werkregels).

---

## 1. Spelregels — levensuren & sterftes

| # | Invariant |
|---|-----------|
| S1 | Geen speler heeft **negatieve levensuren**. Zou een Δ de levensuren onder 0 brengen, dan worden ze op **0** vastgehouden en krijgt de speler **+1 sterfte**. |
| S2 | Levensuren worden **uitsluitend bij de controle** toegekend — niet live tijdens de reactietijd. |
| S3 | **Legale winst** (doelwit, geldig pad) veroorzaakt **nooit een sterfte**. |
| S4 | **Sterftes en globale stats** (levensdagen, totaalUren, sterftes) **blijven bewaard** bij `Stop spel`. Enkel `[BEHEER] Wis globale stats` zet ze terug op 0. |
| S5 | **`Stop spel`** reset enkel de partij (effect-registers, posities, teller) — nooit de globale stats. |

---

## 2. Verplaatsing — pad-gebaseerde beoordeling

| # | Invariant |
|---|-----------|
| V1 | Een verplaatsing is een **geordende reeks atomaire acties** (STAP + TELEPORT). Richting en score worden **actie-per-actie** bepaald — nooit uit de netto begin/eind-verplaatsing. |
| V2 | Een **STAP** gaat altijd **vooruit** (klok loopt rond: na 24 → 1). Een STAP achteruit is altijd verboden. |
| V3 | Een **TELEPORT** verbruikt 0 budget, levert 0 levensuren, is **richting-agnostisch** (ook van hoger naar lager uur is legaal), en mag **max 1× per portaal per verplaatsing** (geen ping-pong). |
| V4 | Een legale portaal-sprong van een hoger naar een lager uur geeft **geen "TERUG IN TIJD"** — de controle is portaal-bewust. |
| V5 | Bij elk event mag enkel het **beweging-doelwit** bewegen. Elke andere speler die beweegt krijgt straf `−(voor+achter)`. |
| V6 | Niemand verbruikt meer budget dan het event toestaat (`voor ≤ x` bij max-event). |
| V7 | Scoring: `basis = aantal STAP vooruit`; `verdiend = (eindpaal happy-hour) ? 2×basis : basis`. |

### Scoringtabel (na elke controle)

| Geval | Status | Δ levensuren |
|-------|--------|--------------|
| doelwit, geldig (`voor ≤ x`, geen achterstap) | OK | **+voor** (×2 op happy-hour-eindpaal) |
| doelwit, `voor > x` | TE VEEL | **−(voor − x)** |
| doelwit, `voor < x` (min-event) | TE WEINIG | **−voor** |
| doelwit, `voor ∉ {x, y}` (of-event) | ONGELDIGE KEUZE | **−voor** |
| doelwit, achterwaartse STAP | TERUG IN TIJD | **−achter** |
| doelwit, >1× zelfde portaal | ONGELDIGE TELEPORT | **−voor** |
| niet-doelwit dat beweegt | BEWOOG (mocht niet) | **−(voor+achter)** |
| stil blijven staan | OK (stil) | 0 |

---

## 3. Speelveld & klok

| # | Invariant |
|---|-----------|
| F1 | De klok telt **24 uren** (palen), genummerd 1–24. Na paal 24 volgt paal 1 (rond). |
| F2 | Elke `speler.positie` is een **bestaande, actieve paal** (`palenActief`). |
| F3 | De **voorwaartse richting** is vaste klokrichting — er zijn voorlopig geen achteruit-events. |

---

## 4. Effecten & toestanden

| # | Invariant |
|---|-----------|
| E1 | Een **portaal** koppelt **precies 2 verschillende palen** via `data.partner`. `max: 1` = hooguit 1 portaal tegelijk. |
| E2 | **Happy hour** beïnvloedt enkel de levensuren-berekening bij verplaatsing, niet het budget of de positie. `max: 2`. |
| E3 | **LED-toestanden zijn effect-gedreven.** "Sync toestanden + LEDs" leidt de LED-kleur af uit het actieve uur-effect (`portaal` → paars, `happy_hour` → goud, `medicijn` → felroze). Een loopt af of het spel stopt → Node-RED stuurt `ACTIE_NIETS`. Toestand-events hebben geen `commando`-gevolg nodig voor hun LED. |
| E4 | Elk effect krijgt automatisch `bron` (event-id) en `instId` (één per afvuring). Alle effecten van **één afvuring** delen dezelfde `instId`. |
| E5 | `resterendeRondes` telt elke ronde af met −1. Bij ≤ 0 wordt het effect verwijderd. |
| E6 | De **max-engine** telt actieve instanties als `distinct instId met bron === event.id` over alle drie registers (`bordStaat`, `spelerEffecten`, `wereldEffecten`). |
| E7 | `mag_niet_bewegen` (speler-effect) → de speler verdient **geen** levensuren door te bewegen in die ronde (positie mag wel bijgewerkt worden). |
| E8 | `events_sneller` (wereld-effect) → de reactietijd van elk event wordt **gehalveerd** (min 1 s). |

---

## 4b. Ziekte

| # | Invariant |
|---|-----------|
| Z1 | Zieke spelers staan in `global.ziekeSpelers` (`{ naam: rondesOver }`), apart van de effect-registers; `medicijn` is een **niet-verouderend** uur-effect in `bordStaat`. |
| Z2 | Een **zieke** speler doorloopt de **normale** verplaatsingscontrole (geen vrijstelling): verdient **geen** levensuren, **verliest** ze bij een onwettige zet, en krijgt "BEWOOG (mocht niet)" als hij beweegt zonder bewegings-doelwit te zijn. |
| Z3 | Genezen kan **enkel** bij een **wettelijke** zet (status OK / OK (stil)) die op een **medicijn-uur** eindigt → die spelers staan in `pofGenezen`, "Ziekte-beheer" verwijdert ze uit `ziekeSpelers`. Zodra **iemand** op een medicijn-uur geneest wordt dat medicijn **verbruikt** (verdwijnt; roze LED uit) — ook bij één genezer, zodat één medicijn nooit meerdere spelers na elkaar geneest. |
| Z4 | Een zieke telt elke ronde 1 af; bij **0** zonder medicijn → **dood**: levensuren → **0** én **+1 sterfte**. |
| Z5 | Vanaf `rondesOver ≤ 3` krijgt een zieke elke ronde een hartslag-waarschuwing op zijn uur: `ACTIE_ZIEK_W3/W2/W1` (5/6/7) = monitor-piep + 3/2/1 hartslagen (= events resterend). |
| Z6 | Zijn er **geen zieken** meer (allen genezen of dood) → **alle** medicijn-effecten worden verwijderd; ongebruikte felroze palen komen weer vrij. |
| Z7 | `max: 1` op het ziekte-event + persistente medicijnen ⇒ er is **hooguit één** ziekte-episode tegelijk. |
| Z8 | **Reset** (`Stop`/`Herstart`) wist `ziekeSpelers` én `pofGenezen` en publiceert een lege `pof/ziekte` (retained). Bij Start blijven er dus nooit oude zieken/medicijnen hangen. |

---

## 4c. NUKE (wereld-event)

| # | Invariant |
|---|-----------|
| N1 | Bij de NUKE-controle ontploft **elke speler die nog gedetecteerd is** (in `spelerLocaties`): levensuren → **0** én **+1 sterfte**. Wie ontkomen is (onder de RSSI-vloer / buiten het veld → niet in `spelerLocaties`) is **VEILIG**. |
| N2 | Tijdens een NUKE gelden **geen** bewegings-straffen (iedereen mág vluchten). |
| N3 | Na de ontploffing staat de engine in de fase **`regroup`** gedurende `regroup_s` s (standaard 60), daarna terug naar `aanloop`/`wacht`. |
| N4 | Een wereld-event heeft `doelwit.type === "geen"`; het kiest/afroept geen spelers of uren. |
| N5 | Een nuke **wist de wereld**: bij de controle worden de lopende ziekte-episode (`ziekeSpelers` + medicijn-effecten) en alle `dienaars` gewist; `pof/ziekte`/`pof/dienaars` worden leeg geherpubliceerd. Geen zieken/medicijnen/dienaars blijven een nuke overleven. |
| N6 | De groene NUKE-lichtshow (actie 8) dekt **alle bespeelbare palen behalve de middernacht-poort-paal** (de hoogste paal). Na de ontploffing forceert `paalLedForceRebuild` elke ring-paal terug naar zijn juiste toestand (actie 0 of een nog-actief effect) — geen paal blijft groen hangen. |

---

## 4d. Middernacht (permanent mechanisme)

| # | Invariant |
|---|-----------|
| M1 | De middernacht-poort volgt de **eerste 500 cijfers van π** (daarna opnieuw); ze start **open**, elk cijfer is de duur (events) van een fase, dan wisselt open↔dicht. De **π-sequentie loopt door** over Stop/Start heen (`midnightIndex`/`midnightOpen`/`midnightRemaining` worden **niet** gereset). De enige manier om de klok bewust terug naar de start te zetten is de admin-knop "Middernacht-klok → start" (topic `reset_klok`) of "Reset ALLES": die maakt de drie globals leeg, waarna de Middernacht-node opnieuw initialiseert (poort open, fase = eerste π-cijfer) en `pof/middernacht` + poort-LED herpubliceert. |
| M2 | De middernacht-node draait **één keer per event** (getriggerd door "Kies event"), plus eenmalig bij een `reset_klok`-trigger vanuit de admin. |
| M3 | Bij een **dichte** poort (LED rood, `midnightOpen === false`) mag **geen speler uur 1 (de laagste paal) lopend in zijn pad nemen** (de voorwaartse hoogste→laagste-wrap). Overtreding → `MIDDERNACHT DICHT`: **alle levensuren kwijt + 1 sterfte**. Tot aan de poort lopen zonder uur 1 te nemen mag. **Uitzondering:** een **TELEPORT** naar uur 1 via een actief portaal (portaal-hop telt niet als lopen). |
| M3a | De regel wordt op **twee** momenten gehandhaafd: live door de **poort-bewaker** (`c4a00000000000f0`, elke settled paalwissel zolang dicht, ook tussen events) én bij de **na-event-controle** (`Verifieer beweging`, pad-gebaseerd). Een dedup `global.mnGestraft[naam]` (gereset per ronde in `Kies doelwit`) zorgt dat de straf **hoogstens één keer per ronde** valt. |
| M3b | **Gate-block (geen straf)**: een bewegings-doelwit dat door de dichte poort wordt tegengehouden — eindigt op de poort-paal zonder over te steken en heeft exact de ring-afstand `start→poort` gelopen — wordt **niet** bestraft voor `TE WEINIG`/`ONGELDIGE KEUZE`; status wordt `OK` (+gelopen uren). Bv. op uur 22 met `max 5` mag je 2 (tot 24); een `of 3/6` dat door de poort maar 2 toelaat is geen overtreding. |
| M4 | Een **0** in de π-sequentie = **oogst**: elke speler op de middernacht-paal sterft (uren 0 + sterfte) en wordt **dienaar** van de **armste** niet-geoogste, niet-dienaar speler. **Elke meester krijgt hoogstens één dienaar**: bij meerdere gelijktijdig geoogsten worden ze in **willekeurige** volgorde toegewezen (eerste → armste vrije speler, tweede → op-één-na-armste, …); een reeds aangewezen meester valt af. Is er geen vrije meester meer, dan sterft de geoogste wél maar zonder meester. Een 0 verandert de open/dicht-volgorde niet. |
| M5 | Een **dienaar** verdient niets voor zichzelf: positieve `delta` gaat naar `stats[meester].totaalUren`; verlies + sterfte blijven bij de dienaar. Hij speelt door (events vallen nog op hem). |
| M6 | `dienaars` wordt gewist bij **Stop/Herstart** en door de admin-knop "Speler-toestanden → 0"; `pof/dienaars` (retained) wordt dan leeg gepubliceerd. |
| M7 | De poort-LED op de middernacht-paal is **wit** (open) / **rood** (dicht) via "Sync toestanden + LEDs"; bij een 0 toont de hele ring de **oogst-animatie** (actie 11). |
| M8 | Middernacht is **uitschakelbaar** via de simulator-checkbox (`sim/middernacht-config` → global `middernachtAan`). Bij `middernachtAan === false` zet de node `middernachtActief=false` en is de hoogste paal een **gewoon uur** (geen poort-LED, geen `MIDDERNACHT DICHT`, geen oogst, kiesbaar als uur-doelwit). De π-stand wordt niet aangeraakt en loopt verder bij heractivering. |

---

## 4e. Tijdbom (speler-toestand met drukknop-ontmanteling)

| # | Invariant |
|---|-----------|
| T1 | Bom-spelers staan in `global.tijdbomSpelers` (`{ naam: rondesOver }`); de gekozen ontmantel-palen in `global.tijdbomOntmantelPalen` (uur-effect `tijdbom` in `bordStaat`, LED `ACTIE_TIJDBOM` 13). |
| T2 | Het tijdbom-event kiest **evenveel** ontmantel-palen als bommen, willekeurig uit `global.drukknopPalen` (palen mét drukknop). |
| T3 | Een drukknop wordt verwerkt in **elke** fase (node "Knop-verwerking" op `plaatjes/data` `{paal,knop:1}`), onafhankelijk van de event-cyclus. Knoppen op niet-geconfigureerde palen worden genegeerd. |
| T4 | Ontmanteling slaagt met **80%** in de **dag** (uren 7–18) en **50%** in de **nacht** (uren 19–6). Slagen → bom weg, geen gevolgen. |
| T5 | Mislukte ontmanteling → **iedere** speler op die paal verliest `uur` levensuren (clamp ≥ 0; onder 0 → 0 + sterfte). De bom(men) op die paal zijn verbruikt. |
| T6 | Een bom telt elke ronde 1 af (node "Tijdbom-beheer"); bij **0** **ontploft** ze = **identiek aan een mislukte ontmanteling** (iedereen op de paal van de bom-speler verliest `uur` levensuren). |
| T7 | `max: 1` op het tijdbom-event ⇒ hooguit één tijdbom-episode tegelijk. Geen bommen meer → ontmantel-palen (`tijdbom`-effecten) worden opgeruimd. De stand staat op `pof/tijdbom` (retained). |
| T8 | **Toestand-exclusiviteit**: een event met `exclusiefGroep` (ziekte én tijdbom = `"speler-toestand"`) wordt **niet** toegekend aan een speler die al in een toestand van die groep zit, tenzij `global.toestandExclusief === false` (Systeeminstellingen → `sim/systeem-config`). Een **nuke** wist ook de tijdbom-episode. **Reset** (Stop/Herstart) wist `tijdbomSpelers`/`tijdbomOntmantelPalen`. |

> **Tempo**: `global.tempoFactor` (Systeeminstellingen) vermenigvuldigt de reactietijd in "Voer gevolg uit" (min 1 s).
> **Drukknoppen**: `global.drukknopPalen` komt uit `[CONFIG] Drukknop-palen` en wordt retained op `config/drukknoppen` gepubliceerd voor de simulator.

---

## 4f. Tornado (uur-toestand, één-shot)

| # | Invariant |
|---|-----------|
| TO1 | Een tornado kiest **1–2 center-uren** met onderlinge **ring-afstand ≥ `minAfstand` (3)** → center + buururen van twee tornado's overlappen **nooit**. State: `global.tornadoActief = [{center, randen:[a,b]}]`. |
| TO2 | De LED van het **center** is `ACTIE_TORNADO` (14, donkergrijs), de twee **buururen** `ACTIE_TORNADO_RAND` (15, trage grijze pulse). Deze **overschrijven** tijdelijk een onderliggend uur-effect op die palen. |
| TO3 | Spelers die op een **buur-uur** startten moeten bij de controle op het **center** staan → `GEVOLGD` (delta 0). Zo niet → `WEGGEZOGEN`: **alle** levensuren kwijt (`delta = -totaalUren`, `totaalUren=0`), **géén** sterfte. |
| TO4 | Spelers die niet op een buur-uur stonden, worden door de tornado **niet** gestraft (geen "BEWOOG"). |
| TO5 | Tornado is **één-shot** (`duratie: 1`): bij de controle wordt `tornadoActief` geleegd en `paalLedForceRebuild` gezet → de palen keren terug naar hun **oorspronkelijke** LED-staat. **Reset** (Stop/Herstart) wist `tornadoActief` ook. |

> **Wachtrij-dismiss**: het "Volgende events"-paneel toont `global.pofWachtrij`. Een entry wegklikken publiceert `sim/wachtrij-weg` `{index}`; Node-RED splice't die index zodat dat event niet voorkomt, en "Bouw pof/status" vult de rij weer aan tot 5.
> **Event-tiers**: keuze-gewicht per tier — common 50 / uncommon 25 / rare 15 / epic 8 / legendary 2. Effectieve tier = `global.eventTiers[id]` (sim-override via `sim/tiers-config`) ‖ `event.tier` ‖ `common`. Gewogen gekozen in "Bouw pof/status" (wachtrij) en "Kies event" (fallback).
> **Tijd-terug**: "Kies event" pusht bij elk event een diepe snapshot van de spelstaat op `global.pofSnapshots` (max 20); `sim/tijd-terug` popt en herstelt de laatste, herpubliceert de afgeleide states + `pof/herstel-posities` (sim zet de spelers terug). Gewist bij Stop/Herstart.
> **Dramatische animatie**: nuke/oogst/tornado worden als één retained `pof/animatie`-bericht gepubliceerd; de simulator animeert hierop (negeert de per-paal acties 8/11/14/15 → geen "stuck" palen). De firmware blijft op de per-paal acties via de betrouwbare FIFO.
> **Admin paal-reset**: de Admin-dropdown + "Reset paal → rust" zet één gekozen paal terug (effecten weg, LED 0); twee-staps (`admin_unlocked`).

---

## 4g. Spel-tempo & slechte aura

| # | Invariant |
|---|-----------|
| SP1 | `global.spelTempoFactor` (start **1,0**) vermenigvuldigt in "Voer gevolg uit" de reactietijd van elk volgend event (bovenop de test-`tempoFactor`). |
| SP2 | `sneller_events` stapt de factor **−0,1** (min **0,6**); `trager_events` **+0,1** (max **1,3**) → **range 0,6–1,3**. Gevolg `{type:"tempo", richting:"sneller"\|"trager"}`. |
| SP3 | De factor wordt naar **1,0** gereset bij Stop/Herstart (beide `resetSpelStaat`). De huidige waarde staat in `pof/status.spelTempo`. |
| SP4 | **Slechte aura**: events met `slechteAura: true` (Ziekte, Tijdbom — speler-events) kiezen hun doelwit **gewogen** naar regio: avond (uur 20–23 of 1–6) ×1,10, middernacht (uur 24) ×1,15, dag (7–19) ×1,00. Enkel actief als `global.badAuraAan !== false` (Spelinstellingen-tab → `sim/spel-config`). Uur-events en `geen`-doelwit (Nuke) vallen erbuiten; `selectie:"alle"` weegt niet. |

---

## 5. Events — formaat

| # | Invariant |
|---|-----------|
| EV1 | `selectie` is enkel `"willekeurig"` of `"alle"` — de `"rang"`-selectie (met `veld`/`richting`) bestaat niet meer. |
| EV2 | `categorie` is `"verplaatsing"` \| `"toestand"` \| `"wereld"` (soort event). Staat **los** van `doelwit.type` (`speler`/`uur`/`groep`/`geen`): een toestand kan een speler- óf uur-doelwit hebben. |
| EV3 | Actieve spelers = enkel spelers met bekende positie (`spelerLocaties`) en niet gepauzeerd. |
| EV4 | Actieve palen = `palenActief` (= `paaltjesLijst` in echt spel; = 1..24 in simulatie-modus). |
| EV5 | Een nieuw event toevoegen vereist: preconditie + toegestane veld-wijzigingen. De centrale invarianten-checks (dit document) draaien daarna automatisch. |

---

## 5b. PoF-doelen & stats per spel

| # | Invariant |
|---|-----------|
| D1 | Per PoF-spel geldt één **doel** (`global.pofDoel` `{type,x}`) + **aantal spelers** (`pofDoelAantal`); **auto-einde** (`pofAutoEinde`) stopt het spel zodra ≥ `aantal` spelers slaagden. |
| D2 | **Doel 1 (`verplaats_uur`)**: bereikt als `spelerStats[naam].verplaatstSpel >= x` (per-spel som van vooruit-gelopen uren, opgehoogd in "Verifieer beweging"). |
| D3 | **Doel 2 (`inhalen`)**: rivaal = volgende speler alfabetisch onder de deelnemers (**cyclisch**). Bereikt als A van een **lager uur** kwam (`startA < startB`), nu **≥ 1 voorbij** B eindigt (`endA >= endB+1`), en B's eindpositie **lopend (STAP)** passeerde. Een TELEPORT die voorbij B landt telt **niet**; portaal-terug-dan-lopen wél. Latcht in `doelBereikt` (blijft behaald). |
| D4 | "Doel-controle" publiceert retained `pof/doelstatus` (percentage + per-speler) en zet `global.pofDoelBereikt`; de simulator-zijbalk toont % + highlight. |
| D5 | **Stats per spel**: `spelerStats.totaalUren/sterftes` = huidig spel; bij **Stop** opgeteld bij `globaleStats` (cumulatief) en daarna gewist. `spelNummer` +1 per Start; reset via Admin "Reset ALLES" / "[BEHEER] Wis globale stats". De geslaagde-lijst + doel komen in `spelHistorie`. |

## 6. Simulator vs. echt spel

| # | Invariant |
|---|-----------|
| SIM1 | Sim en echt spel draaien **nooit tegelijk** (gedeelde engine, `simVeld24` bepaalt welke bron schrijft). |
| SIM2 | `simVeld24 === true` → alleen `Sim directe locatie` schrijft `spelerLocaties`; hardware-input wordt genegeerd. |
| SIM3 | `simVeld24 !== true` → alleen de echte locatiebepaling schrijft `spelerLocaties`; sim doet niets. |
| SIM4 | **Stop spel** op beide pagina's reset dezelfde partij-staat (gedeeld). Een simulatie kan het echte spel niet vervuilen. |
| SIM5 | Het `sim/bediening`-commando (engine-besturing voor het AI-testharnas, `tools/speltest/`) wordt **alleen uitgevoerd als `simVeld24 === true`** (sim-modus). Buiten sim-modus negeert "Verwerk sim-bediening" elk commando — een testharnas kan een echt spel dus nooit starten/stoppen/wissen. |

---

## 7. Communicatieprotocol

| # | Invariant |
|---|-----------|
| C1 | MAC-adressen zijn altijd **lowercase** — NimBLE geeft ze lowercase door; een adres met hoofdletters wordt niet herkend. |
| C2 | Seriële baudrate is altijd **115200 baud**. |
| C3 | JSON-berichten op serial zijn **één object per regel**, afgesloten met `\n`. |
| C4 | `paal_id` loopt van 1 t/m 24. Routing: 1–7 → master1, 8–16 → master2, 17–24 → master3. |
| C5 | ESP-NOW structs gebruiken altijd `__attribute__((packed))` — voorkomt alignment-issues tussen Xtensa (WROOM) en RISC-V (C3). |
| C6 | Master handhaaft een **ontvangst-whitelist** (`slaveAdressen[]`): pakketten van niet-geregistreerde slaves worden gedropt en niet doorgestuurd. |
| C7 | Bridge detecteert **alle CH340-masters automatisch** (VID 0x1A86, PID 0x7523) — geen vaste USB-poort vereist. Routering per `paal_id` wordt geleerd uit de eerste binnenkomende batch. |

---

## 8. Firmware (slave & master)

| # | Invariant |
|---|-----------|
| HW1 | **Geen `delay()`** in main loops — alles non-blocking via `millis()`. |
| HW2 | Slave GPIO3 (`INPUT_PULLDOWN`) = drukknop-framework; pulldown houdt de pin LOW zonder knop → geen valse triggers. |
| HW3 | Slave GPIO8 (ingebouwde LED, active-LOW) knippert bij elke **succesvolle ESP-NOW-zend**. |
| HW4 | Master GPIO2 (ingebouwde LED, active-HIGH) pulst bij elke **ontvangen slave-batch**. |
| HW5 | Rode LED GPIO6 (slave): **vrij** — oude diagnose-functie (batterij-waarschuwing + knop-puls) verwijderd, gereserveerd voor toekomstig gebruik. |
| HW6 | Batterijmeting op GPIO4 (slave) — waarde 0.0 = niet gemeten of onbekend. |
| HW7 | De actie-set is minimaal: enkel acties die direct aan een bestaand event hangen (0 = uit, 1 = portaal/paars, 2 = happy-hour/goud, 3 = buzzer-piep). |

---

## 9. Node-RED / flows

| # | Invariant |
|---|-----------|
| NR1 | `flows.json` wordt **chirurgisch** bewerkt (gerichte Perl/JSON::PP-patches, CRLF, `use utf8`). Nooit herschrijven via PowerShell `ConvertTo-Json`. |
| NR2 | Deployen = **`deploy-flows.ps1`** (Windows) of `deploy-flows.sh` (Pi) via de Admin API. `docker restart` herlaadt de repo-`flows.json` niet. |
| NR3 | `[CONFIG] Spelerslijst` en `[CONFIG] Paaltjeslijst` zijn essentieel: zonder hen is `global.spelersLijst` leeg → geen beacon-mapping → geen doelwit. |
| NR4 | MQTT broker-server in Node-RED config: **`192.168.1.43`** (niet `127.0.0.1` — Node-RED draait in bridge-netwerk). |
| NR5 | Enkel **beproefde dashboard-widgets** (`ui-button`, `ui-switch`, `ui-text`, `ui-table`). Onbekende widget-types kunnen de dashboardpagina laten crashen. |
| NR6 | `ui-switch` gebruikt het **Schakelaar C-patroon** (`passthru: false`, `decouple: "true"`, feedbacklus via function-node) om visuele live-update zonder page-refresh te garanderen. |
