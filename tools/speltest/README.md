# Magnum Opus — autonoom AI-agent testharnas (`tools/speltest/`)

Laat AI-agents het **Plates-of-Fate middagspel** autonoom testen op **bugs, crashes,
glitches en exploits**. De agents spelen het spel zoals jij dat met de hand doet in de
simulator — spelers verplaatsen, events laten vallen — en elke ronde wordt tegen een
onafhankelijk **orakel** getoetst dat de regels exact kent.

## Hoe het werkt

De simulator zit al op dezelfde MQTT-naad als de hardware. Dit harnas doet hetzelfde,
maar **zonder browser**: het publiceert `sim/locatie`, `sim/modus` en `sim/bediening`
(nieuw — engine-besturing over MQTT, zie `docs/protocol.md` §5) en leest `pof/status`,
`pof/controle`, `pof/portalen`, enz. **Node-RED draait de echte engine** — dezelfde voor
sim en echt spel.

```
[speltest-harnas (Python)]  --MQTT 1883-->  [Mosquitto op de Pi]  <-->  [Node-RED engine]
        |  game_driver: handen + ogen (sim/* publiceren, pof/* lezen)
        |  oracle:      grond-waarheid (referentie-scoring uit docs/spel/event-systeem.md)
        |  strategies:  braaf / grens / overtreder / chaos / exploit
        |  fuzzer:      misvormde payloads + liveness-check
        +  runner:      speelt sessies, vergelijkt met het orakel, schrijft rapport + replays
```

Het orakel deelt **geen code** met Node-RED: een bug in de engine wordt dus niet door
dezelfde bug in het orakel gemaskeerd. Een afwijking tussen engine en orakel = **kandidaat-bug**.

## Vereisten

- Python 3.11+ en `pip install -r requirements.txt` (alleen `paho-mqtt==2.1.0`).
- De Pi draait (Mosquitto + Node-RED met de geüpdatete `flows.json`).
- **De Node-RED-flow met de `sim/bediening`-hook moet gedeployed zijn**:
  `pi/node-red/deploy-flows.ps1` (Windows) of `deploy-flows.sh` (Pi). Een `docker restart`
  herlaadt `flows.json` níét.
- Stop de echte `serial-bridge` om dubbele bronnen te vermijden: `docker stop serial-bridge`.

> Draait het harnas het makkelijkst **op de Pi zelf** (broker = `localhost`), of vanaf je
> pc met `--broker 192.168.1.43`. Op Windows zonder Python: installeer Python of draai het
> op de Pi.

## Snelstart

```bash
pip install -r tools/speltest/requirements.txt

# 0) Orakel-zelftest (offline, geen broker nodig) — bewijst dat het orakel de spec volgt
python -m tools.speltest.oracle --selftest

# 1) Driver-rooktest — bewijst de hele MQTT-naad (verbinden, sim-modus, 1 speler terug)
python -m tools.speltest.game_driver --broker 192.168.1.43 selftest

# 2) Scripted sessie + orakel + fuzzing → rapport in out/
python -m tools.speltest.runner --broker 192.168.1.43 --strategie all --rondes 30 --fuzz --report out/

# 3) Een bevinding herafspelen
python -m tools.speltest.runner --broker 192.168.1.43 --replay out/replays/<id>.json
```

Open daarna `out/rapport.md` (mens) of `out/rapport.json` (machine).

## Wat elke strategie test

| Strategie | Mikt op |
|-----------|---------|
| `braaf` | speelt exact legaal → kent de engine de juiste levensuren toe? (regressie) |
| `grens` | exact op de grenzen (`voor == x`, `voor == 0`, portaal als laatste hop) → off-by-one |
| `overtreder` | opzettelijk fout (te veel / achteruit / niet-doelwit beweegt) → worden alle straffen correct toegepast, geen valse positieven? |
| `chaos` | willekeurige zetten / snelle hops → races, stalls, settle-problemen |
| `exploit` | gericht misbruik (portaal-pingpong, happy-herhaling, dienaar-omzeiling) → economische exploits |

## Wat het orakel dekt

- **Volledig**: verplaatsing-events (`max`/`of`/`min`), OK/stil, BEWOOG, TE VEEL/WEINIG,
  ONGELDIGE KEUZE, TERUG IN TIJD, ONGELDIGE TELEPORT, happy-hour ×2, portaal-teleport (0),
  middernacht-poort dicht, en de sterfte-collaps in de `pof/controle`-status.
- **Best-effort** (gemarkeerd, delta-gericht): ziekte- en nuke-rondes (complexe lifecycle).

Bron-van-waarheid: `docs/spel/event-systeem.md` §3/§7, `docs/invarianten.md` §2,
`docs/spel/event-catalogus.md`. De `oracle.py --selftest` toetst het orakel tegen de
gespecificeerde voorbeelden (5→8=−3, portaal-sprong=0, happy ×2, …).

## Soorten bevindingen

| Type | Wat | Severity |
|------|-----|----------|
| `scoring-mismatch` | engine-`delta`/`status` ≠ orakel | hoog |
| `stall` | geen `pof/controle`/`wacht_controle` binnen timeout (engine hangt) | kritiek |
| `fuzz-liveness` | `locatie/spelers` reageert niet na een fuzz-burst | kritiek |
| `fuzz-uitzondering` | harness-fout bij het versturen van een payload | midden |

Elke `scoring-mismatch` krijgt een **replay-bestand** (`out/replays/<id>.json`) dat het
scenario vastlegt (startposities, event, zetten, verwacht vs. gekregen). `--replay` forceert
hetzelfde event (door alle andere uit te sluiten via `sim/events-config`), herstelt de
posities en speelt de zetten opnieuw.

## Live AI-agent-modus

Naast de scripted strategieën kan een **Claude-subagent** zelf spelen via de one-shot CLI
van `game_driver.py` (stateful via `.session.json`). Zie **`AGENT.md`** — dat is het
instructiedocument dat je aan een subagent meegeeft.

## Veiligheid

- `sim/bediening` werkt **alleen in sim-modus** (`simVeld24 === true`) — het harnas kan een
  echt spel niet starten/stoppen/wissen (invariant SIM5).
- De runner wist bij sessie-start de globale stats (`wis-stats`), zodat runs niet op elkaar
  voortbouwen en het orakel synchroon van 0 start.
- Fuzzing kan Node-RED tijdelijk laten hangen; herstel = `docker restart` van de Node-RED-
  container. Het rapport vermeldt dit per liveness-bevinding.

## Bestanden

| Bestand | Rol |
|---------|-----|
| `config.py` | constanten (broker, spelerset/MAC's, topics, opties) |
| `mqtt_client.py` | dunne MQTT-laag (paho 2.x), wacht-op-topic helpers |
| `game_driver.py` | handen + ogen; in-process klasse + one-shot CLI |
| `oracle.py` | referentie-scoring + invarianten + `--selftest` |
| `strategies.py` | de vijf speelprofielen |
| `fuzzer.py` | protocol-fuzzing + liveness |
| `runner.py` | orkestratie, bevindingen, replays |
| `report.py` | `rapport.md` + `rapport.json` |
| `AGENT.md` | instructies voor een spelende Claude-subagent |
