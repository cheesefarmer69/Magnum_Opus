# Handleiding: het spel testen met de AI-agent-frameworks

**Code:** [tools/speltest/](tools/speltest/)
**Draait als:** Python-CLI op de Pi (broker = `localhost`) of vanaf je pc (`--broker 192.168.1.43`)
**Bron-van-waarheid voor de regels:** [docs/spel/event-systeem.md](docs/spel/event-systeem.md) · [docs/invarianten.md](docs/invarianten.md)

---

## 1. Wat dit is & wanneer je het gebruikt

Je hebt **twee test-frameworks** gebouwd die het Plates-of-Fate middagspel autonoom op
**bugs, crashes, glitches en exploits** testen. Beide spelen het spel zoals jij dat met de hand
doet in de simulator — spelers verplaatsen, events laten vallen — maar **zonder browser**, en
beide besturen **dezelfde echte Node-RED-engine** over MQTT in sim-modus.

| Framework | Bestand | Wat het doet | Wanneer kiezen |
|-----------|---------|--------------|----------------|
| **A — Scripted harnas** | [runner.py](tools/speltest/runner.py) | Speelt N rondes per strategie (braaf/grens/overtreder/chaos/exploit), toetst elke ronde tegen het orakel, fuzzt het protocol, schrijft rapport + replays | Brede regressie, herhaalbaar, ongemonitord laten draaien |
| **B — Live Claude-subagent** | [game_driver.py](tools/speltest/game_driver.py) + [AGENT.md](tools/speltest/AGENT.md) | Een AI-agent speelt beurt-per-beurt zelf via een one-shot CLI en bedenkt creatieve randgevallen | Gericht exploits/randgevallen zoeken die scripts niet bedenken |

De naad die beide gebruiken (uit [tools/speltest/README.md](tools/speltest/README.md)):

```
[speltest-harnas (Python)]  --MQTT 1883-->  [Mosquitto op de Pi]  <-->  [Node-RED engine]
        |  game_driver: handen + ogen (sim/* publiceren, pof/* lezen)
        |  oracle:      grond-waarheid (referentie-scoring uit docs/spel/event-systeem.md)
        |  strategies:  braaf / grens / overtreder / chaos / exploit
        |  fuzzer:      misvormde payloads + liveness-check
        +  runner:      speelt sessies, vergelijkt met het orakel, schrijft rapport + replays
```

> **Kernidee.** Het **orakel** ([oracle.py](tools/speltest/oracle.py)) deelt **geen code** met
> Node-RED. Het rekent onafhankelijk uit wat de engine *zou moeten* teruggeven (status + delta
> levensuren). Een bug in de engine wordt dus niet door dezelfde bug in het orakel gemaskeerd:
> **elke afwijking tussen engine en orakel = kandidaat-bug.**

---

## 2. Vereisten (eenmalig)

- **Python 3.11+** en de enige dependency:
  ```bash
  pip install -r tools/speltest/requirements.txt   # alleen paho-mqtt==2.1.0
  ```
- **De Pi draait** Mosquitto + Node-RED.
- **De Node-RED-flow met de `sim/bediening`-hook moet gedeployed zijn.** Deploy met
  [pi/node-red/deploy-flows.ps1](pi/node-red/deploy-flows.ps1) (Windows) of `deploy-flows.sh`
  (Pi). ⚠️ Een `docker restart` herlaadt `flows.json` **níét** — de container draait op zijn
  eigen `/data/flows.json`. Zonder deze hook reageert de engine niet op het harnas.
- **Draaien vanaf de Pi** (broker = `localhost`) of vanaf je pc met `--broker 192.168.1.43`.

Alle commando's draai je **vanuit de repo-root** (`tools.speltest.*` is een Python-package).

---

## 3. Pre-flight: éérst controleren of alles werkt

Voer deze drie trappen **in volgorde** uit. Elke trap bewijst iets onafhankelijk van de vorige,
zodat een groen vinkje betekenisvol is. Pas als alle drie slagen, kun je een testrun vertrouwen.

### Voorzorgen vóór elke run

- **Stop de echte bron** zodat er geen dubbele detecties zijn:
  ```bash
  ssh pi@192.168.1.43
  docker stop serial-bridge
  ```
- **Bevestig dat niemand een echt spel speelt.** Sim en hardware **delen** de engine
  (invariant SIM1 in [docs/invarianten.md](docs/invarianten.md)). Het harnas kán een echt spel
  niet starten/stoppen/wissen — `sim/bediening` werkt **alleen** in sim-modus (`simVeld24 === true`,
  invariant SIM5) — maar een nog-draaiende `serial-bridge` vervuilt wél je meting met echte beacons.

### Trap 0 — Orakel-zelftest (offline, géén broker nodig)

```bash
python -m tools.speltest.oracle --selftest
```

Dit toetst het orakel tegen de gespecificeerde voorbeelden uit
[docs/spel/event-systeem.md](docs/spel/event-systeem.md) §3/§7. **Geen broker, geen Pi nodig** —
het bewijst dat je *meetlat* klopt vóór je er de engine mee meet. Verwacht onderaan:
**`ALLE TESTS GESLAAGD`**.

De voorbeelden die het dekt:

| Scenario | Verwacht |
|----------|----------|
| Niet-doelwit loopt 5→8 (3 stappen) | `BEWOOG (mocht niet)`, delta −3 |
| Niet-doelwit loopt 5→4 (achteruit) | `BEWOOG (mocht niet)`, delta −1 |
| Portaalsprong: 10→12, 12⇒20 (teleport), 20→23 (max 5) | `OK`, delta **+5** (sprong telt 0) |
| 3 stappen vooruit, eindigt op happy-hour | `OK`, delta **+6** (×2) |
| `max 3`, maar 5 stappen | `TE VEEL`, delta −2 |
| `of 2/5`, maar 3 stappen | `ONGELDIGE KEUZE`, delta −3 |
| `of 2/5`, exact 5 stappen | `OK`, delta +5 |
| Doelwit zet 1 stap achteruit | `TERUG IN TIJD`, delta −1 |
| Portaal-pingpong (2× door zelfde portaal) | `ONGELDIGE TELEPORT`, delta 0 |
| `TE VEEL` (−2) bij saldo 0 → sterfte | base `TE VEEL`, maar `pof/controle`-status klapt naar **`OK`** |

> Faalt deze trap, dan is er iets aan het orakel of aan de regels veranderd — **stop hier** en
> los dat eerst op; anders zijn alle latere "bevindingen" onbetrouwbaar.

### Trap 1 — Driver-rooktest (de volledige MQTT-naad)

```bash
python -m tools.speltest.game_driver --broker 192.168.1.43 selftest
```

Dit verbindt met de broker, zet **sim-modus** (`sim/modus {sim24:true}`), plaatst één speler op
paal 7 (`sim/locatie`) en leest die positie terug via `locatie/spelers`. Verwacht:
`"selftest": "ok"`.

| Uitvoer | Betekenis | Actie |
|---------|-----------|-------|
| `"selftest": "ok"` | Verbinden + sim-modus + Node-RED-locatiebepaling werken | Door naar Trap 2 |
| `"selftest": "GEEN ANTWOORD"` | Broker bereikbaar, maar geen `locatie/spelers` terug | Flows gedeployed? `serial-bridge` botst? sim-hook aanwezig? |
| `{"fout": "verbinden mislukt: ..."}` | Geen TCP naar de broker | Broker-adres/poort, netwerk, Mosquitto draait? |

### Trap 2 — Eén handmatige ronde (engine bestuurt **én** scoort)

Bewijst dat de engine een volledig event-rondje doorloopt en de scoring leeft. Gebruik de
one-shot CLI van [game_driver.py](tools/speltest/game_driver.py) (stateful via `.session.json`):

```bash
B="--broker 192.168.1.43"
python -m tools.speltest.game_driver $B setup    # sim-modus + wis-stats + manueel + start + spelers spreiden
python -m tools.speltest.game_driver $B peek      # fase / huidig event / doelwit
python -m tools.speltest.game_driver $B next      # trigger volgend event, lees doelwit + budget (x/y)
python -m tools.speltest.game_driver $B move Lilou 6 7   # loop Lilou stap voor stap (elke paal = één hop)
python -m tools.speltest.game_driver $B verify    # controle + orakel-oordeel
python -m tools.speltest.game_driver $B stop
```

Een **legale** zet moet `"oordeel": "OK"` geven (lege `mismatches`). Krijg je dat, dan leven de
naad én de scoring. `"oordeel": "BUG-KANDIDAAT"` of een `"STALL"`-uitvoer betekent dat je hier al
beet hebt — zie §6 en §8. Gebruik `state` voor een volledige wereld-snapshot (posities, portalen,
toestanden, ziekte, middernacht, dienaars).

---

## 4. Framework A — Scripted harnas (`runner.py`)

### Snelstart

```bash
# Scripted sessie: alle strategieën, 30 rondes elk, + protocol-fuzzing, rapport in out/
python -m tools.speltest.runner --broker 192.168.1.43 --strategie all --rondes 30 --fuzz --report out/
```

Bij sessie-start regelt het harnas zelf de hele opstart (`Sessie.voorbereiden`): het abonneert op
de `pof/*`-topics, zet sim-modus, **wist de globale stats** (`wis-stats`), reset het orakel naar 0,
zet **manueel-modus** aan, start het spel en spreidt de spelers gelijkmatig over de ring. Daarna
speelt het per strategie `--rondes` rondes; elke ronde wordt tegen het orakel getoetst.

### Alle CLI-vlaggen

| Vlag | Default | Betekenis |
|------|---------|-----------|
| `--broker` | `192.168.1.43` | MQTT-broker (TCP 1883, niet de WebSocket 9001) |
| `--port` | `1883` | Broker-poort |
| `--strategie` | `all` | Komma-lijst (`braaf,exploit`) of `all` |
| `--rondes` | `20` | Aantal rondes per strategie |
| `--settle` | `0.5` | Settle-tijd per hop in seconden (tijd om een paalwissel te laten "landen") |
| `--geen-middernacht` | uit | Schakel de middernacht-poort uit voor deze sessie |
| `--fuzz` | uit | Draai na de strategieën een protocol-fuzz-burst |
| `--fuzz-herhalingen` | `1` | Aantal fuzz-bursts |
| `--report` | `out` | Uitvoermap voor `rapport.md`/`rapport.json` + `replays/` |
| `--replay` | — | Speel een replay-bestand af i.p.v. een sessie (zie §7) |

### De vijf strategieën

| Strategie | Mikt op |
|-----------|---------|
| `braaf` | Speelt exact legaal → kent de engine de juiste levensuren toe? (regressie) |
| `grens` | Exact op de grenzen (`voor == x`, `voor == 0`, portaal als laatste hop) → off-by-one |
| `overtreder` | Opzettelijk fout (te veel / achteruit / niet-doelwit beweegt) → worden alle straffen correct toegepast, geen valse positieven? |
| `chaos` | Willekeurige zetten / snelle hops → races, stalls, settle-problemen |
| `exploit` | Gericht misbruik (portaal-pingpong, happy-herhaling, dienaar-omzeiling) → economische exploits |

### Wat het orakel dekt

- **Volledig:** verplaatsing-events (`max`/`of`/`min`), `OK`/stil, `BEWOOG`, `TE VEEL`/`TE WEINIG`,
  `ONGELDIGE KEUZE`, `TERUG IN TIJD`, `ONGELDIGE TELEPORT`, happy-hour ×2, portaal-teleport (0),
  middernacht-poort dicht, en de sterfte-collaps in de `pof/controle`-status.
- **Best-effort** (advisory, delta-gericht): ziekte- en nuke-rondes (complexe lifecycle). Mismatches
  hierop zijn een *aanwijzing*, geen hard oordeel — verifieer ze handmatig tegen de invarianten.

### Protocol-fuzzing (`--fuzz`)

De fuzzer ([fuzzer.py](tools/speltest/fuzzer.py)) stuurt misvormde/extreme payloads naar de
sim-topics: `paal` als string/negatief/te hoog/float/null, ontbrekende velden, lege array, object
i.p.v. array, een **array van 10 000** detecties, kapotte JSON, en een snelle **start/stop-race**.
Na elke burst volgt een **liveness-check**: een geldige detectie publiceren en verifiëren dat
`locatie/spelers` nog antwoordt.

> ⚠️ Fuzzing kan Node-RED tijdelijk laten hangen. Herstel = **`docker restart` van de Node-RED-
> container** op de Pi. Het rapport vermeldt dit per liveness-bevinding.

---

## 5. Framework B — Live Claude-subagent (`game_driver` CLI + `AGENT.md`)

Naast de scripted strategieën kan een **Claude-subagent** zelf spelen. De agent gebruikt dezelfde
one-shot CLI als in Trap 2, maar bedenkt creatief gemene zetten in plaats van een vast script.

**Wanneer kiezen:** scripted = breed, herhaalbaar, regressie. Live-agent = gericht randgevallen en
exploits zoeken die een script niet bedenkt (bv. een dienaar die toch voor zichzelf scoort, of een
middernacht-oversteek vanaf een andere paal).

De commando-cyclus (stateful via `.session.json`):

```
setup → peek/state → next → move → verify → stop
```

| Commando | Uitvoer |
|----------|---------|
| `setup` | Sim-modus, wis-stats, manueel, start, spelers gespreid |
| `peek` | `pof/status`: fase, huidig event, doelwit |
| `state` | Volledige wereld-snapshot (posities, portalen, toestanden, ziekte, middernacht, dienaars) |
| `next` | Triggert het volgende event; geeft event-naam, voorwaarde, doelwit, `getalWaarde` (x), `getalWaarde2` (y) en `start_pos` |
| `move <speler> <palen...>` | Loopt de speler stap voor stap (elke paal = één settled hop) |
| `verify` | `controle` (wat de engine zei) + `orakel` (wat hoort) + `mismatches` + `oordeel` |
| `stop` | Stopt het spel |

**Hoe starten:** [AGENT.md](tools/speltest/AGENT.md) is het instructiedocument dat je aan de
subagent meegeeft — het bevat de spelregels in het kort, de werkwijze en het gevraagde
rapportformaat. De subagent draait de Bash-commando's zelf en levert een markdown + JSON-lijst van
vondsten.

> **Geen verzonnen bugs.** Een vondst telt alleen als `verify` een niet-lege `mismatches` of
> `"oordeel": "BUG-KANDIDAAT"` toont, of als je een duidelijke regel-overtreding kunt aanwijzen
> tegen [docs/spel/event-systeem.md](docs/spel/event-systeem.md) / [docs/invarianten.md](docs/invarianten.md).
> Een `"STALL"`-uitvoer = de engine hangt (kritiek).

---

## 6. Bevindingen lezen

Open na een scripted run **`out/rapport.md`** (mens) of **`out/rapport.json`** (machine).

### Soorten bevindingen

| Type | Wat | Severity |
|------|-----|----------|
| `scoring-mismatch` | engine-`delta`/`status` ≠ orakel | hoog |
| `stall` | geen `pof/controle`/`wacht_controle` binnen timeout (engine hangt) | kritiek |
| `fuzz-liveness` | `locatie/spelers` reageert niet na een fuzz-burst | kritiek |
| `fuzz-uitzondering` | harness-fout bij het versturen van een payload | midden |

### Opbouw van het rapport

- **Samenvatting:** broker, strategieën, rondes, of er gefuzzt is, en het totaal aantal
  bevindingen per severity en per type.
- **Per bevinding:** voor een `scoring-mismatch` staat per speler `verwacht` vs. `gekregen`
  (veld `delta` of `status`), de basis-status, de spec-regel, het replay-pad en het kant-en-klare
  **reproductiecommando**. Voor een `stall` staat de fase + het herstel; voor `fuzz` de payload +
  het detail.
- **Per-ronde-tabel** (eerste 40 rondes) met ✅/❌ per ronde; de rest staat in `rapport.json`.

### Exitcodes (handig voor scripting)

| Code | Betekenis |
|------|-----------|
| `0` | Geen bevindingen — engine kwam overeen met het orakel en bleef live |
| `1` | Eén of meer bevindingen |
| `2` | Kon niet met de broker verbinden |

---

## 7. Een bevinding herafspelen (`--replay`)

Elke `scoring-mismatch` krijgt een **replay-bestand** (`out/replays/<id>.json`) dat het scenario
vastlegt: startposities, event, wereld-toestand (portalen, happy, ziekte, dienaars, middernacht),
de gespeelde zetten, en verwacht-vs-gekregen.

```bash
python -m tools.speltest.runner --broker 192.168.1.43 --replay out/replays/<id>.json
```

`--replay` **forceert hetzelfde event** (door alle andere uit te sluiten via `sim/events-config`),
herstelt de startposities, en speelt de zetten opnieuw. De uitvoer is:

- **`GEREPRODUCEERD (mismatch)`** → een stabiele, reproduceerbare afwijking — dit is een echte
  kandidaat-bug om uit te zoeken.
- **`geen mismatch deze keer`** → de mismatch kwam niet terug; dat wijst eerder op een **race/
  timing-effect** (bv. een te krappe `--settle`) dan op een vaste regressie. Verhoog `--settle` en
  draai opnieuw.

Het reproductiecommando staat al klaar in `rapport.md` bij elke bevinding.

---

## 8. Triage: is het écht een bug?

```
Mismatch op delta/status?
  ├─ Vergelijk tegen de scoringtabel: docs/invarianten.md §2 + docs/spel/event-systeem.md §3/§7.
  │     Klopt het orakel met de spec, en wijkt de engine af?  → ENGINE-bug (kandidaat).
  │     Wijkt het orakel af van de spec?                       → ORAKEL-bug → fix oracle.py.
  └─ Reproduceerbaar via --replay?  Nee → race/timing → verhoog --settle en herhaal.

STALL (engine hangt)?
  └─ docker restart van de Node-RED-container op de Pi → draai opnieuw. Blijft het hangen → kritiek.
```

**Bekend spec-gat om op te letten:** middernacht-oversteek vanaf een **andere** paal bij dichte
poort. De engine controleert "start op de middernacht-paal", niet de oversteek zélf — een speler
die 23→24→1 loopt bij dichte poort kan ontsnappen. Zie M3 in [docs/invarianten.md](docs/invarianten.md)
en de notitie in [AGENT.md](tools/speltest/AGENT.md). Test dit bewust en weeg af of het wenselijk is.

---

## 9. Veiligheid & opruimen

- **Kan geen echt spel raken.** `sim/bediening` wordt alleen in sim-modus uitgevoerd (SIM5); sim en
  hardware draaien nooit samen (SIM1).
- **Schone start.** De runner wist bij sessie-start de globale stats (`wis-stats`), zodat runs niet
  op elkaar voortbouwen en het orakel synchroon van 0 begint.
- **Na fuzzing met een hang:** `docker restart` van de Node-RED-container.
- **Klaar met testen?** Herstart de echte bron als je weer met hardware wil spelen:
  ```bash
  docker start serial-bridge
  ```

---

## 10. Snelle referentie

```bash
# Pre-flight (in volgorde)
python -m tools.speltest.oracle --selftest                                  # Trap 0: meetlat (offline)
python -m tools.speltest.game_driver --broker 192.168.1.43 selftest         # Trap 1: MQTT-naad
python -m tools.speltest.game_driver --broker 192.168.1.43 setup            # Trap 2: handmatige ronde
#   ... peek / next / move <speler> <palen...> / verify / stop

# Framework A — scripted sessie + fuzzing + rapport
python -m tools.speltest.runner --broker 192.168.1.43 --strategie all --rondes 30 --fuzz --report out/

# Een bevinding herafspelen
python -m tools.speltest.runner --broker 192.168.1.43 --replay out/replays/<id>.json
```

Vergeet niet vooraf: **`docker stop serial-bridge`** (geen dubbele bron), en de flows gedeployed met
[pi/node-red/deploy-flows.ps1](pi/node-red/deploy-flows.ps1) (níét via `docker restart`).

---

## Zie ook

- [tools/speltest/README.md](tools/speltest/README.md) — architectuur van het harnas + bestandsoverzicht
- [tools/speltest/AGENT.md](tools/speltest/AGENT.md) — instructiedocument voor de live spelende subagent
- [pi/simulator/README.md](pi/simulator/README.md) — de handmatige browser-simulator (monitor/simulatie)
- [docs/spel/event-systeem.md](docs/spel/event-systeem.md) · [docs/invarianten.md](docs/invarianten.md) — de regels die het orakel afdwingt
- [docs/protocol.md](docs/protocol.md) §5 — de MQTT-topics (`sim/*` en `pof/*`)
