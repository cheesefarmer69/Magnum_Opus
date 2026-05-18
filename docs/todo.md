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

- [ ] **Blok: speler-score** — beheer van levensjaren per speler (middagspel),
  inclusief de reset naar 0 bij een nieuwe cyclus.
- [ ] **Blok: event engine** — afroepen en afhandelen van *plates of fate*-events.

## Spelontwerp

- Zie `docs/spel.md` → sectie "Open vragen / nog uit te werken" voor openstaande
  ontwerpbeslissingen (o.a. de rol voor uitgeschakelde spelers).
