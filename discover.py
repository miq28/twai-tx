import socket
import threading
import subprocess
import tkinter as tk

PORT = 17223
SAVVYCAN_PATH = r"E:\Software\SavvyCAN-Windows_x64\SavvyCAN.exe"

devices = {}

# ===== UDP LISTENER =====
def listen_udp(update_ui):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('', PORT))

    while True:
        data, addr = sock.recvfrom(1024)

        try:
            msg = data.decode()
        except:
            continue

        if not msg.startswith("name="):
            continue

        try:
            parts = dict(x.split("=") for x in msg.split(";"))
        except:
            continue

        name = parts.get("name", "unknown")
        ip = parts.get("ip", addr[0])
        port = parts.get("port", "23")

        key = f"{name} ({ip}:{port})"

        if key not in devices:
            devices[key] = (ip, port)
            update_ui(key)

# ===== GUI =====
class App:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP Discovery")

        self.listbox = tk.Listbox(root, width=50, height=10)
        self.listbox.pack(padx=10, pady=10)

        self.connect_btn = tk.Button(root, text="Connect", command=self.connect)
        self.connect_btn.pack(pady=5)

    def add_device(self, name):
        self.listbox.insert(tk.END, name)

    def connect(self):
        selection = self.listbox.curselection()
        if not selection:
            return

        key = self.listbox.get(selection[0])
        ip, port = devices[key]

        subprocess.Popen([
            SAVVYCAN_PATH,
            f"tcp://{ip}:{port}"
        ])

# ===== MAIN =====
root = tk.Tk()
app = App(root)

def update_ui(name):
    root.after(0, app.add_device, name)

threading.Thread(target=listen_udp, args=(update_ui,), daemon=True).start()

root.mainloop()