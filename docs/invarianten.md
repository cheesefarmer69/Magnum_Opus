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
| Z3 | Genezen kan **enkel** bij een **wettelijke** zet (status OK / OK (stil)) die op een **medicijn-uur** eindigt → die spelers staan in `pofGenezen`, "Ziekte-beheer" verwijdert ze uit `ziekeSpelers`. Twee zieken die zo samen op één medicijn-uur genezen → dat medicijn wordt **verbruikt** (verdwijnt). |
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

---

## 4d. Middernacht (permanent mechanisme)

| # | Invariant |
|---|-----------|
| M1 | De middernacht-poort volgt de **eerste 500 cijfers van π** (daarna opnieuw); ze start **open**, elk cijfer is de duur (events) van een fase, dan wisselt open↔dicht. De **π-sequentie loopt door** over Stop/Start heen (`midnightIndex`/`midnightOpen`/`midnightRemaining` worden **niet** gereset). |
| M2 | De middernacht-node draait **één keer per event** (getriggerd door "Kies event"). |
| M3 | Bij een **dichte** poort mag een speler die **op de middernacht-paal staat** (start-positie = hoogste paal) helemaal niet bewegen → `MIDDERNACHT DICHT` (`−voor`); spelers elders (ook wie elders de ring rondgaat) blijven vrij. |
| M4 | Een **0** in de π-sequentie = **oogst**: elke speler op de middernacht-paal sterft (uren 0 + sterfte) en wordt **dienaar** van de **armste** niet-geoogste, niet-dienaar speler. Een 0 verandert de open/dicht-volgorde niet. |
| M5 | Een **dienaar** verdient niets voor zichzelf: positieve `delta` gaat naar `stats[meester].totaalUren`; verlies + sterfte blijven bij de dienaar. Hij speelt door (events vallen nog op hem). |
| M6 | `dienaars` wordt gewist bij **Stop/Herstart** en door de admin-knop "Speler-toestanden → 0"; `pof/dienaars` (retained) wordt dan leeg gepubliceerd. |
| M7 | De poort-LED op de middernacht-paal is **wit** (open) / **rood** (dicht) via "Sync toestanden + LEDs"; bij een 0 toont de hele ring de **oogst-animatie** (actie 11). |
| M8 | Middernacht is **uitschakelbaar** via de simulator-checkbox (`sim/middernacht-config` → global `middernachtAan`). Bij `middernachtAan === false` zet de node `middernachtActief=false` en is de hoogste paal een **gewoon uur** (geen poort-LED, geen `MIDDERNACHT DICHT`, geen oogst, kiesbaar als uur-doelwit). De π-stand wordt niet aangeraakt en loopt verder bij heractivering. |

---

## 5. Events — formaat

| # | Invariant |
|---|-----------|
| EV1 | `selectie` is enkel `"willekeurig"` of `"alle"` — de `"rang"`-selectie (met `veld`/`richting`) bestaat niet meer. |
| EV2 | `categorie` is `"speler"` \| `"toestand"` \| `"wereld"`. `doelwit.type` mag `"uur"` zijn. |
| EV3 | Actieve spelers = enkel spelers met bekende positie (`spelerLocaties`) en niet gepauzeerd. |
| EV4 | Actieve palen = `palenActief` (= `paaltjesLijst` in echt spel; = 1..24 in simulatie-modus). |
| EV5 | Een nieuw event toevoegen vereist: preconditie + toegestane veld-wijzigingen. De centrale invarianten-checks (dit document) draaien daarna automatisch. |

---

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
| HW5 | Rode LED GPIO6 (slave): knop-puls heeft **voorrang** op batterij-waarschuwing. |
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
