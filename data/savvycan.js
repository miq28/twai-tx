let rootEl;

export function init(root, status) {
    rootEl = root;
    render(status);
}

export function update(status) {
    render(status);
}

export function cleanup() {
    rootEl = null;
}

function render(status = {}) {
    if (!rootEl) return;

    const proto = location.protocol === "https:" ? "tcp" : "tcp";
    const url = `${proto}://${location.hostname}:23`;

    rootEl.innerHTML = `
        <h3>SavvyCAN Bridge</h3>
        <div class="mode-summary">
            <div class="kv"><span>Connection</span><strong>${status.tcpClient ? "Connected" : "Waiting"}</strong></div>
            <div class="kv"><span>URL</span><strong>${url}</strong></div>
            <div class="kv"><span>Baud</span><strong>${status.baud || "-"}</strong></div>
            <div class="kv"><span>CAN state</span><strong>${status.canState || "-"}</strong></div>
        </div>
    `;
}
