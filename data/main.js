const appDiv = document.getElementById("app");
const modeSelect = document.getElementById("modeSelect");
const statusEl = document.getElementById("status");

let currentModule = null;

// match your firmware exactly
const MODES = [
    { id: 0, name: "Generator", file: "generator.js" },
    { id: 3, name: "Analyzer", file: "analyzer.js" },
    { id: 4, name: "SavvyCAN", file: "savvycan.js" }
];

// populate dropdown
MODES.forEach(m => {
    const opt = document.createElement("option");
    opt.value = m.id;
    opt.textContent = m.name;
    modeSelect.appendChild(opt);
});

// load module
async function loadMode(modeId) {
    const mode = MODES.find(m => m.id == modeId);

    if (!mode) {
        appDiv.innerHTML = `<h3>Mode ${modeId} not implemented</h3>`;
        return;
    }

    // cleanup previous
    if (currentModule && currentModule.cleanup)
        currentModule.cleanup();

    appDiv.innerHTML = "Loading...";

    const module = await import(`./${mode.file}?t=${Date.now()}`);

    currentModule = module;
    appDiv.innerHTML = "";

    if (module.init)
        module.init(appDiv);
}

// set mode (backend + UI)
async function setMode(modeId) {
    await fetch('/mode', {
        method: 'POST',
        body: `m=${modeId}`,
        headers: {'Content-Type': 'application/x-www-form-urlencoded'}
    });

    await loadMode(modeId);
}

// dropdown change
modeSelect.onchange = () => {
    setMode(parseInt(modeSelect.value));
};

// boot
(async () => {
    const res = await fetch('/status');
    const s = await res.json();

    // reflect current mode
    modeSelect.value = s.mode;

    statusEl.textContent =
        `Mode=${s.mode} Baud=${s.baud} Running=${s.running}`;

    loadMode(s.mode);
})();