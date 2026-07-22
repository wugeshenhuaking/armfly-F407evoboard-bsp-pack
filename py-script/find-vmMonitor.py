import mss

with mss.mss() as sct:
    for i, monitor in enumerate(sct.monitors):
        print(f"[{i}] {monitor}")
        