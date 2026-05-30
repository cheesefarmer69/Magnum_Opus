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
// LED-bolletje dat de actuele kleur/animatie toont op basis van
// commando/master1-berichten (actie-IDs 0-22 uit firmware/Slave).
// ============================================================

// --- VELD-GEOMETRIE (uit docs/playfield.md) ---
const AANTAL_PALEN  = 24;
const R_BUITEN_M    = 11.50;
const R_BINNEN_M    = 8.00;
const HOEK_PER_PAAL = (2 * Math.PI) / AANTAL_PALEN;  // 15° in radialen

// SVG viewBox is -300..300 in pixels; meter->pixel schaal:
const M_TO_PX = 22;  // 11.50 m * 22 = 253 px straal -> past binnen 300 marge

function paalPositie(n) {
    // Paal n staat op het MIDDEN van de n-de buitenzijde (niet op een hoekpunt).
    // De hoek is (n - 0.5) × 15°: halverwege tussen hoekpunt n-1 en hoekpunt n.
    // Index 0 ongebruikt zodat paal_id 1..24 rechtstreeks aansluit.
    const hoek = (n - 0.5) * HOEK_PER_PAAL;
    return { x: R_BUITEN_M * Math.cos(hoek), y: R_BUITEN_M * Math.sin(hoek) };
}

// Geeft het uur (sectie 1-24) op basis van de hoek van positie (x,y).
function welkUur(x, y) {
    const deg = (Math.atan2(y, x) * 180 / Math.PI + 360) % 360;
    return (Math.floor(deg / 15) % AANTAL_PALEN) + 1;
}

// --- RSSI-MODEL (log-distance path loss) ---
const RSSI0_DBM    = -45;   // signaal op d0=1m
const PATH_LOSS_N  = 2.5;   // open buiten
const RSSI_SIGMA   = 3;     // gaussian noise stdev (dBm)
const RSSI_DREMPEL = -85;   // palen onder deze drempel rapporteren niet

function rssiVoorAfstand(d_m) {
    if (d_m < 0.1) d_m = 0.1;
    const ruis = gaussNoise(0, RSSI_SIGMA);
    return RSSI0_DBM - 10 * PATH_LOSS_N * Math.log10(d_m) + ruis;
}
function gaussNoise(mu, sigma) {
    // Box-Muller
    const u1 = Math.random() || 1e-9, u2 = Math.random();
    return mu + sigma * Math.sqrt(-2 * Math.log(u1)) * Math.cos(2 * Math.PI * u2);
}

// --- ACTIE-ID TABEL (uit firmware/Slave/src/main.cpp) ---
const ACTIE_NAAM = {
    0: "NIETS",       1: "ROOD",        2: "GROEN",       3: "BUZZER_AAN",  4: "BUZZER_UIT",
    5: "BLAUW",       6: "WIT",         7: "GEEL",        8: "PAARS",       9: "CYAAN",
    10: "ORANJE",     11: "KNIPPER_SNEL",12: "KNIPPER_TRAAG",13: "PULS_ROOD",14: "PULS_BLAUW",
    15: "REGENBOOG",  16: "POLITIE",    17: "MEL_1PIEP",  18: "MEL_2PIEP",  19: "MEL_OPLOPEND",
    20: "MEL_AFLOPEND",21: "MEL_ALARM", 22: "MEL_FANFARE"
};
const SOLID_KLEUR = {
    0: "#cccccc", 1: "#ff0000", 2: "#00ff00", 5: "#0000ff", 6: "#dddddd",
    7: "#ffff00", 8: "#9c27b0", 9: "#00ffff", 10: "#ff9800"
};

// --- SPELERS (default uit Node-RED config-flow) ---
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
    gepauzeerd: false,
    spelers: [],                // {naam, mac, kleur, x, y, auto, drag}
    paalActie: new Array(AANTAL_PALEN + 1).fill(0),  // actie-ID per paal
    paalLaatsteCmd: new Array(AANTAL_PALEN + 1).fill(0),  // ms
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
        state.client.subscribe(["commando/master1", "audio/afspelen", "plaatjes/data", "pof/status", "pof/controle"]);
    });
    state.client.on("reconnect", () => log("info", "Reconnecting..."));
    state.client.on("offline",   () => { state.verbonden = false; zetStatus("offline"); log("err", "Offline."); });
    state.client.on("error",     (e) => log("err", "MQTT-fout: " + e.message));
    state.client.on("message", (topic, payload) => verwerkBericht(topic, payload.toString()));
}

function verwerkBericht(topic, raw) {
    let data;
    try { data = JSON.parse(raw); } catch { log("err", `Bad JSON op ${topic}: ${raw}`); return; }

    if (topic === "commando/master1") {
        const paal = data.paal, actie = data.actie;
        if (paal >= 1 && paal <= AANTAL_PALEN) {
            state.paalActie[paal] = actie;
            state.paalLaatsteCmd[paal] = Date.now();
        }
        log("cmd", `paal ${paal} → ${ACTIE_NAAM[actie] || "?"} (${actie})`);
    } else if (topic === "audio/afspelen") {
        log("audio", `[${data.fase || "?"}] ${data.tekst || ""}`);
    } else if (topic === "pof/controle") {
        const ev = data.event ? " [" + data.event + "]" : "";
        (data.resultaten || []).forEach(r => {
            log("foutcode", `${r.speler}: ${r.status} (Δ${r.verplaatst})${ev}`);
        });
    } else if (topic === "pof/status") {
        const timerEl = document.getElementById("pof-timer");
        const naamEl  = document.getElementById("pof-event-naam");
        const doelEl  = document.getElementById("pof-doelwit");
        if (!timerEl) return;
        if (!data.actief || data.fase === "idle") {
            timerEl.textContent = "—";
            naamEl.textContent  = "";
            doelEl.textContent  = "—";
        } else if (data.fase === "aanloop") {
            timerEl.textContent = (data.teller != null) ? data.teller + "s" : "…";
            naamEl.textContent  = "Aanloop…";
            doelEl.textContent  = "—";
        } else {
            timerEl.textContent = (data.fase === "bezig") ? "…"
                : (data.teller != null) ? data.teller + "s" : "…";
            const getal = (data.getalWaarde != null) ? " (" + data.getalWaarde + ")" : "";
            const txt = data.eventTekst ? " — " + data.eventTekst : "";
            naamEl.textContent = (data.eventNaam || data.eventTekst)
                ? "⚡ " + (data.eventNaam || "") + getal + txt : "";
            const reveal = data.doelwitReveal
                || (Array.isArray(data.doelwit) ? data.doelwit.map(d => "• " + d).join("\n") : "");
            doelEl.textContent = reveal || "—";
        }
    } else if (topic === "plaatjes/data") {
        // Alleen tonen in monitor-modus (in sim-modus zijn we zelf de bron en zou loggen overkill zijn)
        if (state.modus === "monitor") {
            log("data", `paal ${data.paal}: ${data.mac || ("batt=" + data.batt)} ${data.rssi !== undefined ? data.rssi + " dBm" : ""}`);
            // Speler-positie bijwerken in monitor-modus: zet speler naar binnen voor zijn actieve paal.
            if (data.mac) {
                const sp = state.spelers.find(s => s.mac === data.mac);
                if (sp && data.paal >= 1 && data.paal <= AANTAL_PALEN) {
                    const p = paalPositie(data.paal);
                    // iets naar binnen geprojecteerd zodat de stip niet over de paal valt
                    const f = (R_BUITEN_M - 1.5) / R_BUITEN_M;
                    sp.x = p.x * f; sp.y = p.y * f;
                }
            }
        }
    }
}

function publishDetecties() {
    if (!state.verbonden || state.modus !== "sim" || state.gepauzeerd) return;
    for (const sp of state.spelers) {
        for (let paal = 1; paal <= AANTAL_PALEN; paal++) {
            const p = paalPositie(paal);
            const d = Math.hypot(sp.x - p.x, sp.y - p.y);
            const rssi = rssiVoorAfstand(d);
            if (rssi > RSSI_DREMPEL) {
                state.client.publish("plaatjes/data", JSON.stringify({
                    paal: paal, mac: sp.mac, rssi: Math.round(rssi)
                }));
            }
        }
    }
    // Heartbeat per paal (lichte rate: één random paal per tick) zodat Spelstatus ST-002 niet triggert
    const paalH = 1 + Math.floor(Math.random() * AANTAL_PALEN);
    state.client.publish("plaatjes/data", JSON.stringify({ paal: paalH, batt: 3.90 }));
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
        const hoek = i * HOEK_PER_PAAL;
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
        const hoek = i * HOEK_PER_PAAL;
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

        if (actie in SOLID_KLEUR) {
            // Solid kleur (0,1,2,5-10)
            led.style.fill = SOLID_KLEUR[actie];
        } else if (actie === 3) {
            // BUZZER_AAN: geel bolletje als indicator
            led.style.fill = "#ffeb3b";
        } else if (actie === 4) {
            led.style.fill = "#cccccc";
        } else if (actie >= 11 && actie <= 16) {
            // Animaties via CSS-class
            led.setAttribute("class", "led actie-" + actie);
        } else if (actie >= 17 && actie <= 22) {
            // Melodieën: korte flash (laatste 1.5s na commando)
            const sinceMs = Date.now() - state.paalLaatsteCmd[n];
            if (sinceMs < 1500) led.setAttribute("class", "led melodie-flash");
            led.style.fill = "#cccccc";
        }
    }
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
        const midHoek = (uur - 0.5) * HOEK_PER_PAAL;
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
        const c = document.createElementNS(NS, "circle");
        c.setAttribute("cx", cx); c.setAttribute("cy", cy);
        c.setAttribute("r", 4);
        c.setAttribute("class", "speler");
        c.style.fill = sp.kleur;
        c.dataset.naam = sp.naam;
        grp.appendChild(c);

        const lbl = document.createElementNS(NS, "text");
        lbl.setAttribute("x", cx); lbl.setAttribute("y", cy - 5);
        lbl.setAttribute("class", "speler-label");
        lbl.textContent = sp.naam;
        grp.appendChild(lbl);

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
        const naam = document.createElement("span");
        naam.className = "naam";
        naam.textContent = sp.naam;
        const uurEl = document.createElement("span");
        uurEl.className = "speler-uur";
        uurEl.textContent = "Uur " + welkUur(sp.x, sp.y);
        const btnAuto = document.createElement("button");
        btnAuto.textContent = "auto";
        btnAuto.className = sp.auto ? "actief" : "";
        btnAuto.title = "Automatische random walk";
        btnAuto.addEventListener("click", () => { sp.auto = !sp.auto; renderZijbalk(); });
        const btnDel = document.createElement("button");
        btnDel.textContent = "✕";
        btnDel.title = "Verwijder";
        btnDel.addEventListener("click", () => {
            state.spelers = state.spelers.filter(x => x !== sp);
            renderZijbalk(); renderSpelers();
        });
        li.append(kleur, naam, uurEl, btnAuto, btnDel);
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
        drag.speler.drag = false;
        document.querySelectorAll(".speler.dragging").forEach(el => el.classList.remove("dragging"));
        renderZijbalk();
    }
    drag = null;
    window.removeEventListener("pointermove", doDrag);
    window.removeEventListener("pointerup", endDrag);
}

// ============================================================
// AUTOMATISCHE BEWEGING
// ============================================================
function tickAutoWalk() {
    if (state.gepauzeerd) return;
    const dt_s = state.tickMs / 1000;
    const SNELHEID = 1.2;  // m/s
    const RING_MID = (R_BUITEN_M + R_BINNEN_M) / 2;
    const RING_MARGE = 1.0;

    for (const sp of state.spelers) {
        if (!sp.auto || sp.drag) continue;
        // Brownian-motion stap
        const stap = SNELHEID * dt_s;
        sp.x += (Math.random() - 0.5) * 2 * stap;
        sp.y += (Math.random() - 0.5) * 2 * stap;
        // Houd binnen de ring: als buiten, spiegel terug naar het midden
        const r = Math.hypot(sp.x, sp.y);
        if (r > R_BUITEN_M - RING_MARGE) {
            const k = (R_BUITEN_M - RING_MARGE) / r;
            sp.x *= k; sp.y *= k;
        } else if (r < R_BINNEN_M + RING_MARGE) {
            // Trek licht naar buiten (richting buitenring)
            const k = (R_BINNEN_M + RING_MARGE) / Math.max(r, 0.1);
            sp.x *= k; sp.y *= k;
        }
    }
    renderSpelers();
    renderZijbalk();
}

// ============================================================
// LOG
// ============================================================
function log(soort, tekst) {
    if (soort === "foutcode" && !document.getElementById("filter-foutcodes").checked) return;
    const filterCmdOnly = document.getElementById("filter-cmd-only").checked;
    if (filterCmdOnly && soort !== "cmd") return;
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
        return { naam: s.naam, mac: s.mac, kleur: s.kleur, x: pos.x, y: pos.y, auto: false, drag: false };
    });
}

function resetPosities() {
    state.spelers.forEach((s, i) => {
        const pos = spelerStartPositie(i, state.spelers.length);
        s.x = pos.x; s.y = pos.y;
    });
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
        naam, mac, kleur: palet[idx % palet.length],
        x: 0, y: (R_BUITEN_M + R_BINNEN_M) / 2,
        auto: false, drag: false
    };
    state.spelers.push(sp);
    renderZijbalk(); renderSpelers();
}

window.addEventListener("DOMContentLoaded", () => {
    document.getElementById("rssi-rssi0").textContent   = RSSI0_DBM;
    document.getElementById("rssi-n").textContent       = PATH_LOSS_N;
    document.getElementById("rssi-sigma").textContent   = RSSI_SIGMA;
    document.getElementById("rssi-drempel").textContent = RSSI_DREMPEL;

    tekenVeld();
    laadDefaultSpelers();
    renderZijbalk();
    render();

    document.getElementById("btn-connect").addEventListener("click", () => {
        if (state.client) { state.client.end(true); state.client = null; }
        connecteer();
    });
    document.getElementById("btn-pause").addEventListener("click", () => {
        state.gepauzeerd = !state.gepauzeerd;
        document.getElementById("btn-pause").textContent = state.gepauzeerd ? "Resume" : "Pause";
    });
    document.getElementById("btn-add-speler").addEventListener("click", voegSpelerToe);
    document.getElementById("btn-reset-spelers").addEventListener("click", resetPosities);
    document.getElementById("btn-clear-log").addEventListener("click", () => {
        document.getElementById("log").innerHTML = "";
    });
    document.querySelectorAll('input[name="modus"]').forEach(r => {
        r.addEventListener("change", (e) => {
            state.modus = e.target.value;
            log("info", "Modus: " + state.modus);
            renderSpelers();
        });
    });

    // Log resize handle
    const logPaneel = document.getElementById("log-paneel");
    const resizeHandle = document.getElementById("log-resize-handle");
    resizeHandle.addEventListener("mousedown", (e) => {
        const startY = e.clientY;
        const startH = logPaneel.getBoundingClientRect().height;
        const onMove = (ev) => {
            const delta = startY - ev.clientY;
            const newH = Math.min(Math.max(80, startH + delta), window.innerHeight * 0.6);
            logPaneel.style.maxHeight = newH + "px";
        };
        const onUp = () => {
            window.removeEventListener("mousemove", onMove);
            window.removeEventListener("mouseup", onUp);
        };
        window.addEventListener("mousemove", onMove);
        window.addEventListener("mouseup", onUp);
        e.preventDefault();
    });

    // Loops
    setInterval(() => { tickAutoWalk(); renderLeds(); }, state.tickMs);
    setInterval(publishDetecties, state.publishMs);
});
