# H1 — Veldopzet: het spel opzetten in het veld

Stap-voor-stap van een lege wei naar een speelklaar veld met **GO** op het dashboard. Volg de
secties in volgorde; elke sectie bouwt op de vorige. Reken voor een geoefende ploeg van 2–3
personen op **± 2 uur** totaal (veld uitzetten is het meeste werk).

> Fysieke details die niet in de repo staan zijn gemarkeerd als **[IN TE VULLEN]** en gebundeld in
> [Bijlage A](#bijlage-a--in-te-vullen-vragenlijst) onderaan.

---

## 1. Materiaallijst

**Speelveld**
- 24 **palen** met elk een slave-bord (ESP32-C3), LED-strip (7 LEDs), zoemer, batterij en op de
  drukknop-palen een fysieke knop. Constructie/montage per paal: **[IN TE VULLEN — Bijlage A.1]**.
- **Touw**: ± **206 m** totaal — buitenring 72,1 m, binnenring 50,2 m, 24 spaken van 3,5 m
  (zie [`playfield.md`](../hardware/playfield.md)).
- **Piketten/haringen** voor de touwringen + een **hamer**.
- **Meetlint of uitzet-touw ≥ 11,5 m** en een **afstandslat/touwtje van 3,0 m** (paalafstand).
- 24+ **geladen batterijen** (1S Li-ion/LiPo) + reserves; type & lader: **[IN TE VULLEN — A.2]**.

**Hub-kist** (zie ook het [hub-noodherstel-runbook](../handleidingen/hub-noodherstel.md))
- Raspberry Pi 4 (met de speel-SD) + **gekloonde reserve-SD**.
- **3 masters** (ESP32 WROOM) + 3 USB-kabels.
- **Audio**: versterker/speaker met aux-kabel naar de Pi-jack.
- **WiFi-accesspoint** (voor dashboard/simulator) — configuratie: **[IN TE VULLEN — A.3]**.
- **Powerbank met pass-through** (voeding Pi) + laders/verlengkabel naargelang de locatie.

**Spelers**
- **Bakens** voor alle spelers (~31) + **minstens 2 reservebakens**, opgeladen. Merk/app en
  instellingen (adv-interval 300 ms, tx-power): **[IN TE VULLEN — A.4]**.
- Polsbandjes in de **kleuren** van de spelerslijst (rood/zwart/blauw — zie
  [`spelers.md`](../spel/spelers.md)).

**Papier**
- Dit handboek (of een tablet), de **LED-spiekkaart** en **do's & don'ts** uit
  [H4](04-spelersuitleg.md) geprint voor bij de spelleider.

> **Veiligheid.** De touwringen en spaken zijn op speelhoogte een struikelrisico — span ze strak
> en laag, en wijs spelers er in de briefing op. Gebruik enkel onbeschadigde batterijen; vervang
> gezwollen cellen meteen.

---

## 2. Het veld uitzetten (geometrie)

Het veld is een **regelmatige 24-hoek** (bijna een cirkel) met een niet-bespeelbaar binnenveld —
alle exacte maten staan in [`playfield.md`](../hardware/playfield.md).

| Maat | Waarde |
|---|---|
| Straal palenring (buiten) | **11,50 m** |
| Straal binnenring | **8,00 m** |
| Afstand tussen buurpalen | **3,00 m** (koorde 3,002 m) |
| Aantal palen | 24, op het **midden** van elke buitenzijde (niet op de hoekpunten) |
| Spaken | 24 × 3,50 m, radiaal tussen binnen- en buitenring |

**Werkwijze:**

1. **Kies het middelpunt** en sla daar een piket. Controleer dat er ≥ 12 m vrije ruimte rondom is
   (totale diameter 23 m).
2. **Bepaal de oriëntatie**: kies waar **paal 1** komt (bv. richting de hub of het noorden) en of
   de nummering met of tegen de klok loopt. Leg dit vast — de nummering moet elke speeldag
   dezelfde zijn, want de spellogica ("uur 9 en 11", de middernachtpoort op paal 24) hangt eraan.
3. **Zet de palenring uit**: span een touw van 11,5 m vanaf het centrum. Zet paal 1, en gebruik
   de 3,0 m-lat tussen elke volgende paal terwijl het 11,5 m-touw strak blijft. Na 24 palen moet
   je exact bij paal 1 uitkomen — sluit de ring niet, corrigeer dan gelijkmatig.
4. **Span de buitenring** (touw langs alle 24 palen) en de **binnenring** op 8 m uit het centrum.
5. **Leg de 24 spaken** tussen binnen- en buitenring, één per segment. Let op: de palen staan op
   het **midden** van een zijde — een spaak komt dus telkens **tussen** twee palen uit, nooit op
   een paal.
6. **Nummer de palen zichtbaar** (bordje/tape 1–24). Paal **24 = de middernachtpoort** (krijgt
   de wit/rode poort-LED); paal **1** ligt er direct naast in de looprichting.

> Het binnenveld is **geen** speelgebied: spelers bewegen op de ring tussen binnen- en buitenring.
> De hub met de muziekinstallatie staat in het centrum ([`spel.md`](../spel/spel.md)).

---

## 3. Palen activeren

Elke paal is identiek geflasht (**één firmware voor alle 24 borden**) en herkent zichzelf aan zijn
hardware-MAC via de tabel [`firmware/shared/paal_macs.h`](../../firmware/shared/paal_macs.h).

1. **Batterij erin** — de paal boot (~enkele seconden).
2. Wat je hoort te zien:
   - de **ingebouwde LED knippert** kort bij elke zending (~1×/s): de paal scant en zendt;
   - de LED-strip blijft **uit** (rust) tot Node-RED iets stuurt.
3. **Rode LED knippert ritmisch** (aan/uit ~0,12 s)? Dan is dit bord **onbekend** — zijn MAC staat
   nog niet in `paal_macs.h`. Het bord doet bewust **niet mee** (claimt nooit een verkeerd
   paalnummer). Oplossing: MAC uitlezen en toevoegen — zie het
   [MAC-werkblad](../../firmware/tools/mac-tabel.md) en
   [`slave.md`](../handleidingen/slave.md) ("Nieuwe slave in gebruik nemen").
4. Zet elke paal bij zijn nummer. **Welk bord bij welke paal staat maakt niet uit** zolang de
   MAC-tabel klopt — het paalnummer zit aan het bórd (via zijn MAC), niet aan de plek. Wissel je
   borden van plek, wissel dan bewust: bord "paal 7" **is** paal 7, waar het ook staat.

> **Batterij wisselen kan altijd**, ook midden in het spel: de paal reboot, meldt zich vanzelf
> terug en het spel loopt door (zie [`hardware-info.md`](../hardware/hardware-info.md), "Hot-swap").

---

## 4. De hub opstellen

Volgorde maakt weinig uit, behalve: **eerst alles aansluiten, dan de Pi voeden.**

1. Plaats de hub-kist **in het centrum** van het veld (of aan de rand met zicht op het veld).
2. Sluit de **3 masters** aan op USB-poorten van de Pi. **Welke poort maakt niet uit** — de
   serial-bridge herkent elke master automatisch en leert de routes uit hun aankondiging
   ([`serial-bridge.md`](../handleidingen/serial-bridge.md)).
3. Sluit de **audio** aan op de Pi-jack en zet de versterker aan (volume halverwege; bijstellen
   na de audio-test in H2/T7).
4. Zet het **WiFi-accesspoint** aan. **Belangrijk: het AP moet op kanaal 6 of 11 staan** — nooit
   kanaal 1, want daar zit ESP-NOW en dan stoort het dashboardverkeer de veld-communicatie (zie
   [`hardware-info.md`](../hardware/hardware-info.md), H6). Doe bij twijfel een kanaalscan met een
   telefoon-app en kies het rustigste van 6/11.
5. **Voed de Pi via de powerbank (pass-through)** die zelf aan een lader/stopcontact hangt — zo
   overleeft de hub een stroomdip. Wacht ~2 minuten tot alle containers draaien.
6. **Controleer** vanaf een laptop/telefoon op het AP-netwerk:
   - dashboard bereikbaar: `http://192.168.1.43:1880/dashboard` (pagina's Spelstatus, Bediening, …);
   - masters gevonden: de masters' ingebouwde LED pulst zodra palen zenden.
   Komt er niets op, volg dan het [hub-noodherstel-runbook](../handleidingen/hub-noodherstel.md) §Runbook.

---

## 5. Bakens uitdelen & koppelen

Elke speler draagt een baken; de koppeling **baken ↔ spelernaam** beheer je **live op het
dashboard** — geen laptop of deploy nodig
([`locatiebepaling.md`](../locatiebepaling.md), "Bakens toewijzen en vervangen"):

1. Open dashboard → **Beacons & Locatie** → groep **"Spelers / bakens beheren"**. De tabel toont de
   huidige koppelingen.
2. Voor een **nieuw of reserve-baken**: wapper het even vlak bij een willekeurige paal → het
   verschijnt als **"Nieuw baken: …"** → kies de speler in de dropdown → **Koppel**. Koppelen aan
   een speler die al een baken had **vervangt** dat automatisch.
3. Deel de bakens uit samen met het **polsbandje in de juiste kleur** (de kleur bepaalt de
   groep-events — controleer tegen [`spelers.md`](../spel/spelers.md)).
4. De koppelingen zijn **retained** opgeslagen (`config/spelers`) en overleven een herstart.

> Draagadvies voor spelers: baken **niet** in een binnenzak tegen het lichaam (dempt het signaal);
> hangend/bovenop is het stabielst.

---

## 6. Configuratie-check (dashboard)

Eén keer controleren vóór de eerste partij (daarna blijft dit staan):

| Wat | Waar | Moet zijn |
|---|---|---|
| **Paaltjeslijst** | Node-RED editor, tab 00 → `[CONFIG] Paaltjeslijst` | alle paal-nummers die vandaag echt staan (volledige veld = 1–24) |
| **Spelerslijst** | dashboard "Spelers / bakens beheren" (§5) | elke aanwezige speler heeft een baken |
| **Speler-eigenschappen** | tab 00 → `[CONFIG] Speler-eigenschappen` | kleur/jaar per speler klopt met de polsbandjes |
| **Drukknop-palen** | tab 00 → `[CONFIG] Drukknop-palen` | exact de palen waar fysiek een knop op zit |
| **Doelwit-dichtheid** | Bediening → "Spelbalans" | 25 % is de standaard; hoger bij een kleine testgroep |
| **PoF-doel + aantal** | Bediening → "Doel (Plates of Fate)" | verplicht vóór een PoF-start (anders weigert Start) |

---

## 7. Pre-flight naar GO

Open dashboard → **Spelstatus**. De pagina evalueert continu en toont **GO** of **NO-GO** met
foutcodes ([blokken/02](../../pi/node-red/blokken/02_spelstatus/README.md)):

1. **Palen**: alle verwachte palen op **OK** (heartbeat < 60 s). "GEEN CONTACT" → batterij/afstand
   checken.
2. **Spelers**: alle bakens **OK** (gezien < 15 s). "NIET GEZIEN" → baken aan? gekoppeld (§5)?
3. **Batterijen**: zet "Toon batterij" aan — geen paal onder **3,5 V** (anders foutcode
   **ST-005**: vervang die batterij nu, vóór de start).
4. **Foutcodes leeg** (of enkel INFO). ST-006 = twee masters met dezelfde rol geflasht — verkeerd
   bord aangesloten.
5. Statustekst = **"GO - spelstatus OK, klaar om te starten"** → je kan starten. (Override bestaat
   voor noodgevallen, maar los liever de fout op.)

**Starten:** Bediening → kies **speltype** (Plates of Fate / Klokslag / Infected), bij PoF een
**doel + aantal spelers**, en zet de **Spel-schakelaar** aan. Brief de spelers met
[H4](04-spelersuitleg.md).

---

## 8. Speeldag-ochtendchecklist (afvinkbaar)

- [ ] Veld + touwen intact; palen recht en op nummer.
- [ ] 24 palen: batterij vers erin; ingebouwde LED knippert; geen rode fout-blink.
- [ ] Hub: masters (3×) + audio + AP aangesloten; Pi gevoed via powerbank; containers up.
- [ ] **AP-kanaal 6/11 bevestigd** + korte kanaalscan van de omgeving.
- [ ] Dashboard bereikbaar; **Spelstatus = GO**; geen ST-005 batterijwaarschuwingen.
- [ ] Audio-test: één afroep hoorbaar over het veld (H2/T7); volume goed.
- [ ] LED-test op 2–3 palen (H2/T2) — kleuren zichtbaar in daglicht.
- [ ] Bakens: allemaal geladen, gekoppeld, juiste kleur polsbandje; 2 reserves in de kist.
- [ ] Reserve-SD + reservebatterijen + dit handboek in de kist.
- [ ] Geheugen-check Pi (`free -h`) — ruim vrij; weinig open dashboard-tabs.
- [ ] Spelleider heeft H4 (briefing) + spiekkaarten bij de hand.

---

## 9. Afbouw / einde van de dag

1. **Stop het spel** (Bediening → Spel-schakelaar uit). De globale stats en historiek blijven
   bewaard (persistente context).
2. Heb je tijdens de dag **live in de Node-RED-editor** iets aangepast? Exporteer de flows terug
   naar de repo vóór je afsluit ([`DEPLOY.md`](../../pi/node-red/DEPLOY.md), "één bron van waarheid").
3. **Pi netjes afsluiten**: `sudo shutdown now` (SSH) of stroom pas wegnemen als de activiteits-LED
   stil is — dat beschermt de SD-kaart.
4. **Palen**: batterijen eruit en aan de lader; borden droog opbergen.
5. **Bakens** verzamelen (tel ze!) en aan de lader.
6. Is er iets aan de hub-configuratie veranderd? **Herkloon de reserve-SD** zodat je plan-B
   actueel blijft ([hub-noodherstel](../handleidingen/hub-noodherstel.md)).

---

## Bijlage A — [IN TE VULLEN] vragenlijst

Vul deze zes punten één keer in (vervang de betreffende `[IN TE VULLEN]`-verwijzingen hierboven),
dan is dit hoofdstuk compleet:

| # | Vraag | Antwoord |
|---|-------|----------|
| A.1 | **Paal-constructie**: hoe wordt een paal fysiek opgebouwd/verankerd (grondpen? statief?), en hoe zit het slave-bord + LED-strip + batterij erin gemonteerd? | … |
| A.2 | **Batterij**: welk celtype/formaat (bv. 18650), welke lader, en hoeveel reserves neem je mee? | … |
| A.3 | **Accesspoint**: welk apparaat is het AP (de Pi zelf met hostapd? een losse router?), wat is de SSID/wachtwoord-afspraak, en waar stel je het **kanaal** in? | … |
| A.4 | **Bakens**: merk/model + app, en de ingestelde parameters (adv-interval 300 ms, tx-power) — hoe wijzig je die als het nodig is? | … |
| A.5 | **Audio**: welk versterker/speaker-model, en de vaste volume-stand die over het hele veld verstaanbaar is? | … |
| A.6 | **Vervoer/opslag**: hoe worden palen en hub verpakt (kisten-indeling), en is er een vaste plek voor de reserve-SD en reservebakens? | … |
