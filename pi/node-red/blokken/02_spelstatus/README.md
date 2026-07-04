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

| Node                       | Functie                                                                   |
|----------------------------|---------------------------------------------------------------------------|
| `Ontvang detecties`        | MQTT-in op `plaatjes/data` (zowel `{paal,mac,rssi}` als `{paal,batt}`)    |
| `Registreer detectie`      | houdt per beacon en per paal bij wanneer ze laatst gezien zijn            |
| `Registreer batterij`      | slaat de laatste batterij-spanning per paal op; dient ook als heartbeat   |
| `MQTT status watcher` + UI | groen/rood blokje linksboven dat de MQTT-broker verbinding toont          |
| `Toon batterij`-switch     | toggelt of de Batterij-kolom in de palen-tabel met data wordt gevuld      |
| `Override NO-GO`-switch    | laat het Bediening-blok het spel starten ook al is de spelstatus NO-GO    |
| `Evalueer (elke 3s)`       | timer-inject die de evaluatie start (naam zegt 3s, maar `repeat` staat op **1 s**)   |
| `Evalueer spelstatus`      | berekent de status van spelers, palen, batterij en foutcodes              |
| `[TEST] Detectie`          | inject-node om zonder hardware een detectie te simuleren                  |
| 3× tabel + statustekst     | dashboard-weergave op de pagina **Spelstatus**                            |

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
| `ST-005` | WAARSCHUWING        | Batterij bijna leeg (< `BATT_VERVANG_V` = 3,5 V) → **vervang batterij**. Niet-blokkerend. |

> Deze codes worden lokaal in Node-RED afgeleid. Wanneer het error messaging
> protocol bestaat, kunnen slaves/master eigen foutcodes sturen die hier
> samenkomen.

## Instelbare drempels

Bovenaan de functie `Evalueer spelstatus`:

- `SPELER_TIMEOUT_MS` (15 s) — speler geldt als niet-gedetecteerd na deze stilte.
- `SLAVE_STALE_MS` (60 s) — paal geldt als "verouderd" na deze stilte.
- `GEEN_DATA_MS` (10 s) — geen enkele data → master/bridge-fout.
- `BATT_VERVANG_V` (3,5 V) — celspanning waaronder een **ST-005**-batterijwaarschuwing verschijnt. Gebruikt
  de `batt`-waarde uit `status_batterijPaal` (onafhankelijk van de "Toon batterij"-toggle); `WAARSCHUWING`
  blokkeert de GO/NO-GO niet. Zie `docs/hardware/hardware-info.md`.

## NUKE-ontsnapping (nuke-scoped prune van `spelerLocaties`)

`spelerLocaties` wordt buiten een nuke **nooit** opgeschoond (een stilgevallen beacon blijft als ghost
staan). Dat maakt ontsnappen aan een NUKE op hardware onmogelijk (de nuke-controle checkt `loc[naam] !=
null`). Daarom bevat `Evalueer spelstatus` een **nuke-scoped** prune, die **alleen** loopt zolang
`global.nukeActief === true` **én** niet in sim (`simVeld24 !== true`):

- Voor elke geregistreerde speler (`spelersLijst` = `{mac:naam}`) wordt `status_lastSeenMac[mac]`
  vergeleken met `global.nukeEscapeMs` (= het nuke-event-veld `escape_s` × 1000, default **4000 ms**).
- Is de beacon > `nukeEscapeMs` niet meer gezien (of nooit), dan wordt de speler uit `spelerLocaties`
  gehaald → hij geldt bij de nuke-controle als **VEILIG (ontkomen)** en verdwijnt live van de radar.

Buiten de nuke raakt deze node `spelerLocaties` **niet** aan (locatiebepaling blijft ongewijzigd voor
Klokslag/Infected/overige events). In sim regelt `Sim directe locatie` het ontsnappen al. `nukeEscapeMs`
wordt gezet in `Voer gevolg uit` bij nuke-start. Zie `docs/spel/event-catalogus.md` (Nuke) en
`docs/invarianten.md §4c` (N1/N7).

## Globale variabelen

| Variabele                | Inhoud                                                |
|--------------------------|-------------------------------------------------------|
| `status_lastSeenMac`     | `{mac: timestamp}` — laatste detectie per beacon      |
| `status_lastSeenPaal`    | `{paalId: timestamp}` — laatste data per paal         |
| `status_lastDataTs`      | timestamp van het laatste bericht op `plaatjes/data`  |
| `status_batterijPaal`    | `{paalId: {volt, ts}}` — laatst gemelde batterij V    |
| `status_toonBatterij`    | boolean — toggle "Toon batterij" op dashboard         |
| `status_override`        | boolean — toggle "Override NO-GO" op dashboard        |
| `status_ok`              | boolean GO/NO-GO                                      |
| `status_fouten`          | array van fouten                                      |

## Heartbeat via batterij-regels

Sinds 2026-05-20 stuurt elke slave per scancyclus óók een `{paal,batt}`-regel,
los van het aantal gevonden spelers. `Registreer batterij` werkt daarmee
tegelijk als heartbeat: `status_lastSeenPaal[paalId]` wordt elke batch
ververst, ook als er 0 spelers in het vak staan. Een paal die niets meer
stuurt valt nu vanzelf in **VEROUDERD** na `SLAVE_STALE_MS` (60 s).

## Dashboard-elementen (toegevoegd 2026-05-20)

| Element                  | Werking                                                       |
|--------------------------|---------------------------------------------------------------|
| MQTT-blok (linksboven)   | Groen `MQTT ✓ OK` of rood `MQTT ✗ WEG`, gestuurd door de status van de `Ontvang detecties` MQTT-in node. |
| Toggle **Toon batterij** | Bij UIT toont de Batterij-kolom in de palen-tabel `-`. Bij AAN: `3.87 V` per paal (laatste meting). De switch triggert direct een herberekening (niet wachten op de 3s-timer). |
| Toggle **Override NO-GO**| Bij UIT en `status_ok = false` weigert `Start spel`. Bij AAN: spel start met expliciete waarschuwing in de speltoestand-melding. Statustekst toont `OVERRIDE actief` zodat de switch nooit per ongeluk aanblijft. |

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
