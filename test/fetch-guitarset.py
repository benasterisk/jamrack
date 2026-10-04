#!/usr/bin/env python3
"""Downloads GuitarSet (CC BY 4.0, Zenodo record 3371780) into a directory.

    python3 test/fetch-guitarset.py <dir> [--hex]

Fetches annotation.zip (39 MB) and audio_mono-pickup_mix.zip (683 MB), and
with --hex the hexaphonic debleeded stems (3.6 GB), then unzips them into
<dir>/annotation, <dir>/audio_mono-pickup_mix, <dir>/audio_hex-pickup_debleeded.
Resumes an interrupted download. The scripts under test/ take <dir> as their
GuitarSet path.
"""
import json, os, sys, urllib.request, zipfile

RECORD = 'https://zenodo.org/api/records/3371780'
WANT = ['annotation.zip', 'audio_mono-pickup_mix.zip']
if '--hex' in sys.argv: WANT.append('audio_hex-pickup_debleeded.zip')
args = [a for a in sys.argv[1:] if not a.startswith('--')]
if not args: sys.exit(__doc__)
out = args[0]
os.makedirs(out, exist_ok=True)

req = urllib.request.Request(RECORD, headers={'User-Agent': 'jamrack-fetch'})
rec = json.load(urllib.request.urlopen(req, timeout=60))
files = {f['key']: f for f in rec['files']}
for name in WANT:
    f = files[name]
    path = os.path.join(out, name)
    size = f['size']
    have = os.path.getsize(path) if os.path.exists(path) else 0
    if have < size:
        print(f'{name}: {size / 1e6:.0f} MB', '(resuming)' if have else '')
        r = urllib.request.Request(f['links']['self'], headers={'User-Agent': 'jamrack-fetch', 'Range': f'bytes={have}-'})
        with urllib.request.urlopen(r, timeout=120) as src, open(path, 'ab') as dst:
            while True:
                chunk = src.read(1 << 20)
                if not chunk: break
                dst.write(chunk)
                have += len(chunk)
                print(f'\r  {have / 1e6:.0f} / {size / 1e6:.0f} MB', end='', flush=True)
        print()
    target = os.path.join(out, name[:-4])
    if not os.path.isdir(target):
        print(f'unzipping {name} …')
        with zipfile.ZipFile(path) as z: z.extractall(target)
print('done:', out)
