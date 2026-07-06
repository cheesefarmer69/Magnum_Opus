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
| S6 | **Geheugen-caps (L4, 1 GB-Pi):** `spelHistorie` houdt **≤ 30** partijen (oudere gedropt; globale stats blijven apart); `pofSnapshots` (tijd-terug) heeft **diepte 10** en klont **niet** de event-log `pofHuidigSpel`; `globaleStats[n].skills` houdt **≤ 50** per speler. Voorkomt onbegrensde global-groei (die ook in de 30 s-`spel/state`-dump + `contextStorage` zit). Zie `docs/hardware/hardware-info.md`. |
| S8b | **Pauze = volledig uit het spel.** Een speler in `global.gespauzeerdePlayers` (dashboard Admin → "Speler pauze") wordt in "Verifieer beweging" **overgeslagen** (status "GEPAUZEERD", `delta 0`) en telt niet mee in tweeling/etenstijd, Klokslag of Infected (EV3). `gespauzeerdePlayers` zit in de `spel/state`-snapshot + rehydrate, dus de pauze-toestand **overleeft een Node-RED-herstart**. |
| S9 | **Handmatige speler-correctie (Admin).** Achter `admin_unlocked` kan de operator via Admin → "Handmatig bijstellen" één spelerveld (`totaalUren`/`sterftes`/`valsspeelpunten`/`godPunten`) op een waarde **zetten** of er een delta bij **optellen** (nooit onder 0). Muteert het **huidige-spel** `spelerStats[naam]` (vloeit bij Stop cumulatief door naar `globaleStats`). Bedoeld om een beacon-/detectiefout achteraf recht te zetten. |

---

## 2. Verplaatsing — pad-gebaseerde beoordeling

| # | Invariant |
|---|-----------|
| V1 | Een verplaatsing is een **geordende reeks atomaire acties** (STAP + TELEPORT). Richting en score worden **actie-per-actie** bepaald — nooit uit de netto begin/eind-verplaatsing. |
| V2 | Een **STAP** gaat altijd **vooruit** (klok loopt rond: na 24 → 1). Een STAP achteruit is verboden — **behalve** tijdens een **tijdreizen**-wereld-event (zie V8). |
| V3 | Een **TELEPORT** verbruikt 0 budget, levert 0 levensuren, is **richting-agnostisch** (ook van hoger naar lager uur is legaal), en mag **max 1× per portaal per verplaatsing** (geen ping-pong). |
| V4 | Een legale portaal-sprong van een hoger naar een lager uur geeft **geen "TERUG IN TIJD"** — de controle is portaal-bewust. |
| V5 | Bij elk event mag enkel het **beweging-doelwit** bewegen. Elke andere speler die beweegt krijgt status "BEWOOG (mocht niet)" en verdient **0** levensuren (proportioneel model — geen aftrek meer, zie **V11**). Dit geldt vanaf de **doelwit-reveal** (`bezig`): paalwissels tijdens de reveal worden óók opgenomen en gecontroleerd — er is geen blind venster meer waarin bewegen ongestraft blijft (S3). **Enkel binnen een event** (fases `bezig`/`reactie`/`wacht_controle`/`grace`); **tussen** events is beweging vrij — zie **V10**. |
| V6 | Niemand verbruikt meer budget dan het event toestaat (`voor ≤ x` bij max-event). |
| V7 | Scoring: `basis = aantal STAP vooruit`; `verdiend = (eindpaal happy-hour) ? 2×basis : basis`. |
| V8 | **Tijdreizen** (wereld-event, `global.tijdreizenActief`): zolang actief telt een **achterwaartse** STAP **mee** als geldige beweging — de stappen worden `voor + achter` voor de voorwaarde-check én de score (geen "TERUG IN TIJD"-straf). Uitzondering: een **achterwaartse middernacht-oversteek** (`ontleed().kruistAchter`, de 1→24-wrap) blijft verboden → "TERUG IN TIJD". Tijdreizen opent de poort niet (M3 blijft gelden voor de voorwaartse oversteek). Buiten tijdreizen gelden V2/V4 onveranderd. |
| V9 | **Settle-grace** (`global.pofSettleGrace`, default 3 s; 0 = uit). In automatische modus draait de controle **niet** meteen bij `reactie`-einde (T) maar na een `grace`-fase (T+grace), zodat traag-settlende paalwissels nog in **dit** event landen. Het pad-opname-venster (`Bereken levensuren`) omvat de fases `bezig` (de doelwit-reveal — zie V5), `reactie`, `wacht_controle` én `grace`; de begin-snapshot van het volgende event wordt pas ná de controle (dus ná de grace) genomen. Manueel-modus gebruikt geen grace (de operator bepaalt zelf het controle-moment). |
| V10 | **Tussen events is beweging vrij.** In de fases **buiten** een event (`idle`/`aanloop`/`wacht`/`regroup`) mag **elke** speler **vrij en onbeperkt** verplaatsen: `Bereken levensuren` neemt daar géén pad op en er volgt géén straf — de **enige** uitzondering is de **middernacht-poort** (M3: bij dichte poort geen 24→1-oversteek). Dit is **bewuste counterplay** (positioneren voor het volgende event; o.a. een zieke die naar een medicijn-paal wandelt om bij de volgende controle als "OK (stil)" te genezen — Z3). De doelwit-beperking V5 geldt dus uitsluitend **binnen** een event (vanaf `bezig`). |
| V11 | **Proportioneel valsspel-model (nooit negatief).** Een foute doelwit-zet trekt **geen** levensuren meer af; hij levert `delta = max(0, legaalBasis − overtreding)` op (zie de scoringtabel). Omdat `delta ≥ 0` altijd geldt, kan de bewegingsscore `totaalUren` **nooit** doen dalen en dus **nooit** een sterfte veroorzaken (de sterfte-op-negatief-tak in `Verifieer beweging` is een vangnet dat niet meer vuurt). Valsspeelpunten (+1) + aura en de god-punt-consumptie blijven ongewijzigd; god-punt → `delta = 0` + geen valsspeelpunt. Dodelijke straffen zitten in aparte mechanismen (M3/N1/tornado/bom/Z4), niet in de bewegingsscore. Rationale: een gemiste/foute detectie (baken-fout) mag hooguit winst kosten, geen catastrofaal verlies. |

### Scoringtabel (na elke controle)

**Proportioneel model (V11):** valsspelen kost **nooit** levensuren — je **verdient er minder** naarmate je verder afwijkt, met een vloer op **0** (`delta = max(0, legaalBasis − overtreding)`). Een foute zet levert dus 0 (of minder winst), nooit een negatief saldo, en veroorzaakt **geen sterfte**. Valsspeel-bookkeeping (`valsspeelpunten +1`, aura) en de god-punt-consumptie blijven ongewijzigd. Dodelijke mechanismen zijn losgekoppeld van de bewegingsscore: **MIDDERNACHT DICHT**-oversteek (M3), **NUKE** (N1), **tornado**, **bom** en **ziekte-dood** (Z4) blijven wél uren-verlies + sterfte geven.

| Geval | Status | Δ levensuren (proportioneel, vloer 0) |
|-------|--------|--------------|
| doelwit, geldig (`voor ≤ x`, geen achterstap) | OK | **+voor** (×2 op happy-hour-eindpaal) |
| doelwit, `voor > x` (max-event) | TE VEEL | **max(0, x − (voor − x))** — overschot eet de winst |
| doelwit, `voor < x` (min-event) | TE WEINIG | **max(0, voor − (x − voor))** |
| doelwit, `voor ∉ {x, y}` (of-event) | ONGELDIGE KEUZE | **max(0, voor − afstand tot dichtste geldige)** |
| doelwit, achterwaartse STAP | TERUG IN TIJD | **max(0, voor − achter)** |
| doelwit, >1× zelfde portaal | ONGELDIGE TELEPORT | **0** |
| niet-doelwit dat beweegt | BEWOOG (mocht niet) | **0** |
| gepauzeerde speler (Admin) | GEPAUZEERD | 0 (niet gescoord — zie **S8b**) |
| stil blijven staan | OK (stil) | 0 |

---

## 3. Speelveld & klok

| # | Invariant |
|---|-----------|
| F1 | De klok telt **24 uren** (palen), genummerd 1–24. Na paal 24 volgt paal 1 (rond). |
| F2 | Elke `speler.positie` is een **bestaande, actieve paal** (`palenActief`). |
| F3 | De **voorwaartse richting** is vaste klokrichting — er zijn voorlopig geen achteruit-events. |
| F4 | **Heartbeat-gestuurde ring (L3, alleen hardware)**: een paal die ooit data stuurde maar > `SLAVE_STALE_MS` (60 s) stil is, wordt door "Evalueer spelstatus" **tijdelijk uit `palenActief`** gehaald (events/portalen/doelwitten kiezen hem niet meer; status "VEROUDERD (uit ring)", LED-rebuild) en komt **automatisch terug** zodra hij weer data stuurt. Nooit-geziene palen blijven in de ring (opbouwfase) en de ring zakt nooit onder **2** palen. In sim beheert "Sim-veld instellen" `palenActief`. |
| F5 | **Handmatige paal-override (Admin).** `global.palenHandmatigUit` (array paal-id's, gezet via Admin → "Palen handmatig uit/in", achter `admin_unlocked`) haalt een paal **onmiddellijk** uit de L3-ring — óók als hij nog data stuurt — zodat een kapot/flapperend bord uit het spel kan terwijl de rest doorloopt. De ≥2-palen-vloer van F4 blijft gelden (zakt de ring eronder, dan wordt de override genegeerd). "Terug in spel" verwijdert de paal weer uit de set; een LED-rebuild volgt automatisch. |

---

## 4. Effecten & toestanden

| # | Invariant |
|---|-----------|
| E1 | Een **portaal** koppelt **precies 2 verschillende palen** via `data.partner`. `max: 1` = hooguit 1 portaal tegelijk. |
| E2 | **Happy hour** beïnvloedt enkel de levensuren-berekening bij verplaatsing, niet het budget of de positie. `max: 1` — hooguit één happy-hour-**episode** tegelijk; één afvuring kan wél meerdere uren tegelijk goud kleuren (`aantal`, dichtheid-geschaald). |
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
| Z3 | Genezen kan **enkel** bij een **wettelijke** zet (status OK / OK (stil)) die op een **medicijn-uur** eindigt → die spelers staan in `pofGenezen`, "Ziekte-beheer" verwijdert ze uit `ziekeSpelers`. Zodra **iemand** op een medicijn-uur geneest wordt dat medicijn **verbruikt** (verdwijnt; roze LED uit) — ook bij één genezer, zodat één medicijn nooit meerdere spelers na elkaar geneest. De zieke bereikt die medicijn-paal typisch door **tussen** events vrij te wandelen (V10) en tijdens het event stil te staan ("OK (stil)"). |
| Z4 | Een zieke telt elke ronde 1 af; bij **0** zonder medicijn → **dood**: levensuren → **0** én **+1 sterfte**. |
| Z5 | Vanaf `rondesOver ≤ 3` krijgt een zieke elke ronde een hartslag-waarschuwing op zijn uur: `ACTIE_ZIEK_W3/W2/W1` (5/6/7) = monitor-piep + 3/2/1 hartslagen (= events resterend). |
| Z6 | Zijn er **geen zieken** meer (allen genezen of dood) → **alle** medicijn-effecten worden verwijderd; ongebruikte felroze palen komen weer vrij. |
| Z7 | `max: 1` op het ziekte-event + persistente medicijnen ⇒ er is **hooguit één** ziekte-episode tegelijk. |
| Z8 | **Reset** (`Stop`/`Herstart`) wist `ziekeSpelers` én `pofGenezen` en publiceert een lege `pof/ziekte` (retained). Bij Start blijven er dus nooit oude zieken/medicijnen hangen. |

---

## 4c. NUKE (wereld-event)

| # | Invariant |
|---|-----------|
| N1 | Bij de NUKE-controle ontploft **elke speler die nog gedetecteerd is** (in `spelerLocaties`): levensuren → **0** én **+1 sterfte**. Wie ontkomen is (niet meer in `spelerLocaties`) is **VEILIG (ontkomen)**. |
| N7 | **Ontsnappen op hardware = nuke-scoped prune.** `spelerLocaties` wordt buiten de nuke **nooit** opgeschoond (ghosts blijven staan). **Enkel** zolang `nukeActief === true` **én** niet-sim (`simVeld24 !== true`) haalt `Evalueer spelstatus` elke ~1 s spelers wiens `status_lastSeenMac` ouder is dan `nukeEscapeMs` (= `escape_s`×1000, default **4000 ms**) uit `spelerLocaties` → zij gelden bij de controle als VEILIG. Buiten de nuke blijft `spelerLocaties` accumulerend en ongewijzigd (Klokslag/Infected/overige events zien exact hetzelfde als voorheen). In sim regelt `Sim directe locatie` het ontsnappen al. Vereiste: `reactietijd_s ≥ escape_s + 2`. |
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
| M3a | De regel wordt op **twee** momenten gehandhaafd: live door de **poort-bewaker** (`c4a00000000000f0`, elke settled paalwissel zolang dicht, ook tussen events) én bij de **na-event-controle** (`Verifieer beweging`, pad-gebaseerd). De bewaker slaat het **hele event-venster** over — `reactie`, `wacht_controle`, `bezig` **én `grace`** — zodat de na-event-controle die oversteken afhandelt (zonder de grace-skip strafte een oversteek die in `grace` settelt zowel live als bij de controle → dubbele sterfte). Guard-actieve fases (`idle`/`aanloop`/`wacht`/`regroup`) en pad-opname-fases (`reactie`/`wacht_controle`/`grace`) zijn dus **disjunct**. Bovendien zorgt de dedup `global.mnGestraft[naam]` (gereset per ronde in `Kies doelwit`, gecheckt+gezet in zowel bewaker als `Verifieer beweging`) dat de straf **hoogstens één keer per ronde** valt. |
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

## 4h. Etenstijd (wolf vs. schapen-groep)

| # | Invariant |
|---|-----------|
| ET1 | Doelwit = een **groep** (kleur/jaar) = de **schapen**. De **wolf** = de speler met de **laagste `auraValsspeel`** (beste aura) **buiten** die groep. State: `global.etenstijd = {wolf, schapen[], gevangen[], over}`. `duratie: 15` rondes via een `wereldEffecten`-effect. |
| ET2 | **Vangst bij de controle**: staat de wolf op **hetzelfde uur** als een nog niet-gevangen schaap, dan steelt hij **`min(uur, schaap-totaalUren)`** levensuren van dat schaap → schaap **−buit + 1 sterfte**, wolf **+buit**. Elk schaap is **eenmalig** vangbaar (`gevangen`-lijst). |
| ET3 | Bij afloop ("Verouder effecten") en bij **Stop/Herstart** → `global.etenstijd = null`. De wolf staat in de wereld-effecten-tabel (`Etenstijd (wolf: <naam>)`). |

## 4i. Tweeling (gekoppeld bewegen)

| # | Invariant |
|---|-----------|
| TW1 | Een tweeling koppelt **2 spelers** (`global.tweelingen = [{a,b,inst}]`). **Max 3** paren (`max: 3`, geteld als niet-verouderend `wereldEffecten`-effect per paar — uitgezonderd van veroudering zoals medicijn). Wie al een tweeling is, wordt **uitgesloten** bij de doelwitkeuze (1 tweeling per speler). |
| TW2 | **Samen bewegen**: in elke controle moeten beide tweelingen **allebei** bewegen of **allebei** stil staan. **Asymmetrisch** (`bewogen(a) ≠ bewogen(b)`, beide deze ronde in `start`) → **beiden `totaalUren = 0`** (géén sterfte). |
| TW3 | **Dood-propagatie**: krijgt één tweeling deze ronde een **sterfte** (om het even welke oorzaak — beweging, middernacht, wolf, …), dan krijgt de andere **`totaalUren = 0` + 1 sterfte** en de **band verbreekt** (paar uit `tweelingen` + zijn wereld-effect verwijderd). Detectie via een sterfte-snapshot vóór de controle. |
| TW4 | **Geen duratie**: een tweeling blijft tot **spel-einde** of een **dood**. **Reset** (Stop/Herstart) wist `tweelingen`. |

---

## 4g. Spel-tempo & slechte aura

| # | Invariant |
|---|-----------|
| SP1 | `global.spelTempoFactor` (start **1,0**) vermenigvuldigt in "Voer gevolg uit" de reactietijd van elk volgend event (bovenop de test-`tempoFactor`). |
| SP2 | `sneller_events` stapt de factor **−0,1** (min **0,6**); `trager_events` **+0,1** (max **1,3**) → **range 0,6–1,3**. Gevolg `{type:"tempo", richting:"sneller"\|"trager"}`. |
| SP3 | De factor wordt naar **1,0** gereset bij Stop/Herstart (beide `resetSpelStaat`). De huidige waarde staat in `pof/status.spelTempo`. |
| SP4 | **Slechte aura**: events met `slechteAura: true` (Ziekte, Tijdbom — speler-events) kiezen hun doelwit **gewogen** naar regio: avond (uur 20–23 of 1–6) ×1,10, middernacht (uur 24) ×1,15, dag (7–19) ×1,00. Enkel actief als `global.badAuraAan !== false` (Spelinstellingen-tab → `sim/spel-config`). Uur-events en `geen`-doelwit (Nuke) vallen erbuiten; `selectie:"alle"` weegt niet. |
| SP5 | **Valsspeel-aura**: elke foute verplaatsing bij de controle (TE VEEL, TE WEINIG, ONGELDIGE KEUZE, TERUG IN TIJD, BEWOOG (mocht niet), ONGELDIGE TELEPORT, MIDDERNACHT DICHT) geeft de speler **+1 `valsspeelpunten`** en **+3% `auraValsspeel`** (in "Verifieer beweging"). Dat aura% vermenigvuldigt **bovenop** het SP4-regiogewicht: `gewicht × (1 + auraValsspeel/100)` → valsspelers worden relatief vaker doelwit van een slechte-aura-event. `auraValsspeel` **reset naar 0** zodra de speler door een slechte-aura-event getroffen wordt (in "Kies event", na de doelwitkeuze). `valsspeelpunten` blijft staan, telt mee in de globale eindstand (transferStats), en reset enkel bij zeroHuidig/Wis. |
| SP6 | **Sensing-vloer op de reactietijd (G1)**: na alle tempo-vermenigvuldigers (E8 `events_sneller`, test-`tempoFactor`, SP1 `spelTempoFactor`) klemt "Voer gevolg uit" de reactietijd op een **dynamische ondergrens** afgeleid van de locatiebepaling: `ceil(((minSamples + switchSamples) × max(scanDuurPerPaal, 1000 ms) + vensterMs/2) / 1000)` s (defaults → **7 s**). Zo kan geen enkele tempo-stapeling de reactietijd onder de fysieke settle-latentie duwen (fysiek correcte zetten worden nooit "TE WEINIG"/"BEWOOG" door traagheid van de sensing). Klemmen wordt gelogd via `node.warn`. |

---

## 4j. Avondspel (omgekeerde scoring + onmiddellijke dood)

| # | Invariant |
|---|-----------|
| AV1 | **Avond is een modus** (`global.avondModus`, retained `sim/avond-modus`) op het lopende spel — géén verse start. De middag-`totaalUren` en -stats (`sterftes`, `valsspeelpunten`) blijven behouden (zet avond aan zonder eerst te stoppen). |
| AV2 | In `avondModus` wordt in "Verifieer beweging" een **positieve** bewegingswinst een **kost** (`delta = -delta`), en `totaalUren` mag **negatief** worden (de 0-clamp-met-sterfte wordt overgeslagen). Legale verplaatsing veroorzaakt in de avond dus **geen** sterfte, enkel verlies. |
| AV3 | Een **`gestorven`** speler (nieuw `spelerStats`-veld, default `false`) kan door **verplaatsing** niet negatief gaan (beweging floort op 0); enkel **events** duwen hem verder omlaag. `gestorven` reset in `zeroHuidig`/"Wis globale stats". |
| AV4 | **Onmiddellijke dood** (gevolg `onmiddellijke_dood`, event `fase:"avond"`): het slachtoffer wordt **geloot** onder de niet-`gestorven`, actieve spelers met gewicht = **`sterftes + valsspeelpunten`**; 0 gewicht = immuun, som 0 → uniform. Het slachtoffer → `totaalUren=0`, `+1 sterfte`, `gestorven=true`. De cirkel-animatie (`pof/dood-anim`) is **Stop-veilig** via het `pofGeneration`-token (vuurt niet meer na een reset). |
| AV5 | **`fase`-veld** op events (`middag` default / `avond` / `beide`): "Kies event" én de simulator filteren hierop volgens `avondModus`. In de avond verschijnen enkel `avond`/`beide`; in de middag verdwijnt `avond`. |

---

## 5. Events — formaat

| # | Invariant |
|---|-----------|
| EV1 | `selectie` is enkel `"willekeurig"` of `"alle"` — de `"rang"`-selectie (met `veld`/`richting`) bestaat niet meer. |
| EV2 | `categorie` is `"verplaatsing"` \| `"toestand"` \| `"wereld"` (soort event). Staat **los** van `doelwit.type` (`speler`/`uur`/`groep`/`geen`): een toestand kan een speler- óf uur-doelwit hebben. |
| EV3 | Actieve spelers = enkel spelers met bekende positie (`spelerLocaties`) en niet gepauzeerd. **Ghost-prune (S1, alleen hardware)** houdt die positie vers: een beacon die > `global.spelerPruneMs` (default 90 s; tijdens een nuke het kortere `nukeEscapeMs`) niet meer gezien is, wordt uit `spelerLocaties` verwijderd — een dode/weggelegde beacon wordt dus geen eeuwige speler. Het gepauzeerd-filter geldt óók in de Klokslag- en Infected-engine. |
| EV4 | Actieve palen = `palenActief` (= `paaltjesLijst` in echt spel; = 1..24 in simulatie-modus). |
| EV5 | Een nieuw event toevoegen vereist: preconditie + toegestane veld-wijzigingen. De centrale invarianten-checks (dit document) draaien daarna automatisch. |
| EV6 | **Doelwit-dichtheid (G3):** `doelwit.aantal` als **string-optie** (`laag`/`midden`/`hoog`) schaalt met **N** (= actieve, niet-gepauzeerde spelers): `aantal = clamp(round(N × doelwitDichtheid × mult), 1, min(N,10))` met `mult` 0,6/1,0/1,8 en `global.doelwitDichtheid` (default **0,25**, dashboard-instelbaar). `enkel` = altijd 1; een **vast getal**, `[min,max]`-**array** en `selectie:"alle"` schalen **niet**. **Groep-events** krijgen in de tier-weging een boost `×(1 + 0,1·max(0, N−15))` — en die weging staat in **zowel** `Kies event` als `Bouw pof/status` (de vooruit-geplande wachtrij), anders wint de wachtrij. |

---

## 5b. PoF-doelen & stats per spel

| # | Invariant |
|---|-----------|
| D1 | Per PoF-spel geldt één **doel** (`global.pofDoel` `{type,x}`) + **aantal spelers** (`pofDoelAantal`); **auto-einde** (`pofAutoEinde`) stopt het spel zodra ≥ `aantal` spelers slaagden. |
| D2 | **Doel 1 (`verplaats_uur`)**: bereikt als `spelerStats[naam].verplaatstSpel >= x` (per-spel som van vooruit-gelopen uren, opgehoogd in "Verifieer beweging"). `verplaatstSpel` telt **enkel legale** voorwaartse stappen: de ophoging (`+r.voor`) gebeurt **ná** de statusbeoordeling en **alleen** bij een schone `status === "OK"` (legaal doelwit, gate-block M3b of legale open-poort-oversteek) — een foute of god-vergeven zet of een dichte-poort-oversteek telt **niet** mee, zodat vals spelen het doel + de +2 god-punten (D7) niet kan halen (S7). |
| D3 | **Doel 2 (`inhalen`)**: rivaal = volgende speler alfabetisch onder de deelnemers (**cyclisch**). Bereikt als A **achter B startte**, nu **≥ 1 voorbij** B eindigt, en B's eindpositie **lopend (STAP)** passeerde. Een TELEPORT die voorbij B landt telt **niet**; portaal-terug-dan-lopen wél. Latcht in `doelBereikt` (blijft behaald). **Ring-bewust:** "achter/voorbij" wordt gemeten via **voorwaartse ring-afstand** (`fwd(a,b) = ((idx(b)-idx(a))%N+N)%N`, met `idx`/`N` = actieve-palen-index), niet via absolute paalnummers — zodat inhalen ook rond de 24→1-naad correct werkt (aanname: A/B liggen binnen een halve ring, wat geldt bij de kleine event-budgetten). |
| D4 | "Doel-controle" publiceert retained `pof/doelstatus` (percentage + per-speler) en zet `global.pofDoelBereikt`; de simulator-zijbalk toont % + highlight. |
| D5 | **Stats per spel**: `spelerStats.totaalUren/sterftes` = huidig spel; bij **Stop** opgeteld bij `globaleStats` (cumulatief) en daarna gewist. `spelNummer` +1 per Start; reset via Admin "Reset ALLES" / "[BEHEER] Wis globale stats". De geslaagde-lijst + doel komen in `spelHistorie`. |
| D6 | **Meta-stats**: per speler ook `valsspeelpunten` (per-spel; bij **Stop** cumulatief in `globaleStats`), `auraValsspeel` (per-spel slechte-aura-% door valsspelen, zie SP5) en `godPunten` (**persistent** saldo over spellen heen, niet gewist bij Start/Stop — enkel door beheer-reset). Getoond in de dashboard-tabel "Vals-spelen & God-punten" (huidig spel) + de globale tabel. Registratie/gebruik: zie SP5 (valsspeel + aura) en D7 (god-punten). |
| D7 | **God-punten**: een speler die in een **lopend** spel zijn doel haalt, krijgt **eenmalig +2 god-punten** (in "Doel-controle"; `godAward`-latch, gereset bij spelstart/wis → opnieuw verdienbaar per spel). Bij een **foute verplaatsing** (de SP5-set, incl. MIDDERNACHT DICHT) wordt, als `godPunten > 0`, **automatisch 1 god-punt verbruikt**: de zet is dan **ongestraft** (geen levensuren/sterfte, status `… [GOD-PUNT]`) en telt **niet** als valsspelen (geen valsspeelpunt/aura). Een ziek persoon kan zo ook na een foute zet **genezen** op een medicijn-paal. Saldo `godPunten` is persistent; reset enkel via beheer-wis. |

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
| C4 | `paal_id` loopt van 1 t/m 24. Routing: 1–8 → master1, 9–16 → master2, 17–24 → master3. |
| C5 | ESP-NOW structs gebruiken altijd `__attribute__((packed))` — voorkomt alignment-issues tussen Xtensa (WROOM) en RISC-V (C3). |
| C6 | Master handhaaft een **ontvangst-whitelist** (`slaveAdressen[]`): pakketten van niet-geregistreerde slaves worden gedropt en niet doorgestuurd. |
| C7 | Bridge detecteert **alle CH340-masters automatisch** (VID 0x1A86, PID 0x7523) — geen vaste USB-poort vereist. Routering per `paal_id` wordt geleerd uit de eerste binnenkomende batch. |
| C8 | **Master-conflict-alarm (R6)**: announcet hetzelfde `MASTER_NR` binnen 10 s op **twee verschillende open poorten** (= twee borden met dezelfde env geflasht → stille route-flip-flop), dan publiceert de bridge `{"bridge_fout":"MASTER_CONFLICT","master":N,"poorten":[..]}` op `plaatjes/data` (max 1×/30 s). Node-RED toont dit als **ST-006 (FOUT)** in de pre-flight → NO-GO tot het verkeerde bord herflasht is. De master-`cmd_seq` slaat **0 én 0xFFFF** over (0 = "geen", 0xFFFF = slave-boot-sentinel, L5). |

---

## 8. Firmware (slave & master)

| # | Invariant |
|---|-----------|
| HW1 | **Geen `delay()`** in main loops — alles non-blocking via `millis()`. |
| HW2 | Slave GPIO3 (`INPUT_PULLDOWN`) = drukknop-framework; pulldown houdt de pin LOW zonder knop → geen valse triggers. |
| HW3 | Slave GPIO8 (ingebouwde LED, active-LOW) knippert bij elke **succesvolle ESP-NOW-zend**. |
| HW4 | Master GPIO2 (ingebouwde LED, active-HIGH) pulst bij elke **ontvangen slave-batch**. |
| HW5 | Rode LED GPIO6 (slave) heeft **twee** functies: (1) **drukknop-feedback** — brandt als de paal gewapend is (`ACTIE_KNOP_ARM`) en dooft zolang de knop ingedrukt is (via de knop-ISR); (2) **provisioning-fout-blink** — ritmisch knipperen wanneer het bord-MAC niet in `paal_macs.h` staat (bord doet niet mee). Zie `docs/hardware/pinout.md`. |
| HW6 | Batterijmeting op GPIO4 (slave) — waarde 0.0 = niet gemeten of onbekend. |
| HW7 | De actie-set hangt aan bestaande spel-/test-functies; de **volledige, gezaghebbende lijst** staat in `docs/protocol.md §2` (0 = uit, 1 = portaal, 2 = happy hour, … t/m 21 = led-config). Voeg nooit een actie toe zonder die tabel bij te werken. |
| HW8 | De **BLE-scan-vensterduur** is runtime instelbaar via `MSG_SCAN_CONFIG` (actie 20): niet-blokkerende scan begrensd door een `millis()`-venster (`scanDuurMs`). Default **1000 ms**, de slave **clamp't 300..2000 ms**. Verloren bij reboot (volatile) → Node-RED **herstelt** de ingestelde waarde automatisch op de eerstvolgende heartbeat (uptime-daling = reboot-detectie). |
| HW9 | De **LED-helderheid** is runtime instelbaar via `MSG_LED_CONFIG` (actie 21, globale FastLED-brightness): dashboard-slider + Min/Middel/Max op **alle** palen. De slave **clamp't 5..255** (nooit volledig uit) en past het toe met `setBrightness`+`show`; het **componeert** met de per-LED-schaling van Klokslag/animaties (die gebruiken `nscale8`/`CHSV val`, niet `setBrightness`). Volatile → default **150** bij boot; Node-RED **herstelt** de ingestelde waarde op de eerstvolgende heartbeat (zoals HW8) en bewaart hem retained op `config/led-helderheid`. "Max" (255) ~verdubbelt de LED-stroom t.o.v. 150 (batterij-runtime, geen hardwarerisico — de 700 mA-power-cap throttelt bij 7 LED's niet). |

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
| NR7 | **Persistente global context**: `pi/node-red/settings.js` zet `contextStorage.default = localfilesystem` (`flushInterval` 15 s) → alle `global.*`-state (`spelerStats`, `globaleStats`, `spelHistorie`, π-stand, `godPunten`, …) wordt naar `/data/context/` bewaard en overleeft restart + deploy. `/data` staat via `pi/node-red/docker-compose.yml` op een persistente bind-mount (op deze Pi de SD-kaart / root-fs — er is geen SSD; `NODE_RED_DATA`, default `/home/pi/nodered-data`) en overleeft zo ook een container-recreate. Dit maakt de "persistent"-claims van M1 (π-stand) en D6/D7 (`godPunten`) ook tegen een Node-RED-herstart waar. |
| NR8 | **`spel/state`-vangnet**: Flow 04 dumpt elke 30 s een compacte snapshot naar het **retained** topic `spel/state` (qos 1); node `Rehydrate spel-state` leest die bij (her)start terug **maar enkel als de betreffende global nog leeg is** (nooit een lopend spel overschrijven). Zo herstelt zelfs een verse container zónder persistente `/data` nog de laatste snapshot. |
| NR9 | **Gedeelde partij-reset (single source)**: alle reset-knoppen (Spel aan/uit, Verwerk bediening, Admin "Reset ALLES") wissen de partij-toestand via **één** synchrone helper `global.get("resetPartij")(global)` (gedefinieerd in `pi/node-red/settings.js` → `functionGlobalContext`). De helper wist **alle** per-partij registers (pof-engine, ziekte, tijdbom, tweeling, dienaars, etenstijd, spelTempoFactor, nuke, tornado, infected, `mnGestraft`, LED-caches, …) en raakt **nooit** persistente state aan (`globaleStats`, `spelHistorie`, `spelNummer`, `godPunten`, `spelerStats`-totalen, de π-klok `midnight*`). Een nieuw partij-veld wordt dus **op één plek** toegevoegd. Dit borgt Z8/T8/M6/TO5/ET3/TW4/SP3 voor élke reset-knop. De helper verhoogt ook **`pofGeneration`** (R4): hangende `setTimeout`-callbacks zoals de doelwit-reveal checken dit token en vuren **niet** meer na een reset — geen gevolgen/LED/buzzer op een gestopt spel. Wijzig `settings.js` → **container-herstart** nodig (niet enkel `deploy-flows`). De aanroepers gebruiken een **typeof-guard** (`typeof _rp === "function"`): ontbreekt `resetPartij` (bv. flows gedeployed vóór de container-herstart), dan **degradeert** de knop gracieus (`node.warn` i.p.v. crash) zodat het spel bestuurbaar blijft; de volledige clear werkt na de herstart. De dedicated **Noodstop**-groep is verwijderd — spel stoppen loopt nu via de hoofd-**Speltoestand**-schakelaar (die `transferStats()` + `resetSpelStaat()` + historiek al draait). |
| NR10 | **Master-verbindingsregel (Spelstatus).** Bovenaan de Spelstatus-pagina toont een `ui-template` per master (M1=palen 1-8, M2=9-16, M3=17-24) een naam + groen/rood bolletje. "Bouw master-status" leidt dit **af uit `status_lastSeenPaal`** (geen aparte master-heartbeat): een master is **groen** als minstens één paal in zijn bereik < `SLAVE_STALE_MS` (60 s) geleden data stuurde én er geen `status_bridgeFout` (ST-006) voor hem loopt. Geen firmware-/bridge-wijziging nodig. |
| NR11 | **Dashboard-indeling**: de cumulatieve globale stats staan op een **eigen Leaderboard-pagina** (gesorteerd op `globaleStats.totaalUren` aflopend), niet meer als groep op Bediening. Sorteer op **`totaalUren`** (totaal), niet op de rest-kolom `totaalUren % 24`. De feeder hangt aan de gedeelde ververs-inject "Ververs globale stats (2s)". |
