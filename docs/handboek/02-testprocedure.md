# H2 — Testprocedure: verifiëren dat elk onderdeel werkt

Een **bottom-up** verificatieketen: elke test bewijst één schakel en bouwt op de vorige, zodat een
groen vinkje betekenis heeft. Doe **T1–T11 integraal** bij een eerste opbouw of na firmware-/
flow-wijzigingen; op een gewone speeldag volstaan **T2, T4, T5, T7** (zit al grotendeels in de
ochtendchecklist van [H1 §8](01-veldopzet.md)).

Notatie per test: **Doel → Stappen → Verwacht → Bij falen** (met verwijzing naar het document met
de volledige foutzoek-boom — die worden hier niet gedupliceerd).

---

## T1 — Slave solo (één paal, seriële monitor)

**Doel:** een individueel bord boot, herkent zichzelf en scant.
**Stappen:** sluit één slave via USB aan op de pc; open de PlatformIO Serial Monitor (115200).
**Verwacht:**
```
SLAVE MAC-ADRES : xx:xx:xx:xx:xx:xx
[SETUP] Eigen MAC herkend -> PAAL_ID N
=== Slave klaar, Paal ID: N ===
[SCAN] Start (1000 ms)...   → [SCAN] Klaar, k beacons gevonden (batt XXXX mV)
```
**Bij falen:** `!! ONBEKEND BORD` → MAC toevoegen aan `paal_macs.h`
([MAC-werkblad](../../firmware/tools/mac-tabel.md)); bord niet vindbaar/lege monitor → download-mode
forceren (BOOT vasthouden → RESET tikken → BOOT los), zie [`slave.md`](../handleidingen/slave.md).

## T2 — LED + zoemer per paal

**Doel:** de uitgangen van elke paal werken en het commando-pad bereikt hem.
**Stappen:** open op je telefoon het dashboard → pagina **Spelstatus** → groep **Paaltest (LED + zoemer)**.
Kies met de schuif de **paal (1-24)** en klik **LED-test** (regenboog over alle 7 LEDs) resp.
**Zoemer-test** (korte piep). Klik **Uit (deze paal)** om de LED weer te doven. Zo loop je snel elke
paal af. (Onder water: LED-test = actie 19, zoemer = actie 3, uit = actie 0, gerouteerd naar de juiste master.)
**Verwacht:** de gekozen paal toont een **rollende regenboog** (alle 7 LEDs) resp. **piept** eenmalig.
**Bij falen:** maar ~3 LEDs branden → verkeerde build-flags (altijd PlatformIO, zie
[`slave.md`](../handleidingen/slave.md)); niets → check T4/T6 (keten) en de master-status-logs.

## T3 — Batterijmeting + waarschuwing

**Doel:** de spanningsmeting klopt en de vervang-waarschuwing werkt.
**Stappen:** Spelstatus → "Toon batterij" aan; vergelijk 1–2 palen met een multimeter.
Simuleer desnoods een lage stand: `mosquitto_pub -h 192.168.1.43 -t plaatjes/data -m
'{"paal":1,"batt":3.4}'`.
**Verwacht:** kolom toont de spanning (±0,05 V); onder **3,5 V** verschijnt foutcode **ST-005
"Batterij bijna leeg … vervangen"** (WAARSCHUWING, blokkeert GO niet).
**Bij falen:** zie [`hardware-info.md`](../hardware/hardware-info.md) (meting/drempels).

## T4 — Detectieketen (baken → radar)

**Doel:** de hele weg baken → slave → master → bridge → MQTT → locatiebepaling werkt.
**Stappen:** leg één baken bij een paal. Kijk in volgorde: (a) master-LED pulst; (b) op de Pi
`docker logs serial-bridge --tail 5` → teller loopt op, routes geleerd; (c) dashboard **Live
Radar**: de speler verschijnt op die paal. Zonder hardware: gebruik de inject **"TEST: publiceer
plaatjes/data"** (tab 01) — Lilou hoort op paal 1 te verschijnen.
**Verwacht:** speler zichtbaar op de juiste paal binnen enkele seconden.
**Bij falen:** volg de **beslisboom** in [`locatiebepaling.md`](../locatiebepaling.md) ("Komt er wel
data binnen?") — die dekt monitor-open/poorten/routes/broker stap voor stap.

## T5 — Spelstatus GO

**Doel:** de pre-flight ziet alle palen én spelers.
**Stappen:** zet alle palen aan en alle bakens in het veld; open **Spelstatus**.
**Verwacht:** alle palen OK (heartbeat < 60 s), alle spelers OK (< 15 s), foutcodes leeg,
statustekst **GO**.
**Bij falen:** per rij staat de ouderdom; ST-codes wijzen de schakel aan (ST-001 speler, ST-002
paal, ST-003 datastroom, ST-006 master-conflict) — zie
[blokken/02](../../pi/node-red/blokken/02_spelstatus/README.md).

## T6 — Commando-keten terug (met aflever-bevestiging)

**Doel:** commando's van Node-RED bereiken de juiste paal én worden bevestigd.
**Stappen:** stuur via de **Paaltest**-groep (Spelstatus) een LED-test naar een paal van **elke master**
(bv. paal 2, 10 en 20). Kijk mee in `docker logs serial-bridge` of desnoods de master-monitor.
**Verwacht:** per commando `{"status":"queued"...}` gevolgd door `{"status":"uitgevoerd","paal":N}`
(de applicatie-ACK). `buiten_bereik` hoort **nooit** te verschijnen (routeringsfout).
**Bij falen:** `Nog geen poort geleerd` → wacht op de master-announce (~3 s) of check T4;
`opgegeven` → paal onbereikbaar (afstand/batterij). Zie
[`serial-bridge.md`](../handleidingen/serial-bridge.md) + [`master.md`](../handleidingen/master.md).

## T7 — Audio

**Doel:** afroepen zijn over het hele veld verstaanbaar.
**Stappen:** start een sim-testronde (of Admin → "Geluid (box)") en laat een event met afroep spelen;
loop naar de rand van het veld. (De aparte *Audio-test*-dashboardpagina is verwijderd.)
**Verwacht:** segmenten spelen naadloos achter elkaar; verstaanbaar op 12 m.
**Bij falen:** [`audio-player.md`](../handleidingen/audio-player.md) (container, `--device=/dev/snd`,
volume, ontbrekende WAV's worden stil overgeslagen).

## T8 — Drukknoppen

**Doel:** elke fysieke knop registreert betrouwbaar.
**Stappen:** dashboard → **Drukknop-test**: wapen een knop-paal (`ACTIE_KNOP_ARM`), druk enkele
keren, ontwapen.
**Verwacht:** de kleine rode LED op de paal brandt (gewapend) en dooft zolang je drukt; de teller
op het dashboard volgt **elke** druk (kogelvrij herverzonden).
**Bij falen:** staat de paal in `[CONFIG] Drukknop-palen`? Zie [`slave.md`](../handleidingen/slave.md)
(knop-ISR) en [`protocol.md`](../protocol.md) (`MSG_KNOP`).

## T9 — Scan-duur + zelfherstel

**Doel:** de BLE-scan-tuning werkt en overleeft een paal-reboot.
**Stappen:** Beacons & Locatie → **"Scan-duur (BLE)"** → zet alle slaves op bv. 700 ms; trek daarna
van één paal de batterij en zet ze terug.
**Verwacht:** (via een seriële monitor) `[SCAN] Venster nu 700 ms`; na de reboot herstelt Node-RED
de waarde automatisch binnen ~10 s (eerstvolgende heartbeat).
**Bij falen:** [`locatiebepaling.md`](../locatiebepaling.md) ("BLE-scan-duur").

## T10 — Simulator-smoke (zonder hardware)

**Doel:** de spel-engine draait een volledige event-cyclus, los van het veld.
**Stappen:** stop de bron (`docker stop serial-bridge`), open de
[browser-simulator](../../pi/simulator/README.md), modus **Simulatie**, voeg spelers toe, start een
PoF-spel (doel kiezen!), laat 2–3 events lopen en versleep spelers legaal/illegaal.
**Verwacht:** afroep + reveal + reactietijd + `grace` + controle; scoring klopt met de
[scoringtabel](../invarianten.md) (foutcodes enkel bij echte overtreding).
**Extra checks (juli 2026):**
- Versleep tijdens de **aanloop** een speler 2 palen → volgende controle toont `| VRIJ GEWANDELD
  (0 uur)`, `totaalUren` blijft gelijk, +1 valsspeelpunt. Met god-punt op zak: `[GOD-PUNT]`, saldo −1.
- Sleep tijdens de **regroup** na een nuke → géén straf, en de timer telt af vanaf **45 s**.
- Vink **Thuisbank** aan (🎲 Spelinstellingen), laat een speler de ring rond en exact op zijn startuur
  landen → `| GESTORT (+N uur globaal)` + Leaderboard stijgt. Maak hem ziek en herhaal →
  `| THUIS (geblokkeerd: toestand)`.
**Bij falen:** dit is een flows-probleem, geen hardware — check dat de laatste flows gedeployed
zijn (`deploy-flows.ps1`, **niet** `docker restart`). Ontbreekt `pofVrijPad` in `resetPartij`, dan is
`settings.js` gewijzigd zonder **container-herstart**. Herstart daarna `serial-bridge`.

## T11 — End-to-end mini-spel (hardware)

**Doel:** het volledige spel op echte hardware, klein.
**Stappen:** 2–3 palen + 2 bakens; Spelstatus GO; start PoF (doel "Verplaats 3 uur", dichtheid
hoog voor een kleine groep); loop één event correct uit en één bewust fout (te ver).
**Verwacht:** afroep hoorbaar, LED's kloppen, de controle geeft de juiste status/Δ (OK / TE VEEL)
en de radar + stats volgen.
**Bij falen:** vergelijk sim (T10 slaagde?) — verschil zit dan in de sensing: tuning-werkwijze in
[`locatiebepaling.md`](../locatiebepaling.md) (venster/hysterese/grace/scan-duur).

## T12 — Autonoom testharnas (regressie, optioneel)

**Doel:** de spelregels breed en herhaalbaar aftoetsen tegen een onafhankelijk orakel.
**Stappen & verwacht:** volg [`spel-testen.md`](../handleidingen/spel-testen.md) — pre-flight
(orakel-zelftest → driver-rooktest → één handmatige ronde) en daarna
`python -m tools.speltest.runner --strategie all --rondes 30 --fuzz`. Exitcode 0 = geen bevindingen.
**Bij falen:** het rapport bevat per bevinding een replay + reproductiecommando.

## T13 — Noodherstel-oefening (aanrader vóór de speeldag)

**Doel:** de plan-B werkt écht.
**Stappen:** doorloop het ["hub vervangen in 10 min"-runbook](../handleidingen/hub-noodherstel.md)
één keer droog met de reserve-SD.
**Verwacht:** dashboard komt op, pre-flight GO, spelstand (stats/π-klok) hersteld.

---

## Symptoom → document

| Symptoom | Kijk in |
|---|---|
| Paal knippert rood (fout-blink) | [`slave.md`](../handleidingen/slave.md) — MAC niet in `paal_macs.h` |
| Bord niet flashbaar / lege monitor | [`pinout.md`](../hardware/pinout.md) — BOOT/RESET download-mode |
| Geen detecties op de radar | [`locatiebepaling.md`](../locatiebepaling.md) — beslisboom |
| Speler springt tussen palen | [`locatiebepaling.md`](../locatiebepaling.md) — tuning (hysterese/grace) |
| Commando's komen niet aan / `buiten_bereik` | [`serial-bridge.md`](../handleidingen/serial-bridge.md) + [`master.md`](../handleidingen/master.md) |
| ST-005 batterij | [`hardware-info.md`](../hardware/hardware-info.md) — hot-swap |
| ST-006 master-conflict | [`serial-bridge.md`](../handleidingen/serial-bridge.md) — verkeerde env geflasht |
| Dashboard traag / Pi vol | [`hardware-info.md`](../hardware/hardware-info.md) — geheugen (L4) |
| Hub dood / SD corrupt | [`hub-noodherstel.md`](../handleidingen/hub-noodherstel.md) |
| Scoring lijkt fout | [`invarianten.md`](../invarianten.md) §2 + T12-harnas |
| Flows-wijziging "doet niets" | [`DEPLOY.md`](../../pi/node-red/DEPLOY.md) — deploy-flows, niet docker restart |
