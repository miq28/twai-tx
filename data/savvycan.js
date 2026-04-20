let timer;

export function init(root) {
    root.innerHTML = `
        <h3>SavvyCAN Bridge</h3>
        <div id="info">Loading...</div>
    `;

    async function update() {
        const res = await fetch('/status');
        const s = await res.json();

        document.getElementById("info").textContent =
            `Mode=${s.mode} Baud=${s.baud}`;
    }

    timer = setInterval(update, 1000);
    update();
}

export function cleanup() {
    clearInterval(timer);
}