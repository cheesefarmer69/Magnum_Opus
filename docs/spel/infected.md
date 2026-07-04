# Infected — spelregels (geïmplementeerd)

Derde game-mode binnen Magnum Opus, naast **Plates of Fate** en **Klokslag**. Dit document beschrijft de
**geïmplementeerde** regels (v1). De engine staat als één function-node **"Infected engine"**
(`c0a00000000000e0`) op de Klokslag-flow (`d7a0000000000001`), gevoed door dezelfde **250 ms-tick**
(`d7a0000000000020`); de simulator-weergave in `pi/simulator/`.

## Kernidee

Iedereen start op het veld. **Eén** willekeurige speler wordt **besmet** en probeert de rest aan te raken.
Spelers bewegen vrij over de **ring van palen** (niet dwars door het binnenveld — dat is een spelersregel,
de software dwingt het niet af). De **laatste 3** niet-besmette spelers **winnen**.

## Besmetting (5 s basis, oplopend)

- Een paal waar een **besmette** speler staat kleurt **constant rood**.
- Een **niet-besmette** speler die **op datzelfde uur** staat, wordt na de besmettingstijd óók besmet.
  Die tijd start op **5 s** (net na het kiezen van patiënt 0) en loopt op met **+1 s per 2 besmetten**:
  `drempel = 5 s + floor(aantalBesmet / 2)`. Dus 1 besmet → 5 s, 2 → 6 s, 4 → 7 s, … — hoe verder de
  epidemie, hoe trager de volgende besmetting.
- De teller loopt per speler en **reset** zodra die speler de paal verlaat of de paal "veilig" wordt
  (een bestrijder erbij, zie onder). Meerdere spelers op één besmette paal raken elk na hun eigen tijd besmet.

## Infectiebestrijders (vanaf 5 besmet, 60 s)

- Zodra er **5 of meer** besmet zijn, kiest de engine **2 infectiebestrijders** uit de overblijvende
  (niet-besmette) spelers.
- Een bestrijder is **1 minuut onbesmetbaar**, en de **paal waar hij staat kleurt blauw**: op een blauwe
  paal kan **niemand** besmet worden (de besmetting-teller reset daar).
- Na **60 s** worden **2 andere** bestrijders gekozen (de vorige twee bij voorkeur uitgesloten; bij te
  weinig kandidaten valt het terug op wie er is).
- Zakt het aantal besmetten weer onder 5, dan zijn er (tijdelijk) geen bestrijders.

## Winnen

- De **laatste 3** overblijvende niet-besmette spelers **winnen**. Daarna staat de modus op fase **`klaar`**
  (winnaars bevroren); de operator stopt het spel met de gewone Stop-knop.
- De win-conditie geldt enkel in een echt spel (meer dan 3 starters).

## LED's

Per paal, elke tick (alleen bij wijziging verzonden — cache):

| Situatie op de paal | Kleur | Commando |
|---------------------|-------|----------|
| Een **bestrijder** staat er | **blauw** | `MSG_KLOKSLAG` (actie 16), rgb (30,111,255), modus 0 |
| Anders: een **besmette** staat er | **rood** | actie 16, rgb (224,36,36), modus 0 |
| Anders | **uit** | actie 16, helderheid 0, modus 3 |

De commando's gaan via `commando/master1` (palen 1–8), `master2` (9–16), `master3` (17–24) — hetzelfde
pad als Klokslag. Bij **Stop** worden alle infected-palen netjes uitgezet.

## State & topics

- **`global.infected`** = `{ besmet[], bestrijders[], bestrijderTot (ms), dwell{naam:ms}, fase, winnaars[],
  start, startAantal }`. Plus `infectedActief` (bool), `infectedLed` (LED-cache), `infectedLaatstePalen`,
  `infectedStatusSig` (status-throttle).
- **Retained MQTT `infected/status`**: `{ actief, fase, besmet[], overlevenden[], bestrijders[], winnaars[],
  aantalBesmet, aantalOver, bestrijderResterend, palen{paal:"rood"|"blauw"} }`. Het dashboard
  (groep "Infected (minigame)") en de simulator renderen hieruit.
- Reset bij **Stop/Herstart** (`Spel aan/uit`): alle infected-globals leeg.

## Zo speel je het

1. **Dashboard → Speltype → "Infected"** kiezen (publiceert retained `spel/type`; PoF- en Klokslag-engine
   gaan stil).
2. Druk op de gewone **Start**-knop. De engine wacht tot er spelers op het veld staan en kiest dan
   patiënt 0.
3. In de **simulator** verschijnt het **Infected-paneel** (besmet/overlevenden/bestrijders/winnaars) en
   kleuren de palen rood/blauw mee.
4. **Stop** beëindigt en wist de modus.

## Timers (samengevat)

| Mechanisme | Tijd |
|------------|------|
| Besmetting op een besmette paal | **5 s basis + 1 s per 2 besmetten** (`5 + floor(aantalBesmet/2)`) |
| Bestrijders actief vanaf | **5 besmet** |
| Bestrijder-immuniteit + rotatie | **60 s** |
| Winnaars | laatste **3** over |

## Invarianten

- **INF1** — Eén patiënt 0 bij start (willekeurig uit de spelers op het veld). De PoF-/Klokslag-engines
  staan stil zolang `spelType === "infected"`.
- **INF2** — Besmetting vereist **`5 + floor(aantalBesmet/2)` s onafgebroken** (basis 5 s, +1 s per 2 besmetten) op een paal met een besmette; reset bij verlaten of
  een blauwe (bestrijder-)paal. Bestrijders zelf zijn immuun.
- **INF3** — Bestrijders bestaan enkel bij **≥ 5 besmet**, met een **60 s**-rotatie; hun paal is een
  blauwe veilige zone.
- **INF4** — Het spel eindigt bij **≤ 3** overlevenden (winnaars); state + LED's worden bij Stop gewist.
