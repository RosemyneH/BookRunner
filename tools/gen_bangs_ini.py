#!/usr/bin/env python3
import json
import sys
import urllib.request

def main() -> None:
    out_path = sys.argv[1] if len(sys.argv) > 1 else "data/bangs_generated.ini"
    with urllib.request.urlopen("https://duckduckgo.com/bang.js", timeout=120) as r:
        arr = json.loads(r.read().decode("utf-8"))
    rows = []
    for o in arr:
        t = o.get("t")
        url = o.get("u")
        s = o.get("s") or t
        if not t or not url:
            continue
        if not (url.startswith("http://") or url.startswith("https://")):
            continue
        tpl = url.replace("{{{s}}}", "%s").replace("{{s}}", "%s")
        if "%s" not in tpl:
            continue
        rows.append((t, tpl, s))
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("[bangs]\n")
        for t, tpl, _s in rows:
            f.write(f"{t}={tpl}\n")
        f.write("\n[bang_desc]\n")
        for t, _tpl, s in rows:
            f.write(f"{t}={s}\n")
    print(len(rows), "bangs ->", out_path, flush=True)

if __name__ == "__main__":
    main()
