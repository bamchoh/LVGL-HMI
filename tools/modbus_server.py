#!/usr/bin/env python3
"""Minimal Modbus TCP server for testing the HML runtime (stdlib only).

    python tools/modbus_server.py [--port 5020] [--verbose]

Holds 256 coils, 256 discrete inputs, 256 holding registers, 256 input registers.
Input register 0 slowly follows holding register 0 (simulated pump speed PV).
Discrete input 0 mirrors coil 0. Writes are logged.
Supports FC 1,2,3,4,5,6,15,16.
"""
import argparse
import socket
import struct
import threading
import time

coils = [0] * 256
discrete = [0] * 256
holding = [0] * 256
inputs = [0] * 256
lock = threading.Lock()
verbose = False
stop = False


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def simulate():
    while not stop:
        with lock:
            # input reg 0 ramps towards holding reg 0 when coil 0 (run) is on
            target = holding[0] if coils[0] else 0
            if inputs[0] < target:
                inputs[0] = min(inputs[0] + 2, target)
            elif inputs[0] > target:
                inputs[0] = max(inputs[0] - 2, target)
            discrete[0] = coils[0]
            inputs[1] = (inputs[1] + 1) & 0xFFFF
        time.sleep(0.1)


def pack_bits(values):
    out = bytearray((len(values) + 7) // 8)
    for i, v in enumerate(values):
        if v:
            out[i // 8] |= 1 << (i % 8)
    return bytes(out)


def handle_pdu(pdu):
    fc = pdu[0]
    with lock:
        if fc in (1, 2):
            addr, qty = struct.unpack(">HH", pdu[1:5])
            table = coils if fc == 1 else discrete
            if addr + qty > len(table):
                return bytes([fc | 0x80, 2])
            data = pack_bits(table[addr:addr + qty])
            return bytes([fc, len(data)]) + data
        if fc in (3, 4):
            addr, qty = struct.unpack(">HH", pdu[1:5])
            table = holding if fc == 3 else inputs
            if addr + qty > len(table):
                return bytes([fc | 0x80, 2])
            data = b"".join(struct.pack(">H", v & 0xFFFF) for v in table[addr:addr + qty])
            return bytes([fc, len(data)]) + data
        if fc == 5:
            addr, val = struct.unpack(">HH", pdu[1:5])
            if addr >= len(coils):
                return bytes([fc | 0x80, 2])
            coils[addr] = 1 if val == 0xFF00 else 0
            log(f"write coil[{addr}] = {coils[addr]}")
            return pdu[:5]
        if fc == 6:
            addr, val = struct.unpack(">HH", pdu[1:5])
            if addr >= len(holding):
                return bytes([fc | 0x80, 2])
            holding[addr] = val
            log(f"write holding[{addr}] = {val}")
            return pdu[:5]
        if fc == 15:
            addr, qty, nbytes = struct.unpack(">HHB", pdu[1:6])
            if addr + qty > len(coils):
                return bytes([fc | 0x80, 2])
            for i in range(qty):
                coils[addr + i] = (pdu[6 + i // 8] >> (i % 8)) & 1
            log(f"write coils[{addr}..{addr + qty - 1}]")
            return pdu[:5]
        if fc == 16:
            addr, qty, nbytes = struct.unpack(">HHB", pdu[1:6])
            if addr + qty > len(holding):
                return bytes([fc | 0x80, 2])
            for i in range(qty):
                holding[addr + i] = struct.unpack(">H", pdu[6 + 2 * i:8 + 2 * i])[0]
            log(f"write holding[{addr}..{addr + qty - 1}] = {holding[addr:addr + qty]}")
            return pdu[:5]
    return bytes([fc | 0x80, 1])


def recv_exact(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def client_thread(conn, addr):
    log(f"client connected {addr}")
    try:
        while not stop:
            hdr = recv_exact(conn, 7)
            if hdr is None:
                break
            tid, pid, length, uid = struct.unpack(">HHHB", hdr)
            pdu = recv_exact(conn, length - 1)
            if pdu is None:
                break
            resp = handle_pdu(pdu)
            if verbose:
                log(f"fc={pdu[0]} req={pdu.hex()} resp={resp.hex()}")
            conn.sendall(struct.pack(">HHHB", tid, 0, len(resp) + 1, uid) + resp)
    except (ConnectionError, OSError) as e:
        log(f"client error {e}")
    finally:
        conn.close()
        log(f"client disconnected {addr}")


def main():
    global verbose, stop
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=5020)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--run-for", type=float, default=0, help="seconds to run (0 = forever)")
    args = ap.parse_args()
    verbose = args.verbose

    holding[0] = 40
    holding[1] = 50
    coils[1] = 1

    threading.Thread(target=simulate, daemon=True).start()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(5)
    srv.settimeout(0.5)
    log(f"Modbus TCP server listening on {args.host}:{args.port}")
    t_end = time.time() + args.run_for if args.run_for else None
    try:
        while not stop:
            if t_end and time.time() > t_end:
                break
            try:
                conn, addr = srv.accept()
            except socket.timeout:
                continue
            threading.Thread(target=client_thread, args=(conn, addr), daemon=True).start()
    except KeyboardInterrupt:
        pass
    stop = True
    srv.close()
    log(f"server stopped; holding[0..1]={holding[0:2]} coils[0..1]={coils[0:2]}")


if __name__ == "__main__":
    main()
