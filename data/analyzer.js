let ws;
let fps = 0;
let last = performance.now();

export function init(root) {
    root.innerHTML = `
        <h3>Analyzer</h3>
        <div id="stats">FPS: 0</div>
        <canvas id="canvas"></canvas>
    `;

    const canvas = document.getElementById("canvas");
    const ctx = canvas.getContext("2d");

    function resize() {
        canvas.width = window.innerWidth;
        canvas.height = window.innerHeight - 100;
    }
    resize();
    window.onresize = resize;

    ws = new WebSocket(`ws://${location.host}/ws`);
    ws.binaryType = "arraybuffer";

    ws.onmessage = () => {
        fps++;
    };

    function loop() {
        const now = performance.now();

        if (now - last > 1000) {
            document.getElementById("stats").textContent =
                `FPS: ${fps}`;
            fps = 0;
            last = now;
        }

        requestAnimationFrame(loop);
    }
    loop();
}

export function cleanup() {
    if (ws) ws.close();
}