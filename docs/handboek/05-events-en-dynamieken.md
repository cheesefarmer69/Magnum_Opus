# H5 — Elk event & elke speldynamiek

Naslagwerk voor spelleider en operator: **alle 15 events** als vaste kaart, daarna de **permanente
mechanismen** van Plates of Fate, en tot slot de dynamieken van **Klokslag** en **Infected**.

Elke event-kaart: **Afroep** (wat spelers horen) · **Wie** · **Wat doen** · **Bij fout** · **Duur &
kans** · **LED/geluid** · **Operator**. De genoemde waarden zijn de huidige configuratie
(`[CONFIG]`-injects in `flows.json`); tier/duur zijn per event bij te sturen. Normatief voor de
scoring: [`invarianten.md`](../invarianten.md); schema: [`events.md`](../spel/events.md); detail:
[`event-catalogus.md`](../spel/event-catalogus.md).

> **Kansen (tiers):** common 50 · uncommon 25 · rare 15 · epic 8 · legendary 2 (relatieve
> gewichten). **Reactietijden** worden nog geschaald door het spel-tempo (0,6–1,3×) en geklemd op
> de sensing-vloer (~7 s); **aantallen** met een optie (`laag/midden/hoog`) schalen mee met het
> aantal actieve spelers (doelwit-dichtheid — zie §Dynamieken).

---

## Verplaatsing-events (2)

### 1. Groep-verplaatsing — `groep_verplaatsing`
- **Afroep:** *"een groep … maximum x uur vooruit … kleur: rood"* (of *jaar: eerste/…*).
- **Wie:** **alle** actieve spelers met die kleur of dat jaar (de engine kiest per afvuring de
  dimensie én de waarde willekeurig).
- **Wat doen:** 0 tot **x** stappen met de klok mee (x uit `midden`, 1–6). Portaal-sprong mag en
  telt 0. Niet-leden: stilstaan.
- **Bij fout (proportioneel, nooit negatief — V11):** te ver → minder winst `max(0, x − (voor − x))`;
  achteruit → `max(0, voor − achter)`; niet-lid dat beweegt → **0**. Valsspelen kost geen levensuren
  en geeft geen sterfte.
- **Duur & kans:** direct afgehandeld; **common** (vaakst voorkomende event).
- **LED/geluid:** geen paal-LED; audio `maximum.wav` + getal + `uur_vooruit.wav`.
- **Operator:** reactietijd **20 s**; scoring bij de controle (+stappen, ×2 op happy-hour-eindpaal).

### 2. Groep-of-verplaatsing — `groep_of_verplaatsing`
- **Afroep:** *"een groep … x of y uur vooruit … jaar: tweede"*.
- **Wie:** een hele kleur- of jaar-groep (zoals hierboven).
- **Wat doen:** **exact x óf exact y** stappen vooruit (x uit 1–3, y uit 4–6). Stilstaan is hier
  **fout** (0 ∉ {x,y}) — kiezen dus.
- **Bij fout:** verkeerd aantal → minder winst naar gelang je afwijkt `max(0, voor − afstand tot dichtste geldige)`; verder als bij kaart 1 (proportioneel, nooit negatief).
- **Duur & kans:** direct; **common**.
- **LED/geluid:** audio: getallen + "of" + `uur_vooruit.wav`.
- **Operator:** reactietijd **20 s**; let op de middernacht-uitzondering: wie door de dichte poort
  wordt tegengehouden op exact de poort-afstand wordt **niet** bestraft (gate-block).

## Toestand-events (7)

### 3. Portalen — `portalen`
- **Afroep:** *"Een portaal opent tussen twee uren."* (geen aantal-prefix); daarna de twee uren.
- **Wie/wat:** 2 willekeurige uren worden gekoppeld; **iedereen staat stil** tijdens dit event.
- **Gebruik daarna:** wie tijdens een beurt legaal op een portaal-uur **landt**, mag gratis naar
  de partner springen (0 stappen, richting-vrij, max 1× per portaal per beurt).
- **Bij fout:** bewegen tijdens de afroep = straf; ping-pong door hetzelfde portaal = beurt ongeldig.
- **Duur & kans:** **3–8 events** actief; max **1** portaal tegelijk; **uncommon**.
- **LED/geluid:** beide palen **continu paars**; afloop-audio *"portaal gesloten"*.
- **Operator:** reactietijd 5 s; het paar staat op `pof/portalen` (simulator tekent de lijn).

### 4. Happy Hour — `happy_hour`
- **Afroep:** *"n uren worden Happy Hour."* + de uren.
- **Wie/wat:** n willekeurige uren (schaalt met het spelersaantal); iedereen stilstaan tijdens de afroep.
- **Gebruik daarna:** eindigt een speler zijn beurt op zo'n uur, dan tellen de **verdiende uren
  van die beurt dubbel**.
- **Duur & kans:** **3–6 events**; max **1** episode tegelijk; **uncommon**.
- **LED/geluid:** **goud continu**; afloop-audio *"happy hour voorbij"*.
- **Operator:** reactietijd 5 s.

### 5. Ziekte — `ziekte`
- **Afroep:** *"n spelers worden ziek."* + de namen; evenveel vrije uren worden medicijn.
- **Wie:** 1+ spelers (schaalt; weging: 's avonds en bij valsspelers vaker — slechte aura).
- **Wat doen (zieke):** binnen **10 events** genezen: zorg dat je bij een controle **legaal** op
  een **roze medicijn-paal** staat (typisch: tussen events ernaartoe wandelen en tijdens het event
  stilstaan). Zolang je ziek bent verdien je **niets**; verliezen kan wél.
- **Bij fout / niet genezen:** na 10 events **dood** = alles kwijt + 1 sterfte. Eén medicijn
  geneest één keer (daarna dooft de paal).
- **Duur & kans:** episode tot iedereen genezen/dood is; max 1 episode; **rare**.
- **LED/geluid:** medicijn-palen **felroze**; vanaf nog 3 events een **hartslag-piep** (3/2/1) op
  het uur van de zieke.
- **Operator:** niet combineerbaar met tijdbom op dezelfde speler (toestand-exclusiviteit,
  uitschakelbaar in Systeeminstellingen); een **nuke** wist de hele episode.

### 6. Tijdbom — `tijdbom_speler`
- **Afroep:** *"n spelers worden een tijdbom."* + de namen; evenveel **knop-palen** gaan rood tikken.
- **Wie:** 1+ spelers (schaalt; slechte-aura-weging).
- **Wat doen (bom):** ga binnen **10 events** naar een **rood tikkende drukknop-paal** en druk de
  knop: **dag** (uur 7–18) 80 % slaagkans, **nacht** (19–6) 50 %. Gelukt = bom weg.
- **Bij fout:** mislukte poging óf aflopen van de klok = **ontploffing**: iedereen op die paal
  verliest evenveel levensuren als het **uurnummer** (uur 11 → −11). Blijf dus weg bij een bom-speler.
- **Duur & kans:** 10 events; max 1 episode; **rare**.
- **LED/geluid:** ontmantel-palen knipperen **rood, tikkend**; de kleine rode knop-LED dooft
  zolang je drukt (druk geregistreerd).
- **Operator:** de knop werkt in **elke** fase; ontmantel-palen komen uit `[CONFIG] Drukknop-palen`.

### 7. Tornado — `tornado`
- **Afroep:** *"n uren worden getroffen door een tornado."* + de uren.
- **Wie:** iedereen op de **twee buur-uren** van elk tornado-center.
- **Wat doen:** binnen de reactietijd (**20 s**) naar het **center** lopen → status GEVOLGD
  (geen winst/verlies).
- **Bij fout:** niet gevolgd = **WEGGEZOGEN**: **alle levensuren kwijt** (geen sterfte). Wie niet
  op een buur-uur stond, blijft buiten schot.
- **Duur & kans:** één-shot (direct afgehandeld); 1–2 centers, onderling ≥ 3 uren uit elkaar;
  max 1; **epic**.
- **LED/geluid:** center **donkergrijs**, buur-uren **trage grijze pulse**; LED's herstellen
  vanzelf na de controle.

### 8. Etenstijd — `etenstijd`
- **Afroep:** *"Een wolf zal jagen op zijn schaapjes."* + het groep-label (bv. *kleur: rood*).
- **Wie:** de afgeroepen groep = **schapen**; de **wolf** = de speler met de **beste aura** buiten
  die groep (zichtbaar op het dashboard — de spelleider kiest of hij de wolf omroept of geheim laat).
- **Wat doen (schaap):** eindig **15 events** lang nooit een controle op **hetzelfde uur** als de
  wolf. **(wolf):** jaag — eindig samen met een schaap.
- **Bij vangst:** de wolf **steelt** min(uurnummer, wat het schaap heeft) levensuren; het schaap
  krijgt **+1 sterfte**. Elk schaap is maar **één keer** vangbaar.
- **Duur & kans:** 15 events; max 1; **epic**.
- **LED/geluid:** geen eigen paal-LED; afloop-audio *"etenstijd voorbij"*.
- **Operator:** reactietijd 15 s; wolf + vangsten staan in de controle-tabel en wereld-effecten.

### 9. Tweeling — `tweeling`
- **Afroep:** *"2 spelers worden een tweeling."* + de twee namen.
- **Wie:** 2 spelers (die nog geen tweeling zijn); max **3 paren** tegelijk.
- **Wat doen:** vanaf nu bij **elke controle samen**: allebei bewogen of allebei stilgestaan
  (niet per se even ver). Wordt maar één van jullie afgeroepen? Dan moet die **stil blijven**.
- **Bij fout:** asymmetrisch bewogen = **beiden alle levensuren kwijt** (geen sterfte). Sterft één
  tweeling (welke oorzaak ook) → **de ander sterft mee** (0 uren + 1 sterfte) en de band breekt.
- **Duur & kans:** tot spel-einde of een dood; **epic**.
- **LED/geluid:** geen paal-LED; afroep-audio *"tweeling"*.

## Wereld-events (6)

### 10. Nuke — `nuke`
- **Afroep:** *"Nuke."* — daarna telt de klok af.
- **Wie:** **iedereen**.
- **Wat doen:** **ren het veld af** binnen de aftelklok (**16 s**): zorg dat de palen je baken
  niet meer zien (≥ 4 s buiten bereik). Tijdens een nuke bestaat er géén bewegingsstraf.
- **Bij fout:** nog gedetecteerd bij de knal = **ONTPLOFT**: alles kwijt + 1 sterfte. Ontkomen =
  VEILIG.
- **Duur & kans:** daarna **60 s regroup** (terugkomen, herpositioneren); max 1; **legendary**
  (zeldzaamste event).
- **LED/geluid:** het hele veld pulseert **groen↔geel** (poort-paal behoudt zijn kleur); daarna
  alles netjes terug.
- **Operator:** een nuke **wist de wereld**: lopende ziekte- én tijdbom-episodes en alle dienaars
  verdwijnen. `escape_s` (4 s) en `regroup_s` zijn event-velden.

### 11. Sneller — `sneller_events`
- **Afroep:** *"events komen sneller."*
- **Effect:** het **spel-tempo** stapt −0,1 (ondergrens 0,6×): elke volgende reactietijd wordt
  korter. **Rare**; geen doelwit, geen LED. Reset naar 1,0 bij Stop.

### 12. Trager — `trager_events`
- **Afroep:** *"events komen trager."*
- **Effect:** tempo +0,1 (bovengrens 1,3×) — meer ademruimte. **Rare**.

### 13. Bomaanslag — `bomaanslag`
- **Afroep:** *"Een bomaanslag vindt plaats op uur 9 en 11."*
- **Wie:** iedereen die bij de knal op uur **9 of 11** staat.
- **Wat doen:** **3 s** om weg te vluchten van die twee uren (vluchten is strafvrij, zoals nuke).
- **Bij fout:** −9 resp. −11 levensuren (onder nul → 0 + sterfte).
- **Duur & kans:** één-shot; **rare**; altijd dezelfde vaste uren.
- **LED/geluid:** waarschuwing: rode tik-LED + zoemer op 9 en 11; knal: witte flikker.

### 14. Identiteitscrisis — `identiteitscrisis`
- **Afroep:** *"Alle spelers krijgen een identiteitscrisis."*
- **Effect:** alle **luisternamen schuiven één plaats door** (alfabetisch, cyclisch): hoor je
  vanaf nu "Lilou", dan is de speler ná Lilou in het alfabet bedoeld. Geldt voor álle afroepen.
- **Wat doen:** onthoud wiens naam jij draagt en reageer dáárop; reageer je op je échte naam, dan
  beweeg je als niet-doelwit → straf.
- **Duur & kans:** **7–15 events**, dan keert alles terug (met afloop-audio); max 1; **epic**.
- **Operator:** reactietijd 15 s; de mapping staat in `global.luisterNaam`.

### 15. Tijdreizen — `tijdreizen`
- **Afroep:** *"Tijdreizen zal worden toegestaan."*
- **Effect:** zolang het duurt mag **iedereen ook achteruit** in de tijd lopen: achteruit-stappen
  tellen als geldige stappen voor de opdracht (geen "terug in tijd"-straf). Je moet nog steeds
  afgeroepen zijn en het aantal moet kloppen.
- **Uitzondering:** de **middernachtpoort achterwaarts** oversteken (1→24) blijft verboden, en een
  **dichte** poort blijft dicht.
- **Duur & kans:** **10–15 events**; max 1; **rare**; afloop-audio *"tijdreizen voorbij"*.

---

## Permanente mechanismen & dynamieken (Plates of Fate)

**Middernacht — de poort van π.** Geen event maar een vast mechanisme op de hoogste paal.
Open (zacht wit) / dicht (rood) volgens de cijfers van π (3 open, 1 dicht, 4 open, …; loopt door
over partijen heen). Dicht = de 24→1-oversteek is verboden, óók tussen events; overtreding =
**alles kwijt + 1 sterfte** (één keer per ronde). Een TELEPORT naar uur 1 via een portaal mag wél.
**Gate-block:** wie door de dichte poort exact wordt tegengehouden, krijgt geen "te weinig"-straf.
Een **0** in π = **oogst**: iedereen óp middernacht sterft en wordt **dienaar** van de armste
vrije speler (max 1 dienaar per meester) — een dienaar verdient alles voortaan voor zijn meester;
verlies en sterftes blijven van hemzelf. Het paneel/dashboard toont de π-stand; het mechanisme is
uitschakelbaar. *(Invarianten M1–M8.)*

**Vrije beweging tussen events (V10).** Buiten een event (aanloop/wacht/regroup) beweegt iedereen
vrij en onbespied — bewust ontwerp voor positioneren (bv. een zieke richting medicijn). De
doelwit-regel geldt pas **vanaf de reveal** tot en met de controle.

**Doelwit-dichtheid (Spelbalans-knob).** Aantallen met een optie schalen met het aantal actieve
spelers: `laag` ≈ 15 %, `midden` ≈ 25 %, `hoog` ≈ 45 % van het veld (× de dashboard-knob, default
25 %; clamp 1–10). Zo blijft elk event even "druk" bij 6 of 31 spelers. Groep-events wegen bij
> 15 spelers bovendien zwaarder. *(EV6.)*

**Tiers & wachtrij.** Events worden gewogen gekozen op zeldzaamheid; de engine plant 5 events
vooruit (paneel "Volgende events"). De operator kan daar een event **wegklikken** of via de
event-kaarten een event **vooraan** zetten, en per event de tier live overschrijven.

**Slechte aura & valsspeel-aura.** Ziekte en tijdbom kiezen hun slachtoffer gewogen: 's avonds
(uur 20–6) ×1,10, op middernacht ×1,15 — overdag is veiliger. Elke overtreding geeft bovendien
**+1 valsspeelpunt** en **+3 % aura**, wat die kans verder verhoogt; word je zelf getroffen, dan
is je aura "afbetaald" (reset). *(SP4/SP5.)*

**God-punten.** Wie het partij-doel haalt krijgt eenmalig **+2** (persistent over partijen). Bij
een overtreding wordt automatisch 1 punt verbruikt: de zet is dan **ongestraft** en telt niet als
valsspelen — daarmee mag je zelfs een dichte poort door of ziek-zijnd "fout" naar een medicijn.
*(D7.)*

**Doelen per partij.** De spelleider kiest vóór de start: **"Verplaats X uur"** (som van je legale
voorwaartse stappen deze partij) of **"Inhalen"** (passeer je alfabetische rivaal lopend). Plus
het aantal spelers dat moet slagen en optioneel **auto-einde**. Voortgang live op het dashboard en
in de simulator. Enkel **legale** stappen tellen voor het doel. *(D1–D3.)*

**Spel-tempo & sensing-vloer.** Sneller/Trager stappen een globale factor (0,6–1,3×) op alle
reactietijden; een ondergrens (~7 s bij standaard-tuning) garandeert dat geen enkele stapeling de
reactietijd onder wat de sensoren fysiek aankunnen duwt. *(SP1–SP3, SP6.)*

**Settle-grace.** Na elke reactietijd wacht de engine kort (default 3 s) zodat de laatste
paalwissels "landen" vóór de controle — trage detectie wordt zo nooit een straf. *(V9.)*

**Pauze & robuustheid.** Gepauzeerde spelers tellen nergens in mee (ook niet in Klokslag/
Infected). Een baken dat > 90 s zwijgt verdwijnt als "ghost" van het veld; een paal die > 60 s
zwijgt gaat tijdelijk uit de ring en komt vanzelf terug. *(EV3, F4.)*

**Stats, historiek & tijd-terug.** Levensuren/sterftes tellen **per partij** en schuiven bij Stop
naar het **globale** klassement (valsspeelpunten idem; god-punten blijven staan). De laatste 30
partijen staan in de Historiek. In sim-modus kan de operator één ronde **terugdraaien** (↶) bij
een verkeerd gelopen event. *(S4–S6, D5–D6.)*

---

## Klokslag — dynamieken

Teamspel op dezelfde klok ([`klokslag.md`](../spel/klokslag.md)):

| Mechaniek | Regel |
|---|---|
| Innemen | sta met je team bij een paal: de teller `P` loopt op tot het **uurnummer H** (uur 20 = 20 s) |
| Meerderheid | snelheid = 1,0 + 0,1 per extra speler voorsprong (max 1,3×); gelijkstand = teller **vervalt** (1/s) |
| Overnemen | een paal van de tegenstander kost **2H**: eerst hun `P` afbouwen, dan zelf opbouwen |
| Verlaten | een **ingenomen** paal blijft van de eigenaar (vergrendeld) tot actieve overname |
| Score | 1 punt per bezeten uur (config: som van uurnummers); **tiebreak** = som uurnummers |
| Tijd | speeltijd instelbaar (default **10 min**); eindsignaal + mijlpaal-geluiden |
| LED | rust = ademend wit · innemen = **kaarsflikker** in teamkleur (helderheid ∝ voortgang) · bezet = vol · gelijkstand = bevroren |

Teams (2 of 3, naam + kleur + leden) staan in `[CONFIG] Teams` (flow 07). Strategie-spanning:
veel goedkope uren pakken vs. enkele dure "sticky" uren verdedigen.

## Infected — dynamieken

Besmettingsspel ([`infected.md`](../spel/infected.md)):

| Mechaniek | Regel |
|---|---|
| Patiënt 0 | 1 willekeurige speler bij de start |
| Besmetting | **5 s basis + 1 s per 2 besmetten** onafgebroken op een paal met een besmette → ook besmet; weglopen reset jouw teller |
| Rood | paal met een besmette speler kleurt **rood** |
| Bestrijders | vanaf **5 besmet**: 2 bestrijders, **60 s** immuun; hun paal is **blauw** = niemand raakt daar besmet; elke minuut rotatie |
| Winnen | de **laatste 3** gezonde spelers winnen; daarna staat het spel op "klaar" tot Stop |

Operator: speltype kiezen op Bediening (`spel/type`), gewone Start/Stop; status + kleuren staan op
het dashboard en in de simulator.
