let rootEl;
let ws;
let frames = 0;

export function init(root, status) {
    rootEl = root;
    frames = 0;
    render(status);

    ws = new WebSocket(`ws://${location.host}/ws`);
    ws.binaryType = "arraybuffer";
    ws.onmessage = event => {
        if (event.data instanceof ArrayBuffer) frames++;
        update(status);
    };
}

export function update(status = {}) {
    if (!rootEl) return;
    render(status);
}

export function cleanup() {
    if (ws) ws.close();
    ws = null;
    rootEl = null;
}

function render(status = {}) {
    if (!rootEl) return;

    rootEl.innerHTML = `
        <h3>Analyzer</h3>
        <div class="mode-summary">
            <div class="kv"><span>WebSocket frames</span><strong>${frames}</strong></div>
            <div class="kv"><span>RX rate</span><strong>${status.rxRate || 0}/s</strong></div>
            <div class="kv"><span>RX drops</span><strong>${status.rxDropRate || 0}/s</strong></div>
            <div class="kv"><span>RX buffer</span><strong>${status.rxUsage || 0}/${status.rxCapacity || 0}</strong></div>
        </div>
    `;
}
