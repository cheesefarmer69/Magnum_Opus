# To-do — Magnum Opus

Openstaande taken die nog uitgevoerd moeten worden. Per item: korte omschrijving
en waarom het nodig is.

> Claude Code mag taken die absoluut nog moeten gebeuren aan deze lijst
> toevoegen, op een gestructureerde manier: juiste categorie, korte omschrijving,
> en de reden waarom het nodig is.

## Protocol & communicatie

- [x] **Error messaging protocol opstellen** *(Batch 1 — protocol v2: transport klaar)*
  Het ESP-NOW-transport is er: `MSG_FOUT` met `ernst` (info/waarschuwing/fout) + `foutcode`
  + `detail`, vertaald naar `{"paal","fout","ernst","detail"}` op `plaatjes/data` (foutcode-tabel
  in `docs/protocol.md` §3). De slave stuurt al fouten voor batterij-kritiek, ESP-NOW-zendfout en
  BLE-overflow. **Follow-up:** Node-RED-zijde (pre-flight check de foutcodes laten tonen/aggregeren).

- [x] **Slave heartbeat / keepalive** *(Batch 1 — protocol v2)*
  De slave stuurt elke `HEARTBEAT_INTERVAL_S` (10 s) een `MSG_HEARTBEAT` (uptime, batt, fw-versie),
  ook zonder detecties → `{"paal","hb":1,...}` op `plaatjes/data`. **Follow-up:** Node-RED de
  heartbeats laten gebruiken voor echte connectiviteit-per-paal in de pre-flight check.

- [x] **Knop-event bereikt de Pi niet** *(Batch 1 — protocol v2)*
  Opgelost: de slave stuurt nu `MSG_KNOP` via **ESP-NOW** naar de master; die vertaalt het naar
  `{"paal":N,"knop":1}` op `plaatjes/data` (was de dode USB-CDC).

- [x] **Payload-plafond `spelers[9]` per batch** *(Batch 1 — protocol v2)*
  Opgelost: `batch_message_v2` draagt tot **30** spelers (binaire MAC's, 215 B). Bij >30 in één vak
  wordt niet meer stil gedropt maar een `MSG_FOUT` (BLE-overflow) gestuurd.

- [ ] **Multi-master indexering / `commando/masterN`-routing** *(Batch 4)*
  De simulator en de routing volgen nu alleen `commando/master1`. Voor 3 masters /
  24 palen moeten `commando/master2`/`master3` toegevoegd worden (sim-subscriptions
  én Node-RED-routing per `paal_id`-bereik 1–7 / 8–16 / 17–24).

## Node-RED blokken

- [x] **Blok: speler-score** — flow 04 Puntensysteem: levensuren/levensdagen
  per speler, deficit-model tegen illegaal tijdreizen, reset via flow 05 Admin.
- [x] **Blok: event engine** — flow 06 Plates of Fate: kiest events, leest voor
  (audio-abstractie), kiest doelwitten, voert gevolgen uit, regel-afdwinging.
  Nog uit te werken (zie hieronder).

## Plates of Fate — nog uit te werken

- [ ] **Audio-consument** — een component op de geluidsbox die `audio/afspelen`
  afspeelt (TTS live, of vooraf opgenomen bestanden). Nu publiceert de engine
  enkel het verzoek; er luistert nog niets.
- [ ] **Concrete events** — de huidige `pofEvents` zijn placeholders. Echte
  events met afgestemde teksten, doelwitten, gevolgen en reactietijden.
- [ ] **Straffen tegen valsspelen** — bovenop het deficit-model: actieve sancties
  wanneer een speler regels overtreedt (bv. illegaal tijdreizen of te ver
  verplaatsen). Het deficit-model is hiervan de basis.
- [ ] **Minigames + cyclusbeheer** — afwisseling Plates of Fate ↔ minigame ↔
  herstart (levensjaren-reset), zoals beschreven in `docs/spel/spel.md`.

- [ ] **Sim ↔ hardware perfecte pad-pariteit (rand)** — de simulator publiceert nu de
  **settled** paal (pas bij loslaten), wat voor élke realistische zet (afgelegde boog ≤ 12
  palen) identiek scoort aan de hardware-hysterese. Enkel een bewuste "lange weg rond"
  (>12 palen in één sleep) is uit losse settled-posities niet eenduidig af te leiden — op
  hardware net zomin (zie `docs/spel/event-systeem.md` §8, "sensingkwaliteit = ondergrens").
  Perfecte pariteit zou betekenen dat de sim de hardware-hysterese (grace + sustained-switch)
  namaakt, of dat de zet als geordende enkel-stap-reeks i.p.v. één hop wordt opgenomen.
  Lage prioriteit.

## Hardware / firmware

- [ ] **Buzzer-resonantiefrequentie per paal kalibreren**
  Buzzers uit dezelfde batch verschillen in volume op dezelfde frequentie
  (productiespreiding op de resonantiefrequentie). Per buzzer de luidste
  frequentie opmeten en die per slave instellen, zodat alle palen even
  hoorbaar zijn. Sinds Batch 1 staat `BUZZER_FREQ` als **per-paal constante** naast `PAAL_ID`
  in `firmware/Slave/src/main.cpp` (gebruikt in `MELODIE_PIEP`); de waarden zijn nu nog allemaal
  1500 Hz. **Resteert:** per bordje de luidste frequentie opmeten en die waarde invullen.
  Aanpak: per buzzer een frequentiesweep afspelen en de luidste frequentie
  opmeten (gehoor of dB-meter); die waarde per slave vastleggen — ofwel als
  `PAAL_ID`-afhankelijke constante in de firmware, ofwel via een kleine
  kalibratietabel `{paal → freq}` zodat één firmware-build alle palen dekt.

## Spelontwerp

- Zie `docs/spel/spel.md` → sectie "Open vragen / nog uit te werken" voor openstaande
  ontwerpbeslissingen (o.a. de rol voor uitgeschakelde spelers).
