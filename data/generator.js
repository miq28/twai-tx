let rootEl;
let latestStatus = {};

export function init(root, status) {
    rootEl = root;
    latestStatus = status || {};
    render();
}

export function update(status) {
    latestStatus = status || latestStatus;
    updateReadouts();
}

export function cleanup() {
    rootEl = null;
}

async function post(path, values) {
    const body = new URLSearchParams();
    Object.entries(values).forEach(([key, value]) => body.set(key, String(value)));

    const res = await fetch(path, { method: "POST", body });
    if (!res.ok) throw new Error(await res.text());

    latestStatus = await res.json();
    updateReadouts();
}

function render() {
    if (!rootEl) return;

    const mode = Number(latestStatus.mode);
    const label = mode === 1 ? "Slow generator" : mode === 2 ? "ECU simulator" : "Generator";

    rootEl.innerHTML = `
        <div class="control-header">
            <h3>${label}</h3>
            <span id="genRunBadge" class="pill">-</span>
        </div>

        <div class="control-grid">
            <label class="field">
                <span>Run</span>
                <div class="segmented">
                    <button id="runBtn" type="button">Start</button>
                    <button id="stopBtn" type="button">Stop</button>
                </div>
            </label>

            <label class="field">
                <span>FPS</span>
                <div class="input-action">
                    <input id="fpsInput" type="number" min="0" step="1" inputmode="numeric">
                    <button id="fpsApply" type="button">Apply</button>
                </div>
                <small>0 means max sustainable rate</small>
            </label>

            <label class="field">
                <span>Locked ID</span>
                <select id="lockSelect">
                    <option value="-1">Cycle 0-9</option>
                    <option value="0">0</option>
                    <option value="1">1</option>
                    <option value="2">2</option>
                    <option value="3">3</option>
                    <option value="4">4</option>
                    <option value="5">5</option>
                    <option value="6">6</option>
                    <option value="7">7</option>
                    <option value="8">8</option>
                    <option value="9">9</option>
                </select>
            </label>

            <label class="field">
                <span>Frame ID</span>
                <div class="segmented">
                    <button id="stdBtn" type="button">STD</button>
                    <button id="extBtn" type="button">EXT</button>
                </div>
            </label>

            <label class="field">
                <span>CAN baud</span>
                <select id="baudSelect">
                    <option value="1000000">1M</option>
                    <option value="800000">800k</option>
                    <option value="500000">500k</option>
                    <option value="250000">250k</option>
                    <option value="125000">125k</option>
                    <option value="100000">100k</option>
                    <option value="50000">50k</option>
                    <option value="25000">25k</option>
                </select>
            </label>

            <label class="field">
                <span>Listen only</span>
                <div class="segmented">
                    <button id="listenOffBtn" type="button">Off</button>
                    <button id="listenOnBtn" type="button">On</button>
                </div>
            </label>
        </div>

        <div class="mode-summary compact">
            <div class="kv"><span>TX ok</span><strong id="txOkReadout">-</strong></div>
            <div class="kv"><span>TX attempts</span><strong id="txAttemptReadout">-</strong></div>
            <div class="kv"><span>TX drops</span><strong id="txDropReadout">-</strong></div>
            <div class="kv"><span>RX buffer</span><strong id="rxBufferReadout">-</strong></div>
        </div>
    `;

    rootEl.querySelector("#runBtn").onclick = () => post("/run", { v: 1 });
    rootEl.querySelector("#stopBtn").onclick = () => post("/run", { v: 0 });
    rootEl.querySelector("#fpsApply").onclick = () => post("/generator", { fps: rootEl.querySelector("#fpsInput").value || 0 });
    rootEl.querySelector("#lockSelect").onchange = event => post("/generator", { lock: event.target.value });
    rootEl.querySelector("#stdBtn").onclick = () => post("/generator", { ext: 0 });
    rootEl.querySelector("#extBtn").onclick = () => post("/generator", { ext: 1 });
    rootEl.querySelector("#baudSelect").onchange = event => post("/baud", { v: event.target.value });
    rootEl.querySelector("#listenOffBtn").onclick = () => post("/listen", { v: 0 });
    rootEl.querySelector("#listenOnBtn").onclick = () => post("/listen", { v: 1 });

    setControlValues();
    updateReadouts();
}

function setActive(selector, active) {
    const el = rootEl?.querySelector(selector);
    if (el) el.classList.toggle("active", !!active);
}

function setControlValues() {
    if (!rootEl) return;

    rootEl.querySelector("#fpsInput").value = latestStatus.targetFps ?? 0;
    rootEl.querySelector("#lockSelect").value = latestStatus.lockedId ?? -1;
    rootEl.querySelector("#baudSelect").value = latestStatus.baud ?? 500000;
}

function updateReadouts() {
    if (!rootEl) return;

    const running = !!latestStatus.running;
    const extended = !!latestStatus.extended;
    const listen = !!latestStatus.listen;
    const rxPct = latestStatus.rxCapacity
        ? Math.round((Number(latestStatus.rxUsage || 0) * 100) / Number(latestStatus.rxCapacity))
        : 0;

    const badge = rootEl.querySelector("#genRunBadge");
    badge.textContent = running ? "Running" : "Stopped";
    badge.className = `pill ${running ? "good" : "warn"}`;

    setActive("#runBtn", running);
    setActive("#stopBtn", !running);
    setActive("#stdBtn", !extended);
    setActive("#extBtn", extended);
    setActive("#listenOffBtn", !listen);
    setActive("#listenOnBtn", listen);

    rootEl.querySelector("#txOkReadout").textContent = `${latestStatus.txOkRate || 0}/s`;
    rootEl.querySelector("#txAttemptReadout").textContent = `${latestStatus.txAttemptRate || 0}/s`;
    rootEl.querySelector("#txDropReadout").textContent = `${latestStatus.txDropRate || 0}/s`;
    rootEl.querySelector("#rxBufferReadout").textContent = `${rxPct}%`;
}
