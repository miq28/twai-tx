let ws = null;
let consoleEl = null;
let cmdInput = null;

const appDiv = document.getElementById("app");
const modeSelect = document.getElementById("modeSelect");
const statusEl = document.getElementById("status");

let currentModule = null;

// ===== MODES =====
const MODES = [
    { id: 0, name: "Generator", file: "generator.js" },
    { id: 3, name: "Analyzer", file: "analyzer.js" },
    { id: 4, name: "SavvyCAN", file: "savvycan.js" }
];

// ===== WEBSOCKET =====
function connectWS()
{
    ws = new WebSocket(`ws://${location.host}/ws`);
    window.ws = ws; // debug access

    ws.onopen = () =>
    {
        console.log("WS connected");
        statusEl.textContent = "Connected";
    };

    ws.onclose = () =>
    {
        console.log("WS closed → reconnecting...");
        statusEl.textContent = "Disconnected";
        setTimeout(connectWS, 1000);
    };

    ws.onerror = (err) =>
    {
        console.log("WS error", err);
    };

    ws.onmessage = handleWSMessage;
}

// ===== TEXT (DEBUG / CLI) =====
function handleText(msg)
{
    if (!consoleEl) return;

    const div = document.createElement("div");
    div.textContent = msg;

    consoleEl.appendChild(div);
    consoleEl.scrollTop = consoleEl.scrollHeight;
}

// ===== WS MESSAGE =====
function handleWSMessage(event)
{
    // TEXT
    if (typeof event.data === "string")
    {
        handleText(event.data);
        return;
    }

    // BINARY → forward to module
    if (currentModule && currentModule.handleWSMessage)
    {
        currentModule.handleWSMessage(event);
    }
}

// ===== LOAD MODULE =====
async function loadMode(modeId)
{
    const mode = MODES.find(m => m.id == modeId);

    if (!mode)
    {
        appDiv.innerHTML = `<h3>Mode ${modeId} not implemented</h3>`;
        return;
    }

    if (currentModule && currentModule.cleanup)
        currentModule.cleanup();

    appDiv.innerHTML = "Loading...";

    const module = await import(`./${mode.file}?t=${Date.now()}`);

    currentModule = module;
    appDiv.innerHTML = "";

    if (module.init)
        module.init(appDiv);
}

// ===== SET MODE =====
async function setMode(modeId)
{
    await fetch('/mode', {
        method: 'POST',
        body: `m=${modeId}`,
        headers: {'Content-Type': 'application/x-www-form-urlencoded'}
    });

    await loadMode(modeId);
}

// ===== INIT =====
document.addEventListener("DOMContentLoaded", async () =>
{
    consoleEl = document.getElementById("console");
    cmdInput = document.getElementById("cmd");

    // CLI input
    if (cmdInput)
    {
        cmdInput.onkeydown = (e) =>
        {
            if (e.key === "Enter" && ws)
            {
                ws.send(cmdInput.value);
                cmdInput.value = "";
            }
        };
    }

    // populate dropdown
    MODES.forEach(m => {
        const opt = document.createElement("option");
        opt.value = m.id;
        opt.textContent = m.name;
        modeSelect.appendChild(opt);
    });

    // load status
    const res = await fetch('/status');
    const s = await res.json();

    modeSelect.value = s.mode;

    statusEl.textContent =
        `Mode=${s.mode} Baud=${s.baud} Running=${s.running}`;

    await loadMode(s.mode);

    // 🔥 START WS (you forgot this before)
    connectWS();
});

// dropdown change
modeSelect.onchange = () =>
{
    setMode(parseInt(modeSelect.value));
};