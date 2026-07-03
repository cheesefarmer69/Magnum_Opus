# Handleiding: Node-RED dashboards

De spellogica draait in **Node-RED**; de bediening en monitoring gebeuren via een set
**Node-RED Dashboard 2.0**-pagina's. Dit document beschrijft per dashboard de **functie**
(waarvoor je het gebruikt), de **werking** en de **opbouw** (groepen + widgets).

## Toegang

- **Dashboards (deze pagina's):** `http://192.168.1.43:1880/dashboard`
- **Flow-editor** (de onderliggende flows zelf bewerken): `http://192.168.1.43:1880/`
- **Browser-simulator** (losse test-tool, geen Node-RED-dashboard): `pi/simulator/index.html`,
  verbindt via MQTT-over-WebSocket op poort 9001. Zie `pi/simulator/` en `docs/spel/`.

> Dashboard-base: **"My Dashboard"**, path `/dashboard`. De pagina's hieronder zijn de
> `ui-page`-knopen in `pi/node-red/flows.json`; elke pagina bevat `ui-group`-groepen met
> widgets (`ui-text`, `ui-table`, `ui-switch`, `ui-button`, `ui-slider`, `ui-template`).
> Voor de logica achter de knoppen: zie de blok-README's in `pi/node-red/blokken/*/`.

De **bron van waarheid** is `pi/node-red/flows.json`. Wijzig je daar dashboards, werk dan dit
document bij en deploy met `pi/node-red/deploy-flows.ps1` (een `docker restart` herlaadt de
repo-`flows.json` niet).

---

## 1. Spelstatus (`/spelstatus`)

**Functie:** GO/NO-GO-controle vóór de start — in één oogopslag zien of alle hardware en
verbindingen werken voor je een spel begint.

**Werking:** subscribet op de status-topics (spelers, palen/slaves, MQTT). De tabellen tonen
per speler en per paal wanneer ze laatst gezien zijn; de foutcodetabel vat problemen samen.

**Opbouw:**

| Groep | Widgets | Wat het doet |
|-------|---------|--------------|
| MQTT | `ui-template` MQTT indicator | Groen/rood: staat de MQTT-broker-verbinding aan. |
| Spelstatus | `ui-template` Status | Kleur-gecodeerde GO/NO-GO-status (groen = GO, rood = NO-GO). |
| Spelers | `ui-table` Tabel Spelers | Alle spelers + detectiestatus + laatst gezien. |
| Bediening status | `ui-switch` Toon batterij · `ui-switch` Override NO-GO | Batterijkolom tonen; geforceerd starten ondanks NO-GO. |
| Palen / Slaves | `ui-table` Tabel Palen | Alle palen/slaves: status, laatst gezien, batterijspanning. |
| Foutcodes | `ui-table` Tabel Foutcodes | Actieve foutcodes (ST-001…ST-004) met ernst + uitleg. |

---

## 2. Bediening (`/bediening`)

**Functie:** de centrale **spelbesturing** tijdens een echt spel — starten, pauzeren, het
Plates-of-Fate-verloop volgen en veilig stoppen.

**Werking:** de switches sturen de engine-toestandsmachine (zie `blokken/06_plates_of_fate/`).
In **Manueel**-modus stuur je events stap voor stap met *Volgende event* en *Controle*; anders
loopt de engine automatisch. De tekstvelden en tabellen tonen de live-toestand.

**Opbouw:**

| Groep | Widgets | Wat het doet |
|-------|---------|--------------|
| Speltoestand | `ui-text` Spel-teller (`Spel #`) · `ui-switch` Spel (uit/LOOPT) · `ui-switch` Modus monitor/sim (via feedback-node, consistent met de andere) · `ui-switch` Pauze/Hervat + `ui-text` Pauze-status · `ui-switch` Manueel · `ui-button` Volgende event · `ui-button` Controle (manueel) · `ui-template` Speltoestand | Hoofdbesturing; de **monitor/sim-schakelaar** vervangt de aparte Simulatie-pagina (publiceert `sim/modus`). |
| Doel (Plates of Fate) | `ui-dropdown` Doel · `ui-dropdown` Aantal uur (X) · `ui-dropdown` Aantal spelers · `ui-switch` Auto-einde · `ui-text` Doel-status (a/n geslaagd) | PoF-doelkeuze + voortgang. Een PoF-spel start pas met een gekozen doel + aantal. De groep wordt **verborgen** (`ui-control`) wanneer Klokslag het speltype is. |
| Plates of Fate besturing | `ui-text` Timer (groot/gekleurd) · `ui-text` Huidig event · `ui-text` Doelwit · `ui-table` Controle laatste event · `ui-text` Events deze ronde | Live-verloop van de events en de laatste controle-uitslag. |
| Live Radar | `ui-table` Tabel Locatie Spelers | Per speler: huidige paal, RSSI, levensdagen/-uren, sterftes. |
| Actieve effecten (bord-staat) | `ui-table` Tabel Actieve effecten · `ui-table` Tabel Speler-toestanden | Blijvende uur-/speler-effecten + 🤒 ziek / 💣 tijdbom met resterende rondes. |
| Huidig spel | `ui-table` Tabel Huidig spel | Levensdagen/-uren/sterftes van de **lopende** partij (per spel). |
| Globaal (cumulatief) | `ui-table` Tabel Globaal | Som over alle gestopte partijen; bij Stop telt Huidig spel hierbij op. |
| Wereld-effecten | `ui-table` Tabel Wereld-effecten | Actieve wereld-effecten (bv. events sneller). |
| Noodstop | `ui-switch` Bevestig stop (stap 1) · `ui-button` STOP SPEL (stap 2) | 2-staps harde stop van de partij. |

> **De aparte Simulatie-pagina is verwijderd.** Monitor vs. simulatie kies je nu met de
> **Modus**-schakelaar op deze Bediening-pagina (synct via retained `sim/modus` met de
> browser-simulator). Sim en echt spel draaien nooit tegelijk (zie `docs/invarianten.md`, SIM1–SIM4).

---

## 4. Admin (`/admin`)

**Functie:** **beheer/reset** van spel- en speler-data. Alles is 2-staps beveiligd tegen
per-ongeluk-klikken.

**Werking:** *Admin ontgrendelen (stap 1)* zet de reset-knoppen vrij; daarna voert elke knop een
gerichte reset uit. Globale stats blijven anders bewaard bij een gewone *Stop spel*
(zie `docs/invarianten.md`, S4–S5).

**Opbouw:**

| Groep | Widgets | Wat het doet |
|-------|---------|--------------|
| Levensjaren beheer | `ui-switch` Admin ontgrendelen (stap 1) · `ui-button` Levensdagen → 0 · `ui-button` Levensuren → 0 · `ui-button` Sterftes → 0 · `ui-button` Paal-effecten → 0 · `ui-button` Speler-effecten → 0 · `ui-button` Speler-toestanden → 0 · `ui-button` Middernacht-klok → start · `ui-button` ALLES → 0 · `ui-dropdown` Paal om te resetten + `ui-button` Reset paal → rust | Gerichte resets (stap 2) na ontgrendelen. "Middernacht-klok → start" zet de π-sequentie terug; "ALLES → 0" doet alle resets in één klik; "Reset paal → rust" zet één gekozen paal (effecten weg, LED uit) terug. |
| Speler pauze | `ui-template` Speler pauze knoppen | Per speler pauzeren/hervatten (custom HTML). |

---

## 5. Beacons & Locatie (`/beacons`)

**Functie:** de **RSSI-locatiebepaling tunen** en de beacon-signaalkwaliteit diagnosticeren.
Zie `docs/locatiebepaling.md` voor het algoritme achter deze parameters.

**Werking:** de sliders stellen de live-parameters van de locatie-functie in; de tekst toont de
actieve waarden. De tabellen helpen zwakke/instabiele beacons opsporen en per-beacon offsets
kalibreren.

**Opbouw:**

| Groep | Widgets | Wat het doet |
|-------|---------|--------------|
| Locatie-instellingen | `ui-slider` Venster (ms) · `ui-slider` Hysterese (dB) · `ui-slider` RSSI-vloer (dBm) · `ui-slider` Grace (ms) · `ui-slider` Switch-samples · `ui-slider` Min-samples · `ui-text` Actieve locatie-parameters | Tuning van het locatie-algoritme. |
| Scan-duur (BLE) | `ui-slider` Scan-duur alle slaves (ms) · `ui-slider` Paal · `ui-slider` Scan-duur deze paal (ms) · `ui-button` Pas toe · `ui-text` Laatste scan-actie | BLE-scan-vensterduur van de slaves instellen (400–1000 ms), voor alle slaves of per paal. Kortere scan = versere detectie. Stuurt actie 20 (`MSG_SCAN_CONFIG`); auto-herstel na reboot via heartbeat. Zie `docs/locatiebepaling.md`. |
| Beacon-stabiliteit | `ui-table` Beacon-stabiliteit (laagste score bovenaan) | Ranglijst van signaalstabiliteit per beacon. |
| Beacon-kalibratie (RSSI-offset) | `ui-template` Beacon-kalibratie | Per-beacon RSSI-offset instellen, begrensd op −20…+20 dB. |
| Ruwe RSSI (diagnose) | `ui-switch` Toon ruwe RSSI · `ui-table` Ruwe RSSI per beacon per paal (laatste 6 s) | Ruwe meetwaarden tonen voor diagnose. |

---

## 6. Historiek (`/historiek`)

**Functie:** **terugkijken** op gespeelde partijen en hun events.

**Werking:** een `ui-template` toont de opgeslagen spel-historiek (events per partij, in volgorde).
Geselecteerde partijen verwijderen vraagt eerst een bevestiging.

**Opbouw:**

| Groep | Widgets | Wat het doet |
|-------|---------|--------------|
| Spel-historiek | `ui-template` Historiek | Overzicht van vorige partijen en hun event-verloop. |

---

## 7. Buzzer-tuning (`/buzzer-tuning`)

**Functie:** per bordje de **luidste buzzer-toon** vinden. Een passieve piezo klinkt het
luidst rond zijn eigen resonantiefrequentie en die verschilt licht per buzzer
(productiespreiding). Met een **frequentie-sweep** hoor je welke frequentie het hardst
klinkt; die waarde zet je daarna in `BUZZER_FREQ_TABEL` in de slave-firmware.

**Werking:** alle commando's gaan naar **paal 1** (`commando/master1`, actie 12 =
`MSG_BUZZER_TOON`, zie `docs/protocol.md`). Verwissel je test-ESP telkens naar **paal 1**
om elk bordje te meten. De "Buzzer-sweep controller" stuurt een **continue** toon en stapt
de frequentie elke interval op van min → max (en weer terug naar min). **Stop** stuurt
`toon:0`.

**Opbouw:**

| Groep | Widgets | Wat het doet |
|-------|---------|--------------|
| Buzzer-tuning (paal 1) | `ui-slider` Min/Max frequentie (Hz) | Onder- en bovengrens van de sweep (1000–5000 Hz). |
| | `ui-slider` Stapgrootte (Hz) | Hoeveel Hz per stap omhoog. |
| | `ui-slider` Stappen per seconde | Hoe snel de sweep door de range loopt. |
| | `ui-button` Start sweep / Stop | Start de oplopende sweep / zet de toon uit (`toon:0`). |
| | `ui-slider` Handmatige frequentie | Stopt de sweep en houdt één vaste frequentie aan (inzoomen op de piek). |
| | `ui-text` Actuele frequentie | Toont de frequentie die nu klinkt. |

> Wijzig je `flows.json`, draai dan `pi/node-red/deploy-flows.ps1` (Admin API) — een
> `docker restart` herlaadt de repo-flows niet.

---

## Zie ook

- `pi/node-red/blokken/*/README.md` — de logica per flow-blok (bediening, puntensysteem,
  Plates of Fate, …).
- `docs/locatiebepaling.md` — het algoritme achter de Beacons-sliders.
- `docs/invarianten.md` — regels rond globale stats, sim vs. echt spel, dashboard-widgets (NR5/NR6).
- `docs/spel/` — het spelontwerp en de events die de Bediening-pagina aansturen (monitor én simulatie).
