# Hub-noodherstel — "de hub vervangen in 10 minuten" (H10)

De centrale **Raspberry Pi 4** is één *single point of failure*: er hangen **3 masters** (USB), de
**audio**, het **WiFi-accesspoint**, **Node-RED** én de **MQTT-broker** aan, en dat alles op **één
SD-kaart**. Een corrupte SD-kaart is dé klassieke Pi-faalmodus. Dit document is het **runbook** om op de
speeldag snel te herstellen, plus de **voorbereiding** die dat mogelijk maakt.

> **Goed nieuws vooraf:** de spelstand overleeft een hub-herstart. Node-RED bewaart de global-context
> persistent (`settings.js` → `contextStorage: localfilesystem`) én dumpt elke 30 s een retained
> `spel/state`-snapshot. Zolang je op **dezelfde SD-image / hetzelfde `/data`** herstart, komen scores,
> π-stand, historiek en god-punten vanzelf terug. Het herstel gaat dus vooral over de **hardware/hub**, niet
> over de data.

---

## Voorbereiding (vóór de speeldag, in de kist)

1. **Gekloonde reserve-SD-kaart.** Maak een 1-op-1 kloon van de werkende SD (bv. met Win32DiskImager of
   `dd`). Bewaar ze in de kist. **Her-kloon telkens** nadat je iets structureels wijzigt (containers,
   `.env`, settings). Zo start de reserve met exact dezelfde flows, credentials en context.
   - *Tip:* een tweede, identiek geflashte SD is de snelste "plan B" — kaart wisselen en booten.
2. **Voeding via een 1000 Wh power station (UPS-achtig).** De Pi hangt aan zijn gewone Raspberry Pi-lader,
   die zelf op een **1000 Wh power station** draait. Dat grote buffervermogen overbrugt een netstroomdip
   ruim (de Pi reboot dan **niet**) en houdt de hub de hele dag draaiende — en mocht de Pi tóch herstarten,
   dan herstelt de persistente state de spelstand. Reken op een stevige 5 V/3 A-uitgang voor Pi +
   randapparatuur.
3. **Optioneel: kant-en-klare reserve-Pi.** Een tweede Pi 4 met de gekloonde SD er al in, zodat je enkel de
   USB's + audio + voeding hoeft over te prikken.
4. **Label alles.** Markeer welke USB-poort welke master is (al is de bridge poort-onafhankelijk — zie
   `docs/handleidingen/serial-bridge.md`), en welke jack de audio is. Scheelt zoeken onder stress.

---

## Symptomen (wanneer grijp je in?)

- Dashboard (`http://192.168.1.43:1880`) onbereikbaar of leeg; geen detecties meer op de radar.
- Geen audio-afroepen meer; LED's reageren niet op events.
- Pi niet pingbaar (`ping 192.168.1.43`), of de SD-activiteit-LED knippert niet meer normaal.
- In de logs: filesystem read-only / I/O errors (klassiek SD-corruptie-symptoom).

---

## Runbook — hub vervangen (~10 min)

1. **Voeding los** van de Pi (of gebruik de reserve-Pi die al gevoed is).
2. **SD wisselen** (of reserve-Pi pakken): steek de **gekloonde** SD in.
3. **Randapparatuur terugkoppelen:**
   - de **3 masters** op de USB-poorten (volgorde maakt niet uit — de bridge detecteert elke CH340 zelf en
     routeert op `paal_id`; zie `docs/handleidingen/serial-bridge.md`);
   - de **audio-jack** (aux) op de audio-uitgang;
   - het **WiFi-accesspoint** actief (op **kanaal 6 of 11**, weg van ESP-NOW's kanaal 1 — zie H6 in
     `docs/hardware/hardware-info.md`).
4. **Booten** en ~1–2 min wachten tot de containers op zijn.
5. **Controleren dat alles draait** (SSH/PuTTY naar de Pi):
   ```bash
   docker ps          # verwacht: mosquitto, magnum-Opus (node-red), serial-bridge, audio-player = Up
   docker logs serial-bridge --tail 20   # "open poorten: [...]" en routes geleerd?
   ```
6. **Dashboard openen** (`http://192.168.1.43:1880`, hard refresh Ctrl+F5). Controleer de pagina
   **Spelstatus**: pre-flight moet naar **GO** gaan zodra de palen data sturen. Scores/π-stand/historiek
   horen **terug** te zijn (persistente state).
7. **Alleen indien nodig** (flows niet actueel op de reserve-SD): op de Pi
   ```bash
   cd ~/Magnum_Opus && git pull && ./pi/node-red/deploy-flows.sh
   ```
   en eventueel `./pi/deploy.sh` (serial-bridge) / `./pi/deploy-audio.sh` (audio).

---

## Na herstel — checklist

- **Palen leven weer:** Spelstatus toont de palen op OK (niet GEEN CONTACT/VEROUDERD).
- **Kanaal (H6):** het Pi-AP staat op **6/11**; doe zo nodig een korte kanaalscan.
- **Batterijen (ST-005):** check of geen paal een "vervang batterij"-waarschuwing geeft.
- **Bakens (H8):** de baken↔speler-koppelingen staan retained op `config/spelers` en komen vanzelf terug;
  controleer de group "Spelers / bakens beheren".
- **Geheugen (L4):** `free -h` en `docker stats --no-stream` — let op de node-red-container-RSS. Bij twijfel:
  `dmesg | grep -i oom` (OOM-kills?). De global-caps (`spelHistorie` ≤30, `pofSnapshots` diepte 10, `skills`
  ≤50) houden het geheugen in toom; houd het aantal open dashboard-/simulator-tabs beperkt. Zie
  `docs/hardware/hardware-info.md` ("Geheugen").
- **Re-kloon de SD** als je tijdens het herstel iets gewijzigd hebt, zodat je reserve weer actueel is.

---

## Zie ook

- `docs/hardware/hardware-info.md` — aandachtspunten/rev-B, waaronder H10 (deze SPOF) en H6 (WiFi-kanaal).
- `pi/node-red/DEPLOY.md` — persistente spelstate (`contextStorage`) + de `/data`-bind-mount-migratie.
- `docs/handleidingen/serial-bridge.md` — masters automatisch detecteren/routeren.
- `docs/versions.md` — exacte OS-/container-/versie-snapshot van de hub.
