const ws = new WebSocket("ws://" + location.host + "/ws");

const map = {};
let paused = false;
let filter = "";

let totalFrames = 0;
let lastCount = 0;
let lastTime = performance.now();

let rawMode = true;   // CRITICAL for 4k FPS

// ===== UI =====
document.getElementById("filterInput").oninput = (e) => {
    filter = e.target.value.toLowerCase();
};

function togglePause() {
    paused = !paused;
}


function handleFrame(id, dlc, data) {

    let idHex = id.toString(16);

    if (filter && !idHex.includes(filter)) return;

    totalFrames++;

    if (!map[id]) {
        let row = document.createElement("tr");

        row.innerHTML = `
            <td>${formatId(id)}</td>
            <td>${dlc}</td>
            <td>${data}</td>
            <td>1</td>
        `;

        document.querySelector("#t tbody").appendChild(row);

        map[id] = { row: row, count: 1 };
    } else {
        let entry = map[id];
        entry.count++;

        entry.row.cells[2].innerText = data;
        entry.row.cells[3].innerText = entry.count;
    }
}
// ===== BINARY PARSE =====
ws.binaryType = "arraybuffer";

ws.onmessage = (e) => {
    if (paused) return;

    const buf = new Uint8Array(e.data);

    for (let i = 0; i < buf.length; i += 13) {

        let id =
            buf[i] |
            (buf[i+1] << 8) |
            (buf[i+2] << 16) |
            (buf[i+3] << 24);

        let dlc = buf[i+4];

        totalFrames++;

        if (rawMode) continue;   // skip UI (IMPORTANT)

        let data = "";
        for (let j = 0; j < dlc; j++) {
            data += buf[i+5+j].toString(16).padStart(2, "0").toUpperCase();
        }

        handleFrame(id, dlc, data);
    }
};

// ===== FORMAT =====
function formatId(id) {
    return id > 0x7FF
        ? "0x" + id.toString(16).padStart(8, "0")
        : "0x" + id.toString(16).padStart(3, "0");
}

// ===== FPS =====
setInterval(() => {
    let now = performance.now();
    let dt = (now - lastTime) / 1000;

    let fps = (totalFrames - lastCount) / dt;

    document.getElementById("fps").innerText =
        `FPS: ${fps.toFixed(1)} | Total: ${totalFrames}`;

    lastTime = now;
    lastCount = totalFrames;
}, 1000);

setInterval(async () => {
    try {
        const r = await fetch('/ws_stats');
        const j = await r.json();

        document.getElementById("wsstat").innerText =
            `| WS frames:${j.frames} | drops:${j.drops}`;
    } catch {}
}, 1000);

// ===== MODE =====
function setMode() {
    let m = document.getElementById("modeSel").value;

    fetch("/mode", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "m=" + m
    });
}

// ===== INIT =====
fetch("/status")
    .then(r => r.json())
    .then(s => {
        document.getElementById("modeSel").value = s.mode;
    });