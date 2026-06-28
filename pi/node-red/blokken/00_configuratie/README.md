# Flow 00 — Configuratie

## Doel

Eén centrale plek voor de lijsten die het hele systeem nodig heeft:

- de **spelerslijst** — welke BLE-beacons horen bij welke speler;
- de **paaltjeslijst** — welke palen (slaves) het systeem verwacht;
- de **drukknop-palen** — welke palen een fysieke drukknop hebben.

Andere flows (Locatiebepaling, Spelstatus) lezen deze lijsten uit de global
context. Door alles hier te bundelen, pas je de configuratie op één plek aan en
kan niets uit sync raken.

## Wat de flow doet

| Node                       | Functie                                                      |
|----------------------------|--------------------------------------------------------------|
| `[CONFIG] Spelerslijst`    | inject-node met de spelerslijst als JSON                     |
| `Sla op als spelersLijst`  | schrijft die naar `global.spelersLijst`                      |
| `spelersLijst (controle)`  | debug-node die de actieve lijst toont                        |
| `[CONFIG] Paaltjeslijst`   | inject-node met de paaltjeslijst als JSON                    |
| `Sla op als paaltjesLijst` | schrijft die naar `global.paaltjesLijst`                     |
| `paaltjesLijst (controle)` | debug-node die de actieve lijst toont                        |
| `[CONFIG] Drukknop-palen`  | inject-node met de palen die een drukknop hebben (JSON-array) |
| `Sla op als drukknopPalen` | schrijft die naar `global.drukknopPalen` én publiceert ze retained op `config/drukknoppen` (voor de simulator) |
| `[TEST] LED (portaal-kleur)` | inject die een gekozen paal paars maakt (LED-test, `actie:1`) |
| `[TEST] Zoemer (piep)`     | inject die een gekozen paal laat piepen (zoemer-test, `actie:3`) |
| `commando/master1`         | MQTT-out die de test-commando's naar de master stuurt        |

> ⚠️ **De twee `[CONFIG]`-inject-nodes zijn essentieel.** Zonder hen blijft
> `global.spelersLijst` leeg → geen mac→naam-mapping → `spelerLocaties` vult niet
> → het sim-dashboard toont geen spelers en events kiezen geen doelwit, en het
> echte spel kan beacons niet mappen. Verwijder ze niet.

Beide `[CONFIG]`-inject-nodes staan op "inject once": ze vuren automatisch bij
elke deploy en bij het herstarten van Node-RED. Je kan ze ook handmatig aanklikken.

## Outputs (global context)

| Variabele             | Inhoud                                              |
|-----------------------|-----------------------------------------------------|
| `global.spelersLijst` | `{ "mac": "naam", ... }`                            |
| `global.paaltjesLijst`| `[ 1, 2, 3, ... ]` — lijst van verwachte paal-id's  |
| `global.drukknopPalen`| `[ 3, 4, 7, ... ]` — palen met een fysieke drukknop (ook retained op `config/drukknoppen`) |

## De spelerslijst opmaken en aanpassen

De spelerslijst koppelt het MAC-adres van elke beacon aan een spelersnaam.

1. Open Node-RED in de browser (`http://192.168.1.43:1880`).
2. Ga naar de tab **00 Configuratie**.
3. Dubbelklik op de inject-node **`[CONFIG] Spelerslijst`**.
4. Het veld `payload` staat op type **JSON**. De inhoud is een object met per
   speler één regel `"mac": "naam"`:

   ```json
   {
     "48:87:2d:9d:bb:7d": "Lilou",
     "48:87:2d:9d:ba:5c": "Zoë"
   }
   ```

5. Voeg een speler toe of verwijder er een, klik **Done**.
6. Klik rechtsboven op **Deploy**.
7. Klik daarna één keer op het knopje links van de inject-node om de nieuwe
   lijst meteen actief te zetten (of herstart Node-RED).

**Regels voor het MAC-adres:**

- Formaat `aa:bb:cc:dd:ee:ff`, met dubbele punten.
- **Volledig in kleine letters** — NimBLE op de slaves geeft MAC-adressen
  lowercase door. Een adres met hoofdletters wordt niet herkend.
- Het MAC-adres van een onbekende beacon vind je terug in de tab
  **02 Spelstatus**: een niet-geregistreerde beacon verschijnt daar als fout
  `ST-004` met het MAC-adres erbij. Ook de debug-node `Detectie (debug)` in die
  tab toont de binnenkomende `mac`-waarden.

## De paaltjeslijst aanpassen

De paaltjeslijst bepaalt welke palen het systeem verwacht. De Spelstatus-flow
gebruikt die om te controleren of elke paal data stuurt.

1. Tab **00 Configuratie** → dubbelklik op **`[CONFIG] Paaltjeslijst`**.
2. Het veld `payload` (type **JSON**) is een lijst van paal-id's:

   ```json
   [1, 2, 3, 4, 5, 6, 7, 8]
   ```

3. Zet hier de paal-id's die effectief in gebruik zijn. Het volledige systeem
   heeft 24 palen (3 masters × 8 slaves, zie `docs/protocol.md`); tijdens het
   testen zet je hier enkel de palen die je echt aangesloten hebt.
4. **Done** → **Deploy** → inject-node één keer aanklikken.

## De drukknop-palen aanpassen

Sommige palen hebben een fysieke drukknop waarmee spelers (bij bepaalde events, bv. de
**tijdbom**) interactie hebben. Welke palen dat zijn, staat in **`[CONFIG] Drukknop-palen`**:

1. Tab **00 Configuratie** → dubbelklik op **`[CONFIG] Drukknop-palen`**.
2. Het veld `payload` (type **JSON**) is een lijst van paal-id's, bv.:

   ```json
   [3, 4, 7, 9, 11, 13, 15, 16, 17, 19, 21, 22]
   ```

3. **Done** → **Deploy** → inject-node één keer aanklikken.

De lijst wordt naar `global.drukknopPalen` geschreven én **retained** gepubliceerd op
`config/drukknoppen`, zodat de simulator meteen het juiste knoppen-paneel toont. De firmware
stuurt knopdrukken sowieso al (`MSG_KNOP` → `{"paal":N,"knop":1}` op `plaatjes/data`); deze lijst
bepaalt enkel op welke palen een druk **iets doet** in de spellogica.

## Hardware-test (LED + zoemer)

Onderaan de tab staan twee test-inject-nodes (zie ook Design_rules §9 — "exact 2
test-injects"), met de **paal kiesbaar via de inject-payload**:

- **`[TEST] LED (portaal-kleur)`** → `{"paal":N,"actie":1}`: maakt de LED van
  paal N paars.
- **`[TEST] Zoemer (piep)`** → `{"paal":N,"actie":3}`: laat paal N kort piepen.

Ze gaan via de **"Route commando"**-node, die elk commando per paal-bereik naar
`commando/master1|2|3` routeert (1–8 / 9–16 / 17–24). Zet het `paal`-veld dus op
**9** om **master 2** te testen of **17** voor **master 3** — het commando komt
automatisch bij de juiste master. Handig om per paal de hardware te controleren
zonder de Plates-of-Fate engine te starten.

## Controleren of de configuratie geladen is

- Open in Node-RED rechts het **debug-paneel**.
- Klik de inject-nodes aan. De debug-nodes `spelersLijst (controle)` en
  `paaltjesLijst (controle)` tonen exact wat er in de global context staat.
- Of via het menu (≡) → **Context Data** → **Global**: daar zie je
  `spelersLijst` en `paaltjesLijst` live staan.

## Testen

1. Pas de spelerslijst aan en deploy.
2. Klik de inject-node aan; controleer in het debug-paneel dat de juiste JSON
   verschijnt.
3. Doe hetzelfde voor de paaltjeslijst.
4. Een fout in de JSON (bv. een komma vergeten) toont Node-RED meteen rood in de
   inject-node — corrigeer en deploy opnieuw.
