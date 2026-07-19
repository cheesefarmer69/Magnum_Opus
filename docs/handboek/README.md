# Handboek Magnum Opus

Het complete handboek voor het opzetten, testen, begrijpen en spelen van Magnum Opus — het
interactieve buitenspel op een 24-hoekig speelveld met BLE-bakens, LED-palen en een centrale
spelcomputer.

> **Versie:** juli 2026 · **Status:** actueel met de repo op moment van schrijven.
> Dit handboek is de **narratieve en operationele laag**. De **normatieve** bron voor exacte
> regels, waarden en formaten blijven de diepe documenten: [`docs/invarianten.md`](../invarianten.md),
> [`docs/protocol.md`](../protocol.md), [`docs/spel/`](../spel/) en de
> [`docs/handleidingen/`](../handleidingen/). Wijkt dit handboek ooit af, dan winnen die documenten —
> en is dit handboek aan een update toe.

---

## Hoofdstukken

| # | Hoofdstuk | Voor wie | Wat je ermee kan |
|---|-----------|----------|------------------|
| 1 | [Veldopzet](01-veldopzet.md) | bouwploeg / operator | Het spel van nul opzetten in het veld: materiaal, veld uitzetten, palen, hub, bakens, tot en met "GO". Plus de speeldag-checklist en de afbouw. |
| 2 | [Testprocedure](02-testprocedure.md) | operator / technicus | Elk onderdeel stap voor stap verifiëren (T1–T13), van één losse paal tot een end-to-end mini-spel. Met symptoom→document-tabel. |
| 3 | [Technische opbouw](03-technische-opbouw.md) | technicus / ontwikkelaar | Hoe het volledige systeem technisch in elkaar zit, laag voor laag: bakens → palen → masters → bridge → MQTT → Node-RED → dashboards/simulator/audio. |
| 4 | [Spelersuitleg](04-spelersuitleg.md) | spelleider → spelers | **Eén compleet document om het spel uit te leggen**: voorleesbare briefing + volledige regels, de poort van middernacht, **alle events** (~20), aura & god-punten, LED-kleuren, do's & don'ts, straffen én de minigames Klokslag, Infected en Bommen vermijden, plus het avondspel. |
| 5 | [Events & dynamieken](05-events-en-dynamieken.md) | spelleider / operator | Elk event als vaste kaart (afroep · wie · wat doen · straf · duur/kans · LED · operator-notities) + alle permanente spelmechanismen en de minigame-dynamieken. |
| 6 | [Rollen & taakkaarten](06-rollen-en-taken.md) | grote moderator → hulpleiding | **Taken die je tijdens het spel kan doorgeven**: de rollen *Techniek-wacht* (batterijen, stille palen, warme hub) en *Speler-administratie* (pauzes, vastgelopen spelers, correcties), elk met een afdrukbare kaart voor in het veld. |

## Rollen

- **Bouwploeg** — zet het veld en de hardware op (H1) en draait de basistests mee (H2, T1–T5).
- **Operator** — bedient het dashboard tijdens de speeldag: pre-flight, start/stop, events volgen,
  batterijen wisselen, bakens her-toewijzen (H1 §7–8, H2, H5).
- **Spelleider** — brieft de spelers (H4), kiest doelen en speltype, en grijpt in bij vragen over
  regels (H4 + H5).
- **Technicus** — lost storingen op en begrijpt het systeem in de diepte (H2, H3, plus de
  [handleidingen](../handleidingen/) en het [hub-noodherstel-runbook](../handleidingen/hub-noodherstel.md)).

Eén persoon kan meerdere rollen dragen; de hoofdstukken zijn zo geschreven dat elke rol enkel
"zijn" hoofdstukken nodig heeft.

Speelt één iemand als **grote moderator** het spel, dan kan die twee stukken operator-werk afsplitsen
naar hulpleiding — **Techniek-wacht** en **Speler-administratie** — zodat hij zelf enkel nog events,
tempo en regels doet. Die twee rollen staan volledig uitgeschreven in **[H6](06-rollen-en-taken.md)**,
inclusief printkaarten.

## Hoe dit handboek te gebruiken

- **Eerste keer opzetten?** Lees H1 volledig, doe daarna H2 (T1–T11) één keer integraal.
- **Speeldag?** H1 §8 (ochtendchecklist) + H4 (briefing voorlezen) volstaan; H5 als naslag bij
  regelvragen; H2 §symptoomtabel bij storingen.
- **Hulpleiding inzetten?** Print de twee kaarten achteraan [H6](06-rollen-en-taken.md#5-printkaarten)
  en geef ze mee — één voor de batterijen/palen, één voor de spelersadministratie.
- **Iets stuk of onduidelijk?** H2 wijst per symptoom naar het juiste document; voor de hub is er
  het aparte runbook ["hub vervangen in 10 min"](../handleidingen/hub-noodherstel.md).
- **Veldspecifieke gegevens**: de fysieke details (paal-constructie, batterijtype/lader, AP-configuratie,
  baken-app, audio, opslag) staan gebundeld in [H1 — Bijlage A](01-veldopzet.md#bijlage-a--veldspecifieke-gegevens).
  Ingevuld voor deze opstelling; nog te finaliseren op de testdag: **AP-kanaal (6/11)**, **audiovolume** en **opslag/kisten**.
