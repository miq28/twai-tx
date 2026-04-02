import sys
import time
import threading
import struct
import serial
from collections import deque, defaultdict

from PyQt6 import QtWidgets, QtCore, QtGui

# ===== CONFIG =====
PORT = "COM5"
BAUD = 1000000

MAX_QUEUE = 100000
MAX_ROWS = 200

# highlight ID (change this to your test ID)
HIGHLIGHT_ID = 0x002   # e.g. 0x123 or None

# ===== GLOBAL =====
running = True
frame_counter = 0

frame_queue = deque(maxlen=MAX_QUEUE)

id_counter = defaultdict(int)
last_ts_per_id = {}   # 🔥 for Δt calculation


# ===== READER =====
def reader():
    global frame_counter

    ser = serial.Serial(PORT, BAUD, timeout=1)
    ser.reset_input_buffer()

    buf = bytearray()

    while running:
        data = ser.read(4096)
        if not data:
            continue
        
        print(data[:20])    # ← add this line

        buf.extend(data)

        while True:
            # need at least sync + id + dlc
            if len(buf) < 6:
                break

            # ---- FIND SYNC (0xAA) ----
            if buf[0] != 0xAA:
                del buf[0]
                continue

            # ---- ID ----
            identifier = (
                buf[1]
                | (buf[2] << 8)
                | (buf[3] << 16)
                | (buf[4] << 24)
            )

            # ---- DLC ----
            dlc = buf[5]

            if dlc > 8:
                # invalid → resync
                del buf[0]
                continue

            frame_len = 1 + 4 + 1 + dlc

            if len(buf) < frame_len:
                break

            data_bytes = buf[6:6+dlc]

            del buf[:frame_len]

            ts = int(time.time() * 1_000_000)

            if len(frame_queue) < MAX_QUEUE:
                frame_queue.append((ts, identifier, dlc, data_bytes))

            frame_counter += 1


# ===== GUI =====
class CANViewer(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("CAN Analyzer (Timing + Highlight)")
        self.resize(1100, 700)

        layout = QtWidgets.QVBoxLayout(self)

        # ===== STATUS =====
        self.label = QtWidgets.QLabel("Starting...")
        layout.addWidget(self.label)

        # ===== FRAME TABLE =====
        self.table = QtWidgets.QTableWidget()
        self.table.setColumnCount(5)
        self.table.setHorizontalHeaderLabels(["Time (s)", "ID", "Δt (ms)", "DLC", "Data"])
        self.table.verticalHeader().setVisible(False)
        layout.addWidget(self.table)

        # ===== TOP IDS =====
        self.id_table = QtWidgets.QTableWidget()
        self.id_table.setColumnCount(2)
        self.id_table.setHorizontalHeaderLabels(["ID", "Count"])
        self.id_table.setRowCount(10)
        self.id_table.verticalHeader().setVisible(False)
        layout.addWidget(self.id_table)

        # ===== TIMER =====
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.update_ui)
        self.timer.start(100)  # 10 FPS UI

        # stats
        self.last_time = time.time()
        self.last_count = 0
        self.fps_window = deque(maxlen=10)

    def update_ui(self):
        global frame_counter

        # ===== FPS =====
        now = time.time()
        dt = now - self.last_time

        instant_fps = (frame_counter - self.last_count) / dt if dt > 0 else 0
        self.fps_window.append(instant_fps)
        fps_avg = sum(self.fps_window) / len(self.fps_window)

        self.last_time = now
        self.last_count = frame_counter

        # ===== DRAIN QUEUE =====
        drained = 0
        latest_frames = []

        while frame_queue:
            latest_frames.append(frame_queue.popleft())
            drained += 1

        # ===== PROCESS ONLY LAST N FOR DISPLAY =====
        display_frames = latest_frames[-50:]  # limit UI work

        for ts, identifier, dlc, data in display_frames:

            id_counter[identifier] += 1

            # ===== Δt calculation =====
            dt_ms = 0.0
            if identifier in last_ts_per_id:
                dt_us = ts - last_ts_per_id[identifier]
                dt_ms = dt_us / 1000.0

            last_ts_per_id[identifier] = ts

            # ===== SCROLL =====
            self.table.insertRow(0)

            ts_sec = ts / 1_000_000.0
            data_str = " ".join(f"{b:02X}" for b in data)

            self.table.setItem(0, 0, QtWidgets.QTableWidgetItem(f"{ts_sec:.6f}"))
            self.table.setItem(0, 1, QtWidgets.QTableWidgetItem(f"{identifier:08X}"))
            self.table.setItem(0, 2, QtWidgets.QTableWidgetItem(f"{dt_ms:.3f}"))
            self.table.setItem(0, 3, QtWidgets.QTableWidgetItem(str(dlc)))
            self.table.setItem(0, 4, QtWidgets.QTableWidgetItem(data_str))

            # ===== HIGHLIGHT =====
            if HIGHLIGHT_ID is not None and identifier == HIGHLIGHT_ID:
                for col in range(5):
                    self.table.item(0, col).setBackground(QtGui.QColor("yellow"))

            # limit rows
            if self.table.rowCount() > MAX_ROWS:
                self.table.removeRow(self.table.rowCount() - 1)

        # ===== STATUS =====
        self.label.setText(
            f"FPS(avg): {int(fps_avg)} | FPS(inst): {int(instant_fps)} | "
            f"Total: {frame_counter} | Drained: {drained} | Queue: {len(frame_queue)}"
        )

        # ===== TOP IDS =====
        top_ids = sorted(id_counter.items(), key=lambda x: -x[1])[:10]

        for row, (cid, count) in enumerate(top_ids):
            self.id_table.setItem(row, 0, QtWidgets.QTableWidgetItem(f"{cid:08X}"))
            self.id_table.setItem(row, 1, QtWidgets.QTableWidgetItem(str(count)))


# ===== MAIN =====
def main():
    t = threading.Thread(target=reader, daemon=True)
    t.start()

    app = QtWidgets.QApplication(sys.argv)
    win = CANViewer()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        running = False