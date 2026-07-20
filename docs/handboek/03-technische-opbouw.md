# H3 — Technische opbouw: het volledige systeem, laag voor laag

Dit hoofdstuk beschrijft **hoe** Magnum Opus technisch in elkaar zit en **waarom** het zo ontworpen
is. Het is een geleide synthese: exacte structs, tabellen en waarden staan in de normatieve
documenten (per sectie gelinkt); hier krijg je het samenhangende verhaal.

---

## 1. Overzicht: de keten in één beeld

```
[speler-baken]  --BLE-advertentie (300 ms)-->  [SLAVE per paal, ESP32-C3]
                                                    |  ESP-NOW (kanaal 1)
                                    batches ↓       |       ↑ commando's
                                               [MASTER ×3, ESP32 WROOM]
                                                    |  USB-serial (JSON per regel, 115200)
                                               [PI: serial-bridge (Docker)]
                                                    |  MQTT (Mosquitto, ook WebSocket :9001)
       [audio-player] <-- audio/afspelen --- [NODE-RED: spellogica + dashboards]
       [browser-simulator] <---------------------- MQTT-over-WebSocket
```

Eén richting draagt de **waarneming** (wie staat bij welke paal), de andere de **actie** (LED's,
zoemers, audio). De hele spelintelligentie zit in **Node-RED** op de Pi; de veld-elektronica is
bewust dom en robuust gehouden. Volledig contract: [`protocol.md`](../protocol.md).

## 2. Veld-laag: bakens en slaves

**Bakens.** Elke speler draagt een BLE-baken dat ~elke **300 ms** adverteert. Er is geen
verbinding — de palen luisteren alleen. De koppeling MAC→naam is een Node-RED-config
(dashboard-bewerkbaar, retained; zie §6).

**Slave (één per paal, ESP32-C3).** De hoofdlus is een vaste cyclus
([`slave.md`](../handleidingen/slave.md)):

1. **BLE-scan** — niet-blokkerend, begrensd door een `millis()`-venster (`scanDuurMs`, default
   1000 ms, runtime instelbaar 300–2000 ms via het dashboard). De scan filtert op de
   **OUI-prefix** van de spelersbakens + een RSSI-drempel, dedupliceert per MAC (sterkste RSSI)
   en vult zo de batch. Eén antenne deelt BLE en ESP-NOW — de duty (window 64/interval 80,
   ~80 %) is daarop afgestemd.
2. **Zenden** — een `batch_message_v2` met **variabele lengte** (alleen het gebruikte deel; tot 30
   spelers per paal) via **ESP-NOW** naar zijn master, met een random backoff (0–150 ms) zodat 24
   slaves elkaars pakketten niet structureel wegdrukken. Óók bij 0 spelers, zodat "leeg vak" een
   feit is en geen aanname. Elke ~10 s gaat er een **heartbeat** mee (batterij, uptime, fw-versie).
3. **Luistervenster** — commando's die binnenkomen (ook tijdens de scan) staan in een
   **ringbuffer** (producer = radio-callback, consumer = loop) en worden in volgorde en
   **idempotent** uitgevoerd (`cmd_seq`), met een **applicatie-ACK ná uitvoering** terug.

Verder op de slave: **batterijmeting** (GPIO4, deler ×2; kritiek < 3,2 V → `MSG_FOUT`),
**drukknop** via interrupt met een cumulatieve teller die ~6× herverzonden wordt (kogelvrij),
**LED-rendering** van de actie-set (kleuren + animaties zoals nuke/oogst/tijdbom-tik — volledige
tabel in [`protocol.md`](../protocol.md) §2), en de **zoemer** (per paal gekalibreerde frequentie).

**Provisioning: één binary.** Elk bord draait exact dezelfde firmware en zoekt bij boot zijn eigen
MAC op in de gedeelde tabel [`firmware/shared/paal_macs.h`](../../firmware/shared/paal_macs.h) →
zijn `PAAL_ID`. Onbekend MAC = rode fout-blink + niet meedoen. Diezelfde tabel voedt óók de
masters — één bron van waarheid, geen per-bord-configuratie.

## 3. Master-laag (3 × ESP32 WROOM)

Elke master bedient 8 palen (1–8 / 9–16 / 17–24, via build-flags `PAAL_MIN/MAX/MASTER_NR`) en is
de brug tussen ESP-NOW en USB-serial ([`master.md`](../handleidingen/master.md)):

- **Ontvangst-whitelist**: pakketten van MAC's buiten zijn eigen 8 slaves worden gedropt — zo
  segmenteren 3 masters één veld zonder elkaars verkeer te dubbelen.
- **Naar de Pi**: elke detectie wordt één JSON-regel (`{"paal","mac","rssi"}`), plus batterij-,
  heartbeat-, fout- en knop-regels. Alle serial-output loopt via één schrijver (log-queue) zodat
  regels nooit interleaven.
- **Van de Pi**: `{"paal","actie"}`-commando's gaan per slave in een **FIFO met retries** en gelden
  pas als afgeleverd bij de **applicatie-ACK** van de slave (niet de radio-ACK). Volgorde blijft
  behouden; een dode paal blokkeert de andere niet.
- **Directe berichten** (buiten de FIFO, fire-and-forget): buzzer-tuning (actie 12), Klokslag-LED
  (actie 16, teamkleur/helderheid/modus) en scan-duur-config (actie 20).
- **Announce**: elke ~3 s kondigt de master zich aan (`{"announce":1,"master":N,…}`) zodat de
  bridge de routes kent zonder op slave-data te wachten.

## 4. Bridge (Pi, Python in Docker)

[`serial-bridge.md`](../handleidingen/serial-bridge.md) — bewust minimaal: hij **detecteert elke
CH340-master automatisch** (USB-poort maakt niet uit), zet elke geldige JSON-regel door naar MQTT
(`plaatjes/data`) en routeert `commando/master1|2|3` naar de juiste poort op basis van de
announces. Hij is **inhoud-agnostisch** (nieuwe JSON-velden passeren zonder wijziging) en alarmeert
één misconfiguratie zelf: twee borden die dezelfde master claimen (→ **ST-006** op het dashboard).

## 5. MQTT-laag (Mosquitto)

De centrale bus. TCP :1883 voor bridge/Node-RED, **WebSocket :9001** voor browser-clients
(simulator). Topics in drie families ([`protocol.md`](../protocol.md) §5):

- **Data & commando's**: `plaatjes/data` (veld → logica), `commando/masterN` (logica → veld),
  `audio/afspelen` (korte gesproken segmenten) en `audio/muziek` (bestuurbaar lange-track-kanaal:
  play/pause/resume/stop — gebruikt door Bommen vermijden, het Pools-event en de dood-cutscene).
- **Retained configs** (overleven herstart/deploy; dashboard is de bron): `config/spelers`
  (baken↔naam), `config/scan-duur`, `config/drukknoppen`, `config/led-helderheid`, `spel/type`,
  `bommen/keuze` (bommen-track AoT/maki), `sim/avond-modus` (avondspel aan/uit), `audio/volume`.
- **Spel-status voor UI's**: `pof/status` (1 s), `pof/controle`, `pof/portalen`, `pof/ziekte`,
  `pof/tijdbom`, `pof/middernacht`, `pof/dienaars`, `pof/doelstatus`, `pof/animatie`,
  `pof/dood-anim` (onmiddellijke-dood-cutscene), `klokslag/*`, `infected/status`, `bommen/status`,
  `spel/historie`, `spel/state` (30 s-snapshot) en de `sim/*`-ingangen van de simulator/testharnas.

## 6. Node-RED: de spellogica (flows 00–07)

Het hart. Elke flow-tab is een blok met eigen README
([`pi/node-red/blokken/`](../../pi/node-red/blokken/README.md)):

| Flow | Rol |
|---|---|
| **00 Configuratie** | seed-injects (spelerslijst-bootstrap, paaltjeslijst, eigenschappen, drukknop-palen) + de 2 hardware-test-injects |
| **01 Locatiebepaling** | van ruwe RSSI naar "speler staat op paal X" + de dashboard-groepen Beacons/scan-duur/spelersbeheer |
| **02 Spelstatus** | continue pre-flight (GO/NO-GO, ST-foutcodes, batterij), heartbeat-registratie |
| **03 Bediening** | speltoestand (start/stop/pauze/manueel), doelkeuze, speltype |
| **04 Puntensysteem** | pad-opname per settled paalwissel, `spel/state`-dump, middernacht-poortbewaker |
| **05 Admin** | beheersknoppen achter een twee-staps unlock (resets, paal-reset, klok-reset) |
| **06 Plates of Fate** | de event-engine (zie hieronder) |
| **07 Klokslag** | de Klokslag-engine + teams; hierop draaien ook de **Infected-engine**, de **Bommen-engine** (minigame "Bommen vermijden": gescripte muziek-tijdlijn, actie 25/`MSG_BOM`, track-keuze via `bommen/keuze`) en de **onmiddellijke-dood-animatie** (avondspel) |

**Locatiebepaling** ([`locatiebepaling.md`](../locatiebepaling.md)): per speler worden recente
RSSI-samples per paal bijgehouden (venster), de **mediaan** per paal vergeleken, en gewisseld met
**hysterese + sustained-switch + grace** — dat maakt de positie stabiel ondanks radiofluctuaties.
De uitkomst is `spelerLocaties` (naam → paal), de **centrale waarheid** waar alle engines op
draaien. Ghost-bescherming: een baken dat > 90 s niet meer gezien is, wordt eruit gehaald; een
paal die > 60 s zwijgt gaat tijdelijk uit de actieve ring.

**De PoF-engine** (flow 06 — [`event-systeem.md`](../spel/event-systeem.md) is leidend):

- **Toestandsmachine** op een 1 s-tick: `aanloop` (5 s) → **CHOOSE** (event kiezen, getallen
  rollen, doelwitten kiezen, afroep-audio bouwen) → doelwit-**reveal** (namen één voor één) →
  `reactie` (event-specifieke tijd) → **`grace`** (settle-marge, default 3 s) → **VERIFY**
  (controle + scoring) → opschonen → volgende. Manueel-modus vervangt de timers door de knoppen
  "Volgende event" en "Controle".
- **Doelwitkeuze**: gewogen per **tier** (common→legendary), geschaald met **doelwit-dichtheid**
  (aantal ≈ % van het aantal actieve spelers) en een **groep-boost** bij veel spelers; negatieve
  events wegen op avond-uren en op valsspelers zwaarder (aura). Een wachtrij plant 5 events vooruit
  (zichtbaar en bij te sturen in de simulator).
- **Pad-gebaseerde controle**: tijdens reveal+reactie+grace wordt elke settled paalwissel als hop
  opgenomen (`pofPad`). De controle beoordeelt **actie per actie** (STAP vooruit = 1 uur;
  TELEPORT via portaal = 0, richting-vrij, 1×/portaal) — nooit netto begin/eind. Daarbovenop de
  specials: middernacht-oversteek, tornado-zuigkracht, nuke-detectie, wolf-vangst,
  tweeling-synchroon, ziekte-genezing, god-punten. De volledige scoringtabel en alle
  randgevallen: [`invarianten.md`](../invarianten.md) §2 + §4.
- **Effect-registers**: toestanden leven als effecten op uur- (`bordStaat`), speler- of
  wereld-niveau, verouderen per ronde en sturen **centraal** de LED's aan ("Sync toestanden +
  LEDs" — een aflopend effect dooft zijn paal vanzelf). Een `max` per event begrenst gelijktijdige
  instanties.
- **Vangnetten**: een sensing-vloer klemt de reactietijd op wat de locatiebepaling fysiek aankan
  (~7 s bij defaults); de settle-grace laat trage paalwissels in het júiste event landen; een
  generation-token voorkomt dat uitgestelde acties na een reset nog vuren.

**Klokslag-engine** (flow 07): een 4 Hz-tick berekent per paal de inname (`P` groeit naar het
uurnummer `H` met een voorsprong-bonus, vervalt bij gelijkstand, overnemen kost 2H) en stuurt
teamkleur-LED's. **Infected-engine** (zelfde tab): dwell-teller van 15 s per speler op een
besmette paal, bestrijders-rotatie, win bij 3 overlevenden. **Bommen-engine** (zelfde tab): plant
bij de start de volledige muziek-tijdlijn van de gekozen track (`bommen/keuze`: AoT ~122 s of
maki ~84 s) als generation-gated cues; bommen gaan als `MSG_BOM` (actie 25) met `wacht_ms`
vooruit de lucht in zodat de slave het **doofmoment op de beat** ankert; wie op het doofmoment op
de paal staat verliest 10 levensuren (mag negatief, geen sterfte). De engine-modi (volautomatisch /
**Met timer** (semi-auto, EV9) / manueel) bepalen of het volgende event vanzelf start of op de
knop wacht. Regels: [`klokslag.md`](../spel/klokslag.md) / [`infected.md`](../spel/infected.md) /
[`bommen.md`](../spel/bommen.md) / [`avondspel.md`](../spel/avondspel.md).

**Persistentie**: de global-context wordt elke 15 s naar disk geflusht
(`contextStorage: localfilesystem` in [`settings.js`](../../pi/node-red/settings.js)) én elke 30 s
als retained `spel/state`-snapshot gepubliceerd; bij een (her)start rehydrateert Node-RED daaruit
— scores, historiek en de π-klok overleven dus een deploy, herstart of stroomdip. Alle
partij-resets lopen via **één** gedeelde helper (`resetPartij`), zodat elke reset-knop hetzelfde
en volledig wist. Geheugen is bewust begrensd (historie ≤ 30 partijen, snapshots ≤ 10 —
[`hardware-info.md`](../hardware/hardware-info.md) "Geheugen").

## 7. UI-laag

- **Dashboards** (Node-RED Dashboard 2.0, 9 pagina's): Spelstatus (pre-flight, master-bolletjes
  M1–M3, CPU-temperatuurtegel, **Pre-flight zelftest** en **Netwerk (masters)**), Bediening (speltoestand + doel + spelbalans + minigame-groepen;
  **Live Radar is een groep op deze pagina**, geen eigen pagina; sinds juli 2026 ook de groepen
  **Peek & veto**, **Event-regie**, **Event-mix (bag)** en **Spelflow**), **Events** (`/events`:
  welke events meedoen + hun tier), Leaderbord (`/leaderbord`, groot projectiescherm voor de beamer),
  Admin (achter twee-staps unlock), Beacons & Locatie (tuning + scan-duur + spelersbeheer),
  Historiek, Buzzer/LED test (incl. LED-helderheid en "Geluid (box)"-volume), Drukknop-test.
  Functie en opbouw per pagina: [`dashboards.md`](../handleidingen/dashboards.md).
- **Browser-simulator** ([`pi/simulator/`](../../pi/simulator/README.md)): verbindt via
  MQTT-over-WebSocket en is een volwaardige MQTT-deelnemer. **Monitor**-modus kijkt passief mee
  met een echt spel; **Simulatie**-modus vervangt de hardware volledig (deterministische posities,
  24-uurs veld) — sim en echt spel sluiten elkaar uit via één vlag, zodat een simulatie nooit een
  echt spel vervuilt. Er is ook een **autonoom testharnas** dat via dezelfde naad speelt en elke
  ronde tegen een onafhankelijk orakel toetst ([`spel-testen.md`](../handleidingen/spel-testen.md)).
- **Audio-player** ([`audio-player.md`](../handleidingen/audio-player.md)): een kleine service die
  `audio/afspelen`-verzoeken (lijstjes WAV-segmenten: "3" + "spelers" + eventzin + getallen)
  sequentieel over de aux-jack afspeelt. De engine bepaalt *wat*, de player *hoe*.

## 8. Betrouwbaarheid & RF-huishouding

- **Settle-latentieketen**: baken-advertentie (300 ms) → scanvenster (instelbaar) → mediaan/
  hysterese/grace in de locatiebepaling. De engine compenseert met de **settle-grace** en de
  **sensing-vloer**; de scan-duur is live te tunen. Samenspel en afwegingen:
  [`locatiebepaling.md`](../locatiebepaling.md).
- **Kanaalbeheer (H6)**: ESP-NOW zit vast op **kanaal 1**; het Pi-AP hoort op **6 of 11** zodat
  dashboardverkeer de veld-airtime niet opeet.
- **Zelfherstel**: batterij-hot-swap (paal reboot en meldt zich terug, scan-config wordt
  automatisch hersteld), nuke-scoped ghost-prune, heartbeat-gestuurde actieve ring, en het
  hub-runbook met reserve-SD/power station voor de single-point-of-failure
  ([`hub-noodherstel.md`](../handleidingen/hub-noodherstel.md)).
- **Bekende rev-B-punten** (niet blokkerend): geen OTA (H4), overbodige LED-power-gate (H5),
  LED-data op strapping-pin GPIO0 (H7) — uitleg en plan in
  [`hardware-info.md`](../hardware/hardware-info.md).

## 9. Verwijstabel: onderwerp → normatief document

| Onderwerp | Document |
|---|---|
| Wire-formats, actie-tabel (0–28), MQTT-topics | [`protocol.md`](../protocol.md) |
| Alle spelregels als invarianten + scoringtabel | [`invarianten.md`](../invarianten.md) |
| Verplaatsingscontrole (STAP/TELEPORT), event-cyclus | [`event-systeem.md`](../spel/event-systeem.md) |
| Event-schema (velden) / per-event catalogus | [`events.md`](../spel/events.md) / [`event-catalogus.md`](../spel/event-catalogus.md) |
| RSSI-algoritme + tuning + scan-duur + bakenbeheer | [`locatiebepaling.md`](../locatiebepaling.md) |
| GPIO's / veldgeometrie / voeding & geheugen | [`pinout.md`](../hardware/pinout.md) / [`playfield.md`](../hardware/playfield.md) / [`hardware-info.md`](../hardware/hardware-info.md) |
| Per-component how-to's (slave/master/bridge/audio/dashboards) | [`handleidingen/`](../handleidingen/) |
| Flows deployen + persistente context | [`DEPLOY.md`](../../pi/node-red/DEPLOY.md) |
| Ontwerp- en werkregels (voor wie meebouwt) | [`Design_rules.md`](../../Design_rules.md) |
| Versies en compatibiliteit | [`versions.md`](../versions.md) |
