const ws = new WebSocket("ws://" + location.host + "/ws");

const map = {};
let paused = false;
let filter = "";

let totalFrames = 0;
let lastCount = 0;
let lastTime = performance.now();

// ===== UI =====
document.getElementById("filterInput").oninput = (e) => {
    filter = e.target.value.toLowerCase();
};

function togglePause() {
    paused = !paused;
}

// ===== SAFE PARSE =====
ws.onmessage = (e) => {
    if (paused) return;

    let f;
    try {
        f = JSON.parse(e.data);
    } catch {
        return;
    }

    // HARD GUARD (fix your crash)
    if (!f || typeof f.id !== "number" || typeof f.dlc !== "number" || typeof f.data !== "string")
        return;

    let idHex = f.id.toString(16);

    if (filter && !idHex.includes(filter)) return;

    totalFrames++;

    if (!map[f.id]) {
        let row = document.createElement("tr");

        row.innerHTML = `
            <td>${formatId(f.id)}</td>
            <td>${f.dlc}</td>
            <td>${f.data}</td>
            <td>1</td>
        `;

        document.querySelector("#t tbody").appendChild(row);

        map[f.id] = { row: row, count: 1 };
    } else {
        let entry = map[f.id];
        entry.count++;

        entry.row.cells[2].innerText = f.data;
        entry.row.cells[3].innerText = entry.count;

        entry.row.style.background = "#033";
        setTimeout(() => entry.row.style.background = "", 50);
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