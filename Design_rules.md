# Design rules — Magnum Opus

Vaste **ontwerp- en werkregels** voor de events, de simulatie en Node-RED. Dit zijn de
dingen die Nic expliciet heeft gevraagd om bij stil te staan; lees dit vóór je iets
toevoegt of wijzigt. Aanvullend op `CLAUDE.md` (algemene projectcontext) en de `docs/`.

> Werktaal: Nederlands voor commentaar/uitleg, Engels voor code-namen.

---

## 0. Gouden regels (lees eerst)

1. **Echt spel én simulatie tegelijk.** Vraag Nic om een event toe te voegen/aan te passen,
   of welke spellogica dan ook → het geldt **altijd voor zowel het echte hardware-spel als
   de simulatie**. Ze moeten **identiek werken**, maar worden **gescheiden gehouden** voor
   duidelijkheid bij het testen (aparte panelen/weergaven, één gedeelde engine).
2. **Ontbrekende info zelf invullen of vragen.** Krijg je een event/feature half
   gespecificeerd, vul de rest zinvol in volgens dit document, of stel gerichte vragen.
3. **Analyseer randgevallen.** Bij regels die uitkomsten bepalen (scoring, sterftes,
   portaal, max): denk situaties uit die Nic niet expliciet noemde en leg de gekozen
   uitkomst voor / documenteer ze.
4. **Docs meebijwerken.** Elke code-/structuurwijziging gaat samen met de bijhorende docs
   (`docs/…`, blok-README's, sim-README, dit bestand indien een regel wijzigt).

---

## 1. `flows.json` bewerken (Node-RED)

- **Chirurgisch bewerken.** Nooit het hele bestand herschrijven (geen `ConvertTo-Json`).
  Voor `func`-bodies: een Perl-script dat de func per node-id ophaalt en via `JSON::PP`
  ge-encode terugschrijft (rest van het bestand byte-identiek). Voor node-JSON: gerichte
  `Edit`s of brace-bewuste tekst-splicing.
- **Line endings: CRLF.** `flows.json` is overwegend CRLF; ingevoegde blokken ook met
  `\r\n` schrijven zodat het consistent blijft.
- **Deployen = `pi/node-red/deploy-flows.ps1`** (Windows) / `deploy-flows.sh` (Pi), via de
  Admin API. **`docker restart` herlaadt de repo-`flows.json` NIET.**
- **Config-injects herladen `global.pofEvents` pas bij deploy** (`once`-inject). Een event
  dat je uit `flows.json` haalt, blijft in de draaiende Node-RED tot je deployt.
- Functienodes blijven **niet-blokkerend**; geen `delay()` in firmware, alles op `millis()`.

---

## 2. Events — formaat & conventies

- **Categorie** is `speler` | `toestand` | `wereld`. (De oude waarde `uur` heet nu
  `toestand` — je kiest immers een *toestand*.) `doelwit.type` mag wél `uur` zijn.
- **Standaardformaat** bevat enkel: `id, naam, categorie, tekst, reactietijd_s,
  doelwit {type, selectie, aantal}, getal, voorwaarde, max, duratie, gevolgen`.
  **`selectie` is enkel `willekeurig` of `alle`** — de oude `rang`-selectie (met
  `veld`/`richting`) is verwijderd; die velden bestaan niet meer.
- **`duratie`** = hoelang de toestand van een event blijft (in aantal events/rondes), op
  **event-niveau**. Mag een **vast getal** (`3`), een **bereik** `[min,max]` (willekeurig
  gerold, bv. `[2,5]`), of een **preset**-string (`"kort"` 2-4 / `"middel"` 4-7 / `"lang"`
  7-12). Overschrijft de oude per-gevolg `duurRondes`.
- **Documentatiestijl** (in `docs/events.md`): bij elk veld in het standaardformaat kort
  vermelden **wat het is/doet en wat je kan invullen**. Langere uitleg in **aparte
  secties**, niet als lange inline-commentaar in het codevoorbeeld.
- **`max`** = hoeveel instanties van hetzelfde toestand-event **tegelijk** actief mogen zijn
  (om het veld niet te overrompelen). De engine telt actieve instanties **per event-id**
  (distinct `instId` met `bron === id`, over alle effect-registers) en slaat het event over
  zolang `max` bereikt is.
- **LED-toestanden zijn effect-gedreven.** Een toestand-event heeft **geen `commando`-gevolg
  nodig** voor zijn LED: de centrale node "Sync toestanden + LEDs" leidt de LED-kleur af uit
  het actieve uur-effect en **zet de LED weer uit** zodra het effect afloopt of het spel
  stopt (anders blijven "stale" LED's branden — dat lijkt op een dubbel event).
- **Elk effect** krijgt automatisch `bron` (event-id) en `instId` (één per afvuring). Eén
  event dat meerdere palen/spelers raakt telt als **één** instantie. Uur-effect op precies
  2 palen krijgt `data.partner` (koppelt bv. de twee portaal-uren).

---

## 3. Afroep (audio) van een event

- **Aantal vooraan.** Vóór de event-tekst wordt eerst het **aantal getroffen doelwitten**
  afgeroepen + het zelfstandig naamwoord (enkel/meervoud):
  - spelers → "1 speler …" / "3 spelers …"
  - uren → "1 uur …" / "2 uren …"
  - Daarna de event-tekst, en ten slotte de doelwitten **één voor één** (zoals voorheen).
- Audiosegmenten: `getallen/<n>.wav` + `woorden/<speler|spelers|uur|uren>.wav` vooraan, dan
  `events/<id>_voor.wav` → `getallen/<getal>.wav` → `events/<id>_na.wav`. Ontbrekende WAV's
  worden overgeslagen.

---

## 4. Puntensysteem (scoring) — geldt voor echt spel én sim

> **Volledige spec: `docs/event-systeem.md` (leidend).** Hieronder de kernregels.

- **Pad, geen netto.** Een verplaatsing is een **geordende reeks atomaire acties**:
  **STAP** (1 paal vooruit, klok rond, nooit achteruit → 1 levensuur) en **TELEPORT** (sprong
  tussen twee actieve portaal-palen → 0 stappen, 0 levensuren, richting-agnostisch, max 1×/portaal).
  **Nooit** richting/score afleiden uit netto begin/eind — beoordeel **actie per actie** (het pad
  wordt opgenomen uit de settled paalwissels in `pofPad`). Zo geeft een legale portaal-sprong naar
  een lager uur géén "TERUG IN TIJD".
- **Scoren gebeurt PAS bij de controle** (niet live); pas dan zichtbaar in de dashboards.
- **Beweging is altijd gecontroleerd.** Bij elk event mag enkel het **beweging-doelwit**
  (`voorwaarde` min/max) bewegen; anderen blijven stil, anders straf.
- **Levensuren-Δ per speler** (`voor` = aantal STAP vooruit, `x` = budget):
  | Geval | Δ |
  |-------|---|
  | doelwit `max`, `voor ≤ x` (geldig) | **+voor** (×2 op happy hour) |
  | doelwit `max`, `voor > x` (TE VEEL) | **−(voor − x)** |
  | doelwit `min`, `voor < x` (TE WEINIG) | **−voor** |
  | doelwit, achterwaartse STAP (TERUG IN TIJD) | **−achter** |
  | niet-doelwit dat beweegt (BEWOOG mocht niet) | **−(voor+achter)** |
  | stil blijven staan | 0 |
  Voorbeelden van Nic: 5→8 zonder te mogen = −3; 5→4 mag niet = −1.
- **Geen "achterstand"-deficitmodel meer** (vervangen door directe aftrek).
- **Sterftes:** zou Δ een speler **onder 0** brengen, dan blijven zijn levensuren op **0** en
  krijgt hij **+1 sterfte** (hij "sterft" maar speelt verder). Legale winst kan nooit een
  sterfte veroorzaken. Eén Δ per speler per controle ⇒ max één sterfte per event per speler.
- **Happy hour ×2:** eindigt een speler een verplaatsing op een uur met `happy_hour`-effect
  én is Δ > 0 (legaal verdiend), dan verdubbelen die levensuren. Wordt geïnd door een
  verplaatsing-doelwit; het happy-hour-event zelf vereist stilstand zoals elk toestand-event.

---

## 5. Portaal (volledige scoring)

- Portaal opent tussen **2 uren**; beide palen worden **paars** (effect `portaal`,
  `data.partner` koppelt ze). `max: 1` standaard.
- Een speler die volgens de regels op een portaal-uur **landt**, mag (optioneel) naar het
  **andere** portaal-uur springen. De sprong telt **niet als stap** en levert **0
  levensuren** op; de stappen ervoor en erna tellen wel. Stappen die overblijven na de
  sprong mogen nog gebruikt worden.
- **Geen terug in de tijd via een portaal** voor wie niet terug mag.
- **De controle is portaal-bewust:** een legale sprong van een hoger naar een lager uur
  (bv. 21→10) mag géén "TERUG IN TIJD" geven. De netto-berekening rekent portaal-randen als
  0 stappen mee.

---

## 6. Globale stats vs. partij-staat

- **Globale stats blijven** over Stop heen: `totaalUren`/levensdagen, **sterftes**, en
  (later) **skills**. Ze zijn **gedeeld** door sim en echt spel.
- **Stop spel** (en de twee-staps Noodstop) resetten enkel de **partij**: events-teller,
  effect-registers, posities en snapshots — **niet** de globale stats.
- **[BEHEER] Wis globale stats** (inject) zet de globale stats handmatig op 0.
- Dashboard **"Globale stats"** (onder Live Radar, op zowel Bediening- als Simulatie-pagina):
  kolommen **Speler | Levensdagen | Levensuren | Skills | Sterftes** (Skills = placeholder).

---

## 7. Node-RED dashboards / besturing

- **Twee pagina's, elk zelfvoorzienend:** "Bediening" (echt spel) en "Simulatie" (sim). Je
  draait sim **óf** echt, nooit samen (gedeelde engine).
- **Bovenbalk (Speltoestand-groep, volle breedte) met schakelaars i.p.v. losse knoppen:**
  - **1 Spel-schakelaar** (start/stop): ON = spel lopend **+ POF actief**, OFF = stop +
    partij-reset + **alle paal-LED's uit** (node "Spel aan/uit"). Groen = loopt, rood = gestopt.
  - **1 Pauze-schakelaar** (pauzeer/hervat).
  - **Manueel/automatisch-schakelaar** + **Controle** + **Volgend event** in dezelfde balk.
    Controle/Volgend werken **enkel in manuele modus** (`pof_volgende` is manueel-gated;
    `pof_controle` enkel in fase `wacht_controle`) — in auto doen ze niets.
  - **Spel-toestand** staat onderaan (links) in de balk.
- **Geen aparte Start POF / Stop POF / Start / Stop / Pauzeer / Hervat / Herstart knoppen
  meer** — die zijn vervangen door de twee schakelaars.
- **Eén klik op "Controle"** in manuele modus voltooit de ronde.
- Radar-tabel toont **Sterftes** (niet langer "Achterstand").
- **Gebruik enkel beproefde dashboard-widgets** (`ui-button`, `ui-switch`, `ui-text`,
  `ui-table`). Een onbeproefde widget (bv. `ui-text-input` met onvolledige schema) kan de
  **hele dashboard-pagina laten crashen** na een deploy — vermijden.
- **Test-widgets-pagina ("Test widgets"):** een aparte dashboard-pagina, **los van het
  spel** (eigen function-nodes + `ui-text`, géén game-globals), om widget-gedrag uit te
  proberen — o.a. of een `ui-switch` **visueel live mee-toggelt zonder page-refresh**
  (varianten: passthru/gekoppeld, decouple+feedback, passthru-uit+decouple). De **echte
  bediening-balk wijzigt pas** als Nic op basis hiervan een widget kiest.
- **Sterftes resetten:** knop op de **Admin**-pagina (achter de twee-staps unlock,
  topic `reset_sterftes`). Globale stats volledig wissen: inject **[BEHEER] Wis globale stats**.

---

## 8. Simulator (browser, `pi/simulator/`)

- **Log-categorieën:** `info` · `cmd` (commando's) · `audio` · `foutcode`. **Toekomstige
  "zet X in de log"-verzoeken indelen onder één van deze categorieën.** De filter is een
  **dropdown-checklist**; de selectie blijft bewaard (localStorage).
- **Log-grootte:** instelbare **vaste hoogte met scroll** (gekozen hoogte blijft bewaard).
  Het **speelveld mag niet weggeduwd worden** (veld krimpt mee). Oudste regels verdwijnen
  vanzelf; nieuwe schuiven omhoog.
- **Event afgelopen:** toon in de log wanneer een toestand verdwijnt (bv. max bereikt /
  uitgewerkt).
- **"Huidig event"** toont **enkel de tekst die effectief voorgelezen wordt**, in het formaat
  `"<aantal> <speler|spelers|uur|uren> <event-tekst>"` (bv. "3 uren worden Happy Hour.").
- **Events-teller** links van "Huidig event": totaal aantal events deze partij; **reset bij
  Stop**.
- **Toestanden-paneel:** geordende lijst per tag — welke toestand op welk uur, en (bij een
  toestand met `max`) hoeveel events die nog blijft (`resterendeRondes`).
- De sim **vervangt de hardware** in sim-modus (publiceert posities) maar gebruikt **geen
  RSSI-model**; hij test het spelverloop, niet de radioprestaties.

---

## 9. Firmware / acties (slave)

- **Minimale actie-set:** enkel acties die **direct aan een bestaand event** hangen.
  Verwijder alles wat niet gebruikt wordt (kleuren, animaties, melodieën die nergens voor
  dienen). Huidige set:
  | id | actie | gedrag |
  |----|-------|--------|
  | 0 | `ACTIE_NIETS` | LED uit |
  | 1 | `ACTIE_PORTAAL` | LED continu **paars** |
  | 2 | `ACTIE_HAPPY_HOUR` | LED continu **goud** |
  | 3 | `ACTIE_BUZZER_PIEP` | korte piep (uur-afroep / zoemer-test) |
- **LED-toestanden worden centraal door Node-RED gestuurd** op basis van de actieve effecten
  (zie §2). Loopt een effect af of stopt het spel → Node-RED stuurt `ACTIE_NIETS`.
- **Test-injects:** hou er **exact 2** over (LED-test + zoemer-test), met de **paal makkelijk
  kiesbaar** (via de inject-payload). Geen tientallen test-injects.
- **Compileren** (PlatformIO) vóór commit; `__attribute__((packed))` op ESP-NOW-structs.

### Zend-/ontvangst-indicatoren & drukknop

- **Slave ingebouwde LED (GPIO8, active-LOW):** knippert kort bij elke **succesvolle
  ESP-NOW-zend** (visuele zend-indicator).
- **Master ingebouwde LED (GPIO2, active-HIGH):** pulst kort bij elke **ontvangen
  slave-batch** (visuele ontvangst-indicator). Beide niet-blokkerend via `millis()`.
- **Drukknop (slave GPIO3, `INPUT_PULLDOWN`):** het framework draait **altijd, met of
  zonder fysieke knop** (zonder knop houdt de pulldown de pin LOW → geen valse triggers).
  Een druk geeft een puls op de **rode LED (GPIO6)** en stuurt een hook-regel
  `{"paal":N,"knop":1}` over serial. De rode LED is **gedeeld**: knop-puls heeft voorrang
  op de batterij-waarschuwing.
- **Geen lichtsensor/laser-tripwire.** Het TEMT6000-idee is volledig verwijderd; GPIO3 is nu
  de drukknop. Pin-mapping: zie `docs/pcb/pinout.md` (single source of truth, klopt met de
  firmware).

### Master-detectie (Pi serial-bridge)

- **Poort-onafhankelijk.** De bridge detecteert **alle CH340-masters automatisch** (VID
  1a86 / PID 7523), ongeacht USB-poort, en **routeert per master op `paal_id`** (1–7 →
  master1, 8–16 → master2, 17–24 → master3). **Geen vaste `/dev/ttyMaster1`** of
  poort-gebonden udev-regel meer. Dit schaalt naar 3 masters zonder code- of poortwijziging.
- Container krijgt toegang via `--device-cgroup-rule 'c 188:* rmw'` + `-v /dev:/dev`
  (`deploy.sh`); udev zet enkel nog `MODE=0666`.

---

## 10. Snelle checklist bij een nieuw/gewijzigd event

- [ ] Past in het standaardformaat (§2), categorie correct (`speler`/`toestand`/`wereld`).
- [ ] Geldt voor echt spel **én** sim (§0), identiek gedrag.
- [ ] Afroep met aantal-prefix klopt (§3); nodige WAV's vermeld.
- [ ] Scoring/straf/sterfte-uitkomst doordacht, incl. randgevallen (§4); portaal-bewust (§5).
- [ ] LED via effect (geen los `commando` tenzij echt nodig) + dooft bij verval (§2).
- [ ] `max` gezet indien het een veld-toestand is (§2).
- [ ] Docs bijgewerkt; `flows.json` chirurgisch + `deploy-flows.ps1` (§1, §0).
