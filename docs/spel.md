# Spel oriëntering + mechanieken

## Overzicht

Het spel bestaat uit twee grote delen, gespeeld op dezelfde dag:

- **Middagspel** — ± 3,5 uur
- **Avondspel** — ± 1,5 uur

Beide delen hebben een eigen doel, dynamiek en regelset, maar zijn met elkaar
verbonden: wat je in de middag opbouwt, neem je mee naar de avond.

## De wereld: de klok

Het speelveld is een 24-hoekig "donut"-diagram. Het bord is verdeeld in
24 delen; elk deel stelt één uur voor. De volledige klok is dus één dag.

De dag kent drie fases:

| Fase        | Uren      | Opmerking          |
|-------------|-----------|--------------------|
| Dag         | 7u – 19u  |                    |
| Nacht       | 19u – 6u  |                    |
| Middernacht | 00u       | scharniermoment    |

Het is een *plates of fate*-spel: willekeurige events bepalen het verloop.

### Hardware per uur

Elk uur (elk van de 24 delen) is uitgerust met:

- een **LED-lamp** — verschillende kleuren en animaties
- een **zoemer** — verschillende tonen

Daarnaast:

- een **muziekinstallatie** in het centrum van het speelveld roept de events af
- **LED-schermen** tonen tekstboodschappen aan de spelers

### Invloed van de spelers

Op sommige uren staat een **drukknop**. Spelers kunnen die gebruiken wanneer
een event dat toelaat — zo grijpen ze zelf in op het spel.

## Middagspel

**Doel: zoveel mogelijk levensjaren verzamelen.**

Spelers verzamelen levensuren door zich over de klok te verplaatsen: elk uur
dat je verder reist in de tijd, levert een levensuur op (24 levensuren = 1 levensdag).

Het is een *plates of fate*-spel: spelers moeten overleven tussen de events door
en ondertussen zoveel mogelijk levensuren verzamelen.

### Puntensysteem (scoren op controle)

Levensuren worden **niet live** toegekend, maar **pas bij de controle** van een event,
op basis van waar elke speler stond toen het event viel en waar hij eindigt. Tijdens
elk event mag enkel het **doelwit** van een verplaatsing-event bewegen; andere spelers
moeten stil blijven staan.

Per speler, bij de controle:

- **Legaal vooruit** (doelwit dat aan de min/max-voorwaarde voldoet): `+netto` levensuren.
  Eindigt hij op een **happy-hour**-uur, dan tellen die levensuren **dubbel**.
- **Illegale of onvolledige verplaatsing trekt levensuren AF** (geen achterstand meer):
  - niet-doelwit dat tóch beweegt: `−|verplaatsing|` (5→8 = −3; 5→4 = −1).
  - doelwit dat **te weinig** vooruit ging (< min): `−(het aantal dat hij bewoog)`.
  - doelwit dat **te veel** vooruit ging (> max): `−(het teveel)` (netto − max).
  - achteruit (terug in de tijd) terwijl dat niet mag: `−|verplaatsing|`.
- Een portaal-sprong telt als 0 stappen (zie hieronder) en geeft dus geen straf.

### Sterftes

Zou een speler door een straf **onder 0 levensuren** zakken, dan blijven zijn levensuren
op **0** staan en krijgt hij **+1 sterfte** (hij is als het ware gestorven), maar hij
speelt gewoon verder. Legale vooruitgang kan nooit een sterfte veroorzaken. Het aantal
sterftes is een **globale stat** die blijft bestaan over partijen heen.

### Globale stats

**Levensdagen, levensuren, sterftes** (en later skills) zijn **globale stats**: ze blijven
bewaard wanneer een spel gestopt wordt. **Stop spel** reset enkel de partij (events-teller,
toestanden, posities), niet de globale stats. Met **[BEHEER] Wis globale stats** zet je ze
handmatig terug op nul.

### Ritme: opbouw, voorwaarde, beloning, minigame

Het middagspel is bewust géén ononderbroken *plates of fate* — er zijn
rustmomenten nodig. Daarom verloopt het in cycli:

1. **Voorwaarde** — er is telkens een voorwaarde die door één of meerdere
   spelers volbracht moet worden. Die is meestal niet eenvoudig en kost tijd.
2. **Beloning** — de speler die erin slaagt, krijgt een beloning: een
   **voorwerp** dat voordeel geeft, of een **skill** die enkel díe speler kan
   gebruiken. Dit zorgt voor exclusiviteit en een reden om competitief te spelen.
3. **Minigame** — daarna wordt een unieke, meestal rustigere minigame gespeeld
   die de eigenschappen van het speelveld benut. Dit is een echte pauze.
4. **Herstart** — het *plates of fate*-spel begint opnieuw, met een nieuw doel
   en een schone lei: de levensjaren-teller wordt gereset naar 0.

Na elke skill-bemachtiging verandert het *plates of fate*-spel: er worden
**nieuwe events toegevoegd**. Zo blijft het spel fris, zonder dat spelers meteen
met alle events tegelijk overspoeld worden — er blijft tijd om het spel te leren
kennen.

### Flow state

De kracht van deze opzet: ze laat toe om een *flow state* te bereiken. Zodra
iedereen het spel doorheeft, kunnen de events automatisch gespeeld worden.

### Toestanden op het veld: het portaal

Sommige events plaatsen een tijdelijke **toestand** op het veld. Een voorbeeld is het
**portaal**: tussen twee uren opent een doorgang, herkenbaar aan een **continu paarse
LED** op beide palen.

Spelregels voor het portaal:

- Een speler die zich volgens de spelregels verplaatst en op een portaal-uur **landt**,
  mag — als hij wil — meteen naar het **andere** portaal-uur springen. Het is niet
  verplicht.
- De sprong door het portaal telt **niet als stap** en levert **0 levensuren** op. De
  stappen *vóór* het portaal (tot aan het ene uur) en *erna* (vanaf het andere uur)
  tellen wel gewoon mee. Heeft hij na de sprong nog stappen over, dan mag hij die nog
  gebruiken.
- *Voorbeeld*: speler op uur 10 mag 5 stappen zetten, portaal tussen 12 en 20. Hij loopt
  10→12 (2 levensuren), springt 12→20 (0), en loopt 20→23 (3 levensuren): samen 5
  levensuren over 5 stappen.
- Een speler die **niet terug in de tijd** mag, mag het portaal ook niet gebruiken om
  achteruit in de tijd te gaan.

De afdwinging van deze regels (de scoring) zit in flow 04 ("Bereken levensuren"); de
duur, het paar en de `max` van het portaal staan in `docs/event-catalogus.md`.

### Toestanden op het veld: happy hour

Een **happy hour** maakt een uur tijdelijk extra waardevol, herkenbaar aan een **continu
gouden LED**. Eindigt een speler een verplaatsing **op** een happy-hour-uur, dan tellen
de levensuren die hij met díe verplaatsing verdient **dubbel**. Voorbeeld: een speler die
3 uur vooruit gaat en op het happy-hour-uur landt, krijgt 6 levensuren in plaats van 3.
Er kunnen meerdere happy-hour-uren tegelijk zijn (`max: 4`).

### Simulatie vs. echt spel

Het echte spel (bediening) en de simulatie delen één engine; je draait er altijd maar
één tegelijk. **Stop spel** (op zowel de Bediening- als de Simulatie-pagina) zet alles
terug op nul — teller, toestanden én levensuren — zodat een simulatie het echte spel
nooit vervuilt. Elke nieuwe partij begint dus met een schone lei.

## Avondspel

**Doel: de dood voorblijven.**

Het avondspel heeft een andere dynamiek. In de avond komen de LED-lampen pas
echt tot hun recht; daar wordt gebruik van gemaakt met actievere spellen.

De spelers gaan de avond in met de levensjaren die ze in de middag opgespaard
hebben. Het spel dat volgt is **"de dood"**: die probeert hen in te halen.
Zijn al hun levensjaren op, dan zijn ze door de dood ingehaald.

## Open vragen / nog uit te werken

- Functie/rol voor spelers die door de dood ingehaald zijn (de "gesneuvelden")
- Concrete voorwaarden, beloningen en skills in het middagspel
- Concrete invulling van de minigames
- Lijst van events en hun timing/afroeping
- Regels en scenario van het avondspel
