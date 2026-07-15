# Handleiding: audio-player (knip-en-plak audio over de aux-jack)

De `audio-player` is een Pi-service die MQTT-berichten op `audio/afspelen`
ontvangt en de bijbehorende **WAV-segmenten sequentieel afspeelt** via `aplay`
over de 3,5 mm aux-jack naar je geluidsbox. Zo bouw je per event een zin op uit
losse stukken: **begin-segment + variabel getal + eind-segment**, en voor de
doelwitten een opsomming van speler-/uur-clips.

## Architectuur

```
[Node-RED PoF-engine] --MQTT audio/afspelen--> [audio-player container] --aplay--> [aux-jack] --> [box]
```

- **Node-RED** bepaalt *welke* segmenten in *welke volgorde* (zie onder). Het
  publiceert een lijst bestandsnamen; het zegt niet *hoe* af te spelen.
- **audio-player** (`pi/audio-player/player.py`) speelt elk segment met
  `aplay -q -D <device> <bestand>`, één voor één, via een wachtrij zodat audio
  nooit overlapt. Ontbrekende bestanden worden overgeslagen + gelogd.
- De wachtrij is **begrensd** (`AUDIO_QUEUE_MAX`, default **8**) met **drop-oldest**:
  loopt de box achter bij een burst events, dan wordt het **oudste, nog niet gespeelde**
  item weggegooid (met een logregel `Wachtrij vol - oudste gedropt`) i.p.v. dat de audio
  steeds verder achterop raakt. De box blijft zo dicht bij real-time.

### MQTT-payload (`audio/afspelen`)

```json
{
  "fase": "event",
  "tekst": "Minimum 3 uur vooruit.",
  "segments": ["events/verplaatsing1_voor.wav", "getallen/3.wav", "events/verplaatsing1_na.wav"],
  "prioriteit": "normaal"
}
```

- `segments` — lijst bestandsnamen **relatief t.o.v. de audio-map**, in afspeelvolgorde.
- `tekst` — alleen voor de simulator-log/fallback; de player speelt enkel `segments`.
- `prioriteit` — meegestuurd door de engine (altijd `"normaal"`), maar de player **negeert
  dit veld** momenteel; er is geen voorrangsbaan. (Gereserveerd voor later.)
- Geen `segments`? Dan speelt de player niets (alleen een logregel).

## Mapstructuur (op de Pi)

De map `pi/audio-player/audio/` wordt als volume in de container gemount op
`/app/audio`. Je dropt hier WAV-bestanden — **geen rebuild nodig**.

```
audio/
├── events/
│   ├── verplaatsingen/  <eventid>_voor.wav , <eventid>_na.wav   (speler-events)
│   ├── toestanden/      <eventid>_voor.wav , <eventid>_na.wav   (toestand-events)
│   └── wereld-events/   <eventid>_voor.wav , <eventid>_na.wav   (wereld-events)
├── getallen/ 1.wav .. 24.wav
├── spelers/  lilou.wav , zoe.wav , ...   (kleine letters, spaties → _)
└── doelwit/  voor.wav , na.wav
```

## Audiobestanden opnemen & opladen — stappenplan

1. **Neem op** (telefoon/PC) of genereer (TTS) korte clips. Houd ze kort en knip
   stiltes weg aan begin/eind voor vlotte aansluiting.
2. **Exporteer naar WAV** (44.1 kHz, mono of stereo). Met Audacity:
   *Bestand → Exporteren → WAV (16-bit PCM)*.
3. **Benoem volgens de conventie**:
   - Event-begin/eind: `events/<categorie>/<eventid>_voor.wav` en `…_na.wav`, waarbij
     `<categorie>` = `verplaatsingen` (speler-events) / `toestanden` / `wereld-events`
     (afgeleid uit het `categorie`-veld van het event). De `<eventid>` is het `id`-veld.
   - Getallen: `getallen/3.wav` zegt "drie". Deze map wordt óók gebruikt voor een
     **uur-doelwit** (paal 7 → `getallen/7.wav`).
   - Spelers: `spelers/lilou.wav` (exact de spelernaam, kleine letters, spaties → `_`).
   - Vaste omkadering doelwit: `doelwit/voor.wav` ("De volgende doelwitten..."),
     `doelwit/na.wav`.
4. **Kopieer naar de Pi** in `~/Magnum_Opus/pi/audio-player/audio/<submap>/`:
   - via SCP: `scp 3.wav pi@192.168.1.43:~/Magnum_Opus/pi/audio-player/audio/getallen/`
   - of via een Samba-share als je die hebt ingericht.
5. Klaar — de volgende keer dat een event valt, hoor je het resultaat. Geen
   herstart nodig (volume-mount leest live).

## Een nieuw event van audio voorzien

Geef het event in de `[CONFIG]`-inject (Node-RED, tab 06) twee velden:

```json
{
  "id": "verplaatsing1",
  "naam": "verplaatsingMin",
  "tekst": "Minimum x uur vooruit.",
  "getal": "midden",
  "audioVoor": "verplaatsing1_voor.wav",
  "audioNa": "verplaatsing1_na.wav",
  ...
}
```

Node-RED bouwt dan automatisch (submap uit `categorie`, hier `speler` → `verplaatsingen`):
`events/verplaatsingen/verplaatsing1_voor.wav` → `getallen/<gerold getal>.wav` →
`events/verplaatsingen/verplaatsing1_na.wav`.

Voor de doelwitten gebeurt dit altijd automatisch: `doelwit/voor.wav` → per
doelwit `spelers/<naam>.wav` (speler) of `getallen/<n>.wav` (uur/paal) → `doelwit/na.wav`.

## Sfeer-effecten tijdens de reactietijd (`sfxReactie`)

Naast de gesproken afroep kan een event een **geluidseffect** krijgen dat **tijdens de reactietijd**
speelt (zodra het event valt). Zet in de `[CONFIG]`-inject het veld `"sfxReactie": "<bestand>.wav"`;
Node-RED (`Kies event`) hangt dan `sound-effect/<categorie>/<bestand>` **achteraan** de afroep-segmenten.
Huidige koppelingen: bomaanslag → `sound-effect/wereld-events/bomaanslag.wav` (bang), tornado →
`sound-effect/toestanden/tornado.wav`, portalen → `sound-effect/toestanden/portalen.wav`. Elk
**wereld-event** krijgt daarnaast automatisch de generieke sting `sound-effect/wereld-events/woosh.wav`
vooraan. Details: `pi/audio-player/audio/sound-effect/README.md`.

## Installeren / deployen

```bash
ssh pi@192.168.1.43
cd ~/Magnum_Opus && git pull
chmod +x pi/deploy-audio.sh
./pi/deploy-audio.sh
```

Het script bouwt de image, (her)start de container met `--network host`,
`--device=/dev/snd` en de audio-map als volume. De container draait los van
`serial-bridge` (deploy.sh raakt die niet aan).

## Volume regelen (dashboard)

Het volume staat op het dashboard **Buzzer/LED test**, groep **"Geluid (box)"**: een schuif (0–100 %)
plus de knoppen **Stil (30 %)**, **Normaal (70 %)** en **MAX (100 %)**. Buiten op een veld wil je
meestal gewoon MAX.

**Hoe het werkt.** Het dashboard publiceert **retained** op `audio/volume`
(`{"volume": 85}`); `player.py` is daarop geabonneerd en draait

```bash
amixer -q -M -c "$MIXER_CARD" sset "$MIXER_CONTROL" 85%
```

Drie dingen die daarbij van belang zijn:

- **`-M` (mapped)** is essentieel: de bcm2835-schaal is **dB-lineair**. Zonder `-M` klinkt 80 % al
  bijna vol en doet de onderste helft van de schuif niets. Met `-M` loopt hij zoals je verwacht.
- Het volume gaat **niet door de afspeel-wachtrij**, dus het werkt **meteen** — ook midden in een
  lopend geluid.
- Het topic is **retained**: na een container-herstart of een reboot van de Pi krijgt de player de
  laatste stand meteen terug en **herstelt het volume zichzelf**. Je hoeft dit dus niet elke speeldag
  opnieuw te zetten.

De container heeft alles wat daarvoor nodig is al: `alsa-utils` levert **`amixer`** (naast `aplay`),
en `--device=/dev/snd` geeft ook `controlC0` (het mixer-device) door. ALSA-mixerstanden zijn
**kernel-globaal**, dus wat de container zet, geldt meteen voor de hele Pi.

**Klopt de mixer niet?** `player.py` spoort sinds juli 2026 **zowel de kaart als de control zelf op**:
het tast de kaarten uit `aplay -l` (naam én index, plus 0–3 als vangnet) af en kiest de eerste met een
bruikbare control (`Headphone` op Bookworm, `PCM` op oudere images, anders `Master`/`Speaker`). Zo werkt
het volume ook als de kaart níét exact `Headphones` heet — de vorige versie gokte `Headphones` en faalde
dan **stil** (dé reden dat het volume "niets deed"). De keuze staat in de log: `[VOLUME] Mixer
gedetecteerd: kaart '…', control '…'`. Controleer met:

```bash
docker logs --tail 30 audio-player | grep -i volume        # wat detecteerde de player?
docker exec audio-player aplay -l                          # welke kaarten bestaan er
docker exec audio-player amixer -c <kaart> scontrols       # welke controls op die kaart
```

Wijkt jouw Pi af, zet dan `MIXER_CARD` / `MIXER_CONTROL` in `pi/deploy-audio.sh` en draai
`./pi/deploy-audio.sh` opnieuw.

## Geluid testen

```bash
# Lijst van audiokaarten (zoek de analoge uitgang / "Headphones")
aplay -l
# Forceer de analoge jack (i.p.v. HDMI) — eenmalig op de Pi:
sudo raspi-config   # System Options → Audio → Headphones
# Volume: bij voorkeur via het Admin-dashboard (zie boven); handmatig kan met:
alsamixer
# Directe test (host):
aplay ~/Magnum_Opus/pi/audio-player/audio/getallen/3.wav
# Directe test vanuit de container (gebruikt hetzelfde device als de player):
docker exec -it audio-player aplay -D "$AUDIO_DEV" /app/audio/spelers/lilou.wav
```

> **`AUDIO_DEV` (aux-jack kiezen):** standaard `default`. Als `default` op een Pi 4
> naar HDMI gaat, zet je óf de Pi-default op Headphones (`raspi-config`, zie boven),
> óf je zet in `pi/deploy-audio.sh` `AUDIO_DEV` op de analoge kaart uit `aplay -l`
> (bv. `plughw:0,0` of `sysdefault:CARD=Headphones`) en draai je `./pi/deploy-audio.sh`
> opnieuw.

## Foutzoeken

| Symptoom | Oorzaak / oplossing |
|----------|---------------------|
| Geen geluid, log toont "Afgespeeld" | Verkeerde uitgang (HDMI i.p.v. jack) → `raspi-config` audio op Headphones; check het volume (Admin → Geluid). |
| Volumeschuif doet niets | `[VOLUME] Geen mixer-control gevonden` in `docker logs audio-player` → `MIXER_CARD` klopt niet. Zoek de kaart met `aplay -l` en zet hem in `deploy-audio.sh`. |
| Volume springt terug na herstart | Zou niet mogen: `audio/volume` is **retained**. Controleer met `mosquitto_sub -t audio/volume -C 1` of de waarde er nog staat. |
| "Bestand ontbreekt, overgeslagen" | Naam/locatie klopt niet met de conventie. Controleer submap + exacte bestandsnaam. |
| "'aplay' niet gevonden" | `alsa-utils` ontbreekt in image — rebuild met `deploy-audio.sh`. |
| Container ziet `/dev/snd` niet | Start met `--device=/dev/snd` (zit in deploy-audio.sh); voeg user toe aan `audio`-groep. |
| Alles speelt door elkaar | Kan niet: één worker-thread + wachtrij speelt strikt sequentieel. |

## Beperkingen

- WAV-segmenten worden back-to-back afgespeeld; er zit een minieme pauze tussen
  segmenten. Knip stiltes weg voor een vloeiend resultaat.
- De player rapporteert niet terug wanneer hij klaar is; het aftellen in Node-RED
  start na de doelwit-onthulling (reveal-vertraging), niet na het exacte audio-einde.
