"""Measure real PCM capture through the application's HTTP routes."""
import array
import json
import math
import sys
import time
import urllib.request

base = sys.argv[1].rstrip("/") if len(sys.argv) > 1 else "http://192.168.50.198"

def request(path, method="GET"):
    with urllib.request.urlopen(urllib.request.Request(base + path, method=method), timeout=15) as response:
        return response.read()

print("before", request("/audio/status").decode())
request("/audio/start", "POST")
start = time.monotonic()
readings = []
try:
    for index in range(30):
        t0 = time.monotonic()
        raw = request("/audio/chunk")
        print("chunk", index, len(raw), round(time.monotonic()-t0, 3), flush=True)
        if not raw:
            time.sleep(0.03)
            continue
        samples = []
        for code in raw:
            u = (~code) & 0xff
            value = (((u & 15) << 3) + 132) << ((u >> 4) & 7)
            value -= 132
            samples.append(-value if u & 128 else value)
        mean = sum(samples) / len(samples)
        rms = math.sqrt(sum((s - mean) ** 2 for s in samples) / len(samples))
        readings.append((len(raw), round(rms, 1), min(samples), max(samples)))
finally:
    request("/audio/stop", "POST")
duration = time.monotonic() - start
print("duration_s", round(duration, 2), "payload_kbit_s", round(sum(r[0] for r in readings) * 8 / duration / 1000, 1))
print("first", readings[:5], "last", readings[-5:])
print("rms_min_max", min(r[1] for r in readings), max(r[1] for r in readings))
print("after", json.loads(request("/audio/status")))



