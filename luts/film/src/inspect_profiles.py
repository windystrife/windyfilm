import json, sys, os
import numpy as np
P = sys.argv[1]
def shape(v):
    try:
        a = np.array(v, dtype=float); return "arr%s nan=%d min=%.3g max=%.3g" % (a.shape, np.isnan(a).sum(), np.nanmin(a), np.nanmax(a))
    except Exception:
        return type(v).__name__ + (" len=%d" % len(v) if hasattr(v, "__len__") else "")
def walk(d, pre=""):
    for k, v in d.items():
        if isinstance(v, dict):
            print(pre + k + ":"); walk(v, pre + "  ")
        elif isinstance(v, list) and len(v) > 8:
            print(pre + k + " " + shape(v))
        else:
            s = repr(v)
            print(pre + k + " = " + (s[:200]))
for n in sys.argv[2:]:
    print("=====", n)
    d = json.load(open(os.path.join(P, n + ".json")))
    walk({k: v for k, v in d.items() if k != "metadata"})
