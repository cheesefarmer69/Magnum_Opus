# Node-RED flows deployen

Hoe je `flows.json` naar de draaiende Node-RED brengt **zonder** in de browser
alles te wissen en opnieuw te importeren.

## Waarom niet meer via de browser importeren?

Onze nodes hebben **vaste ID's**. Importeer je dezelfde `flows.json` opnieuw via
de UI, dan ziet Node-RED ID-conflicten en maakt het **duplicaten** — daarom moest
je eerst alles (incl. de dashboard-UI's) wissen. Dat is omslachtig en foutgevoelig.

De Admin API van Node-RED kan in één keer de **volledige** flow vervangen — op
node-ID, dus bestaande nodes worden bijgewerkt i.p.v. gedupliceerd, inclusief alle
dashboard-/UI-nodes. Geen handmatig wissen meer.

## De scripts

| Script              | Waar draaien                         |
|---------------------|--------------------------------------|
| `deploy-flows.ps1`  | Windows (jouw dev-pc), PowerShell    |
| `deploy-flows.sh`   | Raspberry Pi of Git Bash             |

Beide doen hetzelfde:
1. **`flows.json` valideren** (geldige JSON? anders stoppen — nooit een kapotte flow pushen).
2. **Volledige deploy** via `POST /flows` met header `Node-RED-Deployment-Type: full`.

## Gebruik

### Vanaf Windows (PowerShell)

Vanuit de map `pi/node-red/`:

```powershell
.\deploy-flows.ps1
```

Andere host/poort:

```powershell
.\deploy-flows.ps1 -Url http://192.168.1.43:1880
```

> Krijg je "kan niet worden geladen omdat het uitvoeren van scripts is
> uitgeschakeld", draai dan eenmalig in dezelfde PowerShell:
> `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass`

### Vanaf de Pi of Git Bash

```bash
cd pi/node-red
chmod +x deploy-flows.sh      # eenmalig
./deploy-flows.sh
# of een andere host:
./deploy-flows.sh http://192.168.1.43:1880
```

## Wat gebeurt er bij een deploy?

- Een **full deploy** herstart alle flows schoon. De `[CONFIG]`-inject-nodes
  (spelerslijst, paaltjeslijst, POF-events) staan op "inject once" en vuren dus
  meteen opnieuw → `global.spelersLijst`, `paaltjesLijst`, `pofEvents` worden vers
  geladen.
- De **global context blijft nu bewaard** — zodra `settings.js` met `contextStorage`
  (`localfilesystem`) actief is (zie "Persistente spelstate" hieronder). Lopende toestand
  zoals `spelerStats`, `globaleStats`, `spelHistorie`, de π-stand en `godPunten` overleeft
  dus een deploy én een container-restart. (Vóór deze wijziging draaide Node-RED op de
  default in-memory store en werd álle global context bij elke deploy geleegd.)

## Persistente spelstate (contextStorage + `spel/state`)

Zonder configuratie draait Node-RED met een **in-memory** context-store: één stroomdip,
reboot of deploy wist de hele speeldag. Twee complementaire lagen lossen dat op:

1. **`settings.js` → `contextStorage: localfilesystem`** (primair). Elke `global.set` wordt
   periodiek (`flushInterval` 15 s) naar `/data/context/` geschreven en overleeft restart +
   deploy. `settings.js` staat in de repo (`pi/node-red/settings.js`) en wordt gemount op
   `/data/settings.js`.
2. **Retained `spel/state`-topic** (secundair vangnet). Flow 04 (Puntensysteem) dumpt elke
   30 s een compacte snapshot (`spelerStats`, `globaleStats`, `spelHistorie`, π-stand,
   `spelToestand`, `spelNummer`) naar het retained MQTT-topic `spel/state`. Bij (her)start
   leest node **`Rehydrate spel-state`** dit terug — maar **alleen als de betreffende global
   nog leeg is**, zodat een lopend spel nooit overschreven wordt. Zo herstelt zelfs een verse
   container zónder SSD-volume nog de laatste snapshot.

### Container + SSD-bind-mount (eenmalige migratie)

De Node-RED-container ligt nu vast in **`pi/node-red/docker-compose.yml`** met `/data` als
bind-mount naar de SSD (zodat `/data/context` een container-recreate overleeft). Migreer de
bestaande, handmatig aangemaakte container éénmalig:

```bash
# 1. Kopieer de HUIDIGE /data uit de draaiende container naar het SSD-pad (pas het pad aan).
export NODE_RED_DATA=/mnt/ssd/magnum-opus/nodered-data
sudo mkdir -p "$NODE_RED_DATA"
docker cp magnum-Opus:/data/. "$NODE_RED_DATA"/      # incl. flows.json, flows_cred.json, .config.*

# 2. Stop/verwijder de oude container en start via compose (vanuit pi/node-red/).
docker rm -f magnum-Opus
cd pi/node-red
NODE_RED_DATA="$NODE_RED_DATA" docker compose up -d

# 3. Deploy de repo-flows en controleer.
./deploy-flows.sh
```

> ⚠️ Kopieer `/data` **vóór** de eerste `compose up`, anders start Node-RED met een lege
> `/data` en ben je de bestaande flows én de credential-sleutel kwijt. `settings.js` zet
> bewust géén `credentialSecret` zodat de bestaande auto-sleutel uit `.config.runtime.json`
> (mee gemigreerd) blijft werken.

## Belangrijk: één bron van waarheid

Deze workflow gaat ervan uit dat **`flows.json` in de repo de waarheid is**:
je past hem aan (of laat hem aanpassen), valideert, en deployt met het script.

Pas je daarnaast **live in de browser** dingen aan (knoppen verslepen, constanten
tunen…), dan raken repo en live uit sync — en overschrijft de volgende deploy je
live-wijzigingen. Wil je live-wijzigingen bewaren, haal ze dan eerst terug naar de
repo:

- **Hele flow ophalen:** `GET http://192.168.1.43:1880/flows` → opslaan als
  `flows.json`. Bijvoorbeeld:
  ```bash
  curl -s http://192.168.1.43:1880/flows -o pi/node-red/flows.json
  ```
- **Of** via de browser: menu (≡) → Export → alle flows → in `flows.json` plakken.

Daarna committen, zodat de repo weer klopt.

## Alternatief: flows-bestand + container herstarten

Wil je liever niet via de API maar via een "echte" uitrol (bv. in een deploy-script
naast `pi/deploy.sh`): kopieer `flows.json` naar het Node-RED data-volume en
herstart de container.

```bash
docker cp pi/node-red/flows.json <nodered-container>:/data/flows.json
docker restart <nodered-container>
```

Trager (volledige herstart) maar dood-eenvoudig. De API-methode is sneller voor
itereren tijdens het ontwikkelen.

## Problemen oplossen

| Symptoom                                   | Oorzaak / oplossing                                            |
|--------------------------------------------|----------------------------------------------------------------|
| `Deploy mislukt` / connection refused      | Draait Node-RED? Klopt het IP/poort (`192.168.1.43:1880`)?     |
| HTTP 401 / 403                             | Admin-authenticatie staat aan; dan is een token nodig (nu niet het geval). |
| `Ongeldige JSON`                           | `flows.json` is kapot — corrigeer en deploy opnieuw.           |
| Dashboard ziet er leeg/raar uit            | Hard refresh in de browser (Ctrl+F5) na een deploy.            |
| Wijzigingen weg na deploy                  | Je had live getweakt; zie "één bron van waarheid" hierboven.   |
