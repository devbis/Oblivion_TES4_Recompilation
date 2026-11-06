#!/usr/bin/env python3
"""Lists imports of the given PE files that no runtime shim seems to implement.
Usage: check_imports.py Oblivion.exe [d3dx9_27.dll binkw32.dll ...]
Heuristic: a name counts as implemented if it appears in runtime/*.c(pp).
Run it on your DLLs before building; anything listed would abort at runtime
when called (see port/log.txt)."""
import glob, os, re, sys, pefile
rt = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "runtime")
src = "".join(open(f, errors="ignore").read() for f in glob.glob(rt + "/*"))
for path in sys.argv[1:]:
    pe = pefile.PE(path)
    miss = []
    for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        if e.dll.lower().startswith((b"d3dx9_", b"binkw32")) and not path.lower().endswith(".dll"):
            continue  # provided by the recompiled DLLs
        for i in e.imports:
            if not i.name:
                continue  # ordinals (DSOUND #11 etc.) are registered by number
            n = i.name.decode() if i.name else "#%d" % i.ordinal
            if not re.search(r"\b" + re.escape(n.split("@")[0].lstrip("_") if e.dll.lower().startswith(b"bink") else n) + r"\b", src):
                miss.append(e.dll.decode() + "!" + n)
    print(os.path.basename(path), "- unimplemented imports:", miss or "none")
