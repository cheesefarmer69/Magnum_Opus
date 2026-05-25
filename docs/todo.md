# To-do — Magnum Opus

Openstaande taken die nog uitgevoerd moeten worden. Per item: korte omschrijving
en waarom het nodig is.

> Claude Code mag taken die absoluut nog moeten gebeuren aan deze lijst
> toevoegen, op een gestructureerde manier: juiste categorie, korte omschrijving,
> en de reden waarom het nodig is.

## Protocol & communicatie

- [ ] **Error messaging protocol opstellen**
  Een gestructureerd protocol voor foutmeldingen door het hele systeem
  (slave → master → Pi → Node-RED). Moet vastleggen: foutcodes, ernst-niveaus
  (info / waarschuwing / fout) en een transportformaat. Zo kunnen blokken zoals
  de pre-flight check fouten op een uniforme manier tonen en doorsturen.
  Voorlopig toont de pre-flight check enkel lokaal in Node-RED afgeleide fouten;
  dit protocol maakt echte foutmeldingen vanuit de hardware mogelijk.

- [ ] **Slave heartbeat / keepalive**
  Slaves sturen nu enkel data wanneer ze een beacon detecteren. Daardoor kan
  Node-RED niet betrouwbaar zien of een slave zonder spelers in de buurt nog
  leeft. Een periodiek "ik leef"-bericht (ook zonder detectie) is nodig zodat de
  pre-flight check echte connectiviteit per paal kan vaststellen i.p.v. enkel
  "ooit data gezien".

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
  herstart (levensjaren-reset), zoals beschreven in `docs/spel.md`.

## Hardware / firmware

- [ ] **Buzzer-resonantiefrequentie per paal kalibreren**
  Buzzers uit dezelfde batch verschillen in volume op dezelfde frequentie
  (productiespreiding op de resonantiefrequentie). Per buzzer de luidste
  frequentie opmeten en die per slave instellen, zodat alle palen even
  hoorbaar zijn. Nu staat `BUZZER_FREQ` als één globale constante in
  `firmware/Slave/src/main.cpp`; dit moet een per-paal waarde worden
  (bv. naast `PAAL_ID`, of een kleine kalibratietabel). Zie de aanpak in
  het antwoord/handleiding hieronder.

## Spelontwerp

- Zie `docs/spel.md` → sectie "Open vragen / nog uit te werken" voor openstaande
  ontwerpbeslissingen (o.a. de rol voor uitgeschakelde spelers).
