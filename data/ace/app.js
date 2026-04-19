const canvas = document.getElementById("canvas");
const ctx = canvas.getContext("2d");

// ===== UI =====
const statusEl = document.getElementById("status");
const fpsEl = document.getElementById("fps");
const totalEl = document.getElementById("total");

const filterIdInput = document.getElementById("filterId");
const filterMaskInput = document.getElementById("filterMask");

const pauseBtn = document.getElementById("pauseBtn");
const freezeBtn = document.getElementById("freezeBtn");
const clearBtn = document.getElementById("clearBtn");

const modeBtn = document.getElementById("modeBtn");
const collapseBtn = document.getElementById("collapseBtn");
const exportBtn = document.getElementById("exportBtn");
const themeBtn = document.getElementById("themeBtn");

const zoomSlider = document.getElementById("zoom");
const scrollSlider = document.getElementById("scroll");

let ws = null;

// ===== STATE =====
let paused = false;
let frozen = false;
let timelineMode = false;
let collapse = false;
let darkMode = true;

let filterId = null;
let filterMask = null;

let fontSize = 14;
let lineHeight = fontSize + 2;

// ===== STATS =====
let frameCount = 0;
let totalFrames = 0;
let lastFpsTime = performance.now();

// ===== HISTORY =====
const MAX_HISTORY = 20000;
const history = new Array(MAX_HISTORY);
let histHead = 0;
let histSize = 0;

// ===== LANES =====
const laneMap = new Map();
let laneCount = 0;

function getCluster(id)
{
    return collapse ? (id & 0x700) : id;
}

// ===== COLOR =====
function idToColor(id)
{
    const r = (id * 97) % 255;
    const g = (id * 57) % 255;
    const b = (id * 23) % 255;
    return `rgb(${r},${g},${b})`;
}

// ===== CANVAS =====
function resizeCanvas()
{
    canvas.width = window.innerWidth;
    canvas.height = window.innerHeight - 80;
}
resizeCanvas();
window.onresize = resizeCanvas;

// ===== FILTER =====
function passFilter(id)
{
    if (filterId === null) return true;

    if (filterMask !== null)
        return (id & filterMask) === (filterId & filterMask);

    return id === filterId;
}

// ===== PUSH =====
function pushFrame(f)
{
    if (paused) return; // ✅ FIXED

    history[histHead] = f;
    histHead = (histHead + 1) % MAX_HISTORY;

    if (histSize < MAX_HISTORY) histSize++;

    const key = getCluster(f.id);

    if (!laneMap.has(key))
        laneMap.set(key, laneCount++);

    scrollSlider.max = Math.max(0, histSize - 1);
}

async function handleWSMessage(event)
{
    let arrayBuffer;

    if (event.data instanceof ArrayBuffer)
    {
        arrayBuffer = event.data;
    }
    else if (event.data instanceof Blob)
    {
        arrayBuffer = await event.data.arrayBuffer();
    }
    else
    {
        return;
    }

    const buf = new DataView(arrayBuffer);

    let offset = 0;

    while (offset + 24 <= buf.byteLength)
    {
        const sync = buf.getUint16(offset, true);
        if (sync !== 0xAA55) { offset++; continue; }

        const ts = buf.getUint32(offset + 3, true);
        const id = buf.getUint32(offset + 7, true);
        const dlc = buf.getUint8(offset + 11);

        if (!passFilter(id))
        {
            offset += 24;
            continue;
        }

        let data = "";
        for (let i = 0; i < dlc; i++)
            data += buf.getUint8(offset + 12 + i).toString(16).padStart(2,"0")+" ";

        pushFrame({
            ts,
            id,
            dlc,
            data: data.trim(),
            color: idToColor(id)
        });

        offset += 24;
        frameCount++;
        totalFrames++;
    }
}

function connectWS()
{
    ws = new WebSocket(`ws://${location.host}/ws`);
    ws.binaryType = "arraybuffer";

    ws.onopen = () =>
    {
        console.log("WS connected");
        statusEl.textContent = "Connected";
    };

    ws.onerror = (err) =>
    {
        console.log("WS error", err);
    };

    ws.onclose = () =>
    {
        console.log("WS closed → reconnecting...");
        statusEl.textContent = "Disconnected";
        setTimeout(connectWS, 1000);
    };

    ws.onmessage = handleWSMessage;
}

// ===== RENDER =====
function render()
{
    if (frozen)
    {
        requestAnimationFrame(render);
        return;
    }

    ctx.fillStyle = darkMode ? "#000" : "#fff";
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    ctx.font = `${fontSize}px monospace`;

    if (!timelineMode)
    {
        let start = histHead - 1 - scrollSlider.value;
        if (start < 0) start += MAX_HISTORY;

        let count = 0;
        const visible = Math.floor(canvas.height / lineHeight);

        for (let i = 0; i < histSize && count < visible; i++)
        {
            const idx = (start - i + MAX_HISTORY) % MAX_HISTORY;
            const f = history[idx];
            if (!f) continue;

            const lane = laneMap.get(getCluster(f.id));
            const y = canvas.height - (count + 1) * lineHeight;

            ctx.fillStyle = f.color;
            ctx.fillText(
                `${f.ts} ${f.id.toString(16)} ${f.data}`,
                5 + lane * 220,
                y
            );

            count++;
        }
    }
    else
    {
        const latest = history[(histHead - 1 + MAX_HISTORY) % MAX_HISTORY];
        if (!latest) return;

        const windowUs = 1000000;

        for (let i = 0; i < histSize; i++)
        {
            const f = history[i];
            if (!f) continue;

            const dt = latest.ts - f.ts;
            if (dt > windowUs) continue;

            const x = canvas.width - (dt / windowUs) * canvas.width;
            const lane = laneMap.get(getCluster(f.id));
            const y = lane * lineHeight + 20;

            ctx.fillStyle = f.color;
            ctx.fillRect(x, y, 3, 3);
        }
    }

    requestAnimationFrame(render);
}

// ===== FPS =====
function updateStats()
{
    const now = performance.now();

    if (now - lastFpsTime >= 1000)
    {
        fpsEl.textContent = `FPS: ${frameCount}`;
        totalEl.textContent = `Total: ${totalFrames}`;
        frameCount = 0;
        lastFpsTime = now;
    }

    requestAnimationFrame(updateStats);
}

// ===== CONTROLS =====
pauseBtn.onclick = () =>
{
    paused = !paused;
    pauseBtn.textContent = paused ? "Resume Capture" : "Pause Capture";
};

freezeBtn.onclick = () =>
{
    frozen = !frozen;
    freezeBtn.textContent = frozen ? "Unfreeze View" : "Freeze View";
};

clearBtn.onclick = () =>
{
    histHead = 0;
    histSize = 0;
    laneMap.clear();
    laneCount = 0;

    scrollSlider.value = 0;
    scrollSlider.max = 0;

    ctx.fillStyle = darkMode ? "#000" : "#fff";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
};

modeBtn.onclick = () =>
{
    timelineMode = !timelineMode;
    modeBtn.textContent = timelineMode ? "Mode: Timeline" : "Mode: Stream";
};

collapseBtn.onclick = () =>
{
    collapse = !collapse;
    collapseBtn.textContent = collapse ? "Collapse: ON" : "Collapse: OFF";
    laneMap.clear();
    laneCount = 0;
};

exportBtn.onclick = () =>
{
    let csv = "ts,id,dlc,data\n";

    for (let i = 0; i < histSize; i++)
    {
        const f = history[i];
        if (!f) continue;
        csv += `${f.ts},${f.id},${f.dlc},"${f.data}"\n`;
    }

    const blob = new Blob([csv], { type: "text/csv" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "can_log.csv";
    a.click();
};

themeBtn.onclick = () =>
{
    darkMode = !darkMode;
};

zoomSlider.oninput = () =>
{
    fontSize = parseInt(zoomSlider.value);
    lineHeight = fontSize + 2;
};

scrollSlider.oninput = () => {};

filterIdInput.onchange = () =>
{
    const v = filterIdInput.value.trim();
    filterId = v ? (v.startsWith("0x") ? parseInt(v,16) : parseInt(v)) : null;
};

filterMaskInput.onchange = () =>
{
    const v = filterMaskInput.value.trim();
    filterMask = v ? (v.startsWith("0x") ? parseInt(v,16) : parseInt(v)) : null;
};

// ===== START =====
render();
updateStats();
connectWS();