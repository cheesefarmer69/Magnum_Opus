// ============================================================
// Magnum Opus simulator — virtuele testomgeving
//
// Praat MQTT-over-WebSocket met de Mosquitto broker en biedt
// twee modi:
//   - monitor: subscribe-only (kijk passief mee met een echt spel)
//   - simulatie: publish 'plaatjes/data' op basis van virtuele
//     speler-posities, vervangt de hardware op de bus
//
// Visualisatie: bovenaanzicht van het speelveld zoals beschreven
// in docs/playfield.md (24-hoek, R=11.50 m), met per paal een
// LED-bolletje dat de actuele kleur toont op basis van
// commando/master1..3-berichten (actie 0=uit, 1=portaal, 2=happy hour, 3=piep).
// ============================================================

// --- VELD-GEOMETRIE (uit docs/playfield.md) ---
const AANTAL_PALEN  = 24;
const R_BUITEN_M    = 11.50;
const R_BINNEN_M    = 8.00;
const HOEK_PER_PAAL = (2 * Math.PI) / AANTAL_PALEN;  // 15° in radialen

// Rotatie van het hele veld: paal 24 staat exact bovenaan (12-uur-positie).
// -90° brengt de hoek naar boven; +7.5° (een halve sector) zet paal 24 (i.p.v. de
// grens tussen 24 en 1) precies bovenaan. Resultaat: 24 boven, 6 rechts, 12 onder, 18 links.
const VELD_OFFSET = -Math.PI / 2 + Math.PI / 24;  // -82.5°

// SVG viewBox is -300..300 in pixels; meter->pixel schaal:
const M_TO_PX = 22;  // 11.50 m * 22 = 253 px straal -> past binnen 300 marge

function paalPositie(n) {
    // Paal n staat op het MIDDEN van de n-de buitenzijde (niet op een hoekpunt).
    // De hoek is (n - 0.5) × 15°: halverwege tussen hoekpunt n-1 en hoekpunt n.
    // Index 0 ongebruikt zodat paal_id 1..24 rechtstreeks aansluit.
    const hoek = (n - 0.5) * HOEK_PER_PAAL + VELD_OFFSET;
    return { x: R_BUITEN_M * Math.cos(hoek), y: R_BUITEN_M * Math.sin(hoek) };
}

// Geeft het uur (sectie 1-24) op basis van de hoek van positie (x,y).
function welkUur(x, y) {
    const offsetDeg = -VELD_OFFSET * 180 / Math.PI;   // 82.5° — compenseert VELD_OFFSET
    const deg = (Math.atan2(y, x) * 180 / Math.PI + 360) % 360;
    return (Math.floor(((deg + offsetDeg) % 360) / 15) % AANTAL_PALEN) + 1;
}

// Een speler staat "uit" (buiten het speelveld, dus ongedetecteerd) als zijn bolletje voorbij de
// buitenrand of binnen het centrale gat valt. Zo'n speler wordt NIET op sim/locatie gepubliceerd en
// verdwijnt uit spelerLocaties — handig voor het NUKE-event (ontkomen = veilig).
const UIT_MARGE = 0.6;  // meter
function isUit(sp) {
    const r = Math.hypot(sp.x, sp.y);
    return r > R_BUITEN_M + UIT_MARGE || r < R_BINNEN_M - UIT_MARGE;
}

// --- GEEN RSSI-MODEL ---
// De simulator test het spelverloop, niet de hardware. In simulatiemodus
// stuurt hij de exacte paal van elke speler direct door (topic sim/locatie),
// zodat de locatie deterministisch is. Geen ruis, geen path-loss.
const BUZZER_PIEP_MS = 600;   // moet overeenkomen met ACTIE_BUZZER_PIEP op de slave

// --- ACTIE-ID TABEL (uit firmware/Slave/src/main.cpp) ---
// Minimale set: 0 = uit, 1 = portaal (paars), 2 = happy hour (goud), 3 = buzzer-piep.
const ACTIE_NAAM = {
    0: "NIETS", 1: "PORTAAL", 2: "HAPPY_HOUR", 3: "BUZZER_PIEP",
    4: "MEDICIJN", 5: "ZIEK_W3", 6: "ZIEK_W2", 7: "ZIEK_W1",
    8: "NUKE", 9: "MN_OPEN", 10: "MN_DICHT", 11: "OOGST"
};
const ACTIE_BUZZER_PIEP = 3;
const ZIEKTE_WAARSCH_MS = 3500;   // hoe lang het kloppend-hart-icoon zichtbaar blijft
const SOLID_KLEUR = {
    0: "#cccccc", 1: "#9c27b0", 2: "#ffb400", 4: "#ff1493",  // 4 = medicijn (felroze)
    9: "#b4c8ff", 10: "#dc0000"                              // 9 = middernacht open (wit), 10 = dicht (rood)
};   // 8 (nuke) en 11 (oogst) worden geanimeerd in renderLeds()

// --- SPELERS (default uit Node-RED config-flow) ---
const GROEP_KLEUR_CSS = { rood: "#c62828", zwart: "#212121", blauw: "#1565c0" };

// Naam → kleurgroep (polsbandje). Bron: docs/spel/spelers.md — bij wijziging dáár ook hier bijwerken.
const SPELER_KLEURGROEP = {
    "Aagje": "rood", "Alix D": "rood", "Attah": "rood", "Emma": "rood", "Blanche": "rood",
    "Casper": "rood", "Elias": "rood", "Tobin": "rood", "Margaux": "rood", "Louisa": "rood",
    "Jinte": "zwart", "Aster": "zwart", "Suzan": "zwart", "Lotta": "zwart", "Elisa": "zwart",
    "Maud": "zwart", "Anna": "zwart", "Lilou": "zwart", "Marie S": "zwart", "Lore": "zwart",
    "Marie D": "blauw", "Mauro": "blauw", "Amélie": "blauw", "Mien": "blauw", "Alix R": "blauw",
    "Mila": "blauw", "Ina": "blauw", "Stelle": "blauw", "Estée": "blauw", "Lola": "blauw", "Zoë": "blauw"
};
function groepKleurVoor(naam) { return GROEP_KLEUR_CSS[SPELER_KLEURGROEP[naam]] || "#ccc"; }

const DEFAULT_SPELERS = [
    { naam: "Lilou",  mac: "48:87:2d:9d:bb:7d", kleur: "#e91e63" },
    { naam: "Zoë",    mac: "48:87:2d:9d:ba:5c", kleur: "#9c27b0" },
    { naam: "Louisa", mac: "48:87:2d:9d:ba:cc", kleur: "#3f51b5" },
    { naam: "Lola",   mac: "48:87:2d:9d:ba:5f", kleur: "#03a9f4" },
    { naam: "Maud",   mac: "48:87:2d:9d:bb:0b", kleur: "#4caf50" },
    { naam: "Mien",   mac: "48:87:2d:9d:ba:a5", kleur: "#ff5722" }
];

// --- GLOBALE STATE ---
const state = {
    client: null,
    verbonden: false,
    modus: "monitor",           // "monitor" of "sim"
    spelers: [],                // {naam, mac, kleur, x, y, auto, drag}
    paalActie: new Array(AANTAL_PALEN + 1).fill(0),  // actie-ID per paal
    paalLaatsteCmd: new Array(AANTAL_PALEN + 1).fill(0),  // ms
    paalBuzzer: new Array(AANTAL_PALEN + 1).fill(0),  // buzzer-icoon actief tot (ms)
    paalHart: new Array(AANTAL_PALEN + 1).fill(null), // ziekte-waarschuwing {tot, slagen} per paal
    portalen: [],               // actieve portaal-paren [{palen:[a,b]}] (uit pof/portalen)
    toestanden: [],             // actieve uur-effecten [{uur,effect,naam,resterendeRondes}] (uit pof/toestanden)
    ziekte: [],                 // actieve zieke spelers [{speler,rondesOver,uur}] (uit pof/ziekte, retained)
    dienaars: {},               // { geoogsteNaam: meesterNaam } (uit pof/dienaars, retained)
    middernacht: null,          // { index, open, remaining, eventsTotOogst, paal } (uit pof/middernacht)
    middernachtAan: true,       // middernacht-mechanisme aan/uit (checkbox -> sim/middernacht-config)
    uitAllen: false,            // true wanneer de 'Out'-knop iedereen het veld uit zette
    events: [],                 // volledige events-pool [{id,naam,categorie,tekst,...}] (uit pof/events, retained)
    uitgeslotenEvents: new Set(), // event-id's die NIET in aanmerking komen (checkbox UI)
    geselecteerdePaal: null,
    tickMs: 250,
    publishMs: 250              // hoe vaak detecties publiceren in sim-modus
};

// ============================================================
// MQTT
// ============================================================
function connecteer() {
    const host = document.getElementById("broker-host").value.trim();
    const port = document.getElementById("broker-port").value.trim();
    const url  = `ws://${host}:${port}`;
    log("info", `Verbinden met ${url} ...`);

    try {
        state.client = mqtt.connect(url, { reconnectPeriod: 2000, clientId: "magnum-sim-" + Math.random().toString(16).slice(2) });
    } catch (e) {
        log("err", "Connectie kon niet starten: " + e.message);
        return;
    }

    state.client.on("connect", () => {
        state.verbonden = true;
        zetStatus("online");
        log("info", "Verbonden.");
        state.client.subscribe(["commando/master1", "commando/master2", "commando/master3", "audio/afspelen", "plaatjes/data", "pof/status", "pof/controle", "pof/portalen", "pof/toestanden", "pof/ziekte", "pof/middernacht", "pof/dienaars", "pof/events", "locatie/spelers", "spel/historie"]);
        publishModus();   // laat Node-RED weten of het 24-uur veld actief is
        publishUitgeslotenEvents();   // synchroniseer de event-checkboxes (retained)
        publishMiddernachtConfig();   // synchroniseer de middernacht-aan/uit-checkbox (retained)
    });
    state.client.on("reconnect", () => log("info", "Reconnecting..."));
    state.client.on("offline",   () => { state.verbonden = false; zetStatus("offline"); log("err", "Offline."); });
    state.client.on("error",     (e) => log("err", "MQTT-fout: " + e.message));
    state.client.on("message", (topic, payload) => verwerkBericht(topic, payload.toString()));
}

// Bouwt de tekst die effectief voorgelezen wordt: het aantal getroffen doelwitten +
// zelfstandig naamwoord (enkel/meervoud) + de event-tekst. Bv. "3 uren worden Happy Hour."
function afroepTekst(data) {
    const tekst = data.eventTekst || "";
    if (data.doelwitType === "groep") return "een groep " + tekst;
    const cnt = Array.isArray(data.doelwit) ? data.doelwit.length : 0;
    if (!cnt || (data.doelwitType !== "speler" && data.doelwitType !== "uur")) return tekst;
    const enkel = cnt === 1;
    const woord = data.doelwitType === "uur" ? (enkel ? "uur" : "uren") : (enkel ? "speler" : "spelers");
    return cnt + " " + woord + " " + tekst;
}

function verwerkBericht(topic, raw) {
    let data;
    try { data = JSON.parse(raw); } catch { log("err", `Bad JSON op ${topic}: ${raw}`); return; }

    if (topic.startsWith("commando/master")) {
        const paal = data.paal, actie = data.actie;
        if (paal >= 1 && paal <= AANTAL_PALEN) {
            if (actie === ACTIE_BUZZER_PIEP) {
                // buzzer-piep: toon icoon voor de duur van de piep, laat paalActie ongemoeid
                state.paalBuzzer[paal] = Date.now() + BUZZER_PIEP_MS;
            } else if (actie >= 5 && actie <= 7) {
                // ziekte-waarschuwing (monitor + kloppend hart): N slagen = 8 - actie (5→3, 6→2, 7→1)
                state.paalHart[paal] = { tot: Date.now() + ZIEKTE_WAARSCH_MS, slagen: 8 - actie };
            } else {
                state.paalActie[paal] = actie;
            }
            state.paalLaatsteCmd[paal] = Date.now();
        }
        log("cmd", `paal ${paal} → ${ACTIE_NAAM[actie] || "?"} (${actie})`);
    } else if (topic === "audio/afspelen") {
        log("audio", `[${data.fase || "?"}] ${data.tekst || ""}`);
    } else if (topic === "pof/controle") {
        const ev = data.event ? " [" + data.event + "]" : "";
        if (data.event === "Nuke") {
            // NUKE-controle: toon ontploft/veilig en laat het veld kort flitsen.
            let ontploft = 0;
            (data.resultaten || []).forEach(r => {
                if (r.status === "ONTPLOFT") { ontploft++; log("foutcode", `💥 ${r.speler}: ONTPLOFT — alle levensuren weg + sterfte${ev}`); }
                else log("info", `✓ ${r.speler}: ${r.status}${ev}`);
            });
            if (ontploft) nukeFlits();
            return;
        }
        const FOUT = new Set(["TE WEINIG", "TE VEEL", "ONGELDIGE KEUZE", "TERUG IN TIJD", "BEWOOG (mocht niet)"]);
        let overtredingen = 0;
        (data.resultaten || []).forEach(r => {
            if (FOUT.has(r.status)) {
                overtredingen++;
                const d = (r.delta != null) ? `, ${r.delta >= 0 ? "+" : ""}${r.delta} uur` : "";
                log("foutcode", `${r.speler}: ${r.status} (verpl ${r.verplaatst}${d})${ev}`);
            }
        });
        if (!overtredingen) log("info", `Controle OK${ev} — alle regels voldaan`);
    } else if (topic === "pof/status") {
        const timerEl = document.getElementById("pof-timer");
        const naamEl  = document.getElementById("pof-event-naam");
        const doelEl  = document.getElementById("pof-doelwit");
        const tellerEl = document.getElementById("pof-teller");
        if (!timerEl) return;
        if (tellerEl) tellerEl.textContent = (data.eventenRonde != null ? data.eventenRonde : 0);
        if (!data.actief || data.fase === "idle") {
            timerEl.textContent = "—";
            naamEl.textContent  = "";
            doelEl.textContent  = "—";
            // Defensief: bij een gestopt spel lokaal alle speler-toestanden leegmaken, zodat een
            // speler die vorig spel ziek/dienaar was niet blijft hangen als een retained bericht hapert.
            if (state.ziekte.length || Object.keys(state.dienaars || {}).length) {
                state.ziekte = []; state.dienaars = {};
                renderZiekte(); renderSpelers(); renderZijbalk();
            }
        } else if (data.fase === "aanloop") {
            timerEl.textContent = (data.teller != null) ? data.teller + "s" : "…";
            naamEl.textContent  = "Aanloop…";
            doelEl.textContent  = "—";
        } else if (data.fase === "regroup") {
            timerEl.textContent = (data.teller != null) ? data.teller + "s" : "…";
            naamEl.textContent  = "☢️ Hergroeperen na NUKE…";
            doelEl.textContent  = "Het spel hervat zo.";
        } else {
            timerEl.textContent = (data.fase === "bezig") ? "…"
                : (data.teller != null) ? data.teller + "s" : "…";
            const isNuke = data.eventNaam === "Nuke";
            // Toon enkel de tekst die effectief wordt voorgelezen: "<aantal> <speler/uur> <event-tekst>".
            naamEl.textContent = (isNuke && data.fase === "reactie") ? "☢️ NUKE — wegwezen!" : afroepTekst(data);
            const groepFallback = data.doelwitType === "groep" && data.groepLabel ? "• Groep " + data.groepLabel : "";
            const reveal = data.doelwitReveal
                || groepFallback
                || (Array.isArray(data.doelwit) ? data.doelwit.map(d => "• " + d).join("\n") : "");
            doelEl.textContent = reveal || "—";
        }
        // Volgende-events-wachtrij (preview; kan licht afwijken zodra een max-grens bijt).
        const wachtEl = document.getElementById("pof-wachtrij");
        if (wachtEl) {
            const w = Array.isArray(data.wachtrij) ? data.wachtrij : [];
            wachtEl.innerHTML = w.length
                ? w.map(e => `<li>${e.naam || e.id}</li>`).join("")
                : '<li class="pof-wachtrij-leeg">—</li>';
        }
    } else if (topic === "locatie/spelers") {
        // Opgeloste locaties uit het Node-RED algoritme (NIET de ruwe paal-berichten).
        // Dit is de enige bron van speler-posities in monitor-modus.
        if (state.modus === "monitor") {
            const f = (R_BUITEN_M - 2.5) / R_BUITEN_M;
            for (const naam in data) {
                const paal = data[naam];
                const sp = state.spelers.find(s => s.naam === naam);
                if (sp && paal >= 1 && paal <= AANTAL_PALEN) {
                    const p = paalPositie(paal);
                    sp.x = p.x * f; sp.y = p.y * f;
                }
            }
            renderSpelers();
        }
    } else if (topic === "pof/portalen") {
        // Actieve portaal-paren: teken de verbindingslijn(en) en onthoud de koppeling.
        const vorig = state.portalen.length;
        state.portalen = Array.isArray(data) ? data : [];
        renderPortalen();
        if (state.portalen.length !== vorig) {
            const tekst = state.portalen.map(p => p.palen.join("↔")).join(", ");
            log("info", state.portalen.length ? `Portaal actief: ${tekst}` : "Portaal gesloten");
        }
    } else if (topic === "pof/toestanden") {
        // Actieve uur-toestanden (tags) voor het Toestanden-paneel.
        const vorige = state.toestanden || [];
        const nieuw = Array.isArray(data) ? data : [];
        const sleutel = (t) => (t.effect || "?") + "@" + t.uur;
        const nieuwSet = new Set(nieuw.map(sleutel));
        for (const t of vorige) {
            if (!nieuwSet.has(sleutel(t))) log("info", `Toestand afgelopen: ${t.naam || t.effect} op uur ${t.uur}`);
        }
        state.toestanden = nieuw;
        renderToestanden();
        renderZiekteIconen();
    } else if (topic === "pof/ziekte") {
        const vorige = state.ziekte || [];
        const nieuw = Array.isArray(data) ? data : [];
        const vorigeSet = new Set(vorige.map(z => z.speler));
        const nieuwSet = new Set(nieuw.map(z => z.speler));
        for (const z of nieuw) if (!vorigeSet.has(z.speler)) log("info", `${z.speler} is ziek geworden (${z.rondesOver} events)`);
        for (const z of vorige) if (!nieuwSet.has(z.speler)) log("info", `${z.speler} is niet langer ziek (genezen of gestorven)`);
        state.ziekte = nieuw;
        renderZiekte();
        renderSpelers();
    } else if (topic === "pof/middernacht") {
        state.middernacht = (data && typeof data === "object") ? data : null;
        renderMiddernacht();
    } else if (topic === "pof/dienaars") {
        const vorige = state.dienaars || {};
        const nieuw = (data && typeof data === "object") ? data : {};
        for (const naam in nieuw) if (!vorige[naam]) log("foutcode", `☠️ ${naam} is geoogst op middernacht → dient nu ${nieuw[naam]}`);
        state.dienaars = nieuw;
        renderZijbalk();
        renderSpelers();
    } else if (topic === "pof/events") {
        state.events = Array.isArray(data) ? data : [];
        renderEvents();
        log("info", `Events-pool ontvangen: ${state.events.length} events`);
    } else if (topic === "spel/historie") {
        renderHistorie(data);
    } else if (topic === "plaatjes/data") {
        // Alleen loggen in monitor-modus; posities komen via 'locatie/spelers'.
        if (state.modus === "monitor") {
            log("data", `paal ${data.paal}: ${data.mac || ("batt=" + data.batt)} ${data.rssi !== undefined ? data.rssi + " dBm" : ""}`);
        }
    }
}

function renderHistorie(data) {
    const lijst = document.getElementById("histo-lijst");
    if (!lijst) return;
    const events = (data && data.events) || [];
    if (!events.length) {
        lijst.innerHTML = '<div class="histo-leeg">Nog geen events in dit spel.</div>';
        return;
    }
    lijst.innerHTML = "";
    events.forEach(e => {
        const div = document.createElement("div");
        div.className = "histo-event";
        const doel = (e.doelwit && e.doelwit.length) ? ` → ${e.doelwit.join(", ")}` : "";
        div.innerHTML = `<b>${e.nr}.</b> ${e.tekst}<span class="histo-doel">${doel}</span>`;
        lijst.appendChild(div);
    });
}

// Toont de actieve toestanden, gegroepeerd per tag, met per uur het aantal
// resterende events (rondes) dat de toestand nog blijft.
function renderToestanden() {
    const el = document.getElementById("toestanden-lijst");
    if (!el) return;
    const lijst = state.toestanden || [];
    if (!lijst.length) {
        el.innerHTML = '<div class="toestand-leeg">Geen actieve toestanden.</div>';
        return;
    }
    const perTag = {};
    for (const t of lijst) {
        const tag = t.naam || t.effect || "?";
        (perTag[tag] = perTag[tag] || []).push(t);
    }
    el.innerHTML = "";
    for (const tag of Object.keys(perTag).sort()) {
        const items = perTag[tag].slice().sort((a, b) => a.uur - b.uur);
        const uren = items.map(t => {
            // resterendeRondes >= 9000 = permanent (engine-sentinel 9999, telt nooit af) → "(/)"
            const r = (t.resterendeRondes == null) ? ""
                : (t.resterendeRondes >= 9000) ? ` <span class="toestand-rondes">(/)</span>`
                : ` <span class="toestand-rondes">(nog ${t.resterendeRondes})</span>`;
            return `uur ${t.uur}${r}`;
        }).join(", ");
        const div = document.createElement("div");
        div.className = "toestand-groep";
        div.innerHTML = `<b>${tag}</b>: ${uren}`;
        el.appendChild(div);
    }
}

// Sidebar-paneel: lijst van zieke spelers met resterende events; hart-emoji bij <= 3.
function renderZiekte() {
    const el = document.getElementById("ziekte-lijst");
    if (!el) return;
    const lijst = state.ziekte || [];
    if (!lijst.length) {
        el.innerHTML = '<div class="ziekte-leeg">Geen zieke spelers.</div>';
        return;
    }
    el.innerHTML = "";
    for (const z of lijst.slice().sort((a, b) => a.rondesOver - b.rondesOver)) {
        const div = document.createElement("div");
        div.className = "ziekte-rij" + (z.rondesOver <= 3 ? " ziekte-kritiek" : "");
        const hart = z.rondesOver <= 3 ? ` <span class="ziekte-hart">${"❤".repeat(z.rondesOver)}</span>` : "";
        div.innerHTML = `🤒 <b>${z.speler}</b> <span class="ziekte-rondes">nog ${z.rondesOver}</span>${hart}`;
        el.appendChild(div);
    }
}

// Middernacht-paneel: pi-cijfer-index, poort open/dicht, resterende events in de fase en
// binnen hoeveel events de volgende oogst valt.
function renderMiddernacht() {
    const el = document.getElementById("middernacht-lijst");
    if (!el) return;
    const cb = document.getElementById("middernacht-aan");
    if (cb) cb.checked = state.middernachtAan;
    if (!state.middernachtAan) {
        el.innerHTML = '<div class="middernacht-leeg">Uitgeschakeld — de hoogste paal is een gewoon uur.</div>';
        return;
    }
    const m = state.middernacht;
    if (!m) { el.innerHTML = '<div class="middernacht-leeg">Nog geen data.</div>'; return; }
    const open = m.open;
    el.innerHTML =
        `<div class="mn-poort ${open ? "mn-open" : "mn-dicht"}">${open ? "🟢 POORT OPEN" : "🔴 POORT DICHT"}</div>` +
        `<div class="mn-rij">Pi-cijfer-index: <b>${m.index != null ? m.index : "?"}</b></div>` +
        `<div class="mn-rij">Nog <b>${m.remaining != null ? m.remaining : "?"}</b> events in deze fase</div>` +
        `<div class="mn-rij mn-oogst">Oogst over <b>${m.eventsTotOogst != null ? m.eventsTotOogst : "?"}</b> events</div>` +
        `<div class="mn-rij mn-paal">Middernacht-paal: <b>${m.paal != null ? m.paal : "?"}</b> (oversteek geblokkeerd bij dichte poort)</div>`;
}

// Veld-iconen: 💊 op medicijn-uren (uit pof/toestanden) en een kloppend hart ❤×N op palen
// die net een ziekte-waarschuwing kregen (uit commando/master1 acties 5/6/7).
function renderZiekteIconen() {
    const grp = document.getElementById("ziekte-iconen");
    if (!grp) return;
    grp.innerHTML = "";
    const nu = Date.now();
    // 💊 op elk medicijn-uur
    for (const t of (state.toestanden || [])) {
        if (t.effect !== "medicijn") continue;
        const p = paalPositie(t.uur);
        const hoek = Math.atan2(p.y, p.x);
        const tx = document.createElementNS(NS, "text");
        tx.setAttribute("x", (p.x + Math.cos(hoek) * 2.4) * M_TO_PX);
        tx.setAttribute("y", (p.y + Math.sin(hoek) * 2.4) * M_TO_PX);
        tx.setAttribute("class", "medicijn-icoon");
        tx.textContent = "💊";
        grp.appendChild(tx);
    }
    // ❤×N op palen met een actieve waarschuwing
    for (let n = 1; n <= AANTAL_PALEN; n++) {
        const h = state.paalHart[n];
        if (!h || h.tot <= nu) continue;
        const p = paalPositie(n);
        const hoek = Math.atan2(p.y, p.x);
        const tx = document.createElementNS(NS, "text");
        tx.setAttribute("x", (p.x - Math.cos(hoek) * 2.2) * M_TO_PX);
        tx.setAttribute("y", (p.y - Math.sin(hoek) * 2.2) * M_TO_PX);
        tx.setAttribute("class", "hart-icoon");
        tx.textContent = "❤".repeat(h.slagen);
        grp.appendChild(tx);
    }
}

function renderEvents() {
    const el = document.getElementById("events-lijst");
    if (!el) return;
    const lijst = state.events || [];
    if (!lijst.length) {
        el.innerHTML = '<div class="events-leeg">Nog geen events ontvangen.</div>';
        return;
    }

    const OPTIE_BEREIK = { enkel: "1", laag: "1–3", midden: "1–6", hoog: "3–10" };
    const CAT_LABEL    = { speler: "Verplaatsing", toestand: "Toestand", wereld: "Wereld" };

    function bereik(g) {
        if (g == null) return null;
        if (Array.isArray(g)) return g[0] + "–" + g[1];
        return OPTIE_BEREIK[g] || String(g);
    }

    el.innerHTML = "";
    const perCat = {};
    for (const e of lijst) (perCat[e.categorie || "wereld"] = perCat[e.categorie || "wereld"] || []).push(e);

    for (const cat of ["speler", "toestand", "wereld"]) {
        const ev = perCat[cat] || [];
        if (!ev.length) continue;

        const hdr = document.createElement("div");
        hdr.className = "events-cat-hdr events-cat-" + cat;
        hdr.textContent = CAT_LABEL[cat] || cat;
        el.appendChild(hdr);

        for (const e of ev) {
            const card = document.createElement("div");
            const uitgesloten = state.uitgeslotenEvents.has(e.id);
            card.className = "events-card events-cat-" + cat + (uitgesloten ? " events-uitgeschakeld" : "");

            const cb = document.createElement("input");
            cb.type = "checkbox";
            cb.className = "events-actief-cb";
            cb.checked = !uitgesloten;
            cb.title = "Event in-/uitschakelen";
            cb.addEventListener("change", () => {
                if (cb.checked) state.uitgeslotenEvents.delete(e.id);
                else state.uitgeslotenEvents.add(e.id);
                publishUitgeslotenEvents();
                renderEvents();
            });

            const chips = [];
            const d = e.doelwit;
            if (d && d.type === "groep") {
                chips.push("groep: " + (d.veld || "?") + (d.waarde ? " = " + d.waarde : " (" + (d.selectie || "?") + ")"));
            } else if (d && d.type && d.type !== "geen") {
                const a = bereik(d.aantal) || "?";
                chips.push(a + " " + d.type + " (" + (d.selectie || "?") + ")");
            }
            if (e.getal) {
                const xb = bereik(e.getal);
                chips.push(e.getal2 ? "x=" + xb + " / y=" + bereik(e.getal2) : "x=" + xb);
            }
            if (e.voorwaarde && e.voorwaarde !== "geen") chips.push(e.voorwaarde);
            if (e.max != null) chips.push("max " + e.max + "×");
            if (e.duratie != null) {
                const dur = Array.isArray(e.duratie) ? e.duratie.join("–") : e.duratie;
                chips.push("duratie " + dur + " rnds");
            }

            card.innerHTML =
                `<div class="events-naam">${e.naam || e.id}</div>` +
                `<div class="events-tekst">${e.tekst || ""}</div>` +
                (chips.length ? `<div class="events-chips">${chips.map(c => `<span class="events-chip">${c}</span>`).join("")}</div>` : "");
            card.appendChild(cb);   // ná innerHTML, anders wist de toewijzing de checkbox
            el.appendChild(card);
        }
    }
}

function publishLocaties() {
    if (!state.verbonden || state.modus !== "sim") return;
    // Deterministisch: publiceer per speler de SETTLED paal. Tijdens het slepen blijft de
    // gepubliceerde paal bevroren op de laatste settled waarde; pas bij loslaten (drop)
    // wordt de nieuwe paal gepubliceerd. Zo levert één zet exact één settled paalwissel op
    // i.p.v. een reeks transiënte tussenstappen langs de cursorbaan — net zoals de hysterese
    // in "Locatiebepaling Spelers" dat op echte hardware doet. Dit houdt de pad-gebaseerde
    // verplaatsingscontrole (Verifieer beweging) zuiver: geen valse "TE VEEL"/"TERUG IN TIJD".
    // Spelers die "uit" staan (buiten het veld) worden NIET gepubliceerd → ongedetecteerd.
    const lijst = state.spelers.filter(sp => !isUit(sp)).map(sp => {
        if (!sp.drag) sp.paal = welkUur(sp.x, sp.y);   // settelen enkel als niet gesleept
        return { mac: sp.mac, paal: (sp.paal != null ? sp.paal : welkUur(sp.x, sp.y)) };
    });
    state.client.publish("sim/locatie", JSON.stringify(lijst));
}

function publishModus() {
    if (!state.verbonden) return;
    state.client.publish("sim/modus", JSON.stringify({ sim24: state.modus === "sim" }));
}

// Korte rood/wit-flits over het speelveld bij een NUKE-ontploffing.
function nukeFlits() {
    const wrap = document.getElementById("veld-wrapper");
    if (!wrap) return;
    wrap.classList.add("nuke-flits");
    setTimeout(() => wrap.classList.remove("nuke-flits"), 900);
}

function publishUitgeslotenEvents() {
    if (!state.verbonden) return;
    state.client.publish("sim/events-config", JSON.stringify({ uitgesloten: [...state.uitgeslotenEvents] }), { retain: true });
}

function publishMiddernachtConfig() {
    if (!state.verbonden) return;
    state.client.publish("sim/middernacht-config", JSON.stringify({ aan: state.middernachtAan }), { retain: true });
}

// ============================================================
// RENDER
// ============================================================
const NS = "http://www.w3.org/2000/svg";

function tekenVeld() {
    const ringen = document.getElementById("ringen");
    const spaken = document.getElementById("spaken");
    const paaltjes = document.getElementById("paaltjes");
    const ledBolletjes = document.getElementById("led-bolletjes");
    const paalLabels = document.getElementById("paal-labels");

    // Buiten- en binnenpolygoon als <polygon>
    const buitenPts = [], binnenPts = [];
    for (let i = 0; i < AANTAL_PALEN; i++) {
        const hoek = i * HOEK_PER_PAAL + VELD_OFFSET;
        buitenPts.push((R_BUITEN_M * Math.cos(hoek) * M_TO_PX) + "," + (R_BUITEN_M * Math.sin(hoek) * M_TO_PX));
        binnenPts.push((R_BINNEN_M * Math.cos(hoek) * M_TO_PX) + "," + (R_BINNEN_M * Math.sin(hoek) * M_TO_PX));
    }
    const buiten = document.createElementNS(NS, "polygon");
    buiten.setAttribute("points", buitenPts.join(" "));
    buiten.setAttribute("class", "ring");
    ringen.appendChild(buiten);
    const binnen = document.createElementNS(NS, "polygon");
    binnen.setAttribute("points", binnenPts.join(" "));
    binnen.setAttribute("class", "ring");
    ringen.appendChild(binnen);

    // Spaken
    for (let i = 0; i < AANTAL_PALEN; i++) {
        const hoek = i * HOEK_PER_PAAL + VELD_OFFSET;
        const line = document.createElementNS(NS, "line");
        line.setAttribute("x1", R_BINNEN_M * Math.cos(hoek) * M_TO_PX);
        line.setAttribute("y1", R_BINNEN_M * Math.sin(hoek) * M_TO_PX);
        line.setAttribute("x2", R_BUITEN_M * Math.cos(hoek) * M_TO_PX);
        line.setAttribute("y2", R_BUITEN_M * Math.sin(hoek) * M_TO_PX);
        line.setAttribute("class", "spaak");
        spaken.appendChild(line);
    }

    // Paaltjes op het midden van elke buitenzijde (paal_id 1..24 direct bruikbaar)
    for (let n = 1; n <= AANTAL_PALEN; n++) {
        const p = paalPositie(n);
        const px = p.x * M_TO_PX, py = p.y * M_TO_PX;
        const paalHoek = Math.atan2(p.y, p.x);  // radiale richting centrum→paal

        const r = document.createElementNS(NS, "rect");
        r.setAttribute("x", px - 3); r.setAttribute("y", py - 3);
        r.setAttribute("width", 6); r.setAttribute("height", 6);
        r.setAttribute("class", "paal");
        r.dataset.paal = n;
        r.addEventListener("click", () => {
            state.geselecteerdePaal = (state.geselecteerdePaal === n) ? null : n;
            render();
        });
        paaltjes.appendChild(r);

        // LED-bolletje een eindje buiten de paal (radiaal naar buiten)
        const ledOffset = 1.25;  // 1.25 m verder dan de paal naar buiten
        const lx = (p.x + Math.cos(paalHoek) * ledOffset) * M_TO_PX;
        const ly = (p.y + Math.sin(paalHoek) * ledOffset) * M_TO_PX;
        const led = document.createElementNS(NS, "circle");
        led.setAttribute("cx", lx); led.setAttribute("cy", ly);
        led.setAttribute("r", 4.5);
        led.setAttribute("class", "led");
        led.id = "led-paal-" + n;
        ledBolletjes.appendChild(led);

        // Label net binnen de paal (radiaal naar binnen)
        const lbl = document.createElementNS(NS, "text");
        const lblOffset = 1.0;
        lbl.setAttribute("x", (p.x - Math.cos(paalHoek) * lblOffset) * M_TO_PX);
        lbl.setAttribute("y", (p.y - Math.sin(paalHoek) * lblOffset) * M_TO_PX + 2);
        lbl.setAttribute("class", "paal-label");
        lbl.textContent = n;
        paalLabels.appendChild(lbl);
    }
}

function renderLeds() {
    for (let n = 1; n <= AANTAL_PALEN; n++) {
        const led = document.getElementById("led-paal-" + n);
        if (!led) continue;
        const actie = state.paalActie[n] || 0;
        // Reset class + fill
        led.setAttribute("class", "led");
        led.style.fill = "";

        // Minimale set: 0 = uit, 1 = portaal (paars), 2 = happy hour (goud).
        if (actie === 8) {
            // NUKE-ring: pulserend radioactief geel↔groen.
            const p = Math.sin(Date.now() / 220 + n * 0.5) / 2 + 0.5;
            led.style.fill = `hsl(${75 + p * 20}, 100%, ${32 + p * 28}%)`;
        } else if (actie === 11) {
            // Oogst: dramatische wit/rood-strobe.
            led.style.fill = (Math.floor(Date.now() / 150) % 2 === 0) ? "#ffffff" : "#c00000";
        } else if (actie in SOLID_KLEUR) {
            led.style.fill = SOLID_KLEUR[actie];
        }
    }
    renderBuzzers();
    renderZiekteIconen();
}

function renderBuzzers() {
    const grp = document.getElementById("buzzers");
    if (!grp) return;
    grp.innerHTML = "";
    const nu = Date.now();
    for (let n = 1; n <= AANTAL_PALEN; n++) {
        if (state.paalBuzzer[n] > nu) {
            const p = paalPositie(n);
            const hoek = Math.atan2(p.y, p.x);
            const t = document.createElementNS(NS, "text");
            t.setAttribute("x", (p.x + Math.cos(hoek) * 2.4) * M_TO_PX);
            t.setAttribute("y", (p.y + Math.sin(hoek) * 2.4) * M_TO_PX);
            t.setAttribute("class", "buzzer-icoon");
            t.textContent = "🔔";
            grp.appendChild(t);
        }
    }
}

function renderPortalen() {
    const grp = document.getElementById("portalen");
    if (!grp) return;
    grp.innerHTML = "";
    for (const pr of state.portalen) {
        if (!pr || !Array.isArray(pr.palen) || pr.palen.length < 2) continue;
        const pa = paalPositie(pr.palen[0]), pb = paalPositie(pr.palen[1]);
        const line = document.createElementNS(NS, "line");
        line.setAttribute("x1", pa.x * M_TO_PX); line.setAttribute("y1", pa.y * M_TO_PX);
        line.setAttribute("x2", pb.x * M_TO_PX); line.setAttribute("y2", pb.y * M_TO_PX);
        line.setAttribute("class", "portaal-lijn");
        grp.appendChild(line);
    }
}

// Geeft het partner-uur als 'uur' deel is van een actief portaal, anders null.
function portaalPartner(uur) {
    for (const pr of state.portalen) {
        if (!pr || !Array.isArray(pr.palen)) continue;
        if (pr.palen[0] === uur) return pr.palen[1];
        if (pr.palen[1] === uur) return pr.palen[0];
    }
    return null;
}

function berekenGroepsOffsets(spelers) {
    const perUur = {};
    spelers.forEach(sp => {
        const u = welkUur(sp.x, sp.y);
        (perUur[u] = perUur[u] || []).push(sp);
    });
    const offsets = new Map();
    const STAP = 0.46;  // m tussen spelercirkels (iets meer dan diameter 0.36m)
    for (const groep of Object.values(perUur)) {
        const n = groep.length;
        if (n === 1) { offsets.set(groep[0], { dx: 0, dy: 0 }); continue; }
        const kolommen = Math.min(3, Math.ceil(Math.sqrt(n)));
        const rijen    = Math.ceil(n / kolommen);
        const uur = welkUur(groep[0].x, groep[0].y);
        const midHoek = (uur - 0.5) * HOEK_PER_PAAL + VELD_OFFSET;
        const tx = -Math.sin(midHoek), ty = Math.cos(midHoek);  // tangentieel
        const rx =  Math.cos(midHoek), ry = Math.sin(midHoek);  // radiaal
        groep.forEach((sp, i) => {
            const rij = Math.floor(i / kolommen);
            const kol = i % kolommen;
            const kolsInRij = (rij === rijen - 1 && n % kolommen) ? n % kolommen : kolommen;
            const dTang = (kol - (kolsInRij - 1) / 2) * STAP;
            const dRad  = (rij - (rijen   - 1) / 2) * STAP;
            offsets.set(sp, {
                dx: (dTang * tx + dRad * rx) * M_TO_PX,
                dy: (dTang * ty + dRad * ry) * M_TO_PX
            });
        });
    }
    return offsets;
}

function renderSpelers() {
    const grp = document.getElementById("spelers");
    grp.innerHTML = "";
    const offsets = berekenGroepsOffsets(state.spelers);
    for (const sp of state.spelers) {
        const off = offsets.get(sp) || { dx: 0, dy: 0 };
        const cx = sp.x * M_TO_PX + off.dx;
        const cy = sp.y * M_TO_PX + off.dy;
        const ziek = (state.ziekte || []).find(z => z.speler === sp.naam);
        const uit = isUit(sp);

        const c = document.createElementNS(NS, "circle");
        c.setAttribute("cx", cx); c.setAttribute("cy", cy);
        c.setAttribute("r", 4);
        c.setAttribute("class", "speler" + (ziek ? " speler-ziek" : "") + (uit ? " speler-uit" : ""));
        c.style.fill = sp.kleur;
        c.dataset.naam = sp.naam;
        grp.appendChild(c);

        if (sp.groepKleur) {
            const gk = document.createElementNS(NS, "rect");
            gk.setAttribute("x", cx + 5); gk.setAttribute("y", cy - 3);
            gk.setAttribute("width", 5); gk.setAttribute("height", 5);
            gk.setAttribute("class", "speler-groep-blok");
            gk.style.fill = sp.groepKleur;
            grp.appendChild(gk);
        }

        const lbl = document.createElementNS(NS, "text");
        lbl.setAttribute("x", cx); lbl.setAttribute("y", cy - 5);
        lbl.setAttribute("class", "speler-label");
        lbl.textContent = sp.naam;
        grp.appendChild(lbl);

        if (ziek) {
            const zlbl = document.createElementNS(NS, "text");
            zlbl.setAttribute("x", cx); zlbl.setAttribute("y", cy + 9);
            zlbl.setAttribute("class", "speler-ziek-label" + (ziek.rondesOver <= 3 ? " kritiek" : ""));
            zlbl.textContent = "🤒 " + ziek.rondesOver;
            grp.appendChild(zlbl);
        }

        if (state.modus === "sim") {
            c.addEventListener("pointerdown", (e) => startDrag(e, sp));
        }
    }
}

function renderZijbalk() {
    const ul = document.getElementById("speler-lijst");
    ul.innerHTML = "";
    for (const sp of state.spelers) {
        const li = document.createElement("li");
        const kleur = document.createElement("span");
        kleur.className = "kleur-blok";
        kleur.style.background = sp.kleur;
        const groep = document.createElement("span");
        groep.className = "groep-blok";
        groep.style.background = sp.groepKleur || "#ccc";
        const naam = document.createElement("span");
        naam.className = "naam";
        naam.textContent = sp.naam;
        const uurEl = document.createElement("span");
        const uit = isUit(sp);
        uurEl.className = "speler-uur" + (uit ? " speler-uur-uit" : "");
        uurEl.textContent = uit ? "uit" : ("Uur " + welkUur(sp.x, sp.y));
        const meester = (state.dienaars || {})[sp.naam];
        let dienaarEl = null;
        if (meester) {
            dienaarEl = document.createElement("span");
            dienaarEl.className = "speler-dienaar";
            dienaarEl.textContent = "🤝 dient " + meester;
            dienaarEl.title = "Geoogst op middernacht — verdient nu voor " + meester;
        }
        const btnDel = document.createElement("button");
        btnDel.textContent = "X";
        btnDel.className = "del-knop";
        btnDel.title = "Verwijder speler";
        btnDel.addEventListener("click", () => {
            state.spelers = state.spelers.filter(x => x !== sp);
            renderZijbalk(); renderSpelers();
        });
        li.append(kleur, groep, naam, uurEl, btnDel);
        if (dienaarEl) li.appendChild(dienaarEl);
        ul.appendChild(li);
    }
}

function render() {
    // Selectie-markering paal
    document.querySelectorAll(".paal").forEach(el => {
        if (parseInt(el.dataset.paal, 10) === state.geselecteerdePaal) el.classList.add("geselecteerd");
        else el.classList.remove("geselecteerd");
    });
    renderLeds();
    renderSpelers();
}

// ============================================================
// SPELER-DRAG
// ============================================================
let drag = null;
function startDrag(ev, sp) {
    drag = { speler: sp, svg: document.getElementById("veld") };
    sp.drag = true;
    ev.target.classList.add("dragging");
    window.addEventListener("pointermove", doDrag);
    window.addEventListener("pointerup", endDrag);
}
function doDrag(ev) {
    if (!drag) return;
    const svg = drag.svg;
    const pt = svg.createSVGPoint();
    pt.x = ev.clientX; pt.y = ev.clientY;
    const ctm = svg.getScreenCTM().inverse();
    const local = pt.matrixTransform(ctm);
    drag.speler.x = local.x / M_TO_PX;
    drag.speler.y = local.y / M_TO_PX;
    renderSpelers();
}
function endDrag() {
    if (drag) {
        const sp = drag.speler;
        sp.drag = false;
        document.querySelectorAll(".speler.dragging").forEach(el => el.classList.remove("dragging"));
        // Portaal-gebruik: losgelaten op een portaal-uur -> teleporteer naar de partner.
        // We laten de speler eerst één publish-cyclus op uur A staan, zodat Node-RED de
        // sprong A->B als portaal (0 levensuren) herkent i.p.v. als gewone verplaatsing.
        const uur = welkUur(sp.x, sp.y);
        const partner = portaalPartner(uur);
        if (partner) {
            log("info", `${sp.naam} op portaal-uur ${uur} → teleport naar ${partner}`);
            setTimeout(() => {
                const p = paalPositie(partner);
                const f = (R_BUITEN_M - 2.5) / R_BUITEN_M;
                sp.x = p.x * f; sp.y = p.y * f;
                renderSpelers(); renderZijbalk();
            }, Math.max(state.publishMs + 100, 350));
        }
        renderZijbalk();
    }
    drag = null;
    window.removeEventListener("pointermove", doDrag);
    window.removeEventListener("pointerup", endDrag);
}

// Spelers bewegen enkel via slepen (geen automatische random walk meer), zodat het
// gelopen pad zuiver gedetecteerd wordt voor de verplaatsingscontrole.

// ============================================================
// LOG
// ============================================================
// Logsoorten -> filtercategorie (dropdown-checklist). Onbekende soorten vallen onder info.
const LOG_CAT = { cmd: "cmd", audio: "audio", foutcode: "foutcode", info: "info", data: "info", err: "info" };
function logCatActief(cat) {
    const cb = document.querySelector('.logfilt[value="' + cat + '"]');
    return !cb || cb.checked;
}
function log(soort, tekst) {
    const cat = LOG_CAT[soort] || "info";
    if (!logCatActief(cat)) return;
    const div = document.getElementById("log");
    const t = new Date();
    const tijdStr = t.toTimeString().slice(0, 8) + "." + String(t.getMilliseconds()).padStart(3, "0");
    const row = document.createElement("div");
    row.className = "row " + soort;
    row.textContent = `${tijdStr}  ${soort.padEnd(5)} ${tekst}`;
    div.appendChild(row);
    while (div.childElementCount > 500) div.removeChild(div.firstChild);
    if (document.getElementById("auto-scroll").checked) div.scrollTop = div.scrollHeight;
}

// ============================================================
// UI WIRE-UP
// ============================================================
function zetStatus(s) {
    const el = document.getElementById("conn-status");
    el.textContent = s;
    el.className = s === "online" ? "conn-on" : "conn-off";
}

function spelerStartPositie(i, totaal) {
    // Snap naar het midden van een sectie door paalPositie() te gebruiken voor
    // een evenredig verdeeld paalnummer — zo valt de speler nooit op een spaak-grens.
    const paalNum = (Math.round(((i + 0.5) / totaal) * AANTAL_PALEN) % AANTAL_PALEN) || AANTAL_PALEN;
    const p = paalPositie(paalNum);
    const f = (R_BUITEN_M - 2.5) / R_BUITEN_M;  // iets binnen de paal, duidelijk in sectie
    return { x: p.x * f, y: p.y * f };
}

function laadDefaultSpelers() {
    state.spelers = DEFAULT_SPELERS.map((s, i) => {
        const pos = spelerStartPositie(i, DEFAULT_SPELERS.length);
        return { naam: s.naam, mac: s.mac, kleur: s.kleur, groepKleur: groepKleurVoor(s.naam), x: pos.x, y: pos.y, auto: false, drag: false };
    });
}

function resetPosities() {
    state.spelers.forEach((s, i) => {
        const pos = spelerStartPositie(i, state.spelers.length);
        s.x = pos.x; s.y = pos.y;
    });
    state.uitAllen = false;
    renderSpelers();
    renderZijbalk();
}

// Toggle: iedereen het veld uit (ring buiten de buitenrand) of terug naar hun oorspronkelijke plaats.
// Handig om het NUKE-event te testen (out = ongedetecteerd = veilig).
function toggleOut() {
    if (!state.uitAllen) {
        const n = state.spelers.length || 1;
        state.spelers.forEach((sp, i) => {
            sp._terugX = sp.x; sp._terugY = sp.y;
            const hoek = (i / n) * 2 * Math.PI;
            const r = R_BUITEN_M + 2;
            sp.x = r * Math.cos(hoek); sp.y = r * Math.sin(hoek);
            sp.paal = null;
        });
        state.uitAllen = true;
        log("info", "Alle spelers het veld UIT (ongedetecteerd).");
    } else {
        state.spelers.forEach(sp => {
            if (sp._terugX != null) { sp.x = sp._terugX; sp.y = sp._terugY; }
        });
        state.uitAllen = false;
        log("info", "Alle spelers terug IN het veld.");
    }
    renderSpelers();
    renderZijbalk();
}

function voegSpelerToe() {
    const idx = state.spelers.length + 1;
    const naam = prompt("Naam nieuwe speler:", "Speler" + idx);
    if (!naam) return;
    const mac = prompt("MAC-adres (lowercase):", "48:87:2d:9d:00:" + idx.toString(16).padStart(2, "0"));
    if (!mac) return;
    const palet = ["#ff5722", "#4caf50", "#03a9f4", "#9c27b0", "#ffc107", "#00bcd4", "#795548"];
    const sp = {
        naam, mac, kleur: palet[idx % palet.length], groepKleur: groepKleurVoor(naam),
        x: 0, y: (R_BUITEN_M + R_BINNEN_M) / 2,
        auto: false, drag: false
    };
    state.spelers.push(sp);
    renderZijbalk(); renderSpelers();
}

window.addEventListener("DOMContentLoaded", () => {
    tekenVeld();
    laadDefaultSpelers();
    renderZijbalk();
    render();

    document.getElementById("btn-connect").addEventListener("click", () => {
        if (state.client) { state.client.end(true); state.client = null; }
        connecteer();
    });
    document.getElementById("btn-add-speler").addEventListener("click", voegSpelerToe);
    document.getElementById("btn-reset-spelers").addEventListener("click", resetPosities);
    document.getElementById("btn-out").addEventListener("click", toggleOut);
    document.getElementById("btn-clear-log").addEventListener("click", () => {
        document.getElementById("log").innerHTML = "";
    });
    document.getElementById("histo-toggle").addEventListener("click", () => {
        document.getElementById("histo-paneel").classList.toggle("open");
    });
    document.getElementById("events-toggle").addEventListener("click", () => {
        document.getElementById("events-paneel").classList.toggle("open");
    });
    document.getElementById("middernacht-toggle").addEventListener("click", () => {
        document.getElementById("middernacht-paneel").classList.toggle("open");
    });
    document.getElementById("middernacht-aan").addEventListener("change", (e) => {
        state.middernachtAan = e.target.checked;
        log("info", "Middernacht " + (state.middernachtAan ? "ingeschakeld" : "uitgeschakeld (uur 24 = gewoon uur)"));
        publishMiddernachtConfig();
        renderMiddernacht();
    });
    document.querySelectorAll('input[name="modus"]').forEach(r => {
        r.addEventListener("change", (e) => {
            state.modus = e.target.value;
            log("info", "Modus: " + state.modus);
            publishModus();   // 24-uur veld aan/uit in Node-RED
            renderSpelers();
        });
    });

    // Log resize handle — de gekozen hoogte blijft vast (en bewaard in localStorage),
    // zodat het speelveld niet meer omhoog geduwd wordt.
    const logPaneel = document.getElementById("log-paneel");
    const resizeHandle = document.getElementById("log-resize-handle");
    function zetLogHoogte(h) {
        const hh = Math.round(Math.min(Math.max(80, h), window.innerHeight * 0.7));
        logPaneel.style.height = hh + "px";
        logPaneel.style.maxHeight = hh + "px";
    }
    const bewaardeH = parseInt(localStorage.getItem("sim-log-hoogte") || "", 10);
    if (bewaardeH) zetLogHoogte(bewaardeH);
    resizeHandle.addEventListener("mousedown", (e) => {
        const startY = e.clientY;
        const startH = logPaneel.getBoundingClientRect().height;
        const onMove = (ev) => zetLogHoogte(startH + (startY - ev.clientY));
        const onUp = () => {
            window.removeEventListener("mousemove", onMove);
            window.removeEventListener("mouseup", onUp);
            localStorage.setItem("sim-log-hoogte", String(parseInt(logPaneel.style.height, 10) || ""));
        };
        window.addEventListener("mousemove", onMove);
        window.addEventListener("mouseup", onUp);
        e.preventDefault();
    });

    // Log-filter (dropdown-checklist): selectie bewaren/herstellen.
    const logFilters = document.querySelectorAll(".logfilt");
    const bewaardFilter = JSON.parse(localStorage.getItem("sim-log-filter") || "null");
    logFilters.forEach(cb => {
        if (bewaardFilter && cb.value in bewaardFilter) cb.checked = bewaardFilter[cb.value];
        cb.addEventListener("change", () => {
            const st = {};
            logFilters.forEach(c => st[c.value] = c.checked);
            localStorage.setItem("sim-log-filter", JSON.stringify(st));
        });
    });

    // Loops
    setInterval(renderLeds, state.tickMs);
    setInterval(publishLocaties, state.publishMs);
});
