const appDiv = document.getElementById("app");
const modeSelect = document.getElementById("modeSelect");
const statusGrid = document.getElementById("statusGrid");
const pollState = document.getElementById("pollState");
const modeTitle = document.getElementById("modeTitle");
const modeBadge = document.getElementById("modeBadge");
const deviceLine = document.getElementById("deviceLine");
const chart = document.getElementById("rateChart");
const ctx = chart.getContext("2d");

let currentModule = null;
let currentMode = null;
let samples = [];

const MODES = [
    { id: 0, name: "Generator", file: "generator.js" },
    { id: 1, name: "Slow", file: "generator.js" },
    { id: 2, name: "ECU", file: "generator.js" },
    { id: 3, name: "Analyzer", file: "analyzer.js" },
    { id: 4, name: "SavvyCAN", file: "savvycan.js" }
];

const modeName = id => (MODES.find(m => m.id === Number(id)) || { name: `Mode ${id}` }).name;
const fmt = value => Number(value || 0).toLocaleString();
const pct = (used, cap) => cap ? Math.round((used * 100) / cap) : 0;

function metric(label, value, hint, tone = "") {
    return `
        <article class="metric ${tone}">
            <span>${label}</span>
            <strong>${value}</strong>
            <small>${hint || ""}</small>
        </article>
    `;
}

function populateModes() {
    MODES.forEach(mode => {
        const opt = document.createElement("option");
        opt.value = mode.id;
        opt.textContent = mode.name;
        modeSelect.appendChild(opt);
    });
}

async function loadMode(modeId, status) {
    const mode = MODES.find(m => m.id === Number(modeId));
    currentMode = Number(modeId);
    modeSelect.value = currentMode;
    modeTitle.textContent = modeName(currentMode);
    modeBadge.textContent = `m${currentMode}`;

    if (currentModule?.cleanup) currentModule.cleanup();
    currentModule = null;
    appDiv.textContent = "Loading...";

    if (!mode) {
        appDiv.textContent = "This mode does not have a GUI page yet.";
        return;
    }

    const module = await import(`./${mode.file}?t=${Date.now()}`);
    currentModule = module;
    appDiv.innerHTML = "";
    if (module.init) module.init(appDiv, status);
}

async function setMode(modeId) {
    const body = new URLSearchParams({ m: String(modeId) });
    await fetch("/mode", { method: "POST", body });
    await refresh(true);
}

function updateStatus(s) {
    const rxPct = pct(s.rxUsage, s.rxCapacity);
    const rxMaxPct = pct(s.rxMax, s.rxCapacity);
    const dropRate = Number(s.rxDropRate || 0) + Number(s.txDropRate || 0) + Number(s.txFailRate || 0);
    const canTone = s.canState === "RUNNING" ? "good" : "bad";
    const dropTone = dropRate ? "bad" : "good";

    deviceLine.textContent = `Baud ${fmt(s.baud)} | ${s.tcpClient ? "TCP client connected" : "No TCP client"} | CAN ${s.canState}`;
    pollState.textContent = "Live";
    pollState.className = "pill good";

    statusGrid.innerHTML = [
        metric("Mode", modeName(s.mode), `m${s.mode} | ${s.running ? "running" : "stopped"}`),
        metric("CAN", s.canState, s.listen ? "listen-only" : "normal", canTone),
        metric("TX ok", `${fmt(s.txOkRate)}/s`, `attempt ${fmt(s.txAttemptRate)}/s`),
        metric("TX drops", `${fmt(s.txDropRate)}/s`, `fail ${fmt(s.txFailRate)}/s block ${fmt(s.txBlockRate)}/s`, dropTone),
        metric("RX", `${fmt(s.rxRate)}/s`, `drop ${fmt(s.rxDropRate)}/s`),
        metric("RX buffer", `${rxPct}%`, `max ${rxMaxPct}%`, rxPct > 90 ? "warn" : "good"),
        metric("SavvyCAN", s.tcpClient ? "TCP" : "Idle", s.tcpClient ? "debug on serial" : "waiting"),
        metric("LED", s.led ? "On" : "Off", `CAN log ${s.canlog}`)
    ].join("");

    samples.push({
        tx: Number(s.txOkRate || 0),
        rx: Number(s.rxRate || 0),
        drop: dropRate
    });
    if (samples.length > 90) samples.shift();
    drawChart();
}

function drawLine(points, color, max, key) {
    if (points.length < 2) return;

    ctx.beginPath();
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;

    points.forEach((p, i) => {
        const x = 40 + (i * (chart.width - 56)) / Math.max(1, points.length - 1);
        const y = 18 + (chart.height - 42) * (1 - (p[key] / max));
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    });

    ctx.stroke();
}

function drawChart() {
    const max = Math.max(1, ...samples.flatMap(p => [p.tx, p.rx, p.drop]));

    ctx.clearRect(0, 0, chart.width, chart.height);
    ctx.fillStyle = "#101820";
    ctx.fillRect(0, 0, chart.width, chart.height);

    ctx.strokeStyle = "#263542";
    ctx.lineWidth = 1;
    for (let i = 0; i <= 4; i++) {
        const y = 18 + (i * (chart.height - 42)) / 4;
        ctx.beginPath();
        ctx.moveTo(40, y);
        ctx.lineTo(chart.width - 16, y);
        ctx.stroke();
    }

    ctx.fillStyle = "#9fb0bd";
    ctx.font = "12px ui-monospace, monospace";
    ctx.fillText(fmt(max), 8, 24);
    ctx.fillText("0", 24, chart.height - 20);

    drawLine(samples, "#54d38a", max, "tx");
    drawLine(samples, "#4fb3ff", max, "rx");
    drawLine(samples, "#ff6b6b", max, "drop");
}

async function refresh(forceModeLoad = false) {
    try {
        const res = await fetch("/status", { cache: "no-store" });
        const status = await res.json();

        updateStatus(status);

        if (forceModeLoad || currentMode !== Number(status.mode)) {
            await loadMode(status.mode, status);
        } else if (currentModule?.update) {
            currentModule.update(status);
        }
    } catch (err) {
        pollState.textContent = "Offline";
        pollState.className = "pill bad";
        deviceLine.textContent = err.message;
    }
}

modeSelect.onchange = () => setMode(Number(modeSelect.value));

populateModes();
refresh(true);
setInterval(refresh, 1000);
