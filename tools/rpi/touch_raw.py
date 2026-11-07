import struct, sys, time
# raw ADS7846 events: prints one line per touch-down and per touch-up with the last raw X/Y
dev = sys.argv[1] if len(sys.argv) > 1 else "/dev/input/event0"
dur = float(sys.argv[2]) if len(sys.argv) > 2 else 120
fmt = "qqHHi"; sz = struct.calcsize(fmt)
x = y = p = None; touch = 0; t0 = time.time()
with open(dev, "rb", buffering=0) as f:
    while time.time() - t0 < dur:
        d = f.read(sz)
        if not d: break
        _, _, typ, code, val = struct.unpack(fmt, d)
        if typ == 3:
            if code == 0: x = val
            elif code == 1: y = val
            elif code == 24: p = val
        elif typ == 1 and code == 330:
            touch = val
            print("%6.1fs %s x=%s y=%s p=%s" % (time.time() - t0, "DOWN" if val else "UP  ", x, y, p), flush=True)
