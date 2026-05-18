# Flow 02 — Spelstatus

## Doel

De huidige **gezondheid van het spel** tonen. Deze flow moet OK zijn vóór een
spel van start gaat. Hij geeft een duidelijk **GO / NO-GO**.

## Functies

1. **Spelers gedetecteerd** — controleren of alle geregistreerde spelers
   effectief gedetecteerd worden in het veld.
2. **Slaves up & running** — controleren of alle verwachte palen data sturen,
   d.w.z. verbinding hebben met de master en de Pi.
3. **Foutcodes zichtbaar** — een plaats waar foutcodes getoond worden. Het
   effectief *versturen* van foutmeldingen vanuit de hardware komt later
   (zie `docs/todo.md` → error messaging protocol); de structuur houdt daar nu
   al rekening mee.

## Wat de flow doet

| Node                   | Functie                                                       |
|------------------------|---------------------------------------------------------------|
| `Ontvang detecties`    | MQTT-in op `plaatjes/data`                                    |
| `Registreer detectie`  | houdt per beacon en per paal bij wanneer ze laatst gezien zijn |
| `Evalueer (elke 3s)`   | timer-inject die de evaluatie elke 3 seconden start           |
| `Evalueer spelstatus`  | berekent de status van spelers, palen en foutcodes            |
| `[TEST] Detectie`      | inject-node om zonder hardware een detectie te simuleren      |
| 3× tabel + statustekst | dashboard-weergave op de pagina **Spelstatus**                |

## Inputs

| Bron                   | Beschrijving                                             |
|------------------------|----------------------------------------------------------|
| `plaatjes/data` (MQTT) | Detecties uit het veld.                                  |
| `global.spelersLijst`  | Gezet door flow 00 — wie wordt verwacht.                 |
| `global.paaltjesLijst` | Gezet door flow 00 — welke palen worden verwacht.        |

## Outputs

| Bestemming            | Beschrijving                                              |
|-----------------------|-----------------------------------------------------------|
| `global.status_ok`    | `true` als alles in orde is, anders `false`.              |
| `global.status_fouten`| Array van actieve fouten. Klaar voor een later error-blok.|
| Dashboard "Spelstatus"| Statustekst (GO/NO-GO) + tabellen voor spelers en palen.  |

`global.status_ok` wordt gelezen door flow 03 Bediening: bij het starten van het
spel meldt die of de spelstatus op dat moment OK was.

## Foutcodes

| Code     | Niveau              | Betekenis                                                        |
|----------|---------------------|------------------------------------------------------------------|
| `ST-001` | FOUT                | Geregistreerde speler niet (meer) gedetecteerd.                  |
| `ST-002` | FOUT / WAARSCHUWING | Paal stuurt geen data (geen contact) of is verouderd.            |
| `ST-003` | FOUT                | Geen enkele data op `plaatjes/data` — controleer master + bridge. |
| `ST-004` | INFO                | Onbekende beacon gedetecteerd (niet in `spelersLijst`).          |

> Deze codes worden lokaal in Node-RED afgeleid. Wanneer het error messaging
> protocol bestaat, kunnen slaves/master eigen foutcodes sturen die hier
> samenkomen.

## Instelbare drempels

Bovenaan de functie `Evalueer spelstatus`:

- `SPELER_TIMEOUT_MS` (15 s) — speler geldt als niet-gedetecteerd na deze stilte.
- `SLAVE_STALE_MS` (60 s) — paal geldt als "verouderd" na deze stilte.
- `GEEN_DATA_MS` (10 s) — geen enkele data → master/bridge-fout.

## Globale variabelen

| Variabele               | Inhoud                                          |
|-------------------------|-------------------------------------------------|
| `status_lastSeenMac`    | `{mac: timestamp}` — laatste detectie per beacon |
| `status_lastSeenPaal`   | `{paalId: timestamp}` — laatste data per paal   |
| `status_lastDataTs`     | timestamp van het laatste bericht op `plaatjes/data` |
| `status_ok`             | boolean GO/NO-GO                                |
| `status_fouten`         | array van fouten                                |

## Bekende beperking

Slaves sturen momenteel enkel data wanneer ze een beacon zien. Een paal zonder
spelers in de buurt is dus niet te onderscheiden van een offline paal. Daarom
geldt een paal als "OK" zodra hij **ooit** data stuurde. Een echte
connectiviteitscheck vereist een slave-heartbeat — zie `docs/todo.md`.

## Testen

### Met de test-inject-node (zonder hardware)

1. Zorg dat flow 00 gedraaid heeft (spelers- en paaltjeslijst geladen).
2. Open dashboard → pagina **Spelstatus**. Zonder data staat alles op
   "NIET GEZIEN" / "GEEN CONTACT" en de status op **NO-GO**.
3. Klik de inject-node `[TEST] Detectie` aan. De speler en de paal uit de
   payload springen naar **OK**.
4. Dubbelklik de inject-node om een andere `paal`/`mac` te testen.
5. Wacht 15 s zonder nieuwe detectie: de speler gaat naar **VERLOREN** en er
   verschijnt een `ST-001`-fout.

### Met echte MQTT-berichten

```bash
mosquitto_pub -h 192.168.1.43 -t plaatjes/data \
  -m '{"paal":1,"mac":"48:87:2d:9d:bb:7d","rssi":-60}'
```

### GO bereiken

Stuur (via test-injects of MQTT) voor **elke** speler uit de spelerslijst en
**elke** paal uit de paaltjeslijst minstens één detectie binnen de
timeout-vensters. De statustekst springt dan naar **GO**.
