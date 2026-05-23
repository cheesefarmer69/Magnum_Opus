# Flow 01 — Locatiebepaling

## Doel

Bepalen op welke paal elke geregistreerde speler zich bevindt, op basis van de
BLE-detecties die uit het veld binnenkomen.

## Wat de flow doet

De master stuurt per gedetecteerde speler een bericht op het MQTT-topic
`plaatjes/data` (zie `docs/protocol.md`):

```json
{"paal": 1, "mac": "48:87:2d:9d:bb:7d", "rssi": -67}
```

| Node                       | Functie                                                       |
|----------------------------|---------------------------------------------------------------|
| `Ontvang Paal/MAC Data`    | MQTT-in op `plaatjes/data`                                    |
| `Locatiebepaling Spelers`  | bepaalt per speler de actieve paal (met hysteresis)           |
| `Tabel Locatie Spelers`    | dashboard-tabel met speler, paal en signaalsterkte            |
| `[TEST] Detectie ...`      | inject-nodes om zonder hardware een detectie te simuleren     |
| debug-nodes                | tonen de ruwe data en de berekende locaties                   |

### Lokalisatiemethode: tijd-gevensterde argmax + hysteresis

Een speler staat zelden exact op één paal — naburige palen vangen hetzelfde
signaal op. De flow bepaalt de actieve paal zo:

1. **Samples met timestamp.** Per speler/paal worden recente RSSI-metingen
   bijgehouden, elk met het tijdstip van binnenkomst.
2. **Venster.** Samples ouder dan `VENSTER_MS` (4 s) vervallen. Hierdoor raakt
   een paal die de speler niet meer ziet vanzelf "uit beeld" — er kleeft geen
   verouderde sterke meting meer aan een verlaten paal.
3. **Argmax.** De speler hoort bij de paal met het sterkste gemiddelde over de
   recente samples.
4. **Hysteresis.** Er wordt pas gewisseld als de beste paal de huidige met
   minstens `HYSTERESE_DBM` (4 dBm) verslaat — behalve wanneer de huidige paal
   géén recente data meer heeft; dan wordt direct losgelaten.

**Tuning-constanten** bovenaan de functie `Locatiebepaling Spelers`:

| Constante       | Standaard | Betekenis                                            |
|-----------------|-----------|------------------------------------------------------|
| `VENSTER_MS`    | 4000      | leeftijd waarna een RSSI-sample vervalt              |
| `MAX_SAMPLES`   | 6         | max bewaarde samples per paal                        |
| `HYSTERESE_DBM` | 4         | drempel (dBm) om naar een sterkere paal te wisselen  |
| `MIN_SAMPLES`   | 1         | min. recente samples voordat een paal kandidaat is   |

> **Waarom dit het "speler kleeft aan verkeerde paal"-probleem oplost:** de
> vorige versie bewaarde een gemiddelde dat nooit verviel. Een paal die een
> speler ooit sterk zag, bleef die waarde eeuwig houden, waardoor de speler
> aan die paal bleef hangen. Met het venster verdwijnt verouderde data en
> "snapt" de speler naar de paal die hem nú het sterkst ziet.

> **Te traag/te gevoelig?** Verhoog `HYSTERESE_DBM` tegen flikkeren, verlaag het
> als de speler te lang aan een paal blijft hangen. Een groter `VENSTER_MS`
> maakt de bepaling stabieler maar trager.

## Inputs

| Bron                  | Beschrijving                                              |
|-----------------------|-----------------------------------------------------------|
| `plaatjes/data` (MQTT)| Detecties uit het veld.                                   |
| `global.spelersLijst` | Gezet door flow 00. Onbekende beacons worden genegeerd.   |

## Outputs

| Bestemming               | Beschrijving                                                            |
|--------------------------|-------------------------------------------------------------------------|
| `global.spelerLocaties`  | `{ naam: paalId }` — huidige paal per speler.                           |
| Dashboard "Live Radar"   | Tabel met speler, actieve paal, signaalsterkte, levensdagen en levensuren. |
| Beweging-event (2e output)| `{ speler, vanPaal, naarPaal }` bij een paal-wissel → via `link out` naar flow 04 Puntensysteem. |

> **2e output:** de functie heeft sinds het puntensysteem twee outputs. Output 1
> levert (zoals voorheen) de tabeldata; output 2 vuurt **alleen** bij een echte
> paal-wissel met een beweging-event. Flow 04 rekent daaruit de levensuren uit.
> De radar-tabel toont de levensdagen/levensuren die flow 04 in
> `global.spelerStats` bijhoudt.

## Afhankelijkheid

Flow **00 Configuratie** moet gedraaid hebben: zonder `global.spelersLijst`
worden alle detecties als "onbekende beacon" genegeerd.

## Testen

### Met de test-inject-nodes (zonder hardware)

In de tab staan inject-nodes `[TEST] Detectie ...` met een voorbeeld-payload.
Ze zijn rechtstreeks op de functie aangesloten.

1. Zorg dat flow 00 gedraaid heeft (spelerslijst geladen).
2. Klik een `[TEST] Detectie`-node aan.
3. Open dashboard → pagina **Live Radar**: de speler verschijnt op de paal uit
   de payload.
4. Wil je een andere paal/speler testen? Dubbelklik de inject-node en pas de
   `payload` aan (`paal`, `mac`, `rssi`).

### Met echte MQTT-berichten (volledige keten)

Om de hele weg master → bridge → MQTT → Node-RED te testen, publiceer je een
bericht op `plaatjes/data`. Vanaf de Pi:

```bash
mosquitto_pub -h 192.168.1.43 -t plaatjes/data \
  -m '{"paal":3,"mac":"48:87:2d:9d:bb:7d","rssi":-60}'
```

Stuur een paar berichten met dezelfde `mac` en stijgende `rssi` op een andere
`paal` om de wissel te zien gebeuren.

### Lokalisatie controleren

1. Stuur een paar detecties voor paal 1 (`rssi` rond -60) → speler op paal 1.
2. Stuur 1× paal 2 met nauwelijks sterker signaal (`rssi` -58) → speler blijft
   op paal 1 (verschil < `HYSTERESE_DBM`).
3. Stuur paal 2 met `rssi` -50 → speler wisselt naar paal 2 (verschil ≥ drempel).
4. **Stop met paal 2 sturen** en stuur alleen paal 1 (`rssi` -60). Binnen
   `VENSTER_MS` (4 s) vervallen paal 2's samples en springt de speler terug
   naar paal 1 — ook zonder dat paal 1 sterker is dan paal 2 ooit was. Dit is
   precies het gedrag dat de vorige versie miste.
