# Handleiding: de Node-RED dashboards

De spellogica draait in **Node-RED**; je bedient en bewaakt het spel via een set
**Node-RED Dashboard 2.0**-pagina's. Dit document beschrijft per pagina **waarvoor je hem gebruikt**,
**wat elke knop doet als je hem aanraakt**, en **in welke volgorde je ze op een speeldag gebruikt**.

## Toegang

Het Pi-adres hangt af van waar je zit — **thuis `192.168.1.43`**, **veld-AP `192.168.50.1`**,
**veld-kabel `192.168.51.1`** (zie [`verbinden-met-de-hub.md`](verbinden-met-de-hub.md)):

| Wat | URL |
|---|---|
| **Dashboards** (deze pagina's) | `http://<pi-adres>:1880/dashboard` |
| **Simulator** (losse test-tool) | `http://<pi-adres>:1880/sim/` |
| **Flow-editor** (de logica zelf) | `http://<pi-adres>:1880/` |

> ⚠️ **`http://`, niet `https://`.** De browser wil er graag `https` van maken; dat werkt niet.

> Wijzig je een dashboard in `pi/node-red/flows.json`, werk dan **dit document** bij en deploy met
> `pi/node-red/deploy-flows.ps1` (Windows) of `deploy-flows.sh` (Pi). Een `docker restart` herlaadt de
> repo-flows **niet**. Wijzig je `pi/node-red/settings.js` (bv. `resetSpeler`), dan is een
> **container-herstart** nodig — `deploy-flows` volstaat dan niet.

---

## Werkstroom op een speeldag

Zo hangen de pagina's samen. Volg deze volgorde:

| # | Fase | Pagina | Wat je doet |
|---|------|--------|-------------|
| 1 | **Opbouw** | [Buzzer/LED test](#8-buzzerled-test-buzzer-tuning) | Elke paal fysiek testen: brandt de LED, klinkt de zoemer? |
| 1b | **Zoemers checken** | [Buzzer/LED test](#8-buzzerled-test-buzzer-tuning) | Groep "Zoemer op de paal": de deuntjes van de firmware per paal afspelen. **Kan niet tijdens een lopend spel.** |
| 1c | **Events kiezen** | [Events](#2b-events-events) | Bepalen welke events meedoen en hoe zeldzaam ze zijn. |
| 2 | **Bakens uitdelen** | [Beacons & Locatie](#6-beacons--locatie-beacons) | Baken ↔ speler koppelen (LED-helderheid staat nu bij [Buzzer/LED test](#8-buzzerled-test-buzzer-tuning)) |
| 3 | **GO/NO-GO** | [Spelstatus](#1-spelstatus-spelstatus) | Staan alle 31 spelers op **OK**? Alle palen groen? Geen foutcodes? |
| 4 | **Testronde** | Simulator (🧪 Test) + [Bediening](#2-bediening-bediening) | Spelers een ronde laten oefenen — telt niet mee |
| 5 | **Spelen** | [Bediening](#2-bediening-bediening) | Spel starten, events volgen, ingrijpen |
| 6 | **Ingrijpen** | [Admin](#5-admin-admin) | Kapotte paal eruit, speler pauzeren, foute score corrigeren |
| 7 | **Afsluiten** | [Leaderbord](#4-leaderbord-leaderbord) | Speltoestand-schakelaar uit → stand op de beamer |

---

## 1. Spelstatus (`/spelstatus`)

**Waarvoor:** de **GO/NO-GO-controle vóór de start**. In één blik zien of alle hardware meedoet.
Dit is de pagina waar je naar kijkt terwijl de spelers hun baken omhangen.

**Hoe je hem gebruikt:** laat hem openstaan tijdens de opbouw. Elke speler die zijn baken omhangt
en langs een paal loopt, springt van **NIET GEZIEN** naar **OK**. Wie na tien minuten nog op NIET
GEZIEN staat, draagt zijn baken niet (of het is stuk).

| Groep | Wat je ziet / doet |
|---|---|
| **Masters verbonden** | Per master (M1 = palen 1–8, M2 = 9–16, M3 = 17–24) een **groen/rood bolletje**. Rood = geen enkele paal in dat bereik stuurde de laatste 60 s data → die master of zijn USB-kabel hangt. Daarnaast de **Hub CPU**-tegel: de Pi-temperatuur (groen < 70 °C, oranje 70–80, rood ≥ 80) — een Pi die throttelt (≥ 80 °C) voelt als "alles is traag"; kist openen/ventileren. |
| **MQTT** | Groen = Node-RED praat met de broker. Rood hier betekent dat *niets* meer werkt. |
| **Spelstatus** | De samenvatting: **GO** (groen) of **NO-GO** (rood, met het aantal fouten). |
| **Spelers** | **Alle** spelers uit de roster (`global.spelersLijst`), niet enkel de geziene. Per speler: **OK** (< 15 s geleden gezien), **VERLOREN** (> 15 s stil) of **NIET GEZIEN** (nog nooit gedetecteerd). |
| **Bediening status** | **Toon batterij** = batterijkolom bij de palen. **Override NO-GO** = tóch kunnen starten ondanks fouten (bewuste ontsnappingsklep). Sinds juli 2026 **gate't de pre-flight écht**: bij NO-GO weigert de Spel-schakelaar te starten (melding in de toestandsbanner + schakelaar springt terug) tot je de fouten oplost óf Override aanzet. In simulatie-modus wordt niet gegate't. Beide schakelaars herstellen hun stand na een pagina-herlaad. |
| **Palen / Slaves** | Per paal: status, laatst gezien, batterijspanning. |
| **Foutcodes** | De actieve problemen met uitleg — zie de tabel hieronder. |

**De foutcodes:**

| Code | Ernst | Betekent |
|---|---|---|
| ST-001 | FOUT | Speler niet (meer) gedetecteerd |
| ST-002 | WAARSCHUWING | Paal > 60 s stil — hij gaat tijdelijk **uit de ring** (events kiezen hem niet meer) en komt vanzelf terug |
| ST-003 | FOUT | Geen enkel bericht op `plaatjes/data` — master of bridge ligt eruit |
| ST-004 | INFO | Onbekend baken gezien (een omstander-telefoon) — onschuldig |
| ST-005 | WAARSCHUWING | **Batterij bijna leeg** (< 3,5 V) → vervangen. Blokkeert niet; **hot-swap mag tijdens het spel** |
| ST-006 | FOUT | **Master-conflict**: twee borden zijn met hetzelfde `MASTER_NR` geflasht → herflash het verkeerde |
| ST-007 | WAARSCHUWING | **Hub te warm** (CPU ≥ 75 °C): throttle-risico vanaf 80 °C → kist openen/ventileren, Pi uit de zon |

---

## 2. Bediening (`/bediening`)

**Waarvoor:** de **centrale spelbesturing**. Hier start je, volg je het verloop, en grijp je in.
Dit is de pagina die openstaat zolang het spel loopt.

### Groep "Speltoestand" — de hoofdschakelaars

| Bediening | Wat het doet |
|---|---|
| **Spel: uit / LOOPT** | Start en stopt het spel. **Stoppen telt de scores op bij het cumulatieve klassement** en schrijft de partij in de historiek. Er is geen aparte noodstop-knop meer — dit *is* hem. |
| **Speltype** | *Plates of Fate* (het hoofdspel), *Klokslag* of *Infected* (minigames). |
| **Simulatie-modus** | Uit = **monitor** (echte hardware). Aan = de browser-simulator vervangt de hardware. Sim en echt spel draaien **nooit** tegelijk. |
| **Pauze / Hervat** | Bevriest de engine (bv. als er iemand valt). |
| **Manueel** | Volledig handmatig: jij drukt op *Volgende event* **én** op *Controle*. Voor debuggen. |
| **🕐 Met timer** | **Semi-automatisch.** Alles loopt vanzelf — 5 s-aanloop, event, doelwit, reactietijd, controle, afroep — maar het **volgende event start pas als jij op *Volgende event* drukt**. Zo bepaal jij het tempo zonder alles met de hand te doen. Staat dit samen met *Manueel* aan, dan **wint Met timer** (de reactietijd telt dus tóch af). |
| **Aanloop tonen (manueel)** | Alleen in *Manueel*: laat *Volgende event* eerst de zichtbare 5 s-aanloop aftellen i.p.v. meteen te vuren. |
| **Volgende event** | Start het volgende event. Werkt in *Manueel* én in *Met timer*. |
| **Controle (manueel)** | Voert de controle van het lopende event uit. Alleen nodig in *Manueel*. |

> **Welke modus kies je?** Voor een gewone partij: alles uit (volautomatisch). Wil je zelf het tempo
> bepalen — bv. wachten tot iedereen weer stilstaat — zet dan **Met timer** aan. *Manueel* is voor
> testen en debuggen.

### De rest van de pagina

| Groep | Wat je ziet / doet |
|---|---|
| **Plates of Fate besturing** | De live-timer (groot), het huidige event, het doelwit, en de **controle-uitslag** van het laatste event per speler. **Tijd terug** draait de laatste ronde ongedaan (10 diep) — je redmiddel na een beacon-fout of een verkeerde controle; je krijgt een pop-up-bevestiging ("Laatste ronde teruggezet" of "geen snapshot meer"). |
| **Doel (Plates of Fate)** | Kies het **spel-doel** en hoeveel spelers het moeten halen. Een PoF-spel start pas met een doel. **Auto-einde** stopt het spel zodra het doel bereikt is (de schakelaar houdt sinds juli 2026 zijn stand vast — hij had geen feedback-lus en "hapertte" daardoor terug). Deze groep verdwijnt bij Klokslag. |
| **Live Radar** | Per speler: huidige paal, RSSI, levensuren, sterftes. Je belangrijkste "wat gebeurt er nu"-tabel. |
| **Actieve events & toestanden** | **Eén gecombineerd overzicht** van alles wat nú loopt, gegroepeerd per **Soort** (`Wereld` / `Uur` / `Speler`) met kolommen **Wat · Waar · Rondes**. Bevat naast de gewone wereld-effecten ook de mechanieken die géén `wereldEffect` zijn en dus in de losse groepen ontbraken: **storm** (met richting en de uren van de baan), **bipolair beestje** (even/oneven + hoeveel humeurwissels het nog heeft), **middernacht uitgebreid**, **max/uur**, **polonaise**, **tijdreizen**, **nuke**, **tornado**, **gelijke verdeling** en **drukknop-roulette** — plus per speler ziek/tijdbom/tweeling/dienaar/wolf/gepauzeerd. Ververst 1×/s (met change-guard). Staat bovenaan Bediening, direct onder Speltoestand. |
| **Actieve effecten (bord-staat)** | Welke uren een effect dragen (portaal, happy hour, medicijn, tijdbom) + welke spelers 🤒 ziek of 💣 bebomd zijn, met resterende rondes. *(Deelverzameling van het gecombineerde overzicht hierboven; blijft staan voor wie de compacte weergave gewend is.)* |
| **Wereld-effecten** | Regelwijzigingen die op **iedereen** slaan: tijdreizen, polonaise, max-per-uur, etenstijd, identiteitscrisis. |
| **Middernacht & Dienaren** | De π-klok: staat de poort **open** of **dicht**, en hoeveel events nog. Plus wie **dienaar** van wie is na een oogst. |
| **Vals-spelen & God-punten** | Wie valsspeelde (en dus een slechtere aura heeft) en wie nog een **god-punt** als schild heeft. |
| **Infected (minigame)** | Status van de Infected-minigame. |
| **Spelbalans** | Twee schuiven die je **tijdens** het spel mag bijstellen:<br>**Doelwit-dichtheid** (10–50 %, default 25) — hoeveel spelers een gemiddeld event raakt (met lichte variatie per event, jitter ±1). Voelt het veld leeg? Zet hoger.<br>**Reactietijd toestanden** (3–30 s, default 10) — hoeveel tijd spelers krijgen bij toestand-events. |

---

## 2b. Events (`/events`)

Hier bepaal je **welke events meedoen** en **hoe vaak** ze voorkomen — dezelfde registers die de
simulator gebruikt, dus beide blijven synchroon.

**Groep "Alle events"** — tabel met elk event: naam, categorie (inclusief de virtuele categorie
**drukknop**), de **effectieve tier** en of het **meedoet**. Bovenaan staat "X van Y events doen mee".

**Groep "Event aanpassen"**:

| Knop | Wat het doet |
|---|---|
| **Event** (dropdown) | Kies het event dat je wil aanpassen. Uitgesloten events krijgen `[UIT]` vooraan. |
| **Tier** (dropdown) | `common` (zeer vaak) → `legendary` (zeldzaam). Bepaalt de trekkans binnen zijn categorie. |
| **Laat meedoen** / **Sluit uit** | Zet het gekozen event aan/uit (`uitgeslotenEvents`, retained). Handig om een kapot of ongewenst event tijdens het spel uit te zetten. |
| **Zet tier** | Schrijft een tier-override (`eventTiers`). |
| **Alles laten meedoen** | Wist alle uitsluitingen. |
| **Tiers terugzetten** | Wist alle overrides — elk event gebruikt weer zijn eigen tier. |

> Wijzigingen gelden **meteen** voor de volgende trekking. Wil je de *verhouding* tussen categorieën
> sturen (bv. meer drukknop-events), gebruik dan de groep **Event-mix (bag)** op Bediening.

---

## 3. Leaderboard (`/leaderboard`)

**Waarvoor:** de **ranglijst** voor jezelf — alle spelers cumulatief over alle gestopte partijen,
met rang, levensuren, levensdagen, sterftes, skills, valsspeelpunten en god-punten. Ververst elke 2 s.

> Gesorteerd op **totale levensuren**, niet op uren-modulo-24. Een speler met 3 levensdagen staat dus
> boven iemand met 20 losse uren.

## 4. Leaderbord (`/leaderbord`)

**Waarvoor:** **dezelfde ranglijst, maar groot** — voor een scherm of beamer naast het veld.
Podium voor de top 3 (winnaar in het midden, uitvergroot), daaronder de rest in grote regels.
Zet deze op de beamer bij de prijsuitreiking.

---

## 5. Admin (`/admin`)

**Waarvoor:** **ingrijpen als er iets misgaat.** Alles achter de knop *Admin ontgrendelen* is
**twee-staps** beveiligd: je moet eerst ontgrendelen, en na élke actie vergrendelt het paneel
zichzelf weer. Dat is met opzet — je wil hier niet per ongeluk op klikken.

### Groep "Beheer" — resets

Zet eerst **Admin ontgrendelen (stap 1)** aan; daarna werkt één knop, en dan valt het slot terug.

| Knop | Wat het wist |
|---|---|
| Levensdagen → 0 | De hele dagen; de losse uren-rest blijft |
| Levensuren → 0 | De uren-rest; de volle dagen blijven |
| Sterftes → 0 | Alle sterftes |
| Paal-effecten → 0 | Alles op het bord (portalen, happy hours, medicijnen) |
| Speler-effecten → 0 | Speler- én wereld-effecten |
| Speler-toestanden → 0 | Ziekte, dienaren, identiteitscrisis — van **iedereen** |
| Middernacht-klok → start | De π-sequentie terug naar het begin |
| **ALLES → 0** | Alles hierboven **plus** het cumulatieve klassement. De grote rode knop. |
| **Paal om te resetten** + **Reset paal → rust** | Eén paal terug naar rust: effecten weg, LED uit |
| **Speler om te resetten** + **Reset speler-toestand** | **Zie hieronder** |

**"Reset speler-toestand"** haalt **één speler** uit **alles** waar hij in vastzit: ziekte, tijdbom,
speler-effecten, dienaar-/meester-relatie, identiteitscrisis, tweelingband (**verbroken zonder dat
zijn tweeling sterft**), etenstijd, infected — en hij **vervalt als doelwit** van het lopende event.

> Zijn **score blijft staan** (levensuren, sterftes, valsspeelpunten, god-punten) — daarvoor is
> *Handmatig bijstellen*. Zijn **locatie** en zijn **pauze-stand** blijven ook staan.
> Gebruik dit als een detectiefout iemand in een toestand heeft geduwd die niet klopt.

### De andere groepen

| Groep | Waarvoor |
|---|---|
| **Handmatig bijstellen** | **Eén spelerveld corrigeren**: kies speler + veld (levensuren / sterftes / valsspeelpunten / god-punten), geef een waarde, en kies **Zet op waarde** of **+ optellen**. Nooit onder 0. Voor als een beacon-fout iemand punten heeft gekost. |
| **Speler pauze** | Een speler **volledig uit het spel** halen (geblesseerd, moet weg). Hij wordt niet meer gescoord en telt niet mee in Klokslag/Infected/tweeling/etenstijd. De pauze **overleeft een herstart**. |
| **Palen handmatig uit/in** | Een **kapotte paal** uit het spel halen terwijl de rest doorloopt. Events kiezen hem niet meer. Er blijven altijd minstens 2 palen over. |

---

## 6. Beacons & Locatie (`/beacons`)

**Waarvoor:** de **RSSI-locatiebepaling tunen** en slechte bakens opsporen.
Zie [`../locatiebepaling.md`](../locatiebepaling.md) voor het algoritme erachter.

| Groep | Waarvoor |
|---|---|
| **Spelers / bakens beheren** | **Dit heb je op de speeldag nodig.** Baken ↔ speler koppelen zonder deploy: wapper het baken bij een paal → kies de speler → **Koppel**. Baken kapot? Neem een reserve, koppel hem, klaar. Retained (`config/spelers`), dus dit overleeft alles. |
| **Scan-duur (BLE)** | Hoe lang een slave per cyclus scant (400–1000 ms), voor alle palen of één paal. Korter = versere detectie = minder scoring-vertraging. |
| **Locatie-instellingen** | De zes schuiven van het locatie-algoritme (venster, hysterese, RSSI-vloer, grace, switch-samples, min-samples). |
| **Profielen (dag/avond)** | Twee opgeslagen sets locatie-parameters. De RF-omgeving 's avonds (dauw, jassen) verschilt van 's middags — tune één keer, bewaar, en wissel met één knop. |
| **Beacon-stabiliteit** | Ranglijst van signaalstabiliteit. Onderaan staan de bakens die problemen gaan geven. |
| **Beacon-kalibratie** | Per baken een RSSI-offset (−20…+20 dB), voor een baken dat structureel zwakker zendt. |
| **Ruwe RSSI (diagnose)** | De rauwe meetwaarden per baken per paal. Alleen aanzetten als je echt aan het zoeken bent. |

---

## 7. Historiek (`/historiek`)

**Waarvoor:** **terugkijken** op gespeelde partijen en hun event-verloop.
Partijen verwijderen vraagt eerst een bevestiging.

---

## 8. Buzzer/LED test (`/buzzer-tuning`)

**Waarvoor:** **de opbouw**. Twee dingen: elke paal fysiek testen, en de buzzer-frequentie per bordje
zoeken.

| Groep | Waarvoor |
|---|---|
| **Paaltest (LED + zoemer)** | **Dit gebruik je bij het opbouwen.** Kies de paal (1–24) en druk **LED-test** (regenboog) of **Zoemer-test** (piep). **Uit** dooft hem weer. Zo loop je in tien minuten alle 24 palen af en weet je zeker dat ze allemaal leven.<br>**Kleur + Toon kleur**: zet de paal in één van **5 vaste kleuren** — Rood, Limoen, Groen, Blauw, Magenta. Hun tinten liggen **gelijkmatig over de kleurencirkel** (0°/72°/144°/216°/288°), dus een **dood kleurkanaal** of een **verkeerde R/G/B-volgorde** valt onmiddellijk op: krijg je bij "Blauw" iets roods, dan staat de volgorde fout; blijft één kleur zwart, dan is dat kanaal dood. De globale LED-helderheid schaalt hier nog overheen. |
| **LED-helderheid** | **Verhuisd vanaf Beacons & Locatie** — dit hoort bij de LED-test, niet bij de beacon-tuning. Helderheid van **alle** palen (10–255) of via **Min/Middel/Max**. Overdag hoog zetten, anders zie je de LED's niet. **Max verdubbelt ongeveer de LED-stroom** — dat kost batterij. Stuurt actie 21; herstelt zichzelf na een reboot via de heartbeat. |
| **Buzzer-tuning (paal 1)** | Een passieve piezo klinkt het luidst rond zijn eigen resonantiefrequentie, en die verschilt per bordje. De **sweep** loopt van min naar max; je hoort waar het het hardst klinkt, en met **Handmatige frequentie** zoom je in op de piek. Die waarde zet je in `BUZZER_FREQ_TABEL` in de slave-firmware. **Alle commando's gaan naar paal 1** — verwissel dus telkens het test-bordje naar paal 1. |
| **Geluid (box)** | **Volume van de aux-box** (verhuisd vanaf Admin). Schuif (0–100 %) of de knoppen **Stil (30 %)** / **Normaal (70 %)** / **MAX (100 %)**. Werkt **meteen**, ook midden in een lopend geluid, en de stand **overleeft een herstart** van de Pi (retained). `player.py` detecteert de geluidskaart nu zelf; werkt het niet, kijk in `docker logs audio-player` naar de `[VOLUME]`-regel. Buiten op een veld wil je dit gewoon op MAX. |

> De kleurtest hergebruikt **actie 16** (`ACTIE_KLOKSLAG`, `modus 0` = solid r/g/b) — dat was al een
> generiek "zet paal op kleur"-commando. Er is dus **geen nieuwe firmware-actie** voor bijgekomen en
> je hoeft **geen enkel bordje te herflashen**.

---

## 9. Drukknop-test (`/drukknop-test`)

**Waarvoor:** de **fysieke drukknoppen** testen (palen 3, 4, 7, 9, 11, 13, 15, 17, 19, 21 en 22).

**Belangrijk om te snappen:** een slave **negeert elke druk** zolang de paal niet **gearmd** is. De
schakelaars in "Knoppen activeren" doen precies dat (actie 17 = armen, 18 = uit). In het echte spel
armt de **tijdbom** zijn ontmantelpalen automatisch — hier doe je het met de hand om te testen.

| Groep | Waarvoor |
|---|---|
| **Knoppen activeren** | Per paal de knop **arm**en. De GPIO6-LED op het bordje gaat aan; hij dooft zolang je de knop indrukt — zo zie je meteen of de knop elektrisch werkt. |
| **Druk-tellers** | Hoe vaak er op elke paal gedrukt is. **Reset tellers** zet ze op 0. |

---

## 10. Zoemer op de paal (groep op [Buzzer/LED test](#8-buzzerled-test-buzzer-tuning))

> **De aparte Audio-pagina is verwijderd** (juli 2026). De fragment-preview en de
> event-aankondiging-preview bleken in de praktijk nauwelijks gebruikt; de **zoemer-test** was het
> enige waardevolle deel en staat nu als groep **"Zoemer op de paal"** op de
> **Buzzer/LED test**-pagina. Wil je een gesproken fragment controleren, doe dat dan met een
> sim-testronde (H2/T7) of luister het bestand rechtstreeks in `pi/audio-player/audio/`.

**Zoemer op de paal:** de **zoemer-deuntjes uit de firmware** afspelen op één paal. Kies het
**Zoemergeluid**, zet **Paal (1-24)** en druk **Speel op de paal**.

### Welke zoemergeluiden zijn er?

Elk deuntje hangt vast aan een **actie-id** — de slave speelt de bijbehorende melodie zelf af (de
noten staan in `firmware/Slave/src/main.cpp`, de acties in [`../protocol.md`](../protocol.md) §2).

| Geluid in de dropdown | Actie | Hoe het klinkt | Waar het in het spel voor staat |
|---|---|---|---|
| **Piep (uur-afroep)** | 3 | één korte toon van 600 ms | het afroepen van een uur-doelwit; klinkt **per paal op een andere frequentie** (elk bordje is gekalibreerd in `BUZZER_FREQ_TABEL`, daarom klinkt dit op de ene paal luider dan op de andere) |
| **Ziek — nog 3 events** | 5 | ziekenhuis-monitor (3× 2200 Hz) + **3** hartslagen | waarschuwing aan een zieke speler |
| **Ziek — nog 2 events** | 6 | idem + **2** hartslagen | idem |
| **Ziek — nog 1 event** | 7 | idem + **1** hartslag | laatste waarschuwing vóór hij sterft |
| **Knop goed (stijgend)** | 22 | twee stijgende tonen (2000 → 2600 Hz) | goede keuze bij een drukknop-event |
| **Knop fout (dalend)** | 23 | twee dalende, doffe tonen (1600 → 1000 Hz) | foute keuze |
| **Ontploffing (sirene+dreunen)** | 24 | dalende sirene 2500 → 300 Hz + drie lage dreunen (~1,6 s) | een tijdbom gaat af — bewust veel langer en lager dan 23, zodat je het van over het veld hoort |

> **22, 23 en 24 zetten óók de LED** (groene flits · rode flits · rode strobe). Dat hoort bij die
> acties en dooft **vanzelf** — je hoeft achteraf geen "uit" te sturen.

**Verschil met de andere twee zoemer-knoppen:**

- *Buzzer/LED test* → **Zoemer-test** speelt altijd dezelfde korte piep (actie 3) op de gekozen paal —
  bedoeld om bij de opbouw snel alle 24 palen af te lopen.
- *Buzzer/LED test* → **Buzzer-tuning** stuurt een **continue, instelbare** toon (actie 12) en altijd
  naar **paal 1**; daarmee zoek je de resonantiefrequentie van een bordje.
- Deze pagina speelt de **echte spel-deuntjes** op een **vrij te kiezen paal**, zodat je hoort wat de
  spelers straks horen.

---

## Zie ook

- [`../invarianten.md`](../invarianten.md) — de harde regels (EV9 engine-modi, TM1–TM3 test-modus,
  S9/S10 admin-ingrepen, M11 middernachtpaal, T1–T8 tijdbom).
- [`../locatiebepaling.md`](../locatiebepaling.md) — het algoritme achter de Beacons-schuiven.
- [`../protocol.md`](../protocol.md) — de actie-tabel (0–24) achter elke LED en zoemer.
- [`../spel/`](../spel/) — het spelontwerp en de events.
- `pi/node-red/blokken/*/README.md` — de logica per flow-blok.
