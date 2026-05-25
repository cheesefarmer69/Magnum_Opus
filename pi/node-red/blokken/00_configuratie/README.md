# Flow 00 — Configuratie

## Doel

Eén centrale plek voor de twee lijsten die het hele systeem nodig heeft:

- de **spelerslijst** — welke BLE-beacons horen bij welke speler;
- de **paaltjeslijst** — welke palen (slaves) het systeem verwacht.

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
| `[TEST] Buzzer paal N AAN/UIT` | inject-nodes die de buzzer van paal 1/2/3 aan/uit zetten |
| `commando/master1`         | MQTT-out die de buzzer-testcommando's naar de master stuurt  |

Beide inject-nodes staan op "inject once": ze vuren automatisch bij elke deploy
en bij het herstarten van Node-RED. Je kan ze ook handmatig aanklikken.

## Outputs (global context)

| Variabele             | Inhoud                                              |
|-----------------------|-----------------------------------------------------|
| `global.spelersLijst` | `{ "mac": "naam", ... }`                            |
| `global.paaltjesLijst`| `[ 1, 2, 3, ... ]` — lijst van verwachte paal-id's  |

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

## Buzzer-test (palen 1–3)

Onderaan de tab staan inject-nodes om de buzzer van elk testbordje te testen:
`[TEST] Buzzer paal 1 AAN` … `paal 3 UIT`. Ze publiceren een commando op
`commando/master1` (`{"paal":N,"actie":3}` = aan, `actie:4` = uit; zie
`docs/protocol.md`). Handig om per paal het buzzervolume/-gedrag te controleren
zonder de Plates-of-Fate engine te starten.

> AAN blijft klinken tot je op de bijbehorende UIT-node drukt.

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
