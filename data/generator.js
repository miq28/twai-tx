let timer;

export function init(root) {
    root.innerHTML = `
        <h3>Generator</h3>

        FPS: <input id="fps" value="10"><br>
        <button id="start">Start</button>
        <button id="stop">Stop</button>
    `;

    document.getElementById("start").onclick = () => {
        const fps = parseInt(document.getElementById("fps").value);

        timer = setInterval(() => {
            // TODO: send CAN frame
            console.log("TX frame");
        }, 1000 / fps);
    };

    document.getElementById("stop").onclick = () => {
        clearInterval(timer);
    };
}

export function cleanup() {
    clearInterval(timer);
}