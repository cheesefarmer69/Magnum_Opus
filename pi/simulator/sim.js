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
// commando/master1-berichten (actie 0=uit, 1=portaal, 2=happy hour, 3=piep).
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

// --- GEEN RSSI-MODEL ---
// De simulator test het spelverloop, niet de hardware. In simulatiemodus
// stuurt hij de exacte paal van elke speler direct door (topic sim/locatie),
// zodat de locatie deterministisch is. Geen ruis, geen path-loss.
const BUZZER_PIEP_MS = 600;   // moet overeenkomen met ACTIE_BUZZER_PIEP op de slave

// --- ACTIE-ID TABEL (uit firmware/Slave/src/main.cpp) ---
// Minimale set: 0 = uit, 1 = portaal (paars), 2 = happy hour (goud), 3 = buzzer-piep.
const ACTIE_NAAM = {
    0: "NIETS", 1: "PORTAAL", 2: "HAPPY_HOUR", 3: "BUZZER_PIEP"
};
const ACTIE_BUZZER_PIEP = 3;
const SOLID_KLEUR = {
    0: "#cccccc", 1: "#9c27b0", 2: "#ffb400"
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
    paalBuzzer: new Array(AANTAL_PALEN + 1).fill(0),  // buzzer-icoon actief tot (ms)
    portalen: [],               // actieve portaal-paren [{palen:[a,b]}] (uit pof/portalen)
    toestanden: [],             // actieve uur-effecten [{uur,effect,naam,resterendeRondes}] (uit pof/toestanden)
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
        state.client.subscribe(["commando/master1", "audio/afspelen", "plaatjes/data", "pof/status", "pof/controle", "pof/portalen", "pof/toestanden", "locatie/spelers", "spel/historie"]);
        publishModus();   // laat Node-RED weten of het 24-uur veld actief is
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
    const cnt = Array.isArray(data.doelwit) ? data.doelwit.length : 0;
    if (!cnt || (data.doelwitType !== "speler" && data.doelwitType !== "uur")) return tekst;
    const enkel = cnt === 1;
    const woord = data.doelwitType === "uur" ? (enkel ? "uur" : "uren") : (enkel ? "speler" : "spelers");
    return cnt + " " + woord + " " + tekst;
}

function verwerkBericht(topic, raw) {
    let data;
    try { data = JSON.parse(raw); } catch { log("err", `Bad JSON op ${topic}: ${raw}`); return; }

    if (topic === "commando/master1") {
        const paal = data.paal, actie = data.actie;
        if (paal >= 1 && paal <= AANTAL_PALEN) {
            if (actie === ACTIE_BUZZER_PIEP) {
                // buzzer-piep: toon icoon voor de duur van de piep, laat paalActie ongemoeid
                state.paalBuzzer[paal] = Date.now() + BUZZER_PIEP_MS;
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
        const FOUT = new Set(["TE WEINIG", "TE VEEL", "TERUG IN TIJD", "BEWOOG (mocht niet)"]);
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
        } else if (data.fase === "aanloop") {
            timerEl.textContent = (data.teller != null) ? data.teller + "s" : "…";
            naamEl.textContent  = "Aanloop…";
            doelEl.textContent  = "—";
        } else {
            timerEl.textContent = (data.fase === "bezig") ? "…"
                : (data.teller != null) ? data.teller + "s" : "…";
            // Toon enkel de tekst die effectief wordt voorgelezen: "<aantal> <speler/uur> <event-tekst>".
            naamEl.textContent = afroepTekst(data);
            const reveal = data.doelwitReveal
                || (Array.isArray(data.doelwit) ? data.doelwit.map(d => "• " + d).join("\n") : "");
            doelEl.textContent = reveal || "—";
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
            const r = (t.resterendeRondes != null) ? ` <span class="toestand-rondes">(nog ${t.resterendeRondes})</span>` : "";
            return `uur ${t.uur}${r}`;
        }).join(", ");
        const div = document.createElement("div");
        div.className = "toestand-groep";
        div.innerHTML = `<b>${tag}</b>: ${uren}`;
        el.appendChild(div);
    }
}

function publishLocaties() {
    if (!state.verbonden || state.modus !== "sim" || state.gepauzeerd) return;
    // Deterministisch: elke speler exact op het uur waar hij staat.
    const lijst = state.spelers.map(sp => ({ mac: sp.mac, paal: welkUur(sp.x, sp.y) }));
    state.client.publish("sim/locatie", JSON.stringify(lijst));
}

function publishModus() {
    if (!state.verbonden) return;
    state.client.publish("sim/modus", JSON.stringify({ sim24: state.modus === "sim" }));
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

        // Minimale set: 0 = uit, 1 = portaal (paars), 2 = happy hour (goud).
        if (actie in SOLID_KLEUR) led.style.fill = SOLID_KLEUR[actie];
    }
    renderBuzzers();
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
        btnDel.className = "del-knop";
        btnDel.title = "Verwijder speler";
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
    document.getElementById("histo-toggle").addEventListener("click", () => {
        document.getElementById("histo-paneel").classList.toggle("open");
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
    setInterval(() => { tickAutoWalk(); renderLeds(); }, state.tickMs);
    setInterval(publishLocaties, state.publishMs);
});
