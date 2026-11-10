#!/usr/bin/env python3
# Usage: C:/Python314/python.exe -I mkpatch.py <out.patch> [file ...]
# Writes a git-apply patch of the files of core/src that differ from the snapshot core/state (paths relative to
# core/, e.g. odin/common/io.hpp). Headers become a/src/... and b/src/...; a new file (absent from the snapshot)
# gets --- /dev/null. The snapshot then advances over the files written, so the series is the sequence of calls.
# CR bytes are kept (a CRLF file of the index, lzma/7zFile.h, must keep them): no sed, no text mode.
import filecmp
import os
import shutil
import subprocess
import sys

ROOT = "C:/claude-work/odinuwp/core"
out_path = sys.argv[1]
files = sys.argv[2:]
os.chdir(ROOT)

if not files:
    files = []
    for dirpath, _, names in os.walk("src"):
        for name in names:
            rel = os.path.relpath(os.path.join(dirpath, name), "src").replace("\\", "/")
            snap = "state/" + rel
            if not os.path.exists(snap) or not filecmp.cmp("src/" + rel, snap, shallow=False):
                files.append(rel)
    files.sort()

chunks = []
for rel in files:
    snap = "state/" + rel
    cur = "src/" + rel
    if os.path.exists(snap) and filecmp.cmp(snap, cur, shallow=False):
        continue
    if os.path.exists(snap):
        result = subprocess.run(["diff", "-u", snap, cur], capture_output=True)
    else:
        result = subprocess.run(["diff", "-uN", snap, cur], capture_output=True)
    if result.returncode not in (0, 1):
        raise SystemExit(f"diff failed for {rel}: {result.stderr!r}")
    lines = result.stdout.split(b"\n")
    fixed = []
    for line in lines:
        if line.startswith(b"--- "):
            name, _, stamp = line[4:].partition(b"\t")
            if not os.path.exists(snap):
                fixed.append(b"--- /dev/null")
            else:
                fixed.append(b"--- a/src/" + name[len(b"state/"):] if name.startswith(b"state/") else line)
        elif line.startswith(b"+++ "):
            name, _, _ = line[4:].partition(b"\t")
            fixed.append(b"+++ b/src/" + name[len(b"src/"):] if name.startswith(b"src/") else line)
        else:
            fixed.append(line)
    chunks.append(b"\n".join(fixed))
    os.makedirs(os.path.dirname(snap) or ".", exist_ok=True)
    shutil.copyfile(cur, snap)

with open(out_path, "wb") as f:
    f.write(b"".join(chunks))
print(f"wrote {out_path} ({len(chunks)} files)")
