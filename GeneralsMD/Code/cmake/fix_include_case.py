#!/usr/bin/env python3
"""Normalise #include directives so the code builds on case-sensitive file systems.

The code was written on Windows: includes use the wrong letter case and backslashes.  For every
#include this resolves the path case-insensitively, exactly like Windows would (the including
file's directory first, then the include directories), and rewrites it with the real on-disk
case and forward slashes.  Behaviour on Windows is unchanged.

Run from GeneralsMD/Code:  python3 cmake/fix_include_case.py [--dry-run]
"""
import os
import re
import sys

CODE = os.getcwd()
SEARCH_DIRS = [
    "Compat", "Compat/d3dx8compat",
    "GameEngine/Include", "GameEngine/Include/Precompiled", "GameEngineDevice/Include", "Main",
    "Libraries/Include", "Libraries/Source/WWVegas", "Libraries/Source/WWVegas/WWLib",
    "Libraries/Source/WWVegas/WWMath", "Libraries/Source/WWVegas/WWDebug",
    "Libraries/Source/WWVegas/WWSaveLoad", "Libraries/Source/WWVegas/WW3D2",
    "Libraries/Source/WWVegas/wwshade", "Libraries/Source/WWVegas/Wwutil",
    "Libraries/Source/Compression", "Libraries/Source/Compression/LZHCompress",
]
SYSTEM_DIRS = ["/usr/i686-w64-mingw32/include", "/usr/x86_64-w64-mingw32/include"]
SKIP_DIRS = {"Tools", "build", ".git"}
INCLUDE_RE = re.compile(r'^(\s*#\s*include\s*)([<"])([^>"]+)([>"])(.*)$')

_listing = {}


def listdir_ci(d):
    if d not in _listing:
        try:
            _listing[d] = {n.lower(): n for n in os.listdir(d)}
        except OSError:
            _listing[d] = {}
    return _listing[d]


def resolve(base, rel):
    """Resolve rel (with / separators) under base case-insensitively; return real-case rel or None."""
    cur = base
    out = []
    for part in rel.split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            cur = os.path.dirname(cur)
            out.append("..")
            continue
        real = listdir_ci(cur).get(part.lower())
        if real is None:
            return None
        out.append(real)
        cur = os.path.join(cur, real)
    return "/".join(out) if os.path.isfile(cur) else None


def fix_file(path, dry):
    text = open(path, encoding="latin-1").read()
    lines = text.split("\n")
    changed = 0
    here = os.path.dirname(path)
    for i, line in enumerate(lines):
        m = INCLUDE_RE.match(line)
        if not m:
            continue
        pre, open_, inc, close, post = m.groups()
        rel = inc.replace("\\", "/")
        candidates = ([here] if open_ == '"' else []) + [os.path.join(CODE, d) for d in SEARCH_DIRS]
        fixed = None
        for base in candidates:
            fixed = resolve(base, rel)
            if fixed is not None:
                break
        if fixed is None:
            for base in SYSTEM_DIRS:
                fixed = resolve(base, rel)
                if fixed is not None:
                    break
        if fixed is None:
            fixed = rel  # unknown (missing SDK header etc.): at least fix the slashes
        # keep a leading "./" style out; preserve relative "../" segments as resolved
        if fixed != inc:
            lines[i] = pre + open_ + fixed + close + post
            changed += 1
    if changed and not dry:
        open(path, "w", encoding="latin-1", newline="").write("\n".join(lines))
    return changed


def main():
    dry = "--dry-run" in sys.argv
    total = files = 0
    for root, dirs, names in os.walk(CODE):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for n in names:
            if n.lower().endswith((".h", ".hpp", ".c", ".cpp", ".inl", ".cxx")):
                c = fix_file(os.path.join(root, n), dry)
                if c:
                    files += 1
                    total += c
    print(f"{'would fix' if dry else 'fixed'} {total} includes in {files} files")


if __name__ == "__main__":
    main()
