#!/usr/bin/env python3
"""Give forward-declared enums an explicit underlying type.

`enum Foo;` (an opaque enum declaration without a fixed underlying type) is a Visual C++
extension.  Standard C++ requires `enum Foo : int;` and the definition must then also say
`enum Foo : int { ... }`.  MSVC already used int for these enums, so layout and values are
unchanged.  Run from GeneralsMD/Code.
"""
import os
import re

FWD = re.compile(r'^(\s*)enum\s+(\w+)\s*;', re.M)
files = []
for root, dirs, names in os.walk('.'):
    dirs[:] = [d for d in dirs if d not in ('Tools', '.git', 'build')]
    for n in names:
        if n.lower().endswith(('.h', '.cpp', '.inl')):
            files.append(os.path.join(root, n))

texts = {f: open(f, encoding='latin-1').read() for f in files}
names = set()
for t in texts.values():
    names.update(m.group(2) for m in FWD.finditer(t))

defined = set()
changed = 0
for f, t in texts.items():
    orig = t
    t = FWD.sub(lambda m: f"{m.group(1)}enum {m.group(2)} : int;" if m.group(2) in names else m.group(0), t)
    def add_type(m):
        defined.add(m.group(2))
        return f"{m.group(1)}enum {m.group(2)} : int{m.group(3)}"
    t = re.sub(r'(^|\b(?:typedef)\s+|^\s*)enum\s+(' + '|'.join(sorted(names)) + r')(\s*\{)', add_type, t, flags=re.M)
    if t != orig:
        open(f, 'w', encoding='latin-1', newline='').write(t)
        changed += 1
print(f"{len(names)} forward-declared enums, {len(defined)} definitions updated, {changed} files changed")
missing = sorted(names - defined)
if missing:
    print("definitions not found (check by hand):", ", ".join(missing))
