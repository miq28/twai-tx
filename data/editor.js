const Editor = (function () {

    const el = ace.edit("editor");

    el.setTheme("ace/theme/monokai");
    el.setOptions({
        fontSize: "14px",
        showPrintMargin: false,
        useWorker: false // REQUIRED for ESP
    });

    // function setMode(path) {
    //     if (path.endsWith(".js"))
    //         el.session.setMode("ace/mode/javascript");
    //     else if (path.endsWith(".json"))
    //         el.session.setMode("ace/mode/json");
    //     else if (path.endsWith(".css"))
    //         el.session.setMode("ace/mode/css");
    //     else
    //         el.session.setMode("ace/mode/html");
    // }

    function setMode(name) {
        const n = name.toLowerCase();

        if (n.endsWith(".js")) el.session.setMode("ace/mode/javascript");
        else if (n.endsWith(".json")) el.session.setMode("ace/mode/json");
        else if (n.endsWith(".css")) el.session.setMode("ace/mode/css");
        else if (n.endsWith(".cpp") || n.endsWith(".h"))
            el.session.setMode("ace/mode/c_cpp");
        else if (n.endsWith(".txt"))
            el.session.setMode("ace/mode/text");
        else
            el.session.setMode("ace/mode/html");
    }

    function setStatus(s) {
        document.getElementById("status").innerText = s;
    }

    async function load() {
        const path = document.getElementById("path").value;

        try {
            const res = await fetch("/file?path=" + encodeURIComponent(path));

            if (!res.ok) {
                setStatus("Load error");
                return;
            }

            el.setValue("", -1);
            el.setReadOnly(false);                 // reset from previous HEX / truncation
            el.session.setMode("ace/mode/text");   // safe default before detection

            const reader = res.body.getReader();
            const decoder = new TextDecoder();

            let total = 0;
            let buffer = "";
            let updates = 0;

            const MAX_SIZE = 300 * 1024;     // 300KB hard cap
            const BATCH_SIZE = 4096;         // insert every 4KB
            const MAX_UPDATES = 200;         // avoid too many inserts

            while (true) {
                const { done, value } = await reader.read();
                if (done) break;

                // 👉 BINARY DETECTION (raw bytes)
                if (isBinaryChunk(value)) {
                    showHex(value, reader); // switch to hex mode
                    return;
                }

                const chunk = decoder.decode(value, { stream: true });

                total += chunk.length;

                if (total > MAX_SIZE) {
                    el.setReadOnly(true);
                    setStatus("Large file (read-only)");
                    break;
                }

                buffer += chunk;

                if (buffer.length >= BATCH_SIZE) {
                    append(buffer);
                    buffer = "";

                    updates++;
                    if (updates > MAX_UPDATES) {
                        setStatus("Too many updates, truncating");
                        break;
                    }
                }
            }

            if (buffer.length > 0)
                append(buffer);

            // only enable editing if fully loaded
            if (total <= MAX_SIZE && updates <= MAX_UPDATES)
                el.setReadOnly(false);

            setMode(path);
            setStatus("Loaded");

        } catch (e) {
            setStatus("Fail");
        }
    }

    function isBinaryChunk(uint8arr) {
        let nonPrintable = 0;

        for (let i = 0; i < uint8arr.length; i++) {
            const c = uint8arr[i];

            if (c === 0) return true; // NULL byte = binary

            if (c < 9 || (c > 13 && c < 32) || c > 126) {
                nonPrintable++;
            }
        }

        return (nonPrintable / uint8arr.length) > 0.2;
    }

    function append(text) {
        const row = el.session.getLength() - 1;

        el.session.insert(
            { row: row < 0 ? 0 : row, column: Number.MAX_SAFE_INTEGER },
            text
        );
    }

    function appendHex(uint8arr, baseOffset) {
        let out = "";

        for (let i = 0; i < uint8arr.length; i += 16) {
            let hex = "";
            let ascii = "";

            for (let j = 0; j < 16; j++) {
                if (i + j < uint8arr.length) {
                    const b = uint8arr[i + j];

                    hex += b.toString(16).padStart(2, "0") + " ";
                    ascii += (b >= 32 && b <= 126)
                        ? String.fromCharCode(b)
                        : ".";
                } else {
                    hex += "   ";
                    ascii += " ";
                }
            }

            const addr = (baseOffset + i).toString(16).padStart(8, "0");

            out += `${addr}  ${hex} ${ascii}\n`;
        }

        append(out);
    }

    async function showHex(firstChunk, reader) {
        el.setValue("", -1);
        el.setReadOnly(true);
        el.session.setMode("ace/mode/text");

        let offset = 0;

        appendHex(firstChunk, offset);
        offset += firstChunk.length;

        while (true) {
            const { done, value } = await reader.read();
            if (done) break;

            appendHex(value, offset);
            offset += value.length;
        }

        setStatus("HEX view (binary file)");
    }

    async function save() {
        const path = document.getElementById("path").value;
        const content = el.getValue();

        try {
            const res = await fetch("/save", {
                method: "POST",
                headers: {
                    "Content-Type": "text/plain",
                    "X-Path": path
                },
                body: content
            });

            if (!res.ok) {
                setStatus("Save error");
                return;
            }

            setStatus("Saved");
        } catch {
            setStatus("Fail");
        }
    }

    // optional: autosave (disabled by default)
    /*
    let t;
    el.on("change", () => {
        clearTimeout(t);
        t = setTimeout(save, 1000);
    });
    */

    return { load, save };

})();

// ===== FILE BROWSER =====

async function loadList(dir = "/") {
    try {
        const res = await fetch("/list?dir=" + encodeURIComponent(dir));
        const list = await res.json();

        const elList = document.getElementById("filelist");
        elList.innerHTML = "";

        list.forEach(item => {
            const div = document.createElement("div");

            div.className = "file " + item.type;
            div.textContent = item.name;

            div.onclick = () => {
                if (item.type === "file") {
                    const full = "/" + item.name;
                    document.getElementById("path").value = full;
                    Editor.load();
                }
            };

            elList.appendChild(div);
        });

    } catch {
        console.log("list failed");
    }
}

let ctxPath = "";

function showCtx(x, y, path) {
    ctxPath = path;

    const m = document.getElementById("ctx");
    m.style.left = x + "px";
    m.style.top = y + "px";
    m.style.display = "block";
}

window.onclick = () => {
    document.getElementById("ctx").style.display = "none";
};

async function ctxDelete() {
    await fetch("/delete", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "path=" + ctxPath
    });
    loadTree();
}

async function ctxRename() {
    const to = prompt("Rename to:", ctxPath);
    if (!to) return;

    await fetch("/rename", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "from=" + ctxPath + "&to=" + to
    });

    loadTree();
}

// ===== TREE BUILD =====
function buildTree(files) {
    const root = {};

    files.forEach(f => {
        const parts = f.path.split("/").filter(Boolean);

        let node = root;

        parts.forEach((p, i) => {
            if (!node[p]) {
                node[p] = {
                    __meta: i === parts.length - 1 ? f : { type: "dir" },
                    __children: {}
                };
            }
            node = node[p].__children;
        });
    });

    return root;
}

function renderTree(node, container, basePath = "") {
    Object.keys(node).forEach(name => {
        const item = node[name];
        const meta = item.__meta;

        const fullPath = basePath + "/" + name;

        const div = document.createElement("div");
        div.className = "file " + meta.type;

        div.textContent =
            name + (meta.size ? ` (${meta.size})` : "");

        // CLICK
        div.onclick = (e) => {
            e.stopPropagation();

            if (meta.type === "dir") {
                div.classList.toggle("open");

                if (!div._loaded) {
                    const child = document.createElement("div");
                    child.className = "children";
                    div.appendChild(child);

                    renderTree(item.__children, child, fullPath);
                    div._loaded = true;
                }
            } else {
                document.getElementById("path").value = fullPath;
                Editor.load();
            }
        };

        // RIGHT CLICK
        div.oncontextmenu = (e) => {
            e.preventDefault();
            showCtx(e.pageX, e.pageY, fullPath);
        };

        container.appendChild(div);
    });
}

async function loadTree() {
    const res = await fetch("/list?dir=/");
    const list = await res.json();

    const tree = buildTree(list);

    const root = document.getElementById("filelist");
    root.innerHTML = "";

    renderTree(tree, root, "");
}

document.body.ondragover = (e) => e.preventDefault();

document.body.ondrop = async (e) => {
    e.preventDefault();

    const file = e.dataTransfer.files[0];
    if (!file) return;

    const path = "/" + file.name;

    const buf = await file.arrayBuffer();

    await fetch("/save", {
        method: "POST",
        headers: {
            "Content-Type": "application/octet-stream",
            "X-Path": path
        },
        body: buf
    });

    loadTree("/");
};

// auto-load on open
window.onload = () => {
    Editor.load();
    // loadList("/");
    loadTree("/");
};