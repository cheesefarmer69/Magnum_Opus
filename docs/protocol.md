# Communicatieprotocol Magnum Opus

Dit document beschrijft alle datastromen tussen componenten in het systeem.

**Belangrijke regel:** bij wijzigingen — pas EERST dit document aan, dan pas de code.
Dit voorkomt dat componenten uit sync raken.

## Overzicht

```
[Slaves (ESP32-C3)] --ESP-NOW--> [Master (ESP32 WROOM)] --Serial USB--> [Pi: bridge.py] --MQTT--> [Node-RED]
                                                                              ^                       |
[Slaves (ESP32-C3)] <--ESP-NOW-- [Master (ESP32 WROOM)] <--Serial USB-- [Pi: bridge.py] <--MQTT-------+
```

## 0. ESP-NOW wire-format v2 — berichttypes

> **Protocol v2.** Alle ESP-NOW-berichten beginnen met een **discriminator-byte `msg_type`**; het
> berichttype wordt **op type** onderscheiden, niet meer impliciet op lengte. Alle structs zijn
> `__attribute__((packed))` aan beide kanten (Xtensa WROOM ↔ RISC-V C3).

| `msg_type` | Constante | Richting | Struct | Grootte |
|-----------:|-----------|----------|--------|--------:|
| `0x01` | `MSG_BATCH`     | slave → master | `batch_message_v2` | **variabel**: 5 + `aantal`×7 B (5–215) |
| `0x02` | `MSG_COMMANDO`  | master → slave | `commando_message_v2` | 5 B |
| `0x03` | `MSG_CMD_ACK`   | slave → master (ná uitvoering) | `cmd_ack_message` | 5 B |
| `0x04` | `MSG_HEARTBEAT` | slave → master (periodiek) | `heartbeat_message` | 9 B |
| `0x05` | `MSG_FOUT`      | slave → master | `fout_message` | 8 B |
| `0x06` | `MSG_KNOP`      | slave → master (bij druk) | `knop_message` | 4 B |
| `0x07` | `MSG_BUZZER_TOON` | master → slave (buzzer-tuning) | `buzzer_toon_message` | 4 B |
| `0x08` | `MSG_KLOKSLAG`  | master → slave (Klokslag-LED) | `klokslag_message` | 7 B |
| `0x09` | `MSG_SCAN_CONFIG` | master → slave (BLE-scan-duur) | `scan_config_message` | 4 B |
| `0x0A` | `MSG_LED_CONFIG` | master → slave (LED-helderheid) | `led_config_message` | 3 B |
| `0x0B` | `MSG_BOM`       | master → slave (bom-animatie, minigame) | `bom_message` | 15 B (v2; 10 B-v1 wordt nog geaccepteerd) |

```cpp
#define MSG_BATCH        0x01
#define MSG_COMMANDO     0x02
#define MSG_CMD_ACK      0x03
#define MSG_HEARTBEAT    0x04
#define MSG_FOUT         0x05
#define MSG_KNOP         0x06
#define MSG_BUZZER_TOON  0x07
#define MSG_KLOKSLAG     0x08
#define MSG_SCAN_CONFIG  0x09
#define MSG_LED_CONFIG   0x0A
#define MSG_BOM          0x0B

typedef struct __attribute__((packed)) {        // 0x01 — slave → master
  uint8_t  msg_type;        // = MSG_BATCH
  uint8_t  paal_id;         // 1..24
  uint8_t  aantal;          // aantal spelers in deze batch (0..30)
  uint16_t batt_mv;         // batterijspanning in mV (0 = niet gemeten)
  struct {
    uint8_t mac[6];         // binair MAC-adres (big-endian, zoals weergegeven)
    int8_t  rssi;           // dBm, bereik ~ -30..-90
  } spelers[30];            // 7 B/speler -> max 5 + 30*7 = 215 B <= 250 B ESP-NOW-limiet
} batch_message_v2;
static_assert(sizeof(batch_message_v2) <= 250, "batch_message_v2 te groot voor ESP-NOW");
// VERZONDEN LENGTE IS VARIABEL: de slave stuurt enkel het gebruikte deel —
// offsetof(spelers) + aantal*7 = 5 + aantal*7 bytes (5 bij 0 spelers, 215 bij 30).
// Korte frames = minder airtime = robuuster op de C3 (BLE/WiFi-coexistence).
// Master-validatie: len >= 5, daarna aantal (byte 2) <= 30, daarna len >= 5 + aantal*7.

typedef struct __attribute__((packed)) {        // 0x02 — master → slave
  uint8_t  msg_type;        // = MSG_COMMANDO
  uint8_t  paal_id;
  uint8_t  actie_id;        // zie actie-tabel §2
  uint16_t cmd_seq;         // volgnummer; retries hergebruiken hetzelfde nummer
} commando_message_v2;

typedef struct __attribute__((packed)) {        // 0x03 — slave → master, NÁ uitvoering
  uint8_t  msg_type;        // = MSG_CMD_ACK
  uint8_t  paal_id;
  uint16_t cmd_seq;         // echo van het uitgevoerde commando
  uint8_t  status;          // 0 = uitgevoerd, 1 = geweigerd/onbekende actie
} cmd_ack_message;

typedef struct __attribute__((packed)) {        // 0x04 — slave → master, periodiek
  uint8_t  msg_type;        // = MSG_HEARTBEAT
  uint8_t  paal_id;
  uint16_t batt_mv;
  uint32_t uptime_s;
  uint8_t  fw_versie;
} heartbeat_message;

typedef struct __attribute__((packed)) {        // 0x05 — slave → master
  uint8_t  msg_type;        // = MSG_FOUT
  uint8_t  paal_id;
  uint8_t  ernst;           // 0 = info, 1 = waarschuwing, 2 = fout
  uint8_t  foutcode;        // zie foutcode-tabel §3
  uint32_t detail;          // vrij veld (bv. spanning in mV, een teller)
} fout_message;

typedef struct __attribute__((packed)) {        // 0x06 — slave → master, bij knopdruk
  uint8_t  msg_type;        // = MSG_KNOP
  uint8_t  paal_id;
  uint16_t teller;          // cumulatieve druk-teller (kogelvrij: ~6x hervast, laatste waarde telt)
} knop_message;

typedef struct __attribute__((packed)) {        // 0x07 — master → slave, buzzer-tuning
  uint8_t  msg_type;        // = MSG_BUZZER_TOON
  uint8_t  paal_id;         // doel-slave (1..24)
  uint16_t freq_hz;         // 0 = stop (noTone), anders een CONTINUE toon op deze frequentie
} buzzer_toon_message;

typedef struct __attribute__((packed)) {        // 0x08 — master → slave, Klokslag-LED
  uint8_t  msg_type;        // = MSG_KLOKSLAG
  uint8_t  paal_id;         // doel-slave (1..24)
  uint8_t  r, g, b;         // teamkleur (controller/eigenaar)
  uint8_t  helderheid;      // 0..255 — de engine schaalt al met de voortgang P/H
  uint8_t  modus;           // 0=owned/solid, 1=capturing/flikker, 2=frozen, 3=rust-ademend
} klokslag_message;

typedef struct __attribute__((packed)) {        // 0x09 — master → slave, BLE-scan-config
  uint8_t  msg_type;        // = MSG_SCAN_CONFIG
  uint8_t  paal_id;         // doel-slave (1..24)
  uint16_t scan_ms;         // gewenste BLE-scan-vensterduur in ms (slave clamp't 300..2000)
} scan_config_message;

typedef struct __attribute__((packed)) {        // 0x0A — master → slave, LED-helderheid
  uint8_t  msg_type;        // = MSG_LED_CONFIG
  uint8_t  paal_id;         // doel-slave (1..24)
  uint8_t  helderheid;      // gewenste FastLED-helderheid 0..255 (slave clamp't 5..255)
} led_config_message;

typedef struct __attribute__((packed)) {        // 0x0B — master → slave, bom-animatie (minigame)
  uint8_t  msg_type;        // = MSG_BOM
  uint8_t  paal_id;         // doel-slave (1..24)
  uint16_t laad_ms;         // oplaad-ramp 0 -> max (bereikt 255 exact bij t==laad_ms)
  uint16_t hold_ms;         // vasthouden op max vóór het knipperen
  uint16_t pink_ms;         // knipperduur op zijn felst; daarna dooft de LED (= ontploft)
  uint16_t pink_hz;         // knipperfrequentie (bv. 2)
  int32_t  wacht_ms;        // v2: ms tot vuren (SIGNED — negatief = "was X ms geleden verschuldigd",
                            //     laat een láát bezorgde resend het anker in het verleden leggen);
                            //     0 = direct vuren (v1-gedrag)
  uint8_t  seq;             // v2: per-slave dedupe-teller van de master (slaat 0 over);
                            //     0 = geen dedupe (v1-frame)
} bom_message;              // 15 B; een 10 B-v1-frame wordt geaccepteerd (v2-velden nul-aangevuld)
```

> **`MSG_KLOKSLAG` (Klokslag-minigame).** De Klokslag-engine kleurt elke paal in de **teamkleur**
> van de controller/eigenaar met een **helderheid die met de inname-voortgang (`P/H`) meeschaalt** en
> een **kaarsflikker** tijdens het innemen — dat past niet in de vaste `actie_id`-kleurenset van
> `commando_message_v2`. Daarom een eigen berichttype met `r/g/b/helderheid/modus`. Net als
> `MSG_BUZZER_TOON` loopt het **niet** via de commando-FIFO/ACK: het is fire-and-forget; de slave
> rendert continu (`updateAnimatie()`) tot een volgend `MSG_KLOKSLAG` of een gewone `MSG_COMMANDO`
> binnenkomt. JSON van Node-RED: `{"paal":N,"actie":16,"r":R,"g":G,"b":B,"helderheid":H,"modus":M}`
> (de master vertaalt actie 16 naar `MSG_KLOKSLAG`, zoals actie 12 → `MSG_BUZZER_TOON`).

> **`MSG_BOM` (minigame "Bommen vermijden").** Een paal-LED gloeit **vloeiend rood op** (oplaad-ramp
> `laad_ms`), houdt zijn **felst** even vast (`hold_ms`), **knippert** dan op zijn felst (`pink_ms` @
> `pink_hz`), en **dooft** (= ontploft). De ramp bereikt 255 **exact** bij `t==laad_ms`, dus de piek
> valt structureel vóór het knipperen. De slave rendert dit **lokaal** (`updateAnimatie()`), zodat de
> animatie vloeiend blijft ongeacht RF-jitter; enkel de trigger + de tijden komen over de lucht.
> JSON van Node-RED: `{"paal":N,"actie":25,"laad_ms":..,"hold_ms":..,"pink_ms":..,"pink_hz":..,"wacht_ms":W}`
> (de master vertaalt actie 25 naar `MSG_BOM`). De **scoring** (−10 levensuren voor wie bij het doven
> op de paal staat) gebeurt in Node-RED op de ontplof-tijd, niet in de firmware.
>
> **Geplande bommen (v2, juli 2026) — beat-vast ondanks RF-jitter.** Met `wacht_ms > 0` stuurt
> Node-RED de cue **vooraf** (LEAD ~1,2 s) en wordt de bezorging losgekoppeld van de timing:
> - **Master**: actie 25 met `wacht_ms>0` gaat níét blind de lucht in maar in een **per-slave
>   bom-wachtrij** (diepte 4, loop-task-privé — géén lock nodig; enqueue via `verwerkRegel`,
>   verzenden via `verwerkBomQueue()`). Herzenden gebeurt **phase-locked** (in het vrije
>   radio-venster na een batch/heartbeat, spacing 40 ms) plus een blinde 150 ms-cadans, telkens
>   met een **vers herberekende signed `wacht_ms = uitvoerOp − nu`**; een entry vervalt op
>   `uitvoerOp + laad+hold+pink` of na 30 pogingen (`{"status":"bom_verlopen"}`). Duplicaat-
>   bezorging is onschadelijk (seq-dedupe op de slave) — bewust géén ACK-machinerie.
> - **Slave**: zet het bericht in een klein **schema** (`bomSchedule[4]`) met `dueMs =
>   millis() + wacht_ms` en vuurt zodra due, met **`actieStartMs = dueMs`** als anker: een te
>   laat bezorgde cue kort automatisch zijn ramp in en het **doofmoment blijft op de geplande
>   tijd** (de beat). Bij meerdere due-slots tegelijk vuurt enkel de laatste (replace-on-fire).
>   Dedupe is **time-boxed** (`seq` onthouden tot due+anim+3 s; `seq 0` = v1 = geen dedupe).
>   **Actie 0** (`ACTIE_NIETS`) wist schema + pending ring (én de master-wachtrij van die slave).
> - `wacht_ms == 0` (of een 10 B-v1-frame) = het oude gedrag: direct blind verzenden / direct vuren.
>
> **Show-gate-uitzondering (S3b).** Tijdens de BLE-scan onderdrukt de slave normaal `FastLED.show()`
> (S3, RMT-corruptierisico) — maar voor **`ACTIE_BOM` en `ACTIE_KLOKSLAG`** (beide continu
> her-getekende animaties; een zeldzaam corrupt frame heelt in één tick) wordt er wél gelatcht,
> anders bevriest de ramp ~50 % van de tijd en latcht het doofmoment tot een scan-duur te laat.
> Revert-switch: build-flag `BOM_SHOW_TIJDENS_SCAN=0` in `firmware/Slave/platformio.ini`.

> **`MSG_BUZZER_TOON` (buzzer-tuning).** Een passieve piezo is het luidst rond zijn
> eigen resonantiefrequentie; die verschilt per bordje (productiespreiding). Dit
> bericht laat de master een **continue** toon op een willekeurige frequentie
> starten/stoppen zodat je per paal de luidste toon kunt zoeken (zie het Node-RED
> dashboard "Buzzer-tuning"). Het loopt **niet** via de commando-FIFO/ACK: het is
> een fire-and-forget tuning-hulpmiddel (de toon houdt op de slave aan tot een
> volgende `MSG_BUZZER_TOON` binnenkomt). De bestaande `commando_message_v2` heeft
> geen parameter-veld, daarom een eigen berichttype met `freq_hz`.

> **`MSG_SCAN_CONFIG` (BLE-scan-duur).** De BLE-scan-vensterduur is runtime instelbaar (versere
> detectie ⇒ minder scoring-latentie). NimBLE 1.4.2 kan enkel in hele seconden blokkeren, dus scant
> de slave niet-blokkerend en begrenst hij het venster zelf met `millis()`. Dit bericht draagt de
> gewenste duur in ms; de slave clamp't ze naar **300..2000 ms** en past ze toe bij de volgende scan.
> Net als `MSG_BUZZER_TOON` loopt het **niet** via de commando-FIFO/ACK (fire-and-forget). JSON van
> Node-RED: `{"paal":N,"actie":20,"scan_ms":M}` (de master vertaalt actie 20 naar `MSG_SCAN_CONFIG`,
> zoals actie 12 → `MSG_BUZZER_TOON`).

### Dispatch + lengte-validatie

- **Master `OnDataRecv`**: check `len ≥ 1`, dan `switch (incomingData[0])`. Valideer per type de **exacte
  lengte** (`len >= sizeof(<struct>)`) vóór `memcpy`. Onbekend `msg_type` of verkeerde lengte → log + drop.
  De sender-MAC-gate (`vindSlaveIndex` tegen `slaveAdressen[]`) blijft als eerste filter staan.
- **Slave `OnDataRecv`**: accepteert `MSG_COMMANDO` (`incomingData[0] == 0x02` + lengtecheck),
  `MSG_BUZZER_TOON` (`0x07`, buzzer-tuning), `MSG_KLOKSLAG` (`0x08`, Klokslag-LED), `MSG_SCAN_CONFIG`
  (`0x09`, scan-duur) en `MSG_BOM` (`0x0B`); al het andere wordt genegeerd. Buzzer-toon, Klokslag en
  scan-config zetten enkel `volatile` doelwaarden (geen `tone()`/`FastLED.show()` in de WiFi-callback);
  de loop past ze toe (`verwerkTestToon()`/`verwerkKlokslag()` / de scan-lus die `scanDuurMs` snapshot).
  **Lengte-uitzondering `MSG_BOM`**: geaccepteerd vanaf `len ≥ 10` (= v1-grootte,
  `offsetof(bom_message, wacht_ms)`); een korter v1-frame wordt nul-aangevuld (`wacht_ms 0`, `seq 0`)
  en gedraagt zich als vanouds. MSG_BOM gaat in een eigen SPSC-ring (4 slots) → `verwerkBom()`.

### Applicatie-ACK (afronding ná uitvoering)

Een commando geldt pas als **afgeleverd** wanneer de slave het **heeft uitgevoerd** en een `MSG_CMD_ACK`
terugstuurt — niet bij de MAC-laag-ACK van de radio (die zegt enkel "pakket ontvangen door de radio").

- De slave stuurt `MSG_CMD_ACK` **ná** `voerActieUit()`, met `status` 0 (uitgevoerd) of 1 (onbekende actie).
- **Idempotent**: de slave onthoudt het laatst uitgevoerde `cmd_seq`. Komt hetzelfde `cmd_seq` opnieuw binnen
  (master deed een retry omdat de ACK verloren ging), dan voert de slave **niet opnieuw uit** maar stuurt hij
  **wél opnieuw een ACK**.
- **Sentinels (L5)**: de slave boot met `laatsteUitgevoerdeSeq = 0xFFFF` ("nog niets uitgevoerd"); de
  master-teller slaat daarom **0 én 0xFFFF** over bij het uitdelen — een vers geboote slave kan zo nooit een
  echt commando als "al uitgevoerd" wegfilteren.
- De master koppelt de ACK aan het **head-item** van de per-slave FIFO via `cmd_seq` en popt het dan.
  Een **dubbele ACK** (her-ACK, zie onder) op een al-gepopte seq matcht niets en is onschadelijk
  (enkel een `[ACK] … stale`-logregel).
- **Timing (niet-blokkerende scan)**: de slave draineert zijn commando-ring óók tijdens de BLE-scan
  (elke ~5 ms) en voert een ontvangen commando dus vrijwel direct uit. De bottleneck is de **RF-capture**:
  tijdens de scan is de C3-radio ~80 % met BLE bezet (window 64 / interval 80), zodat het betrouwbare
  ESP-NOW-ontvangstvenster de ~250–400 ms ná de scan is. De werkelijke ACK-round-trip bij een geslaagde
  capture is **10–250 ms**.
- **Phase-locked retry (master)**: elke `MSG_BATCH`/`MSG_HEARTBEAT` markeert het begin van het vrije
  radio-venster van die slave (`slaveVensterVlag`). Een pending commando wordt dan **direct** herzonden
  (guard 50 ms tegen batch+heartbeat-dubbeltrigger) — de resend landt zo vrijwel altijd in het
  luistervenster. `APP_ACK_TIMEOUT` (**600 ms** ≈ halve slave-cyclus) is enkel nog de blinde fallback
  (batch richting master verloren / dode paal); **niet terugtunen naar ~1500 ms** — die oude waarde was
  gekalibreerd op de verdwenen blokkerende scan. Retries hergebruiken hetzelfde `cmd_seq`; na
  `MAX_POGINGEN` (**6**) volgt `opgegeven` (dode paal: ~3,6 s).
- **Her-ACK ná de scan (slave)**: een ACK die mid-scan verzonden werd (coex-risico) wordt éénmalig
  herhaald direct na `pBLEScan->stop()`, in schone lucht — dat dicht het "uitgevoerd maar master weet
  het niet"-gat zonder heruitvoering (idempotent op `cmd_seq`).

## 1. Slave → Master (ESP-NOW)

Slave detecteert BLE-beacons van spelers en stuurt batches naar de master.
Het systeem heeft **3 masters**, elk met **8 slaves** (palen). Totaal 24 palen.

### Datastruct (C++)

De batch is `batch_message_v2` (`msg_type = MSG_BATCH`, zie §0): **binaire** MAC's (6 B) en `int8 rssi`
i.p.v. een 18-byte string + `int32`, en `uint16 batt_mv` i.p.v. `float`. Daardoor passen **30 spelers**
in één pakket (max 215 B ≤ 250 B), tegen 9 in v1. De slave verstuurt **alleen het gebruikte deel**
(`5 + aantal×7` bytes; 5 B bij een leeg vak): kortere frames = minder airtime = betrouwbaarder op de
single-antenne C3 direct na de BLE-scan.

```cpp
typedef struct __attribute__((packed)) {
  uint8_t  msg_type;        // = MSG_BATCH (0x01)
  uint8_t  paal_id;         // 1..24 (uniek per slave, hardcoded)
  uint8_t  aantal;          // aantal gedetecteerde spelers in deze batch (0..30)
  uint16_t batt_mv;         // batterijspanning in mV (0 = niet gemeten)
  struct {
    uint8_t mac[6];         // binair MAC-adres (big-endian, zoals weergegeven)
    int8_t  rssi;           // dBm, bereik ~ -30..-90
  } spelers[30];
} batch_message_v2;
```

**Belangrijk:**
- `__attribute__((packed))` aan beide kanten — voorkomt alignment-issues
  tussen Xtensa (WROOM, master) en RISC-V (C3, slave).
- MAC's zijn nu **binair** (6 bytes). De master vertaalt ze terug naar de string-vorm
  `"aa:bb:cc:dd:ee:ff"` (lowercase) in de serial-JSON naar de Pi, zodat de Node-RED-flows ongewijzigd blijven.
- `spelers[30]` is een vaste array; `aantal` geeft het aantal geldige slots aan.
- `batt_mv` wordt elke batch meegestuurd (mV; `0` = niet gemeten). De master deelt door 1000 voor de
  `{"batt":3.87}`-regel.

### Whitelist (v2): OUI-prefix + RSSI-drempel

In v1 stond een **hardcoded lijst van beacon-MAC's** in elke slave — elke beacon-wissel vereiste 24 palen
herflashen. In v2 filtert de slave op **OUI-prefix** (de eerste 3 MAC-bytes van de beacons, `48:87:2d`)
**én** een **RSSI-drempel** (`rssi ≥ RSSI_DREMPEL`, bv. −85 dBm). Voordelen: geen herflash bij beacon-wissel
(zolang dezelfde fabrikant-OUI), en omstanders/telefoons (andere OUI, of te zwak) vallen weg. De ruime
30-slot array vangt resterende ruis op. Bij **>30** gedetecteerde spelers in één vak wordt niet meer stil
gedropt maar een teller bijgehouden en een `MSG_FOUT` (foutcode "BLE-overflow") gestuurd.

### Frequentie en timing

- Slave scant BLE met scanInterval/scanWindow afgestemd op antenne-coexistence
  (single-antenne C3 moet schakelen tussen ESP-NOW en BLE). Window 64 / interval 80
  (~80% duty, units 0.625 ms) zodat een kort scan-venster bij 300 ms-adverterende beacons
  toch genoeg samples ziet.
- **Scan-vensterduur is runtime instelbaar** (`MSG_SCAN_CONFIG`, actie 20): niet-blokkerende
  scan begrensd door een `millis()`-venster (NimBLE 1.4.2 blokkeert enkel in hele seconden).
  Kortere scans = versere detectie = minder scoring-latentie. Default 1000 ms, clamp 300..2000 ms.
- Batch wordt **elke** scan-cyclus verzonden, óók bij 0 gevonden spelers.
  Zo herkent het systeem een leeggelopen vak en blijft de stand niet hangen.
- **Dedup binnen een batch**: een beacon adverteert meerdere keren per seconde,
  maar elke MAC die het OUI/RSSI-filter passeert komt maximaal één keer voor in
  `spelers[]`. Bij meerdere advertenties van dezelfde beacon binnen één scan houdt
  de slave de sterkste RSSI. Zonder deze dedup zou `spelers[30]` volstromen met
  duplicaten en zou de master tientallen JSON-regels per seconde doorsturen voor één paal.
- Vóór verzenden wacht de slave een willekeurige tijd (`0..MAX_BACKOFF_MS`,
  standaard 150 ms, hardware-RNG `esp_random()`). Deze random backoff
  ontkoppelt de zendmomenten van meerdere slaves zodat hun pakketten elkaar
  niet structureel wegdrukken bij de master.

## 2. Master → Slave (ESP-NOW)

Master stuurt commando's naar specifieke slaves voor uitvoer (LED, geluid, etc.).

### Datastruct (C++)

```cpp
typedef struct __attribute__((packed)) {
  uint8_t  msg_type;   // = MSG_COMMANDO (0x02)
  uint8_t  paal_id;    // doel-slave (1..24)
  uint8_t  actie_id;   // wat te doen (zie tabel hieronder)
  uint16_t cmd_seq;    // volgnummer; de slave echo't dit in MSG_CMD_ACK
} commando_message_v2;
```

`cmd_seq` wordt door de **master** toegekend (de Pi/Node-RED stuurt enkel `{"paal","actie"}`); retries van
hetzelfde commando hergebruiken hetzelfde `cmd_seq` zodat de slave dubbel-uitvoeren kan detecteren (§0,
"Applicatie-ACK"). De slave bevestigt met `cmd_ack_message` (`MSG_CMD_ACK`) **ná** uitvoering.

### Geldige `actie_id` waarden

De actie-set is bewust **minimaal**: enkel acties die aan een spel-event hangen.

| ID | Constante | Gedrag |
|----|-----------|--------|
| 0  | `ACTIE_NIETS`       | LEDs uit (CRGB::Black) |
| 1  | `ACTIE_PORTAAL`     | LED strip **paars** continu (portaal-toestand) |
| 2  | `ACTIE_HAPPY_HOUR`  | LED strip **goud** continu (happy-hour-toestand) |
| 3  | `ACTIE_BUZZER_PIEP` | Eén duidelijke piep, 1500 Hz, 600 ms (niet-blokkend, auto-stop). Gebruikt om een afgeroepen **uur** hoorbaar te maken én als zoemer-test. |
| 4  | `ACTIE_MEDICIJN`    | LED strip **felroze** (`CRGB(255,20,147)`) continu (medicijn-toestand, ziekte-event) |
| 5  | `ACTIE_ZIEK_W3`     | Zoemer: ziekenhuis-monitor-piep + **3** hartslagen (zieke speler, nog 3 events te gaan) |
| 6  | `ACTIE_ZIEK_W2`     | Zoemer: ziekenhuis-monitor-piep + **2** hartslagen (nog 2 events) |
| 7  | `ACTIE_ZIEK_W1`     | Zoemer: ziekenhuis-monitor-piep + **1** hartslag (nog 1 event) |
| 8  | `ACTIE_NUKE`        | LED strip **geanimeerd** NUKE-ring over alle palen, **tijd-gefaseerd** vanaf `actieStartMs`: 0–16 s opbouwend radioactief geel↔groen (ademhaling versnelt richting de knal), 16–19 s de **ontploffing** (witte knal → felle wit/rood-strobe), ≥19 s smeulende rode nagloed. Node-RED houdt de ring op actie 8 tot het einde van de regroup (`nukeNaglow`) zodat de firmware-klok doorloopt en de escalatie zichtbaar is. |
| 9  | `ACTIE_MN_OPEN`     | LED strip **zacht wit** continu (middernacht-poort **open**) |
| 10 | `ACTIE_MN_DICHT`    | LED strip **rood** continu (middernacht-poort **dicht**) |
| 11 | `ACTIE_OOGST`       | LED strip **geanimeerd** dramatische wit/rood-strobe (middernacht-oogst bij een 0 in pi) |
| 12 | `ACTIE_BUZZER_TOON` | Buzzer-tuning: **continue** toon op een instelbare frequentie. **Geen `commando_message_v2`** — de master vertaalt dit naar een `MSG_BUZZER_TOON` (zie §0). Vereist het extra JSON-veld `toon` (Hz; `0` = stop). |
| 13 | `ACTIE_TIJDBOM`     | LED strip **geanimeerd** tikkende rode flits (~2 Hz). Gezet op de **ontmantel-palen** van een actief tijdbom-event (paal met drukknop waar een bom ontmanteld kan worden). De paal wordt tegelijk **gearmd** met `ACTIE_KNOP_ARM` (17) — zonder dat negeert de slave elke druk. |
| 14 | `ACTIE_TORNADO`     | LED strip **donkergrijs** continu (tornado-center; zuigt de aanliggende uren naar zich toe). Overschrijft tijdelijk een onderliggend effect; herstelt na het event. |
| 15 | `ACTIE_TORNADO_RAND`| LED strip **geanimeerd** trage grijze pulse (aanliggend uur van een tornado). |
| 16 | `ACTIE_KLOKSLAG`    | Klokslag-LED: **teamkleur** continu/flikker/ademend op een meeschalende helderheid. **Geen `commando_message_v2`** — de master vertaalt dit naar `MSG_KLOKSLAG` (zie §0). Vereist de extra JSON-velden `r`,`g`,`b`,`helderheid`,`modus` (`0` owned = **solid**, `1` capturing = kaarsflikker, `2` frozen = solid, `3` rust = ademend dim wit).<br>**Dubbel gebruik:** met `modus: 0` is dit in feite een **generiek "zet paal op een willekeurige RGB-kleur"**-commando. Het dashboard **Buzzer/LED test → Paaltest** gebruikt het zo voor de kleurtest (5 vaste kleuren), zonder dat daar een aparte actie voor nodig was. Voeg dus **geen** nieuwe "zet-kleur"-actie toe — die bestaat al.<br>Node-RED gebruikt `modus: 0` ook voor korte **event-flitsen**: **blauw** bij een geslaagde tijdbom-ontmanteling, een **witte** puls op de even/oneven-palen bij een pariteit-verplaatsing, en (met `modus: 1`) een **oranje kaarsflikker** op de twee body-swap-palen. Deze flitsen hebben **géén** firmware-timeout, dus Node-RED dooft ze zelf met een `ACTIE_NIETS` of een force-rebuild. |
| 17 | `ACTIE_KNOP_ARM`    | Drukknop-paal **actief** zetten: GPIO6-feedback-LED **aan** (uit zolang de knop ingedrukt is) en de cumulatieve druk-teller op 0. Raakt de WS2812B-strip niet. |
| 18 | `ACTIE_KNOP_UIT`    | Drukknop-paal **inactief** zetten: GPIO6-LED **uit**, geen tellen meer. |
| 19 | `ACTIE_REGENBOOG`   | **Test**: roterende regenboog over de 7 LEDs (volledig spectrum, deltaHue 255/7). Puur voor kleur-/LED-controle; via de losse inject `[TEST] Regenboog (paal 1, actie 19)` op de "00 Configuratie"-flow (zelf naar een `commando/masterN` mqtt-out bedraden). Blijft tekenen tot een andere actie binnenkomt (bv. `ACTIE_NIETS`). |
| 20 | `ACTIE_SCAN_CONFIG` | BLE-scan-vensterduur (ms) instellen. **Geen `commando_message_v2`** — de master vertaalt dit naar `MSG_SCAN_CONFIG` (zie §0). Vereist het extra JSON-veld `scan_ms` (ms; slave clamp't 300..2000). Fire-and-forget (geen FIFO/ACK). Raakt de LED-strip niet. |
| 21 | `ACTIE_LED_CONFIG` | LED-helderheid (globale FastLED-brightness) instellen. **Geen `commando_message_v2`** — de master vertaalt dit naar `MSG_LED_CONFIG`. Vereist het extra JSON-veld `helderheid` (0..255; slave clamp't 5..255). Fire-and-forget (geen FIFO/ACK). Componeert met Klokslag/animatie-kleuren (die schalen per-LED). |
| 22 | `ACTIE_KNOP_GOED` | Knop-feedback **positief**: korte **groene** flits over de 7 LEDs (~800 ms, dan auto-terug naar `ACTIE_NIETS`) + kort **positief** zoemerdeuntje. Gewone `commando_message_v2` (FIFO/ACK). Voor drukknop-events (goede keuze) en de knoppendans-minigame. |
| 23 | `ACTIE_KNOP_FOUT` | Knop-feedback **negatief**: korte **rode** flits over de 7 LEDs (~800 ms, dan auto-terug naar `ACTIE_NIETS`) + kort **negatief** zoemerdeuntje (dalend). Gewone `commando_message_v2` (FIFO/ACK). Voor drukknop-events (slechte keuze) en de knoppendans-minigame (fout/strike). |
| 24 | `ACTIE_ONTPLOFFING` | **Tijdbom gaat af**: witte knal → uitdovende **rode strobe** (~1,6 s, `ONTPLOF_MS`, dan auto-terug naar `ACTIE_NIETS`) + een **dalende sirene-sweep** (2500 → 300 Hz) met drie lage dreunen. Bewust veel lager/langer dan de korte foute-keuze-flits (23) zodat je van over het veld hoort dat er iemand ontploft is. Gewone `commando_message_v2` (FIFO/ACK). Gestuurd bij een **mislukte ontmanteling** én bij een **afgelopen bom-teller**. |
| 25 | `ACTIE_BOM` | **Bom-animatie** (minigame "Bommen vermijden"): LED gloeit **vloeiend rood op** (`laad_ms`) → houdt zijn **felst** vast (`hold_ms`) → **knippert** op zijn felst (`pink_ms` @ `pink_hz`) → **dooft** (= ontploft). **Geen `commando_message_v2`** — de master vertaalt dit naar `MSG_BOM` (zie §0). JSON-velden: `laad_ms`, `hold_ms`, `pink_ms`, `pink_hz` + optioneel **`wacht_ms`** (>0 = **geplande bom**: master-wachtrij met phase-locked herzendingen, slave vuurt op `dueMs` met `actieStartMs = dueMs` als anker — doofmoment beat-vast; 0/afwezig = direct, fire-and-forget). De slave rendert lokaal (piek altijd vóór het knipperen). De −10-scoring gebeurt in Node-RED op de ontplof-tijd. |

De LED-toestanden (1/2/4/9/10) worden centraal door Node-RED gestuurd ("Sync toestanden + LEDs")
op basis van de actieve effecten/poort-staat; loopt een effect af of stopt het spel, dan stuurt
Node-RED `ACTIE_NIETS`. De zoemer-acties (3/5/6/7) zijn niet-blokkende melodieën op de slave
(state-machine `updateMelodie()`); de hartslag-waarschuwingen (5/6/7) verschillen enkel in het
aantal hartslagen na de monitor-piep. De **geanimeerde** acties (8 = nuke, 11 = oogst) worden op de
slave gerenderd door `updateAnimatie()` (millis-gebaseerd, blijft animeren tot een nieuwe actie binnenkomt).
De LED-voeding/massa-gate (IRLZ44N op GPIO1, low-side) wordt **eenmalig in `setup()` permanent
AAN gezet** en door de acties **niet** geschakeld; "uit" (`ACTIE_NIETS`) is puur `CRGB::Black`.
De WS2812B-bitstream gebruikt de IDF-RMT-driver (`-DFASTLED_RMT_BUILTIN_DRIVER=1`) tegen
RMT-underrun onder WiFi/BLE-belasting (zie `docs/hardware/pinout.md`).

### Reliability: per-slave commando-FIFO + applicatie-ACK

De master verstuurt commando's niet fire-and-forget. Hij houdt **per slave een kleine FIFO** bij
(`cmdPerSlave[AANTAL_SLAVES]`, diepte `CMD_FIFO_DIEPTE` = 4, index = `paal_id − 1`); de FIFO's worden elke
loop-tick **parallel** verwerkt. Het **HEAD-item** is in-flight en draagt zijn eigen `cmd_seq`.

Afronding in v2 gebeurt op de **applicatie-ACK** (`MSG_CMD_ACK`), niet op de MAC-laag-ACK:

- De master verstuurt het head-item als `commando_message_v2` met `esp_now_send()`. `OnDataSent` (MAC-ACK)
  wordt nog enkel gebruikt voor **radio-logging** (`send_err` bij FAIL) en een hint om meteen opnieuw te
  proberen — het rondt het commando **niet** af.
- Het head-item wordt **gepopt** zodra een `MSG_CMD_ACK` met matchend `cmd_seq` binnenkomt. De master meldt
  dan `{"status":"uitgevoerd","paal":N,"seq":S}` (of `"geweigerd"` bij `status == 1`) en stuurt het volgende
  item.
- **Resend is phase-locked**: elke binnenkomende `MSG_BATCH`/`MSG_HEARTBEAT` van een slave markeert
  het begin van diens vrije radio-venster (~250–400 ms post-scan, `slaveVensterVlag`); een pending
  head-item wordt dan **direct** herzonden (guard 50 ms). Blinde fallback: geen ACK binnen
  `APP_ACK_TIMEOUT` (**600 ms**) → resend met **hetzelfde** `cmd_seq`. Na `MAX_POGINGEN` (**6**)
  geeft de master het head-item op met `{"status":"opgegeven",...}` (dode paal: ~3,6 s) en gaat
  verder met het volgende.

Eigenschappen van dit model:

- **Volgorde-behoud (geen laatste-wint).** Twee snel opeenvolgende, verschillende commando's naar dezelfde
  paal (bv. buzzer-piep + portaal) worden **beide** in volgorde afgeleverd — vroeger overschreef het laatste
  het eerste, waardoor bv. de piep wegviel.
- **Phase-locked retry.** Retries synchroniseren op het post-scan-venster van de slave i.p.v. op een
  blinde timer — een resend landt daardoor vrijwel altijd wanneer de slave-radio vrij is. Samen met de
  **her-ACK ná de scan** (slave herhaalt een mid-scan verzonden ACK éénmalig in schone lucht) is de
  typische klik→uitvoering-latentie ~0,4–0,7 s.
- **Geen head-of-line blocking tussen palen.** Een onbereikbare paal doorloopt zijn eigen retry-cyclus
  zonder de commando's naar andere palen te vertragen. (Binnen één paal is de FIFO wel ordelijk: het head-item
  blokkeert tot het ge-ACK't of opgegeven is.)
- **Drop-oudste bij volle FIFO.** Stapelen er meer dan `CMD_FIFO_DIEPTE` commando's op (bv. een dode paal),
  dan wordt het **oudste** gedropt met `{"status":"fifo_vol","paal":N,"gedropt_seq":S}`.
- **Placeholder-palen.** Een commando naar een paal met all-zero MAC wordt direct geweigerd met
  `{"status":"geen_slave"}` — geen nutteloze retry-cyclus.
- **Idempotent.** Door het vaste `cmd_seq` per (re)send voert de slave een herhaald commando niet dubbel uit
  (hij her-ACK't enkel), zodat een verloren ACK geen dubbele uitvoering veroorzaakt.

Dit dicht het afleveringsgat van v1, waar een MAC-ACK al "afgeleverd" betekende terwijl de slave het
commando daarna alsnog kon mislopen.

### Slave: luistervenster + commando-ringbuffer

De slave verwerkt binnenkomende commando's via een **SPSC-ringbuffer** (`CMD_BUF_SLOTS` = 8), niet via één
variabele. `OnDataRecv` is de **producent** (pusht `{actie, cmd_seq}`, dropt de nieuwste bij volle buffer +
`cmdDrops++`); de loop is de **consument** (`verwerkCommandos()`) die de buffer **in volgorde** leegmaakt,
elk commando idempotent uitvoert (op `cmd_seq`) en daarna `MSG_CMD_ACK` stuurt. De consument draait op
meerdere punten per cyclus (ná de BLE-scan, ná het zenden, in en na het 200 ms-luistervenster), zodat een
commando dat binnenkomt tijdens de scan, `voerActieUit()` of de afsluitende `delay()` **nooit verloren gaat
of overschreven wordt**. De drop-teller is zichtbaar in de seriële debug (`[CMD] Ringbuffer-drops: N`).

## 3. Master → Pi (Serial USB)

Master stuurt detecties door naar de Pi, één JSON-bericht per regel.

- **Poort op Pi**: automatisch gedetecteerd (CH340 USB-UART, elke USB-poort).
  De bridge leert welke poort welke master is uit de **identiteits-aankondiging** (`announce`, zie onder)
  en routeert `commando/masterN` daarheen (master1 = palen 1–8, master2 = 9–16, master3 = 17–24). Zie
  `docs/handleidingen/serial-bridge.md`.
- **Baudrate**: 115200
- **Regelafsluiting**: `\n`
- **Encoding**: UTF-8

### Formaat: detectie

```json
{"paal":1,"mac":"aa:bb:cc:dd:ee:ff","rssi":-67}
```

Eén JSON-object per gedetecteerde speler. Bij een batch met 5 spelers
stuurt master 5 aparte regels.

### Formaat: batterij

```json
{"paal":1,"batt":3.87}
```

Eén regel per ontvangen batch — onafhankelijk van of er spelers in zaten.
Zo blijft de batterij-status van een paal up-to-date óók als er niemand
in de buurt is. Node-RED bewaart de laatste waarde per paal in
`global.status_batterijPaal`.

### Formaat: drukknop

```json
{"paal":1,"knop":1,"teller":4}
```

De slave detecteert een **druk op de knop** (GPIO3) via een **interrupt** (werkt ook tijdens de
BLE-scan), houdt een **cumulatieve `teller`** bij en stuurt een `MSG_KNOP`-pakket via **ESP-NOW** naar
de master — **~6 keer hervast** zodat een verloren pakket door het volgende gecorrigeerd wordt
(**kogelvrij tellen**, de laatste `teller`-waarde telt). Node-RED **zet** de dashboard-teller op die
waarde (niet optellen). Tellen + de GPIO6-feedback-LED zijn enkel actief als de paal **gewapend** is
(`ACTIE_KNOP_ARM`). De master vertaalt dat naar de regel hierboven op de Pi.

### Formaat: heartbeat

```json
{"paal":1,"hb":1,"batt":3.87,"uptime":1234,"fw":2}
```

Periodiek "ik leef"-bericht (uit `MSG_HEARTBEAT`), ook zonder spelers in de buurt — interval is een
firmware-constante (`HEARTBEAT_INTERVAL_S`, default 10 s). `uptime` in seconden, `fw` = firmware-versie.
Hiermee kan de pre-flight check in Node-RED echte connectiviteit per paal vaststellen.

### Formaat: identiteits-aankondiging (announce)

```json
{"announce":1,"master":1,"paal_min":1,"paal_max":8}
```

Periodiek door de master verstuurd (elke `ANNOUNCE_INTERVAL_MS`, default 3 s; **ook meteen na boot**) zodat
de **bridge** weet welke seriële poort welke master is en `commando/masterN` correct kan routeren —
**zonder** te moeten wachten tot een slave een batch/heartbeat stuurt. Zonder dit bericht bleef de route
ongeleerd zolang geen slave rapporteerde, waardoor commando's werden genegeerd (`Nog geen poort geleerd`).
De bridge gebruikt de aankondiging **enkel intern** om de route te (her)koppelen
(`[ROUTE] /dev/ttyUSBx -> commando/masterN`) en stuurt ze **niet** door naar MQTT. Een master die naar een
andere USB-poort verhuist, wordt door de volgende aankondiging automatisch opnieuw correct gerouteerd.

**Master-conflict (R6/C8):** announcet hetzelfde `master`-nummer binnen 10 s op **twee verschillende open
poorten** (= twee borden met dezelfde `MASTER_NR`-env geflasht → stille route-flip-flop), dan publiceert de
bridge — max 1×/30 s — een foutbericht op `plaatjes/data`:

```json
{"bridge_fout":"MASTER_CONFLICT","master":2,"poorten":["/dev/ttyUSB0","/dev/ttyUSB1"]}
```

Node-RED ("Registreer detectie" → "Evalueer spelstatus") toont dit als **ST-006 (FOUT, blokkerend)** in de
pre-flight. Oplossing: het verkeerd geflashte bord met de juiste env herflashen.

### Formaat: fout

```json
{"paal":1,"fout":1,"ernst":2,"detail":3150}
```

Gestructureerde foutmelding (uit `MSG_FOUT`). `ernst`: 0 = info, 1 = waarschuwing, 2 = fout. `detail` is een
vrij veld (bv. spanning in mV of een teller).

| `fout` | Betekenis | `detail` |
|-------:|-----------|----------|
| `1` | Batterij kritiek | spanning in mV |
| `2` | ESP-NOW-zend mislukt | `esp_err_t`-code |
| `3` | BLE-overflow (>30 spelers in dit vak) | aantal extra gedetecteerd |

### Formaat: commando-uitvoering (ACK)

```json
{"status":"uitgevoerd","paal":1,"seq":42}
```

De master meldt dit zodra de slave een commando **heeft uitgevoerd** (`MSG_CMD_ACK`, `status 0`); bij een
onbekende/geweigerde actie `"geweigerd"` (`status 1`). Vervangt de v1 MAC-ACK-gebaseerde `"ack"`.

### Indicator-LED's (geen serieel bericht)

- **Slave** knippert zijn ingebouwde LED (GPIO8, active-LOW) kort bij elke
  **succesvolle ESP-NOW-zend**.
- **Master** pulst zijn ingebouwde LED (GPIO2, active-HIGH) kort bij elke
  **ontvangen slave-batch**.

### Debug-output

Niet-JSON regels (bijvoorbeeld `[SETUP] Master MAC: ...`) worden door
bridge.py als debug-output behandeld: gelogd maar niet doorgestuurd naar MQTT.

### Serial-output: één schrijver (geen interleaving)

De master produceert serial-regels vanuit twee taken: `OnDataRecv()` draait op de
WiFi-task, de queue-/serieel-verwerking op de loop-task. `Serial.print` is niet
atomair over taken, dus twee regels konden vroeger door elkaar geweven raken — een
half-gemengde regel werd door `bridge.py` als debug gezien en stil gedropt
(verdwenen detecties/acks). Daarom bouwen alle producenten hun regel volledig op via
`logRegel()` en zetten die in een FreeRTOS-queue; **alleen `loop()`** schrijft naar
Serial. Elke regel komt zo in één stuk op de lijn. Raakt de log-queue onder pieklast
vol, dan worden regels geteld en gemeld via `{"status":"log_drop","aantal":N}`.

## 4. Pi → Master (Serial USB)

Pi stuurt commando's terug naar de master, doorgegeven vanuit Node-RED.

### Formaat: commando

```json
{"paal":1,"actie":1}
```

De Pi/Node-RED stuurt enkel `{"paal","actie"}`; de master kent zelf het `cmd_seq` toe en zet het commando
(`commando_message_v2`) achteraan de **per-slave FIFO** van die paal (zie sectie 2 "Reliability"). Master
antwoordt per regel met een status-JSON:

**Buzzer-tuning (actie 12).** Bij `{"paal":1,"actie":12,"toon":2500}` gaat het commando **niet** door de
FIFO/ACK: de master stuurt direct een `MSG_BUZZER_TOON` (zie §0) naar die paal met `freq_hz = toon`
(`toon:0` = stop). Het veld `actie` blijft aanwezig zodat `bridge.py` (eist `paal`+`actie`) ongewijzigd
blijft; `toon` is het extra frequentie-veld (Hz).

**BLE-scan-config (actie 20).** Bij `{"paal":1,"actie":20,"scan_ms":600}` gaat het commando **niet** door
de FIFO/ACK: de master stuurt direct een `MSG_SCAN_CONFIG` (zie §0) naar die paal met de gewenste
scan-vensterduur (`scan_ms` in ms; de slave clamp't 300..2000). Het veld `actie` blijft aanwezig zodat
`bridge.py` (eist `paal`+`actie`) ongewijzigd blijft. De master antwoordt met status `scan` (verstuurd)
of `send_err`.

**LED-helderheid (actie 21).** Bij `{"paal":1,"actie":21,"helderheid":255}` vertaalt de master dit —
net als scan-config, buiten de FIFO om — naar een `MSG_LED_CONFIG` met de gewenste globale
FastLED-helderheid (`helderheid` 0..255; de slave clamp't 5..255 zodat de LEDs nooit volledig doven).
Dit schaalt de **hele** LED-strip; Klokslag/animatie-effecten (die per-LED kleuren schalen) blijven er
bovenop werken. `bridge.py` ongewijzigd (`helderheid` komt mee als extra veld). Status `led`/`send_err`.

| Status         | Betekenis |
|----------------|-----------|
| `queued`       | Commando achteraan de per-slave FIFO gezet, wordt in volgorde async verzonden (geen laatste-wint). |
| `fifo_vol`     | De FIFO van die paal zat vol (`CMD_FIFO_DIEPTE`) — het oudste commando is gedropt (`gedropt_seq`). |
| `uitgevoerd`   | Slave heeft het commando **uitgevoerd** (`MSG_CMD_ACK status 0`). Bevat `seq`. Vervangt de v1 `ack`. |
| `geweigerd`    | Slave gaf `MSG_CMD_ACK status 1` (onbekende/geweigerde actie). Bevat `seq`. |
| `send_err`     | `esp_now_send()` gaf geen ESP_OK (radio). Retry volgt automatisch. |
| `scan`         | BLE-scan-config (actie 20) direct verstuurd als `MSG_SCAN_CONFIG`. Bevat `scan_ms`. Fire-and-forget (geen ACK). |
| `led`          | LED-helderheid (actie 21) direct verstuurd als `MSG_LED_CONFIG`. Bevat `helderheid`. Fire-and-forget (geen ACK). |
| `opgegeven`    | Na `MAX_POGINGEN` zonder applicatie-ACK — commando verloren. |
| `geen_slave`   | `paal_id` wijst naar een leeg/placeholder slot (all-zero MAC) — geweigerd. |
| `buiten_bereik` | `paal_id` valt buiten het paalbereik van deze master (`PAAL_MIN..PAAL_MAX`). Bevat `master`. Hoort nooit te gebeuren (de bridge routeert op `paal_id`) → wijst op een routeringsfout. |
| `log_drop`     | De interne serial-log-queue zat vol; `aantal` regels gingen verloren onder pieklast. |

## 5. Pi ↔ Node-RED (MQTT)

Broker: Eclipse Mosquitto op `192.168.1.43:1883`, anonymous access toegestaan
(lokaal netwerk).

| Topic              | Richting           | Payload                                      |
|--------------------|--------------------|----------------------------------------------|
| `plaatjes/data`    | Pi → Node-RED      | `{"paal":1,"mac":"aa:bb:..","rssi":-67}`     |
| `commando/master1\|2\|3` | Node-RED → Pi | `{"paal":1,"actie":1}` — Node-RED routeert per paal-bereik (1–8/9–16/17–24); de bridge levert bij de juiste master |
| `audio/afspelen`   | Node-RED → audio-player | `{"fase":"event","tekst":"...","segments":["getallen/3.wav","woorden/spelers.wav","events/x_voor.wav","getallen/3.wav","events/x_na.wav"],"prioriteit":"normaal"}` — de event-fase begint met de aantal-prefix (`getallen/<aantal>` + `woorden/<speler\|spelers\|uur\|uren>`) |
| `audio/muziek`     | Node-RED → audio-player | `{"cmd":"play\|pause\|resume\|stop","track":"muziek/reactie_pools.wav"}` — **bestuurbaar kanaal** los van de segment-queue: een lange track die pauzeert/hervat-op-positie (`pause`/`resume`) of hard stopt mid-track (`stop`). `play` = vanaf 0 (reset positie), maar **idempotent**: speelt exact dezelfde track al actief, dan wordt een herhaald `play` genegeerd (geen herstart-sprong) — zo kan een producent het commando veilig her-bevestigen. Gebruikt door het Poolse-reactietijd-event (muziek tijdens de reactietijd), de onmiddellijke-dood-cutscene (24 s-track afgekapt bij de landing) en de bommen-minigame (die het `play` ~800 ms na de start eenmalig herhaalt tegen een verloren/te-late eerste publish). `player.py` streamt de PCM via `wave.setpos` naar `aplay` (positie via wandklok) |
| `locatie/spelers`  | Node-RED → browser | `{"Lilou":5,"Maud":12}` — opgeloste paal per speler (algoritme-uitkomst) |
| `spel/historie`    | Node-RED → browser | `{"actief":true,"start":"...","events":[{"nr":1,"tekst":"...","doelwit":["Lilou"]}]}` |
| `spel/state`       | Node-RED ↔ Node-RED | `{"ts":..,"spelerStats":{..},"globaleStats":{..},"spelHistorie":[..],"spelToestand":..,"spelNummer":..,"midnight":{"midnightIndex":..,"midnightOpen":..,"midnightRemaining":..,"piDigits":".."}}` (retained, qos 1) — **compacte state-snapshot** die Flow 04 elke 30 s dumpt; node `Rehydrate spel-state` leest hem bij (her)start terug, maar enkel als de global nog leeg is. Vangnet naast `contextStorage` (zie invariant NR8) |
| `sim/modus`        | browser ↔ Node-RED | `{"sim24":true}` (retained) — monitor/simulatie-keuze → Node-RED forceert een 24-uur veld (`palenActief`). Wordt **zowel door de browser-simulator als door de monitor/sim-schakelaar op het Bediening-dashboard** gepubliceerd; de simulator abonneert er ook op zodat zijn radio meeschuift |
| `sim/locatie`      | browser → Node-RED | `[{"mac":"aa:..","paal":7}]` — exacte paal per speler (sim-modus, deterministisch, geen RSSI) |
| `sim/systeem-config` | client → Node-RED | `{"toestandExclusief":true,"tempo":1,"settleGrace":3,"dichtheid":0.25}` (retained) — systeeminstellingen: `toestandExclusief` = tijdbom & ziekte niet samen op één speler; `tempo` = reactietijd-multiplier (`global.tempoFactor`); `settleGrace` = settle-grace in s (`global.pofSettleGrace`); `dichtheid` = doelwit-dichtheid 0–1 (`global.doelwitDichtheid`, ook via het Bediening-dashboard "Spelbalans") |
| `sim/wachtrij-weg` | client → Node-RED | `{"index":2}` — verwijder het aankomende event op die index uit `global.pofWachtrij` ("Volgende events"-paneel); de rij schuift door en vult zich weer aan |
| `sim/wachtrij-toevoegen` | client → Node-RED | `{"id":"<event-id>"}` — zet dat event **vooraan** in `global.pofWachtrij` (= volgend event); de rij wordt op 5 gecapt en schuift door. Bron: de "→ wachtrij"-knop per event-kaart in de simulator-events-zijbalk |
| `sim/spel-config` | client → Node-RED | `{"badAura":true}` (retained) — spelinstelling: **slechte aura** (`global.badAuraAan`); negatieve speler-events (ziekte/tijdbom) treffen 's avonds/'s nachts vaker |
| `sim/avond-modus` | client → Node-RED | `{"avond":true}` (retained) — **avondspel-modus** (`global.avondModus`): verplaatsing **kost** levensuren (mag negatief) i.p.v. verdient, en de events-tab toont enkel `fase:"avond"`-events. Zie `docs/spel/avondspel.md` |
| `sim/tiers-config` | client → Node-RED | `{"<event-id>":"rare",...}` (retained) — per-event **tier-override** (`global.eventTiers`); bepaalt de kans dat een event gekozen wordt |
| `sim/tijd-terug` | client → Node-RED | trigger om **één ronde terug** te gaan: herstelt de laatste snapshot (`global.pofSnapshots`) |
| `pof/animatie` | Node-RED → browser | `{"type":"nuke"\|"oogst"\|"tornado"\|null,"gate":24,"centers":[...]}` (retained) — **dramatische animatie** als één bericht; de sim animeert hierop (robuust tegen verloren per-paal commando's). De firmware blijft op de per-paal acties 8/11/14/15 |
| `pof/dood-anim` | Node-RED → browser | `{"fase":"loopt"\|"gekozen","paal":7,"speler":"Lilou"}` (transient) — **onmiddellijke dood** (avondspel): de cirkelende paarsrode LED per stap; `gekozen` = de slachtoffer-paal + naam. De simulator rendert de bewegende LED. Zie `docs/spel/avondspel.md` |
| `pof/herstel-posities` | Node-RED → browser | `{"Lilou":5,...}` — bij 'tijd terug' de herstelde paal per speler (de sim zet de bolletjes terug) |
| `sim/bediening`    | client → Node-RED | UTF-8 string-commando om de engine programmatisch te besturen (AI-testharnas, zie `tools/speltest/`). Geldige waarden: `start`/`aan`, `stop`/`uit`, `manueel-aan`, `manueel-uit`, `volgende`, `controle`, `wis-stats`. **Werkt enkel in sim-modus** (`simVeld24 === true`) — buiten sim-modus wordt het genegeerd zodat een echt spel nooit geraakt wordt. |
| `pof/status`       | Node-RED → browser | `{"actief":true,"fase":"reactie","eventNaam":"...","eventTekst":"...","doelwit":[],"doelwitType":"uur","doelwitReveal":"• Lilou","getalWaarde":2,"getalWaarde2":null,"groepLabel":null,"eventenRonde":3,"teller":7,"maxTeller":10}` — `doelwitType`+`doelwit.length` voor de afroep-tekst, `eventenRonde` voor de events-teller; `getalWaarde2` is het tweede getal `y` (bij `voorwaarde: "of"`, anders `null`); `doelwitType` kan `"groep"` zijn met `groepLabel` (`"kleur: rood"`) en afroep-prefix "een groep"; `wachtrij` = `[{id,naam}]` (preview volgende events); `spelTempo` = huidige spel-tempo-factor (0,6–1,3) |
| `pof/controle`     | Node-RED → browser | `{"event":"...","resultaten":[{"speler":"Lilou","status":"TE WEINIG","verplaatst":1,"delta":-1,"tag":"-"}]}` — `delta` = toegekende/afgetrokken levensuren |
| `pof/portalen`     | Node-RED → browser | `[{"palen":[12,20]}]` — actieve portaal-paren (retained); simulator tekent de verbindingslijn en teleporteert |
| `pof/toestanden`   | Node-RED → browser | `[{"uur":12,"effect":"portaal","naam":"Portalen","resterendeRondes":3}]` — actieve uur-effecten (retained); voedt het sim-"Toestanden"-paneel |
| `pof/ziekte`       | Node-RED → browser | `[{"speler":"Lilou","rondesOver":3,"uur":12}]` — actieve zieke spelers + events resterend (retained); voedt het sim-"Ziekte"-paneel (badge + hart-waarschuwing) |
| `pof/middernacht`  | Node-RED → browser | `{"index":7,"open":true,"remaining":2,"eventsTotOogst":14,"paal":24}` — middernacht-poort: pi-cijfer-index, open/dicht, events in fase + tot volgende oogst (retained) |
| `pof/dienaars`     | Node-RED → browser | `{"Maud":"Mien"}` — geoogste spelers → hun meester (retained); voedt sim-speler-menu + dashboard-tabel |
| `pof/tijdbom`      | Node-RED → browser | `{"spelers":[{"speler":"Lilou","rondesOver":7,"uur":5}],"ontmantelPalen":[3,7]}` — actieve tijdbommen + de gekozen ontmantel-palen (retained); voedt de 💣-badges + het knoppen-paneel |
| `pof/knop`         | Node-RED → browser | `{"paal":7,"teller":4}` — een drukknop is ingedrukt (visuele flits in de sim; `teller` = cumulatieve druk-teller voor het drukknop-testdashboard; transient) |
| `config/drukknoppen` | Node-RED → browser | `[3,4,7,9,11,13,15,16,17,19,21,22]` — palen met een fysieke drukknop (retained); bron = `[CONFIG] Drukknop-palen`. Voedt het sim-knoppen-paneel |
| `config/scan-duur` | Node-RED ↔ client | `{"1":700,"2":700,…}` (retained) — BLE-scan-vensterduur (ms) per paal; bron = dashboard-group "Scan-duur (BLE)" (Beacons & Locatie). Node-RED herlaadt hem in `global.scanDuurPerPaal` bij (her)start en herstelt gereboote slaves via de heartbeat |
| `config/led-helderheid` | Node-RED ↔ client | `200` (retained getal) — globale LED-helderheid (0..255); bron = dashboard-group "LED-helderheid" (Beacons & Locatie). Node-RED herlaadt hem in `global.ledHelderheid` bij (her)start en herstelt gereboote slaves via de heartbeat (actie 21). Overdag hoog voor zichtbaarheid; 's avonds lager spaart batterij |
| `config/spelers` | Node-RED ↔ client | `{"48:87:2d:..":"Lilou",…}` (retained) — baken-MAC → spelernaam (`global.spelersLijst`); bron = dashboard-group "Spelers / bakens beheren" (Beacons & Locatie). Retained → **wint** na (her)start van de flows.json-seed `[CONFIG] Spelerslijst` (die enkel nog bootstrap is). Zo wissel/her-toewijs je een baken zonder deploy |
| `spel/type`        | browser ↔ Node-RED | `{"type":"plates_of_fate"\|"klokslag"}` (retained) — **gekozen spel**, één bron van waarheid. Zowel de simulator als het Bediening-dashboard publiceren én abonneren erop; beide engines lezen `global.spelType` en draaien enkel als het hún spel is |
| `klokslag/status`  | Node-RED → browser | `{"actief":true,"fase":"lopend"\|"einde"\|"idle","resterend_s":480,"speeltijd_s":600,"winnaar":"team1"\|null}` (retained) — Klokslag-timer + einde/winnaar |
| `klokslag/palen`   | Node-RED → browser | `{"palen":[{"paal":10,"P":4.2,"H":10,"controller":"team1","eigenaar":null,"modus":"capturing"}]}` (retained) — per-paal inname-staat; voedt de Klokslag-LED-render in de sim. `modus`: `rust`/`capturing`/`owned`/`frozen` |
| `klokslag/score`   | Node-RED → browser | `{"teams":[{"id":"team1","naam":"Blauw","kleur":"blauw","score":5,"somUren":42,"uren":5}],"winnaar":null}` (retained) — scorebord + teamlegenda |
| `pof/doelstatus`   | Node-RED → browser | `{"doel":{"type":"verplaats_uur","x":5},"percent":40,"aantal":2,"totaal":5,"spelers":{"Lilou":true,"Maud":false}}` (retained) — PoF-doelvoortgang: per-speler `doelBereikt` + percentage; voedt de %-weergave + highlight in de simulator-zijbalk |

### Plates-of-Fate: doelwit-reveal en `pof/status`

Wanneer een event gekozen is, kiest Node-RED de doelwitten (spelers of uren).
Die worden **één-voor-één** onthuld door een server-side sequencer-function
("Doelwit reveal"): elke ~1,2 s wordt een naam toegevoegd aan de global
`pofDoelwitReveal`. Pas **nadat de laatste naam getoond is** start de
reactietijd-aftelling (de sequencer triggert dan "Voer gevolg uit").

`pof/status` (elke seconde gepubliceerd) draagt de actuele stand:
- `eventNaam` / `eventTekst`: naam en ingevulde tekst van het huidige event.
- `doelwit`: volledige array van gekozen doelwitten.
- `doelwitReveal`: de progressief opgebouwde tekst (`• naam\n• naam`), zodat
  de simulator dezelfde één-voor-één-onthulling toont als het dashboard.
- `fase`: `idle` / `aanloop` / `bezig` / `reactie` / `grace` (settle-grace vóór de controle) / `regroup` (NUKE-pauze) / `wacht*`.

Zo tonen het Node-RED dashboard (ui_text "Doelwit") én de browser-simulator
identieke informatie zonder browser-specifieke scripting.

### Plates-of-Fate controle-resultaten (`pof/controle`)

Na de reactietijd controleert "Verifieer beweging" of elke speler aan de
beweging-voorwaarde voldeed. Het resultaat wordt — naast de dashboard-tabel —
ook gepubliceerd op `pof/controle`. Elke `status` (`OK`, `TE WEINIG`, `TE VEEL`,
`ONGELDIGE KEUZE`, `OK (stil)`, `BEWOOG (mocht niet)`) is in feite een **foutcode** van het event
na zijn controle. De browser-simulator logt deze regels onder de checkbox
"Foutcodes".

### Locatiebepaling-globals (Node-RED)

De locatiebepaling en beacon-diagnose gebruiken enkele globals — details en
afregeling staan in `docs/locatiebepaling.md`:

- `locParams` — live tuning van venster/hysterese/vloer/grace/switch/min-samples
  (instelbaar via de dashboard-pagina "Beacons & Locatie", geen redeploy nodig).
- `beaconKalibratie` — `{ "<mac>": offsetDb }`, RSSI-offset per beacon.
- `beaconBuf` — interne sample-buffer voor de stabiliteitsanalyse.
- `spelerLocaties` — `{ spelerNaam: paalId }`, de centrale waarheid.

### MQTT-config in Node-RED

- **Server**: `192.168.1.43` (NIET `127.0.0.1` — Node-RED draait in bridge-netwerk)
- **Port**: 1883
- **Protocol**: MQTT V3.1.1
- **QoS**: 0 voor commando's én data (fire-and-forget — laagste latentie; de master-FIFO/retries
  maken commando's idempotent-betrouwbaar, zie §2). Géén QoS 2 gebruiken: dat kost twee extra
  broker-round-trips per commando en de flows/bridge staan bewust op 0.

### MQTT-config in bridge.py (serial-bridge container)

- **Server**: `127.0.0.1` (bridge draait in host-netwerk, dus localhost = Pi)
- **Port**: 1883

### MQTT WebSocket-listener (poort 9001)

Naast de standaard TCP-listener op 1883 luistert Mosquitto óók op poort 9001
voor het **WebSocket**-protocol. Dit is bedoeld voor browser-gebaseerde
deelnemers (zoals de simulator in `pi/simulator/`) die geen rauwe TCP
kunnen openen.

```
listener 9001
protocol websockets
```

Topics en payloads zijn identiek aan de TCP-listener — alleen de transport-
laag verschilt. Een browser-client verbindt via `ws://192.168.1.43:9001`.

### Simulator als legitieme MQTT-deelnemer

`pi/simulator/` is een browser-app die zich op precies dezelfde naad in het
systeem (`plaatjes/data` en `commando/master1`) gedraagt als de echte
hardware. Twee modi:

- **Monitor**: subscribe-only — kijkt passief mee met een echt spel en volgt de
  opgeloste posities (`locatie/spelers`).
- **Simulatie**: de simulator is een **spelverloop-/conflict-tester**. Hij gebruikt
  **geen RSSI-model**, maar stuurt de exacte paal van elke speler direct door op
  `sim/locatie` (deterministisch). Via `sim/modus {sim24:true}` forceert Node-RED
  een **24-uur veld** (`palenActief`), onafhankelijk van `paaltjesLijst` (die blijft
  de echte, gebouwde palen voor de hardware). Node-RED schrijft die posities direct
  in `spelerLocaties` en stuurt beweging-events naar het puntensysteem.

De firmware wordt voor de simulator niet aangepast; Node-RED kreeg enkel de twee
sim-ingangen (`sim/modus`, `sim/locatie`) erbij naast het echte hardware-pad.

**Wederzijdse uitsluiting van de bron.** `global.simVeld24` (gezet via `sim/modus`)
bepaalt wie `spelerLocaties` schrijft:
- `simVeld24 === true` (sim-modus): de echte `Locatiebepaling Spelers` doet niets
  (negeert `plaatjes/data` én de `[TEST]`-injects); **alleen** `Sim directe locatie`
  schrijft. Zo is de simulatie een standalone pakket en kan echte hardware de
  virtuele posities niet vervuilen.
- `simVeld24 !== true` (monitor): `Sim directe locatie` doet niets; alleen de echte
  hardware bepaalt de posities. Monitor-modus is dan een zuivere visualisatie.

Het dashboard heeft een aparte pagina **"Simulatie"** (PoF-besturing + live radar)
die in sim-modus de virtuele wereld toont. Dezelfde PoF-engine bedient zowel het
echte spel (pagina "Bediening") als de simulatie.

### Audio-abstractie (`audio/afspelen`)

De Plates-of-Fate engine (Node-RED flow 06) publiceert audio-verzoeken op
`audio/afspelen`. Het is bewust een **abstractie**: de engine zegt *welke*
audiosegmenten in *welke volgorde*, niet *hoe* ze klinken. De consument is de
**`audio-player`** Pi-service (`pi/audio-player/`) die de WAV-segmenten
sequentieel via `aplay` over de aux-jack speelt. Zie `docs/handleidingen/audio-player.md`.

```json
{"fase":"event","tekst":"3 spelers worden ziek.","segments":["getallen/3.wav","woorden/spelers.wav","events/toestanden/worden_ziek.wav"],"prioriteit":"normaal"}
```

- `fase`: `"event"` of `"doelwit"`.
- `tekst`: leesbare tekst (simulator-log + fallback); de player gebruikt `segments`.
- `segments`: lijst WAV-bestandsnamen relatief t.o.v. de audio-map, in afspeelvolgorde.
  De **event-fase** begint met de aantal-prefix (`getallen/<aantal>` +
  `woorden/<speler|spelers|uur|uren>`) gevolgd door begin + getal + eind van het event;
  de **doelwit-fase** is `doelwit/voor` + per doelwit een clip + `doelwit/na`.
- `prioriteit`: vrije tekst voor latere afspeel-volgorde.

Naast `event`/`doelwit` bestaat de **sfx-fase** voor niet-gesproken cues:
`sound-effect/countdown/countdown.wav` (aftelklok, t−5 s) en
`sound-effect/wereld-events/woosh.wav` — een **woosh-signatuur** die vlak vóór elke
**wereld-event**-afroep speelt (na de countdown), zodat spelers meteen horen dat er een groot
event aankomt zonder dat er doelwitten worden opgenoemd. Het **Iedereen**-event
(`geenDoelwitAfroep`) roept géén 31 namen af maar één `prefix/iedereen.wav` (anders zoekt de
telprefix `getallen/31.wav`, die niet bestaat, en duurt de namen-reveal ~25 s).

Plates-of-Fate LED-toestanden worden centraal gestuurd. Node-RED **routeert** elk `{paal,actie}`-commando
via de node **"Route commando"** naar `commando/master1|2|3` op basis van het paal-bereik (1–8/9–16/17–24)
— hetzelfde bereik als de bridge en de firmware. Er is geen apart commando-formaat voor events.

## 6. Slave-registratie en sender-MAC gate (master code)

### Multi-master: één codebase, drie environments

Het veld heeft **3 masters**: master 1 bedient palen **1–8**, master 2 **9–16**, master 3 **17–24**.
Eén master-codebase, drie PlatformIO-environments (`master1/2/3` in `firmware/Master/platformio.ini`) die
elk `PAAL_MIN`/`PAAL_MAX`/`MASTER_NR` via `build_flags` zetten. Afgeleid:

- `AANTAL_SLAVES = PAAL_MAX − PAAL_MIN + 1` en `paalNaarIndex(paal) = paal − PAAL_MIN` (de **globale**
  paal-ID wordt zo de juiste 0-based array-index — index 0 = `PAAL_MIN`).
- Een commando voor een paal **buiten** `PAAL_MIN..PAAL_MAX` wordt geweigerd met
  `{"status":"buiten_bereik","paal":N,"master":M}` (hoort nooit te gebeuren; de bridge routeert al op
  `paal_id`).
- Het verzonden commando draagt de **globale** `paal_id` (`PAAL_MIN + index`), zodat de slave op zijn
  eigen `PAAL_ID` matcht.
- De slave-MAC's per master staan in `firmware/Master/include/slave_macs.h` (`#if MASTER_NR`-blokken) —
  elke master kent **uitsluitend zijn eigen** slaves, zodat de sender-MAC gate automatisch segmenteert.
- De **slave** kiest zijn master-MAC uit `PAAL_ID` (1–8→master1, 9–16→master2, 17–24→master3) via een
  tabel `masterMacs[3][6]` — niets extra per slave te configureren behalve `PAAL_ID`.

MAC-adressen van slaves staan per master in `slave_macs.h` (geladen in `slaveAdressen[]`).
Deze array vervult **twee** rollen:

1. **Peer-lijst voor zenden**: alle MACs worden via `esp_now_add_peer()`
   geregistreerd zodat de master commando's naar die slaves kan sturen.
2. **Ontvangst-whitelist**: `OnDataRecv()` vergelijkt de afzender-MAC met
   `slaveAdressen[]`. Pakketten van slaves die niet in deze lijst staan
   worden gedropt en NIET doorgestuurd naar de Pi.

> **Waarom een ontvangst-whitelist nodig is**: ESP-NOW levert standaard
> pakketten van **elke** afzender aan de receive-callback. `esp_now_add_peer()`
> is alleen nodig om te kunnen zenden, het filtert geen binnenkomst.
> Zonder deze gate zou een master pakketten ontvangen en doorzetten van
> alle 24 slaves in het veld — terwijl één master maar 8 specifieke slaves
> hoort te bedienen.

Bij het toevoegen/wijzigen van een slave:
1. Flash slave (zet `PAAL_ID`) en lees MAC uit Serial Monitor (banner `SLAVE MAC-ADRES : ...`)
2. Vul het MAC in op de juiste rij in `firmware/Master/include/slave_macs.h` (in het `MASTER_NR`-blok
   van de master die die paal bedient; rij-index = `paal − PAAL_MIN`)
3. Herflash de juiste master-environment (`AANTAL_SLAVES` is afgeleid uit het bereik — niet meer handmatig)

Slots met placeholder MAC `0x00:0x00:0x00:0x00:0x00:0x00` worden overgeslagen
bij zowel peer-registratie als de ontvangst-gate, zodat je veilig vooruit
kunt definiëren.

## Wijzigingsgeschiedenis

- 2026-07-15: **Lang-draai-hardening firmware** — geen wire-format-wijziging. Master: alle
  `cmdPerSlave`-FIFO-mutaties (enqueue, ACK-pop, opgegeven-pop, send_err-rewind) lopen onder de
  **`cmdMux`-spinlock** — OnDataRecv/OnDataSent (WiFi-task, core 0) en de loop (core 1) muteerden
  `head`/`count` voorheen ongesynchroniseerd, wat een paal-queue na uren kon wedgen; serial-parsing
  is **heap-vrij** (`jsonVeld()` i.p.v. Arduino-String-substring-churn). Beide firmwares:
  **`enableLoopWDT()`** (task-WDT 5 s, panic=reset) — een gehangen bord reset zichzelf i.p.v. dood
  te blijven tot een power-cycle. Slave: **`setMaxResults(0)`** op de NimBLE-scan (callback-only;
  voorheen bewaarde NimBLE álle omliggende BLE-apparaten per cyclus → heap-fragmentatie op druk
  terrein). Vereist herflash van slaves + masters.
- 2026-07-13: **Commando-latency-optimalisatie (phase-locked retry + her-ACK)** — geen wire-format-
  wijziging. Master: retries zijn nu **phase-locked** op de binnenkomende batch/heartbeat van de
  slave (= begin van diens vrije radio-venster, `slaveVensterVlag`, guard 50 ms); `APP_ACK_TIMEOUT`
  1500 → **600 ms** (blinde fallback; de oude waarde was gekalibreerd op de verdwenen blokkerende
  scan) en `MAX_POGINGEN` 4 → **6** (opgeven ~3,6 s). Log-drain is **budget-begrensd**
  (`Serial.setTxBufferSize(2048)` vóór `begin()` + alleen schrijven wat in de TX-ring past) zodat de
  loop nooit meer ~150-200 ms stalt op UART-backpressure; `[RECV]`-debugregel achter `LOG_RECV_DEBUG`.
  Slave: kale `delay()`s in backoff/cyclus-staart vervangen door `servicedWait()` (commando's blijven
  bediend) en een mid-scan verzonden ACK wordt éénmalig **her-ACK't** direct na `pBLEScan->stop()`
  (idempotent). Node-RED: commando-mqtt-out QoS 2 → 0. Typische klik→uitvoering: 2-5 s → ~0,4-0,7 s.
  Vereist herflash van masters (fase 1) + slaves (fase 2).
- 2026-07-05: **`MSG_LED_CONFIG` (0x0A) + actie 21 `ACTIE_LED_CONFIG`** — runtime-instelbare LED-helderheid
  (globale FastLED-brightness) voor daglicht-zichtbaarheid. Config via `{"paal":N,"actie":21,
  "helderheid":M}` op `commando/masterN`; de master vertaalt actie 21 → `MSG_LED_CONFIG` en stuurt direct
  (geen FIFO/ACK, status `led`). De slave clamp't 5..255 en past het toe met `setBrightness`+`show`
  (componeert met de per-LED-schaling van Klokslag/animaties). Node-RED: dashboard-group "LED-helderheid"
  (slider + Min/Middel/Max) + retained `config/led-helderheid` + auto-herstel na slave-reboot via de
  heartbeat. `bridge.py` ongewijzigd (`helderheid` komt mee als extra veld). Vereist herflash van
  slave(s) + master(s).
- 2026-07-02: **`MSG_SCAN_CONFIG` (0x09) + actie 20 `ACTIE_SCAN_CONFIG`** — runtime-instelbare BLE-scan-
  vensterduur. De slave scant nu **niet-blokkerend**, begrensd door een `millis()`-venster (`scanDuurMs`,
  default 1000 ms, clamp 300..2000), i.p.v. de vaste `start(1 s)` (NimBLE 1.4.2 blokkeert enkel in hele
  seconden). Scan-duty verhoogd naar window 64/interval 80 (~80%). Config via `{"paal":N,"actie":20,
  "scan_ms":M}` op `commando/masterN`; de master vertaalt actie 20 → `MSG_SCAN_CONFIG` en stuurt direct
  (geen FIFO/ACK, status `scan`). Node-RED: dashboard-group "Scan-duur (BLE)" + retained `config/scan-duur`
  + auto-herstel na slave-reboot via de heartbeat. `bridge.py` ongewijzigd (`scan_ms` komt mee als extra
  veld). Vereist herflash van slave(s) + master(s). Bijkomend (Node-RED-only, geen wire-format): een
  **settle-grace**-fase in de PoF-engine draait de controle op T+grace i.p.v. T.
- 2026-06-14: **`MSG_BUZZER_TOON` (0x07) + actie 12 `ACTIE_BUZZER_TOON`** voor buzzer-tuning. Een nieuw
  parameter-dragend ESP-NOW-bericht (`buzzer_toon_message`, 4 B) waarmee de master een **continue** toon
  op een willekeurige frequentie op een slave start/stopt — om per bordje de luidste resonantie te zoeken.
  Aangestuurd via `{"paal":1,"actie":12,"toon":<Hz>}` op `commando/master1` (`toon:0` = stop); de master
  vertaalt dit naar `MSG_BUZZER_TOON` en stuurt het **direct** (geen FIFO/ACK, fire-and-forget). Slave:
  `OnDataRecv` accepteert nu ook `0x07` en zet een `volatile` doelfrequentie; de loop houdt de continue toon
  aan (over de blokkerende BLE-scan heen). `bridge.py` ongewijzigd (`actie` blijft aanwezig). Node-RED:
  nieuwe dashboardpagina "Buzzer-tuning". Vereist herflash van slave(s) + master(s).
- 2026-06-12: **`sim/bediening`-topic** toegevoegd (AI-testharnas `tools/speltest/`). Een UTF-8
  string-commando (`start`/`stop`/`manueel-aan`/`manueel-uit`/`volgende`/`controle`/`wis-stats`)
  waarmee de Plates-of-Fate-engine **programmatisch over MQTT** bestuurd kan worden i.p.v. via de
  dashboard-knoppen. Node-RED-zijde: één mqtt-in + functie-node "Verwerk sim-bediening" die naar de
  bestaande besturingsnodes routeert (`Spel aan/uit`, `Sla pofManueel op`, `Engine tick`,
  `Wis globale stats`). **Werkt enkel in sim-modus** (`simVeld24 === true`). Geen wire-format- of
  firmware-wijziging; enkel een Node-RED-flow-edit (chirurgisch, daarna `deploy-flows.ps1`).
- 2026-06-11: **MSG_BATCH variabele lengte** (veldfix). De slave verstuurde altijd het volle 215-byte
  frame (30 slots), óók bij 0 spelers; in de praktijk bereikten die lange frames de master niet terwijl
  de kleine v2-frames (heartbeat 9 B) wél aankwamen. De slave verstuurt nu enkel het gebruikte deel
  (`5 + aantal×7` B); de master valideert in twee stappen (header ≥ 5, `aantal` ≤ 30, `len ≥ 5 + aantal×7`)
  en accepteert ook nog volle frames. Vereist herflash van slave(s) + master(s).
- 2026-06-11: **slave commando-race + master FIFO** (Batch 3). Slave: commando's via een **SPSC-ringbuffer**
  (8 slots) i.p.v. één variabele; de flag-reset bovenaan de loop is weg en `verwerkCommandos()` draineert op
  meerdere punten → geen verloren/overschreven commando's, twee commando's per cyclus allebei uitgevoerd,
  idempotent op `cmd_seq`, `cmdDrops`-teller. Master: het enkele per-slave slot (laatste-wint) is vervangen
  door een **per-slave FIFO** (diepte 4, drop-oudste = `fifo_vol`), zodat bv. piep + portaal allebei in
  volgorde de slave bereiken. Geen wire-format-wijziging t.o.v. v2 (zelfde flash-moment als Batch 1).
- 2026-06-11: **protocol v2** (Batch 1) — één ESP-NOW wire-format revisie (vereist herflash van alle
  slaves + master). Elk bericht krijgt een **`msg_type`-discriminator** (dispatch op type i.p.v. lengte).
  `batch_message` → `batch_message_v2`: **binaire MAC's** (6 B) + `int8 rssi` + `uint16 batt_mv`, en
  **`spelers[30]`** (215 B ≤ 250 B) i.p.v. 9 — geen stille drop meer bij >9 spelers. Nieuwe berichttypes:
  `MSG_CMD_ACK` (applicatie-ACK ná uitvoering → master meldt `uitgevoerd`/`geweigerd` i.p.v. MAC-ack `ack`,
  met `cmd_seq` voor idempotente retries), `MSG_HEARTBEAT`, `MSG_FOUT` (foutcode-tabel), `MSG_KNOP` (knop nu
  via ESP-NOW i.p.v. de dode USB-CDC). Whitelist v2: **OUI-prefix + RSSI-drempel** op de slave i.p.v. een
  hardcoded MAC-lijst. `bridge.py` ongewijzigd (inhoud-agnostisch). Buzzer-frequentie nu per-paal constante.
- 2026-06-10: master-robuustheid (Batch 2). FIFO-queue (16) vervangen door
  **per-slave pending-slots** (laatste-wint, parallelle retries) → geen
  `queue_vol` meer en geen head-of-line blocking door een dode paal;
  placeholder-palen worden direct geweigerd met `geen_slave`. Serieel lezen is
  nu **niet-blokkerend** (eigen regelbuffer i.p.v. `readStringUntil`, geen 1 s
  Stream-timeout). Alle serial-output loopt via één task (loop-gedraineerde
  FreeRTOS-log-queue) tegen interleaving; verlies onder pieklast meldt
  `log_drop`. Geen wire-format-wijziging — slaves niet herflashen.
- 2026-05-29: master kreeg retry-queue voor commando's (max 5 pogingen per
  commando, 250 ms tussen retries, FIFO-queue van 16). Statusantwoorden
  uitgebreid: `queued`, `ack`, `send_err`, `opgegeven`, `queue_vol`. Lost
  het probleem op waarbij een commando soms gemist werd door de slave
  tijdens BLE-scannen of bij collision.
- 2026-05-28: Mosquitto-broker extra listener op poort 9001 (WebSocket) voor
  browser-clients zoals `pi/simulator/`. TCP 1883 voor bridge.py + Node-RED
  blijft ongewijzigd.
- 2026-05-23: nieuw MQTT-topic `audio/afspelen` (Node-RED → geluidsbox) als
  abstractie voor de Plates-of-Fate engine. Engine-gevolgen voor LED/buzzer
  hergebruiken `commando/master1`.
- 2026-05-20: `batch_message` uitgebreid met `float batterij_v`. Master
  stuurt per batch één extra JSON-regel `{"paal":N,"batt":3.87}` naar de Pi.
  Node-RED toont dit in de Spelstatus-tabel onder een toggle "Toon batterij".
- 2026-05-20: master filtert binnenkomende ESP-NOW pakketten op afzender-MAC
  tegen `slaveAdressen[]`. Vreemde slaves worden gelogd als `[GATE]` en niet
  doorgezet naar de Pi. Maakt 1 master → 8 slaves segmentatie mogelijk in
  een veld met 3 masters / 24 slaves.
- 2026-05-20: slave dedupliceert nu binnen één batch (elke whitelisted MAC
  max één entry per scan, sterkste RSSI behouden) — voorheen vulde
  `spelers[9]` zich met duplicaten van dezelfde beacon
- 2026-05-18: slave verstuurt nu elke cyclus (ook bij 0 spelers) + random
  backoff vóór verzenden tegen botsende ESP-NOW-pakketten van meerdere slaves
- 2026-05-17: actie_id tabel bijgewerkt — alle 5 acties (0–4) geïmplementeerd in slave firmware
  *(correctie: destijds waren het 4 acties, 0–3 — `NIETS`/`PORTAAL`/`HAPPY_HOUR`/`BUZZER_PIEP`; de
  acties 4–11 zijn er later bij gekomen, zie de actietabel in §2)*
- 2026-05-10: initieel document, opgesteld bij overstap naar VS Code + GitHub workflow