# Verbinden met de hub (Pi) — thuis en op het veld

Dé referentie voor hoe je met dashboard en simulator op de Pi geraakt, in elke situatie.
De Pi is **dual-homed**: `eth0` (kabel) + `wlan0` (het eigen `MagnumOpus`-accesspoint).
Alles hieronder werkt **zonder internet** — op het veld is er geen router en dat is oké.

## Adressen-overzicht

| Situatie | Netwerk | Pi-adres | Dashboard | Simulator |
|---|---|---|---|---|
| **Thuis** (router) | thuis-LAN via eth0 | `192.168.1.43` | `http://192.168.1.43:1880/dashboard` | `http://192.168.1.43:1880/sim/` |
| **Veld — gsm/tablet** | WiFi `MagnumOpus` | `192.168.50.1` | `http://192.168.50.1:1880/dashboard` | `http://192.168.50.1:1880/sim/` |
| **Veld — laptop** | ethernetkabel rechtstreeks in de Pi | `192.168.51.1` | `http://192.168.51.1:1880/dashboard` | `http://192.168.51.1:1880/sim/` |

De Node-RED **editor** (flows) staat telkens op hetzelfde adres zonder `/dashboard`:
`http://<pi-adres>:1880/`.

⚠️ **Twee klassieke instinkers:**
- **Altijd `http://` typen, nooit `https://`.** Browsers "upgraden" een adres stiekem naar https;
  Node-RED spreekt geen TLS en dan lijkt de hub dood terwijl alles draait.
- **Verwar `192.168.50.1` (veld-AP) niet met `192.168.1.50`** — dat laatste adres bestaat niet.
  Thuis is het `192.168.1.43`.

💡 Op desktop/laptop werkt thuis ook **`http://raspberrypinic.local:1880/...`** (mDNS) — dat
blijft kloppen zelfs als de router het IP ooit verandert (het thuis-IP is DHCP, geen garantie).

> **Waarom dit altijd werkt:** Node-RED verbindt met de MQTT-broker via `host.docker.internal`
> (docker host-gateway, zie `pi/node-red/docker-compose.yml`) — niet via een hardcoded IP. De
> broker (Mosquitto) luistert op alle interfaces (1883 TCP + 9001 WebSocket). De simulator vult
> zijn broker-adres automatisch in met het adres waarmee je de pagina opende. Er is dus nergens
> nog een adres dat "toevallig moet kloppen".

## Veld: gsm op het `MagnumOpus`-WiFi

1. WiFi → netwerk **`MagnumOpus`** → wachtwoord **`scoutskamp`**.
2. De melding **"geen internet"** is normaal en géén probleem — het is een lokaal netwerk.
   Zet zo nodig "toch verbonden blijven" aan (Android vraagt dit soms).
3. Browser → **`http://192.168.50.1:1880/dashboard`**.

Technisch: het AP is een **NetworkManager**-profiel `MagnumOpus-AP` op `wlan0`
(WPA2-PSK, 2,4 GHz, kanaal 6, `ipv4.method shared` = de Pi deelt zelf adressen uit).
Er draait **géén** hostapd/dnsmasq — oudere docs die dat vermelden zijn achterhaald.

## Veld: laptop via ethernetkabel

1. Prik een gewone netwerkkabel **rechtstreeks** tussen laptop en Pi (`eth0` is op het veld vrij).
2. Wacht even: bij het ontbreken van een thuisrouter valt de Pi (na ~30–45 s, eenmalig per boot)
   terug op het profiel **`Veld-eth`** en deelt hij zelf adressen uit op de kabel.
3. De laptop krijgt automatisch `192.168.51.x` (controle: `ipconfig` → gateway `192.168.51.1`).
4. Browser → **`http://192.168.51.1:1880/dashboard`** en **`http://192.168.51.1:1880/sim/`**.

De laptop-WiFi mag tegelijk uit of aan staan; kabel en gsm-op-AP werken **gelijktijdig**
(MQTT is multi-client — dashboard op de gsm en simulator op de laptop bijten elkaar niet).

## Simulator

- Open **`http://<pi-adres>:1880/sim/`** (geserveerd door Node-RED via `httpStatic`;
  bron: `pi/simulator/`, read-only gemount — zie `docker-compose.yml` + `settings.js`).
- Het **Broker**-veld staat automatisch juist (= het adres in je adresbalk); druk **Connect**.
- De MQTT-library is **lokaal gebundeld** (`pi/simulator/vendor/mqtt.min.js`) — geen internet/CDN
  nodig. `index.html` los openen via `file://` kan ook nog; de broker-fallback is dan `192.168.50.1`.

## Eenmalige Pi-configuratie (naslag)

Deze commando's zijn eenmalig op de Pi uitgevoerd; hier ter referentie/herstel:

```bash
# Thuisprofiel: snel opgeven als er geen router antwoordt (anders blijft NM eeuwig DHCP proberen)
sudo nmcli connection modify "Wired connection 1" connection.autoconnect-priority 10 \
     ipv4.dhcp-timeout 15 connection.autoconnect-retries 2

# Veldprofiel: Pi deelt zelf adressen uit op de kabel (zoals het AP dat op WiFi doet)
sudo nmcli connection add type ethernet ifname eth0 con-name Veld-eth autoconnect yes \
     connection.autoconnect-priority 0 ipv4.method shared ipv4.addresses 192.168.51.1/24
```

- Thuis wint `Wired connection 1` (DHCP slaagt in seconden) — niets verandert aan de dev-workflow.
- Subnetkeuze: **51** botst bewust niet met het AP (**50**) of thuis (**1**).
- ⚠️ **Caveat:** reageert de thuisrouter ooit >45 s niet terwijl de Pi boot, dan activeert `Veld-eth`
  en wordt de Pi tijdelijk DHCP-server op het thuisnet. Herstel: kabel eruit/erin of
  `sudo nmcli connection up "Wired connection 1"`.

## Probleemoplossing

**Gsm kan niet joinen op `MagnumOpus`**
- Controleer op de Pi: `nmcli device status` → `wlan0` moet `connected` zijn op `MagnumOpus-AP`.
  Zo niet: `sudo nmcli connection up MagnumOpus-AP`.
- AP-config controleren: `nmcli -g 802-11-wireless-security.key-mgmt,802-11-wireless.band connection show "MagnumOpus-AP"` → verwacht `wpa-psk` / `bg`.

**Windows-laptop kan niet joinen op `MagnumOpus` (WiFi)**
- Profiel vergeten: *Instellingen → Wi-Fi → Bekende netwerken beheren → MagnumOpus → Vergeten*
  (of `netsh wlan delete profile name="MagnumOpus"`), daarna opnieuw met `scoutskamp`.
- Lukt het nog niet: handmatig profiel (Netwerkcentrum → "Handmatig verbinding maken"):
  SSID `MagnumOpus`, **WPA2-Personal**, **AES**. En zet "Willekeurige hardware-adressen" uit.
- Adapter moet 2,4 GHz aankunnen/aan hebben staan (Apparaatbeheer → Geavanceerd → Band/Wireless mode).
- De bekabelde route (hierboven) is de betrouwbare fallback voor precies dit scenario.

**Laptop krijgt geen `192.168.51.x` op de kabel**
- Even geduld na het inpluggen (profiel-fallback duurt tot ~45 s na Pi-boot).
- Op de Pi: `nmcli device status` → `eth0` moet `connected` op `Veld-eth` staan.
  Forceer desnoods: `sudo nmcli connection up Veld-eth`.
- Laptop-adapter op "automatisch IP (DHCP)" (standaard) — geen vast IP ingesteld laten staan.

**Pagina laadt niet, netwerk is wél oké**
- `ping <pi-adres>` als sanity-check; let op **http** (niet https) en poort **1880**; Ctrl+F5.

**Alles extreem traag (pagina's doen er 10+ seconden over) of time-outs terwijl ping werkt**
- Dat is de hub zelf (Node-RED/Pi overbelast), geen netwerkprobleem. Triage op de Pi:
  ```bash
  uptime && free -h
  docker stats --no-stream
  docker logs magnum-Opus --tail 40
  ```
- Eerste hulp: `docker restart magnum-Opus` — veilig, de spelstand overleeft dit
  (contextStorage + retained `spel/state`). Sluit ook overtollige dashboard-/simulator-tabs
  (elke tab kost geheugen op de 1 GB-Pi). Komt de traagheid terug → logs bekijken op
  MQTT-reconnect-fouten of een loop; zie ook `hub-noodherstel.md`.
