#!/usr/bin/env python3
"""Lists Korean UI strings in the sources that have no entry in i18n/strings.txt (after the lookup fallbacks)."""
import glob, json, os, re, sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
table = json.loads(open(os.path.join(root, 'web', 'i18n.js'), encoding='utf-8').read().split('const I18N = ', 1)[1].rstrip().rstrip(';'))
han = re.compile(r'[가-힣]')

def known(k):
    if k in table: return True
    t = k.strip()
    if t != k: return known(t)
    if k.endswith(' %1'): return known(k[:-3])
    m = re.match(r'^(.*) \(([^()]*)\)$', k)
    return bool(m and not han.search(m.group(2)) and m.group(1) in table)

missing = {}
def scan(path, pattern):
    text = re.sub(r'(?m)^\s*//[^\n]*', '', open(os.path.join(root, path), encoding='utf-8').read())
    for m in re.finditer(pattern, text):
        s = next(g for g in m.groups() if g is not None)
        if han.search(s) and not known(s): missing.setdefault(s, set()).add(path)

for f in ['engine/doc.cpp', 'engine/tools.cpp', 'engine/raster.cpp', 'mac/src/app.mm', 'haiku/src/app.cpp'] + sorted(glob.glob(os.path.join(root, 'linux/src/*.cpp'))):
    scan(os.path.relpath(f, root) if os.path.isabs(f) else f, r'(?:TR|L|K)\("((?:[^"\\\n]|\\.)*)"\)|\{[^{}\n]*?"((?:[^"\\\n]|\\.)*[가-힣](?:[^"\\\n]|\\.)*)"')
scan('web/app.js', r"\bt[f]?\('((?:[^'\\\n]|\\.)*)'")
for k, v in sorted(missing.items()): print(', '.join(sorted(v)), '\t', k)
print(len(missing), 'missing')
sys.exit(1 if missing else 0)
