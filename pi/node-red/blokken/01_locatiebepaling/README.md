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

### Hysteresis

Een speler staat zelden exact op één paal — naburige palen vangen hetzelfde
signaal op. Om te voorkomen dat een speler "knippert" tussen twee palen, wisselt
de flow pas van paal als het nieuwe signaal duidelijk sterker is:

- `MAX_METINGEN` (3) — er wordt een voortschrijdend gemiddelde over de laatste
  3 RSSI-metingen per paal bijgehouden.
- `HYSTERESIS_DBM` (5) — de nieuwe paal moet gemiddeld minstens 5 dBm sterker
  zijn dan de huidige paal voor er gewisseld wordt.

Beide constanten staan bovenaan de functie `Locatiebepaling Spelers`.

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
`paal` om de hysteresis-wissel te zien gebeuren.

### Hysteresis controleren

1. Stuur 3× een detectie voor paal 1 (`rssi` rond -60).
2. Stuur 1× een detectie voor paal 2 met een nauwelijks sterker signaal
   (`rssi` -58) → de speler blijft op paal 1 (verschil < 5 dBm).
3. Stuur 3× paal 2 met `rssi` -50 → nu wisselt de speler naar paal 2.
