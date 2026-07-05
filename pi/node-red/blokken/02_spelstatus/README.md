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
| `ST-006` | FOUT                | **Master-conflict** (bridge): twee poorten melden hetzelfde `MASTER_NR` — twee borden met dezelfde env geflasht. Herflash het verkeerde bord (invariant C8). |

> Deze codes worden lokaal in Node-RED afgeleid. Wanneer het error messaging
> protocol bestaat, kunnen slaves/master eigen foutcodes sturen die hier
> samenkomen.

## Instelbare drempels

Bovenaan de functie `Evalueer spelstatus`:

- `SPELER_TIMEOUT_MS` (15 s) — speler geldt als niet-gedetecteerd na deze stilte.
- `SLAVE_STALE_MS` (60 s) — paal geldt als "verouderd" na deze stilte (en gaat dan óók uit de ring, zie L3-sectie).
- `GEEN_DATA_MS` (10 s) — geen enkele data → master/bridge-fout.
- `global.spelerPruneMs` (90 s, runtime instelbaar) — ghost-prune-drempel van `spelerLocaties` (zie S1-sectie).
- `BATT_VERVANG_V` (3,5 V) — celspanning waaronder een **ST-005**-batterijwaarschuwing verschijnt. Gebruikt
  de `batt`-waarde uit `status_batterijPaal` (onafhankelijk van de "Toon batterij"-toggle); `WAARSCHUWING`
  blokkeert de GO/NO-GO niet. Zie `docs/hardware/hardware-info.md`.

## Ghost-prune van `spelerLocaties` (S1) — algemeen + nuke-venster

`Evalueer spelstatus` schoont `spelerLocaties` **altijd** op in hardware-modus (niet enkel bij een
nuke, zoals vroeger): een speler wiens beacon te lang niet meer vers gezien is (`status_lastSeenMac`)
wordt uit `spelerLocaties` verwijderd, mét `node.warn`-logregel. Zo wordt een dode/weggelegde beacon
geen eeuwige speler in Klokslag/Infected of de doel-telling (invariant EV3).

- **Normale drempel:** `global.spelerPruneMs` (default **90 000 ms**). Ruim boven de ST-001-waarschuwing
  (15 s), zodat de operator eerst "VERLOREN" ziet vóór de speler echt verdwijnt.
- **Tijdens een nuke:** het kortere `global.nukeEscapeMs` (= nuke-event-veld `escape_s` × 1000, default
  **4000 ms**, gezet in `Voer gevolg uit`) → wie wegloopt geldt bij de nuke-controle als
  **VEILIG (ontkomen)**.
- **Sim-modus:** niets — `Sim directe locatie` is daar de enige schrijver.
- Komt de beacon terug, dan zet de eerstvolgende detectie de speler gewoon weer in het veld.

Het **gepauzeerd-filter** (`gespauzeerdePlayers`) geldt sinds deze fix óók in de **Klokslag-** en
**Infected-engine** (flow 07): gepauzeerde spelers nemen geen palen in en tellen niet als
overlever/besmettingsbron. Zie `docs/invarianten.md` (EV3) en `docs/locatiebepaling.md`.

## Heartbeat-gestuurde ring (L3): dode palen uit `palenActief`

Aansluitend op de staleness-detectie haalt `Evalueer spelstatus` (alleen hardware-modus) een paal die
ooit data stuurde maar > `SLAVE_STALE_MS` stil is **tijdelijk uit `palenActief`** — events, portalen en
uur-doelwitten kiezen hem dan niet meer, en `paalLedForceRebuild` herbouwt de LED-staat. De paal komt
**automatisch terug** zodra hij weer data stuurt (heartbeat/batch). Details:

- Status in de palen-tabel wordt dan **"VEROUDERD (uit ring)"**; de ST-002-omschrijving vermeldt het.
- **Nooit-geziene** palen blijven in de ring (opbouwfase vóór de eerste boot telt niet als uitval).
- De ring zakt **nooit onder 2 palen** (vangnet; dan blijft de laatste bekende ring staan).
- In sim-modus beheert `Sim-veld instellen` `palenActief` en doet deze logica niets.

Zie `docs/invarianten.md` (F4).

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
| `status_bridgeFout`      | `{code, master, poorten, ts}` — laatste `bridge_fout` van de serial-bridge (bv. MASTER_CONFLICT → ST-006, vervalt na 30 s) |

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
