let isDirty = false;

const Editor = (function () {

    const el = ace.edit("editor");

    el.setTheme("ace/theme/monokai");
    el.setOptions({
        fontSize: "14px",
        showPrintMargin: false,
        useWorker: false // REQUIRED for ESP
    });
    el.commands.addCommand({
        name: "beautify",
        bindKey: {
            win: "Ctrl-B", mac: "Command-B"
        },
        exec: function (editor) {
            ace.require("ace/ext/beautify").beautify(editor.session);
        }
    });
    el.commands.addCommand({
        name: "saveFile",
        bindKey: {
            win: "Ctrl-S", mac: "Command-S"
        },
        exec: function (editor) {
            Editor.save();
        }
    });

    function setMode(name) {
        const n = name.toLowerCase();

        if (n.endsWith(".js")) el.session.setMode("ace/mode/javascript");
        else if (n.endsWith(".json")) el.session.setMode("ace/mode/json");
        else if (n.endsWith(".css")) el.session.setMode("ace/mode/css");
        else if (n.endsWith(".cpp") || n.endsWith(".h"))
            el.session.setMode("ace/mode/c_cpp");
        else if (n.endsWith(".txt"))
            el.session.setMode("ace/mode/text");
        else if (n.endsWith(".py"))
            el.session.setMode("ace/mode/python");
        else if (n.endsWith(".ini"))
            el.session.setMode("ace/mode/ini");
        else if (n.endsWith(".csv"))
            el.session.setMode("ace/mode/csv");
        else
            el.session.setMode("ace/mode/html");
    }

    function setStatus(s, state = "") {
        const el = document.getElementById("status");
        el.innerText = s;

        el.classList.remove("dirty", "clean");

        if (state) el.classList.add(state);
    }

    async function load() {
        const path = document.getElementById("path").value;

        // 🚫 BLOCK directories
        if (!/\.[a-z0-9]+$/i.test(path)) {
            setStatus("Not a file");
            return;
        }

        try {
            const res = await fetch("/file?path=" + encodeURIComponent(path));

            if (!res.ok) {
                setStatus("Load error");
                return;
            }

            el.setValue("", -1);
            el.setReadOnly(false); // reset from previous HEX / truncation
            el.session.setMode("ace/mode/text"); // safe default before detection

            const reader = res.body.getReader();
            const decoder = new TextDecoder();

            let total = 0;
            let buffer = "";
            let updates = 0;

            const MAX_SIZE = 512 * 1024; // 300KB hard cap
            const BATCH_SIZE = 4096; // insert every 4KB
            const MAX_UPDATES = 200; // avoid too many inserts

            while (true) {
                const {
                    done,
                    value
                } = await reader.read();
                if (done) break;

                // 👉 BINARY DETECTION (raw bytes)
                if (isBinaryChunk(value)) {
                    showHex(value, reader); // switch to hex mode
                    return;
                }

                const chunk = decoder.decode(value, {
                    stream: true
                });

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
            isDirty = false;
            setStatus("Loaded", "clean");

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
            {
                row: row < 0 ? 0: row, column: Number.MAX_SAFE_INTEGER
            },
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
                    ? String.fromCharCode(b): ".";
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
            const {
                done,
                value
            } = await reader.read();
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
                    "Content-Type": "application/octet-stream",
                    "X-Path": path
                },
                body: new TextEncoder().encode(content) // 🔥 critical
            });

            if (!res.ok) {
                setStatus("Save error");
                return;
            }

            isDirty = false;
            setStatus("Saved", "clean");
        } catch {
            setStatus("Fail");
        }
    }

    el.on("change", () => {
        if (!isDirty) {
            isDirty = true;
            // setStatus("Modified");
            setStatus("● Modified", "dirty");
        }
    });

    // optional: autosave (disabled by default)
    /*
    let t;
    el.on("change", () => {
        clearTimeout(t);
        t = setTimeout(save, 1000);
    });
    */

    return {
        load,
        save,
         setStatus   // ✅ expose it
    };

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
    await fetch("/delete",
        {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded"
            },
            body: "path=" + ctxPath
        });
    loadTree();
}

async function ctxRename() {
    const to = prompt("Rename to:",
        ctxPath);
    if (!to) return;

    await fetch("/rename", {
        method: "POST",
        headers: {
            "Content-Type": "application/x-www-form-urlencoded"
        },
        body: "from=" + ctxPath + "&to=" + to
    });

    loadTree();
}

async function ctxDownload() {
    if (!ctxPath) return;

    const res = await fetch("/file?path=" + encodeURIComponent(ctxPath));
    const blob = await res.blob();

    const filename = ctxPath.split("/").pop();

    const url = URL.createObjectURL(blob);

    const a = document.createElement("a");
    a.href = url;
    a.download = filename;

    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);

    URL.revokeObjectURL(url);
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
                    __meta: {},
                    __children: {}
                };
            }
            // assign meta ONLY for leaf
            if (i === parts.length - 1) {
                node[p].__meta = f;
            }
            node = node[p].__children;
        });
    });

    return root;
}

function renderTree(node, container, basePath = "") {

    Object.keys(node)
    .sort((a,
        b) => {
        const A = Object.keys(node[a].__children).length > 0;
        const B = Object.keys(node[b].__children).length > 0;
        return (B - A) || a.localeCompare(b);
    })
    .forEach(name => {

        const item = node[name];
        const fullPath = basePath + "/" + name;

        const isDir = Object.keys(item.__children).length > 0;

        const div = document.createElement("div");

        div.classList.add("file");

        if (isDir) {
            div.classList.add("dir");
        } else {
            div.classList.add("file-item"); // avoid duplicate "file"
        }

        const meta = item.__meta;

        div.innerHTML = `
        <span class="label">${name}</span>
        <span style="color:#888"> ${meta.size ? `(${meta.size})`: ""}</span>
        `;

        let child = null;

        if (isDir) {
            child = document.createElement("div");
            child.className = "children";
            div.appendChild(child);
        }

        // CLICK
        div.onclick = (e) => {
            e.stopPropagation();

            if (isDir) {
                div.classList.toggle("open");

                if (!div._loaded) {
                    renderTree(item.__children, child, fullPath);
                    div._loaded = true;
                }
                return;
            }

            // 🔥 ADD THIS
            if (!confirmDiscard()) return;

            document.getElementById("path").value = fullPath;
            Editor.load();
        };

        // RIGHT CLICK
        div.oncontextmenu = (e) => {
            e.preventDefault();
            e.stopPropagation(); // 🔥 IMPORTANT
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

    renderTree(tree,
        root,
        "");
}

// ===== SIMPLE FILE ACTIONS =====

function newFile() {
    const name = prompt("New file name:",
        "new.txt");
    if (!name) return;

    const path = "/" + name;

    fetch("/save", {
        method: "POST",
        headers: {
            "Content-Type": "application/octet-stream",
            "X-Path": path
        },
        body: new TextEncoder().encode("\n")
    }).then(res => {
        if (res.status === 409) {
            alert("File already exists");
            return;
        }

        document.getElementById("path").value = path;
        loadTree();
        Editor.load();
    });
}

function uploadFile() {
    const input = document.createElement("input");
    input.type = "file";

    input.onchange = async () => {
        const file = input.files[0];
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

        document.getElementById("path").value = path;
        loadTree();
        Editor.load();
    };

    input.click();
}

function confirmDiscard() {
    if (!isDirty) return true;
    return confirm("You have unsaved changes. Discard?");
}

function safeLoad() {
    if (!confirmDiscard()) return;
    Editor.load();
}

async function downloadAll() {
    const zip = new JSZip();

    const res = await fetch("/list?dir=/");
    const list = await res.json();

    for (const file of list) {
        if (file.type !== "file") continue;
        
        Editor.setStatus(`Downloading ${file.name}...`);

        const r = await fetch("/file?path=" + encodeURIComponent(file.path));
        if (!r.ok) continue;

        const blob = await r.blob();

        // remove leading "/"
        const name = file.path.substring(1);

        zip.file(name, blob);
    }

    const content = await zip.generateAsync({ type: "blob" });

    const a = document.createElement("a");
    a.href = URL.createObjectURL(content);
    a.download = "backup.zip";
    a.click();

    URL.revokeObjectURL(a.href);
    Editor.setStatus("Download complete", "clean");
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

document.addEventListener("keydown", function (e) {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") {
        e.preventDefault();
    }
});

window.addEventListener("beforeunload", function (e) {
    if (!isDirty) return;

    e.preventDefault();
    e.returnValue = "";
});

// auto-load on open
window.onload = () => {
    Editor.load();
    // loadList("/");
    loadTree("/");
};