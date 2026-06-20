# Een nieuw event toevoegen — wat geef je door?

> Praktische checklist voor Nic: **welke info lever je aan** (aan Claude of aan jezelf) om
> een nieuw Plates-of-Fate event toe te voegen, en **in welk formaat**. De volledige
> schema-referentie (alle velden + waarden) staat in [events.md](events.md); de spelregels
> en scoring in [event-systeem.md](event-systeem.md). Dit document is de korte versie.

---

## De 4 dingen die ik minimaal nodig heb

Om een event te kunnen bouwen heb ik dit nodig — gewoon in gewone taal, ik giet het in het
juiste formaat:

1. **Categorie** — wat voor soort event?
   - `speler` = spelers moeten **bewegen** (een verplaatsing-opdracht).
   - `toestand` = ken iets toe aan **spelers of uren** (portaal, happy hour, bonus, straf, …).
   - `wereld` = verandert iets voor **het hele spel** (bv. events gaan sneller).
2. **Wat de spelers horen** — de afroep-`tekst` (bv. "Maximum x uur vooruit."). Een losse
   `x` vul ik automatisch met een gerold getal.
3. **Wie/wat het raakt** — het **doelwit**: spelers, uren of een **groep**? Hoeveel
   (`enkel`/`laag`/`midden`/`hoog` of een vast getal)? Willekeurig of allemaal?
   - Een **groep** (`type: "groep"`, `veld: "kleur"`/`"jaar"`) richt het event op alle spelers met
     één gedeelde eigenschap (bv. "een groep — kleur: rood") — handig om niet steeds dezelfde paar
     spelers aan te roepen. Zie [events.md](events.md) (Groep-doelwit) en [spelers.md](spelers.md).
4. **Wat er gebeurt** — het **gevolg**:
   - niets behalve een beweging-controle (`voorwaarde: min`/`max` + `getal`),
   - een blijvend **effect** (portaal / happy hour / mag-niet-bewegen / events-sneller),
   - een directe **score** (±levensuren),
   - een speler-**toestand** met eigen lifecycle (`ziekte` / `tijdbom`),
   - of een los **commando** (LED/zoemer).
   - Is het een speler-toestand die niet samen met een andere mag voorkomen (clutter)? Zet
     `exclusiefGroep: "speler-toestand"` — spelers in zo'n toestand worden dan niet gekozen (tenzij
     de Systeeminstelling "toestand-exclusiviteit" uit staat). Ziekte en tijdbom delen die groep.

Niet zeker over een veld? Laat het weg of zeg "kies maar" — ik vul een zinvolle default in
volgens [events.md](events.md) en leg de keuze voor.

## Handigste formaat om aan te leveren

Een paar regels gewone taal is genoeg, bv.:

> "Maak een **toestand**-event 'Bevriezing': kies **1 willekeurige speler**, die mag de
> volgende **2 rondes niet bewegen**. Reactietijd 15 s."

Of, als je het al precies weet, meteen als event-object (zie de voorbeelden onderaan
[events.md](events.md)):

```js
{ id:"bevriezing", naam:"Bevriezing", categorie:"toestand",
  tekst:"Een speler bevriest en mag niet bewegen.", reactietijd_s:15,
  doelwit:{ type:"speler", selectie:"willekeurig", aantal:"enkel" },
  gevolgen:[ { type:"effect", niveau:"speler", effect:"mag_niet_bewegen", duurRondes:2, data:{} } ] }
```

## Wat ik er daarna mee doe (zodat jij het kan volgen)

1. Ik giet je input in het **standaardformaat** ([events.md](events.md) → "Het standaardformaat").
2. Ik denk de **randgevallen** door (scoring, sterftes, portaal, `max`) en leg keuzes voor.
3. Ik voeg het object **chirurgisch** toe aan de juiste inject in `pi/node-red/flows.json`:
   `[CONFIG] Speler-events`, `[CONFIG] Toestand-events` of `[CONFIG] Wereld-events`
   (nooit het hele bestand herschrijven — zie [Design_rules.md](../../Design_rules.md) §1).
4. Ik werk de docs bij ([events.md](events.md) / [event-catalogus.md](event-catalogus.md)).
5. **Jij deployt**: `pi/node-red/deploy-flows.ps1` (Windows). ⚠️ Een `docker restart` herlaadt
   `flows.json` **niet** — zonder deploy verandert er niets in de draaiende Node-RED.

## Audio (optioneel maar leuk)

Wil je het event laten **voorlezen**, dan zijn er WAV-segmenten nodig:
- `events/<id>_voor.wav` en `events/<id>_na.wav` (rond het getal), via `audioVoor`/`audioNa`.
- De **aantal-prefix** ("3 spelers …") gebruikt bestaande `getallen/<n>.wav` en
  `woorden/<speler|spelers|uur|uren>.wav`. Zie `pi/audio-player/audio/README.md`.
Ontbrekende WAV's worden gewoon overgeslagen — het event werkt ook zonder audio.

## Mini-checklist

- [ ] Categorie gekozen (`speler` / `toestand` / `wereld`).
- [ ] `tekst` (afroep) geschreven; `x` gebruikt als er een getal in moet.
- [ ] Doelwit bepaald (`type` + `selectie` + `aantal`).
- [ ] Gevolg(en) gekozen (`geen` + `voorwaarde` / `effect` / `score` / `commando`).
- [ ] `reactietijd_s` gezet; bij toestand evt. `max` en `duratie`.
- [ ] Toegevoegd aan de juiste `[CONFIG]`-inject + **`deploy-flows.ps1`** gedraaid.

> Volledige veldreferentie, opties (`enkel`/`laag`/`midden`/`hoog`), scoringtabel en meer
> voorbeelden: [events.md](events.md). Werkregels: [Design_rules.md](../../Design_rules.md) §2 en §10.
