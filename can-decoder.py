import serial
import struct
import time
import threading
import curses
from collections import deque, defaultdict

# ===== CONFIG =====
PORT = "COM5"
BAUD = 1000000

# Wire format:
# uint16 sync (0xAA55)
# uint8  version
# uint32 ts
# uint32 id
# uint8  dlc
# uint8[8] data
# uint32 flags
unpacker = struct.Struct("<H B I I B 8s I")
FRAME_SIZE = unpacker.size  # 24 bytes

MAX_QUEUE = 50000
DISPLAY_REFRESH = 0.05   # ~20 FPS UI

# ===== GLOBAL =====
running = True
frame_counter = 0

frame_queue = deque(maxlen=MAX_QUEUE)

# stats
id_counter = defaultdict(int)
last_frames = deque(maxlen=20)

# ===== READER (binary + resync safe) =====
def reader():
    global frame_counter

    ser = serial.Serial(PORT, BAUD, timeout=1)
    ser.reset_input_buffer()

    buf = bytearray()

    while running:
        data = ser.read(4096)
        if not data:
            continue

        buf.extend(data)

        while len(buf) >= FRAME_SIZE:
            frame = buf[:FRAME_SIZE]

            try:
                sync, ver, ts, identifier, dlc, data_bytes, flags = unpacker.unpack(frame)
            except struct.error:
                # should not happen, but safe guard
                del buf[0]
                continue

            # ===== SYNC CHECK =====
            if sync != 0xAA55:
                # shift by 1 byte (resync)
                del buf[0]
                continue

            # valid frame → consume
            del buf[:FRAME_SIZE]

            data = data_bytes[:dlc]

            if len(frame_queue) < MAX_QUEUE:
                frame_queue.append((ts, identifier, dlc, data))

            frame_counter += 1


# ===== PROCESSOR =====
def processor():
    while running:
        if frame_queue:
            ts, identifier, dlc, data = frame_queue.popleft()

            id_counter[identifier] += 1
            last_frames.append((ts, identifier, dlc, data))
        else:
            time.sleep(0.001)


# ===== CURSES UI =====
def ui(stdscr):
    global frame_counter

    curses.curs_set(0)
    stdscr.nodelay(True)

    last_time = time.time()
    last_count = 0

    while running:
        stdscr.erase()

        height, width = stdscr.getmaxyx()

        # ===== FPS =====
        now = time.time()
        dt = now - last_time
        fps = (frame_counter - last_count) / dt if dt > 0 else 0

        last_time = now
        last_count = frame_counter

        stdscr.addstr(0, 0, f"FPS: {int(fps)}  Queue: {len(frame_queue)}"[:width])

        # ===== LAST FRAMES =====
        row = 2
        if row < height:
            stdscr.addstr(row, 0, "Last Frames:"[:width])
        row += 1

        for ts, identifier, dlc, data in list(last_frames):
            if row >= height:
                break

            data_str = " ".join(f"{b:02X}" for b in data)
            ts_sec = ts / 1_000_000.0

            line = f"{ts_sec:.6f} ID:{identifier:08X} DLC:{dlc} {data_str}"
            stdscr.addstr(row, 0, line[:width])
            row += 1

        # ===== TOP IDS =====
        if row < height:
            stdscr.addstr(row, 0, "Top IDs:"[:width])
            row += 1

        sorted_ids = sorted(id_counter.items(), key=lambda x: -x[1])[:10]

        for cid, count in sorted_ids:
            if row >= height:
                break

            line = f"ID:{cid:08X} Count:{count}"
            stdscr.addstr(row, 0, line[:width])
            row += 1

        stdscr.refresh()
        time.sleep(DISPLAY_REFRESH)

        # exit on 'q'
        try:
            if stdscr.getch() == ord('q'):
                break
        except:
            pass


# ===== MAIN =====
def main():
    t1 = threading.Thread(target=reader, daemon=True)
    t2 = threading.Thread(target=processor, daemon=True)

    t1.start()
    t2.start()

    curses.wrapper(ui)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        running = False