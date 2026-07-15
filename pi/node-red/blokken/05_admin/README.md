# Flow 05 — Admin

## Doel

Een afgeschermd beheerpaneel voor gevaarlijke en corrigerende acties: resets, handmatige
score-correcties, palen uit het spel nemen, spelers pauzeren en het box-volume. Destructieve
knoppen zitten achter een **twee-staps verificatie** (eerst ontgrendelen) zodat één losse klik
nooit iets wist.

## Dashboard-pagina **Admin** — de vijf groepen

### 1. Beheer (resets, twee-staps)

| Element | Werking |
|---|---|
| Switch **Admin ontgrendelen (stap 1)** | Moet AAN staan voordat een resetknop iets doet. Na elke reset springt hij terug op UIT. |
| Knop **Levensdagen → 0** | `totaalUren = totaalUren % 24` — hele dagen weg, losse uren blijven. |
| Knop **Levensuren → 0** | `totaalUren = floor(totaalUren/24)*24` — losse uren weg, dagen blijven. |
| Knop **Sterftes → 0** | Sterfte-tellers van alle spelers op 0. |
| Knop **Paal-effecten → 0** | Wist alle blijvende uur-effecten (`bordStaat`); LEDs herstellen. |
| Knop **Speler-effecten → 0** | Wist `spelerEffecten` + `wereldEffecten`. |
| Knop **Wis toestanden — ALLE spelers** | Wist toestanden (ziekte, dienaars, effecten) van álle spelers tegelijk. |
| Knop **Middernacht-klok → start** | Zet de π-sequentie terug (poort open, eerste π-cijfer) en herpubliceert `pof/middernacht` + poort-LED. |
| Dropdown **Kies reset-paal** + knop **Reset paal → rust** | Zet één paal (1–24) terug naar rust: effecten weg, LED uit (actie 0). |
| Dropdown **Kies reset-speler** + knop **Reset EEN speler** | Haalt één speler uit álle toestanden via de gedeelde `resetSpeler`-helper (settings.js; score blijft — daar is "Handmatig bijstellen" voor). De dropdown vult zich uit de actuele roster en **behoudt je selectie** (options worden alleen bij wijziging gepusht). |
| Knop **ALLES → 0** (rood, onderaan) | Alle resets in één klik — bewust als láátste widget geplaatst. |

### 2. Handmatig bijstellen (score-correctie, per speler)

Kies speler + parameter (**Levensuren** `totaalUren` / **Sterftes** / **Valsspeelpunten** /
**God-punten**), vul een waarde in en kies **Zet op waarde** of **+ optellen** (nooit onder 0).
Gate: `admin_unlocked` moet aan staan (paneel blijft hierna bewust ontgrendeld — handig voor
meerdere correcties na elkaar). Bedoeld om een beacon-/detectiefout recht te zetten (invariant S9).

### 3. Speler pauze

Per speler pauzeren/hervatten. Een gepauzeerde speler is **volledig uit het spel** (niet gescoord,
genegeerd in Klokslag/Infected/tweeling/etenstijd) en de stand overleeft een herstart (S8b).
Niet admin-gated (niet-destructief).

### 4. Palen handmatig uit/in

Kies paal → **Uit spel** / **Terug in spel** (gate: `admin_unlocked`). Zet `palenHandmatigUit`;
de L3-ring in "Evalueer spelstatus" slaat die palen over met behoud van de ≥2-palen-vloer (F5).
De statusregel toont welke palen uit staan.

### 5. Geluid (box)

Volume-slider + presets (Stil 30 / Normaal 70 / Max 100) → retained `audio/volume` → de
audio-player past het direct toe (ook midden in een lopend segment). Herstelt zichzelf na een
herstart via het retained topic.

## Twee-staps verificatie

1. Zet **Admin ontgrendelen** aan.
2. Druk op een resetknop → uitvoering + bevestiging; de switch springt terug op UIT.

Vergrendeld drukken → melding "VERGRENDELD - zet eerst 'Admin ontgrendelen' aan", er verandert
niets. Uitzonderingen op het auto-vergrendelen: **Handmatig bijstellen** en **Palen uit/in**
blijven na een actie ontgrendeld (multi-edit-workflow); **Speler pauze** en **Geluid** zijn
niet gated.

## Globale variabelen

| Variabele | Type | Gezet door | Gelezen door |
|---|---|---|---|
| `admin_unlocked` | boolean | 05 Admin | alle admin-handlers |
| `adminResetPaal` / `adminResetSpeler` | number / string | dropdowns | paal-/speler-reset-knop |
| `palenHandmatigUit` | number[] | Palen uit/in | "Evalueer spelstatus" (L3-ring) |
| `gespauzeerdePlayers` | object | Speler pauze | scoring/Klokslag/Infected + `spel/state` |
| `audioVolume` | number | Geluid (box) | (retained `audio/volume` → player) |

`admin_unlocked` is standaard `false` (ook een ontbrekende waarde geldt als vergrendeld).
De helpers `resetPartij`/`resetSpeler` komen uit `settings.js` (functionGlobalContext) en laden
**alleen bij een container-herstart** — de knoppen degraderen gracieus met een duidelijke melding
als de helper ontbreekt.

## Testen

1. Open dashboard → **Admin**; druk vergrendeld op een resetknop → "VERGRENDELD…", niets wijzigt.
2. Ontgrendel → **Levensdagen → 0** → bevestiging + switch springt terug; Live Radar toont 0 dagen.
3. Kies in **Kies reset-speler** een naam → wacht 10 s (selectie blijft staan) → **Reset EEN
   speler** → melding; toestanden van die speler zijn weg, score intact.
4. **Handmatig bijstellen**: speler +5 levensuren → Leaderbord volgt binnen 2 s.
5. **Palen uit/in**: paal uit → verdwijnt uit de ring (Spelstatus); terug in → herstelt.
