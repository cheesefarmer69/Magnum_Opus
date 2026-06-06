# Magnum Opus — Event-systeem & verplaatsingscontrole

> Leidend document voor hoe events bedoeld zijn en hoe de **verplaatsingscontrole** na elk
> event werkt. De belangrijkste sectie is "Verplaatsing = reeks atomaire acties": daar zit het
> concept waarop de portaal-logica eerder fout liep.
>
> **Consistent houden:** bij elke wijziging aan het event-systeem moeten dit document,
> `docs/events.md` en `docs/event-catalogus.md` samen kloppen.

---

## 1. Kernprincipe: scheid "wat gebeurde" van "wat mag"

Elk event beschrijven we met drie dingen:

- **Preconditie** — wat waar moet zijn *vóór* het event mag plaatsvinden.
- **Effect** — welke velden dit event *mag* wijzigen, en hoe. Wat hier niet staat, mag niet veranderen.
- **Invariant** — wat *altijd* waar moet blijven, ongeacht het event.

Twee regels die alles schaalbaar houden:

1. **Centraliseer de invarianten** — schrijf de algemene regels één keer, als checks die ná
   *elk* event draaien. Niet per event herhalen.
2. **Frame-check** — neem een snapshot vóór het event en controleer dat enkel de velden uit het
   effect-lijstje veranderd zijn (bv. een niet-geselecteerde speler die toch bewoog = bug/straf).

Een nieuw event toevoegen = preconditie + toegestane wijzigingen schrijven; het zware werk
(invarianten) staat al klaar.

---

## 2. State-model (wat de validator kent)

```
speler:   id, positie (paal/uur), levensuren, sterftes, (rol)
paal/uur: index, is_happy_hour (goud LED), portaal_partner (paars LED als gezet)
speelveld: voorwaartse_richting (klok loopt ROND), actieve_portalen [{a,b}], happy_hour_palen []
```

In Node-RED: `spelerStats` (levensuren=`totaalUren`, `sterftes`, `huidigePaal`), `spelerLocaties`
(settled paal per speler), `bordStaat[uur].effecten` (toestanden `portaal`/`happy_hour` met
`data.partner` en `resterendeRondes`), `palenActief` (de ring 1..24 in sim).

---

## 3. ⭐ Verplaatsing = reeks atomaire acties

Een verplaatsing is **niet** "speler van X naar Y", maar een **geordende reeks atomaire acties**:

### STAP (vooruit)
- Eén paal vooruit in de voorwaartse richting (klok loopt rond: na 24 → 1).
- Verbruikt **1** budget; levert **1 levensuur** (vóór happy-hour-×2).
- Mag **nooit** achteruit — harde regel op STAP-niveau.

### TELEPORT (portaal)
- Van het ene portaal-eindpunt naar het andere, enkel als dat portaal **actief** is.
- Verbruikt **0** budget; levert **0 levensuren**.
- **Richting-agnostisch**: 13→20 én 20→13 zijn beide geldig (een wormgat, geen stap in de tijd).
- **Optioneel** (een geldig pad mag uit louter STAPpen bestaan, ook al lag er een portaal).
- **Max 1× per portaal per verplaatsing** (geen pingpong).

### De fout die vermeden wordt
Richting **nooit** afleiden uit de netto begin/eind-verplaatsing. `eind < begin` is op zichzelf
**geen** bewijs van vals spel — een legale portaal-sprong kan de eindpaal lager maken. Beoordeel
**actie per actie**; de "niet achteruit"-regel geldt enkel op STAP, nooit op TELEPORT.

### Hoe het pad bekend is (implementatie)
We leiden het pad af uit de **settled paalwissels** (uit de locatiebepaling) tijdens de
reactietijd: een geordende reeks hops `[van,naar]` per speler (`global.pofPad`). Classificatie:
- `{van,naar}` = de twee eindpunten van een **actief portaal** → **TELEPORT** (0 stappen).
- anders: voorwaartse afstand `fd`, achterwaartse `bd` (ring); `fd ≤ bd` → `fd` STAPpen vooruit,
  anders `bd` STAPpen achteruit (= verboden).

> In de **simulatie** is dit deterministisch (drag = settled posities; sleep-op-portaal = de
> A→B-hop tussen de twee paarse palen = TELEPORT). In het **echte spel** zijn de paarse palen het
> portaal; een robuuster expliciet signaal (bv. drukknop) kan later toegevoegd worden. De
> logica-laag is correct gegeven correcte settled-input; de sensingkwaliteit is de ondergrens.

---

## 4. De evenement-cyclus

1. **Aanloop** — timer telt af.
2. **Event kiezen + tonen** — respecteer de `max`/`getal`-grenzen van het event.
3. **Doelwitten bekendmaken** — wie is geselecteerd (afroep: aantal + zelfst.nw + tekst).
4. **Reactietijd** — geselecteerde spelers bewegen; pad wordt opgenomen.
5. **Controle** — de verplaatsingscontrole draait (sectie 7): pad actie-per-actie + invarianten.
6. **Toestanden opschonen** — verlopen toestanden (`resterendeRondes ≤ 0`) verdwijnen; portalen/
   happy hour die nog actief zijn blijven.

Portalen en happy hour zijn **toestanden** (open in hun eigen event, blijven actief tot ze
aflopen of voor altijd als er geen einde is).

---

## 5. Globale invarianten (na ELK event)

1. Geen speler heeft negatieve levensuren (clamp op 0; onder 0 → +1 sterfte).
2. Elke `speler.positie` is een bestaande paal.
3. **Frame-check**: geen niet-geselecteerde speler veranderde van positie of levensuren ongestraft.
4. Elke STAP ging vooruit (TELEPORT is de enige uitzondering; geen achteruit-events voorlopig).
5. Niemand verbruikte meer budget dan het event toestaat.

---

## 6. De events

### Event A — "maximum x vooruit" (`verplaatsing2`)
Geselecteerde spelers mogen tot `x` palen vooruit; minder mag, meer niet, achteruit niet.
- **Preconditie**: elke geselecteerde speler staat op een geldige paal.
- **Effect**: `positie` via geldige verplaatsing met `aantal_STAP ≤ x`; `levensuren += verdiend`.
- **Invariant**: `aantal_STAP ≤ x`; elke STAP vooruit.

### Event B — "portalen" (`portalen`, toestand)
Twee palen worden paars; er ontstaat een portaal. Verplaatst zelf niemand.
- **Preconditie**: twee bestaande palen, nog niet aan een portaal gekoppeld.
- **Effect**: `actieve_portalen += {a,b}`; beide palen paars (LED actie 1).
- **Invariant**: een portaal koppelt precies twee verschillende palen. `max: 1`.

### Event C — "happy hour" (`happy_hour`, toestand)
Eén of meer uren worden goud; wie zijn verplaatsing op zo'n uur **eindigt** krijgt de levensuren
van die verplaatsing verdubbeld.
- **Preconditie**: bestaande paal/palen.
- **Effect**: `happy_hour_palen += index`; goud (LED actie 2).
- **Invariant**: beïnvloedt enkel de levensuren-berekening bij verplaatsing, niet budget/positie.
  `max: 4`.

---

## 7. Verplaatsingscontrole + scoring (Verifieer beweging)

Per speler, op basis van het opgenomen pad (`pofPad[speler]`):

1. Classificeer elke hop → `voor` (STAP vooruit), `achter` (STAP achteruit, verboden), TELEPORT
   (0, max 1×/portaal — anders ongeldig).
2. **Beoordeel + scoor** (Δ = levensuren toegekend/afgetrokken):

| Geval | Status | Δ |
|------|--------|---|
| doelwit, geldig (`achter=0`, `voor ≤ x`) | OK | **+voor** (×2 als eindpaal happy hour) |
| doelwit, `voor > x` | TE VEEL | **−(voor − x)** |
| doelwit, `achter > 0` | TERUG IN TIJD | **−achter** |
| doelwit, >1× zelfde portaal | ONGELDIGE TELEPORT | **−voor** |
| niet-(bewegings)doelwit dat bewoog | BEWOOG (mocht niet) | **−(voor+achter)** |
| stil blijven staan | OK (stil) | 0 |

3. **Sterfte**: zou Δ de levensuren onder 0 brengen → blijf op 0 en **+1 sterfte** (speler speelt
   door; legale winst geeft nooit een sterfte).
4. `mag_niet_bewegen`-effect → Δ = 0.
5. De controle meldt per speler **welke regel brak + waarden** (status + Δ) naar de tabel
   **Controle** en `pof/controle`. TELEPORTs tellen niet mee in `voor` (dus niet in de levensuren).

### Levensuren-berekening
```
basis = aantal STAP-acties (teleports tellen niet)
verdiend = (eindpaal happy hour) ? 2*basis : basis
```

---

## 8. Grenzen & scheidsrechter-override

- De logica is deterministisch correct **gegeven correcte settled-input**; in de simulatie volledig
  toetsbaar (geen RSSI-ruis).
- Op hardware is de **sensingkwaliteit de ondergrens**: slechte dwell-detectie = foute input.
- Sub-sensing valsspel (te snel om te settelen, eindigt legaal) kan ontsnappen; gedetecteerde
  tussenstappen worden wél bestraft.
- **Scheidsrechter-override** (Bediening-pagina, groep "Scheidsrechter"): typ `"<naam> +5"` om
  levensuren te corrigeren of `"<naam> s+1"` voor sterftes — menselijke laatste instantie bij
  disputen. Werkt op de gedeelde globale stats (dus ook voor de sim).

---

## 9. Aannames (bevestigd)
1. Levensuren-basis = aantal STAP-acties (1 stap = 1 levensuur).
2. Klok loopt **rond** (na 24 → 1).
3. Max **1 teleport per portaal** per verplaatsing.
4. STAP achteruit altijd verboden; geen achteruit-events (voorlopig).
