# H6 — Rollen & taakkaarten voor de hulpleiding

Tijdens een echte speeldag leidt **één persoon** het spel: de **grote moderator**. Die kiest de
events, houdt het tempo vast, beslist bij twijfel en praat met de spelers. Dat lukt alleen als hij
of zij níét tegelijk batterijen loopt te wisselen of iemand uit een vastgelopen toestand moet
klikken.

Dit hoofdstuk maakt twee taken los die je veilig kan doorgeven aan **hulpleiding**:

| Rol | Kern in één zin | Heeft nodig |
|---|---|---|
| **Techniek-wacht** | Houdt de hardware in leven: batterijen, stille palen, warme hub. | Gsm/tablet met het dashboard + reserve-batterijen |
| **Speler-administratie** | Houdt de spelerslijst kloppend: pauzes, vastgelopen spelers, correcties. | Gsm/tablet met het dashboard |

> Deze twee rollen komen bovenop de bestaande rolindeling (bouwploeg · operator · spelleider ·
> technicus) uit [`README.md`](README.md). Ze zijn zo geknipt dat ze **geen spelbeslissingen**
> bevatten — de grote moderator blijft de enige die events, doelen en start/stop aanraakt.

**Verbinden met het dashboard** doe je zoals in
[`verbinden-met-de-hub.md`](../handleidingen/verbinden-met-de-hub.md): op het veld je gsm op WiFi
**`MagnumOpus`** (wachtwoord `scoutskamp`), dan `http://192.168.50.1:1880/dashboard`.
⚠️ **`http://`, niet `https://`.** De melding "geen internet" op je gsm is normaal en juist.

---

## 1. De drie gouden regels (voor beide rollen)

1. **Je raakt de knop `Spel: uit / LOOPT` nooit aan.** Stoppen telt de scores op bij het cumulatieve
   klassement en schrijft de partij weg — dat is onomkeerbaar en dat doet alleen de grote moderator.
2. **Bij twijfel: melden, niet klikken.** Elke rol heeft hieronder een lijstje "escaleren bij". Een
   fout die je meldt kost tien seconden; een verkeerde klik kan een partij kosten.
3. **Zeg wat je doet.** "Paal 14 krijgt een nieuwe batterij" of "Lotta staat op pauze" — de moderator
   moet weten waarom het veld verandert.

---

## 2. Rol: Techniek-wacht

**Wanneer:** vanaf de opbouw tot de afbouw, doorlopend.
**Doel:** de moderator hoeft nooit naar de hardware te kijken.

### 2.1 Jouw scherm

Zet je gsm/tablet op **dashboard → Spelstatus** en laat die pagina openstaan. Zet éénmalig de switch
**Toon batterij** aan (groep *Bediening status*) — dan staat de celspanning in de palen-tabel.

Wat er op die pagina staat, van boven naar beneden:

| Onderdeel | Wat je ziet | Wat het betekent |
|---|---|---|
| **Masters verbonden** | M1 · M2 · M3 met een groen/rood bolletje | M1 = palen 1–8, M2 = 9–16, M3 = 17–24. **Rood** = geen enkele paal in dat bereik stuurde 60 s lang data → die master of zijn USB-kabel hangt |
| **Hub CPU** | temperatuur van de Pi | zie §2.4 |
| **MQTT** | groen/rood | rood = *niets* werkt meer → onmiddellijk melden |
| **Spelstatus** | GO / NO-GO + aantal fouten | NO-GO blokkeert de start (vóór het spel) |
| **Spelers** | OK / VERLOREN / NIET GEZIEN | zie §3.6 (speler-administratie) |
| **Palen / Slaves** | status, laatst gezien, batterijspanning | jouw belangrijkste tabel |
| **Foutcodes** | ST-001 … ST-007 | zie §2.5 |

### 2.2 Batterijen — jouw hoofdtaak

De palen draaien op één cel. Deze drempels gelden:

| Spanning | Betekenis | Wat jij doet |
|---|---|---|
| **≥ ~3,6 V** | gezond | niets |
| **< 3,5 V** | dashboard-waarschuwing **ST-005** "vervang batterij" | **wissel de cel** — je hebt nog ~20–30 % marge, dus geen haast, maar laat het niet liggen |
| **< 3,2 V** | firmware meldt *kritiek* | te laat begonnen; wissel nu |
| **< ~3,0 V** | cel is leeg en gaat kapot van verder ontladen | wissel nu, leg die cel apart |

**Vuistregel: wissel zodra het dashboard < 3,5 V toont.**

### 2.3 Een batterij wisselen tijdens het spel (hot-swap)

**Dit mag gewoon terwijl het spel loopt.** Het spel hoeft niet gepauzeerd te worden.

1. Trek de bijna lege cel eruit, zet een volle in.
2. De paal **herstart** in enkele seconden en meldt zich vanzelf weer aan. Node-RED zet zijn
   instellingen automatisch terug op de eerstvolgende hartslag.
3. Klaar. **Je hoeft verder niets te doen** — een onderbreking korter dan **60 s** geeft geen fout en
   stopt het spel niet.

> Spelers die vlak bij die paal staan houden een paar tellen hun laatste positie vast. Wissel dus bij
> voorkeur niet exact op het moment dat de controle valt — wacht die tien seconden even af.

Leg de lege cellen apart in een aparte doos, zodat niemand ze per ongeluk terugzet.

### 2.4 Hub te warm (ST-007)

Bij **CPU ≥ 75 °C** verschijnt ST-007. Vanaf 80 °C gaat de Pi vertragen en voelt *alles* traag aan.

Wat jij doet: **kist openzetten, ventileren, de Pi uit de zon.** Meer niet — dit is geen storing die
je hoeft te melden zolang de temperatuur daarna weer zakt. Blijft hij stijgen, meld het dan wel.

### 2.5 Foutcodes: wat is van jou, wat escaleer je?

| Code | Betekent | Van wie |
|---|---|---|
| **ST-005** | batterij < 3,5 V | **jij** — wisselen (§2.3) |
| **ST-002** | paal > 60 s stil → hij gaat **tijdelijk uit de ring** en komt **vanzelf** terug | **jij** — zie hieronder |
| **ST-007** | hub ≥ 75 °C | **jij** — ventileren (§2.4) |
| **ST-004** | onbekend baken gezien (telefoon van een omstander) | niemand — onschuldig, negeren |
| **ST-001** | speler niet meer gedetecteerd | speler-administratie (§3.6) |
| **ST-003** | geen enkel bericht meer binnen — master of bridge ligt eruit | ⚠️ **grote moderator / technicus** |
| **ST-006** | twee borden met hetzelfde masternummer geflasht | ⚠️ **grote moderator / technicus** |

**Bij ST-002 (stille paal):** het spel lost dit zelf op — events kiezen die paal even niet meer en
zodra hij weer data stuurt, doet hij weer mee. Loop er wel naartoe en kijk wat er aan de hand is:
meestal is het de batterij (§2.3), soms is de paal omgevallen of staat hij te ver. Komt de paal na
een verse batterij **niet** terug, meld het dan.

### 2.6 Rondje langs de palen

Loop in rustige momenten een rondje en let op:
- staat elke paal rechtop en op zijn plek?
- brandt de LED zoals verwacht (niet dood, niet permanent wit)?
- ligt er niets tegenaan dat het signaal blokkeert?

### 2.7 Wat je NIET doet

- ❌ De schakelaar **Spel: uit / LOOPT** aanraken.
- ❌ Instellingen wijzigen op *Beacons & Locatie* (scan-duur, locatie-sliders, profielen) — die zijn
  afgestemd; ze midden in een partij verzetten verandert het spelgevoel.
- ❌ **LED-helderheid** op **Max** zetten "omdat het mooier is": dat verdubbelt ongeveer het
  LED-verbruik en kost je dus batterijen. Overdag hoog mag, 's avonds op **Middel**.
- ❌ Een paal uit het spel halen (*Admin → Palen handmatig uit/in*) — dat vraag je aan de moderator.

---

## 3. Rol: Speler-administratie

**Wanneer:** doorlopend tijdens het spel.
**Doel:** de moderator hoeft nooit de spelerslijst te repareren.

### 3.1 Eerst dit: twee dingen die "pauze" heten

Dit is dé valkuil van deze rol. Ze staan op verschillende pagina's en doen iets totaal anders:

| Knop | Waar | Wat het doet |
|---|---|---|
| **Speler pauze** | *Admin* → groep **Speler pauze** | Haalt **één speler** uit het spel. De rest speelt gewoon door. ✅ **Dit is jouw knop.** |
| **Pauze / Hervat** | *Bediening* → **Speltoestand** | Bevriest **het hele spel** voor iedereen. ❌ Alleen de moderator. |

### 3.2 Een speler pauzeren of hervatten

Ga naar *Admin* → groep **Speler pauze** en klik de speler op pauze. Deze knop zit **niet** achter
het slot — je kan hem meteen gebruiken.

Gebruik dit wanneer iemand:
- naar het toilet moet of even weg is;
- geblesseerd is of moet uitrusten;
- zijn baken kwijt is en je het nog aan het oplossen bent.

Een gepauzeerde speler wordt **niet gescoord**, telt niet mee als doelwit en doet niet mee in
Klokslag, Infected, tweeling of etenstijd. De pauze **overleeft zelfs een herstart** van het systeem.

> Vergeet niet te **hervatten** als iemand terugkomt. Een speler die per ongeluk op pauze blijft
> staan, scoort de rest van de partij niets — en dat merk je pas op het eindklassement.

### 3.3 Het slot op de Admin-pagina

De rest van deze rol zit achter een bewuste beveiliging:

1. Zet **Admin ontgrendelen (stap 1)** aan.
2. Doe **één** handeling.
3. Het slot **valt vanzelf weer dicht**.

Voor elke volgende handeling ontgrendel je dus opnieuw. Dat is met opzet zo — op deze pagina staan
ook knoppen die een hele partij kunnen wissen.

### 3.4 Een vastgelopen speler resetten

Soms duwt een detectiefout iemand in een toestand die niet klopt: hij is "ziek" terwijl hij nooit op
een ziekte-paal stond, of hij hangt vast als tweeling.

*Admin* → ontgrendelen → dropdown **Speler om te resetten** → **Reset EEN speler**.

| Dit wist het | Dit blijft staan |
|---|---|
| ziekte, tijdbom, speler-effecten | zijn **score** (levensuren, sterftes, punten) |
| dienaar-/meester-relatie, identiteitscrisis | zijn **locatie** |
| tweelingband (**zonder** dat zijn tweeling sterft) | zijn **pauze-stand** |
| etenstijd, infected, en hij vervalt als doelwit van het lopende event | |

Gebruik dit dus voor **toestanden**, niet om punten te corrigeren — daarvoor is §3.5.

### 3.5 Een score corrigeren

Alleen wanneer het systeem iemand aantoonbaar onterecht heeft gestraft of beloond (bijna altijd door
een baken- of detectiefout).

*Admin* → ontgrendelen → groep **Handmatig bijstellen** → kies de speler, kies het veld
(**levensuren** / **sterftes** / **valsspeelpunten** / **god-punten**), geef een waarde en kies
**Zet op waarde** of **+ optellen**. Het gaat nooit onder 0.

> **Overleg dit altijd even met de moderator.** Punten bijstellen is een spelbeslissing, geen
> administratie — jij voert ze alleen netjes uit.

### 3.6 Een speler die "verdwijnt"

Staat iemand op **VERLOREN** of **NIET GEZIEN** in de spelerstabel (*Spelstatus* → groep *Spelers*),
loop dan dit lijstje af:

1. **Draagt hij zijn baken?** Verreweg de meest voorkomende oorzaak.
2. **Draagt hij het goed?** Niet in een binnenzak tegen het lichaam — dat dempt het signaal. Hangend
   of bovenop werkt het best.
3. **Staat hij ergens raar?** Precies tussen twee palen of in de binnencirkel (die hoort niet bij het
   speelveld) geeft geen nette positie.
4. **Baken kapot of leeg?** Pak een reserve: *Beacons & Locatie* → groep **Spelers / bakens
   beheren** → wapper het nieuwe baken vlak bij een paal → het verschijnt als **"Nieuw baken: …"** →
   kies de speler → **Koppel**. Dat vervangt zijn oude baken automatisch en overleeft een herstart.

Zet hem op **pauze** (§3.2) zolang je dit aan het uitzoeken bent, zodat hij intussen niet bestraft
wordt voor "niet bewegen".

### 3.7 Tijd terug

*Bediening* → **Tijd terug (laatste ronde ongedaan)** draait de laatste controle terug (tot 10
rondes diep) en vraagt eerst een bevestiging.

Dit is het redmiddel na een beacon-fout die een hele ronde verpestte. **Vraag het altijd eerst aan de
moderator** — het raakt iedereen, niet één speler.

### 3.8 Wat je NIET doet

- ❌ De schakelaar **Spel: uit / LOOPT** aanraken.
- ❌ **Pauze / Hervat** op *Bediening* (dat bevriest het hele spel — zie §3.1).
- ❌ Eender welke **"→ 0"-knop** op de Admin-pagina, en zeker niet **ALLES → 0**: die wist het
  cumulatieve klassement van alle partijen.
- ❌ Partijen verwijderen op de *Historiek*-pagina.

---

## 4. Escaleren naar de grote moderator

Meld het meteen bij:

- **ST-003** of **ST-006**, of een **rode MQTT-tegel** — er is iets structureels stuk.
- Een **master-bolletje dat rood blijft** nadat je de palen in dat bereik hebt nagekeken.
- Een paal die na een **verse batterij** niet terugkomt.
- Een **hub die warm blijft** ondanks ventileren, of "alles voelt traag".
- Elke twijfel over **punten, straffen of regels** — dat is altijd een moderator-beslissing.

Voor de moderator/technicus: het runbook
["hub vervangen in 10 min"](../handleidingen/hub-noodherstel.md) staat klaar in de kist.

---

## 5. Printkaarten

Print deze twee kaarten af (of zet ze als foto op de gsm van je hulpleiding). Elke kaart staat op
zichzelf — je hoeft het hoofdstuk er niet bij te hebben.

---

### ✂️ KAART 1 — TECHNIEK-WACHT

**Jouw scherm:** `http://192.168.50.1:1880/dashboard` → **Spelstatus** (laten openstaan).
Zet **Toon batterij** aan. WiFi: **MagnumOpus** / `scoutskamp` · altijd **http://**

**BATTERIJ — je hoofdtaak**

| < 3,5 V | wisselen (waarschuwing **ST-005**) |
|---|---|
| < 3,2 V | te laat, nu wisselen |

**Wisselen mag terwijl het spel loopt:** cel eruit, volle erin, paal herstart in seconden, meldt zich
vanzelf. **Verder niets doen.** Lege cellen apart leggen.

**VERDER BEWAKEN**
- **ST-002** paal > 60 s stil → gaat vanzelf uit de ring en komt vanzelf terug. Ga kijken: meestal
  batterij. Komt hij na een verse cel niet terug → **melden**.
- **ST-007** hub ≥ 75 °C → **kist open, ventileren, uit de zon**.
- **ST-004** onbekend baken → negeren.
- Rondje lopen: palen rechtop, LED leeft, niets ertegenaan.

**MELDEN AAN DE MODERATOR BIJ**
ST-003 · ST-006 · rode MQTT-tegel · master-bolletje blijft rood · paal blijft dood na verse batterij ·
hub blijft warm

**NOOIT** — `Spel: uit / LOOPT` aanraken · scan-/locatie-instellingen wijzigen · LED op Max zetten ·
een paal uit het spel halen

---

### ✂️ KAART 2 — SPELER-ADMINISTRATIE

**Jouw scherm:** `http://192.168.50.1:1880/dashboard` → **Admin**
WiFi: **MagnumOpus** / `scoutskamp` · altijd **http://**

⚠️ **TWEE SOORTEN PAUZE**
- **Speler pauze** (*Admin*) = één speler eruit → **dit is van jou**
- **Pauze / Hervat** (*Bediening*) = hele spel bevriezen → **nooit aanraken**

**SPELER PAUZEREN** — *Admin* → **Speler pauze**. Geen slot nodig.
Bij wc, blessure, of terwijl je een baken-probleem oplost. Hij wordt dan niet gescoord.
👉 **Vergeet niet te hervatten als hij terug is.**

**HET SLOT** — *Admin* → **Admin ontgrendelen (stap 1)** → **één** handeling → slot valt vanzelf dicht.

**VASTGELOPEN SPELER** — ontgrendelen → **Speler om te resetten** → **Reset EEN speler**
Wist toestanden (ziekte, tijdbom, tweeling, dienaar, infected …). **Score, locatie en pauze blijven.**

**SPELER VERDWENEN?** (VERLOREN / NIET GEZIEN)
1. draagt hij zijn baken? 2. hangend dragen, niet in een binnenzak 3. staat hij tussen twee palen of
in de binnencirkel? 4. baken kapot → *Beacons & Locatie* → wapper reserve bij een paal → kies speler →
**Koppel**. Zet hem intussen op **pauze**.

**EERST VRAGEN AAN DE MODERATOR**
punten bijstellen (*Handmatig bijstellen*) · **Tijd terug** · alles wat je niet zeker weet

**NOOIT** — `Spel: uit / LOOPT` · `Pauze / Hervat` · eender welke **"→ 0"-knop** · **ALLES → 0** ·
partijen verwijderen in *Historiek*
