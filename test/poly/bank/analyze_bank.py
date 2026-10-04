#!/usr/bin/env python3
"""Runs test/poly/analyze.py unchanged with a template bank from
make_bank.py instead of the synthetic bank of templates.py.

  python3 test/poly/bank/analyze_bank.py --bank bank-avg.npz --guitarset <mixdir> --takes <takes.json> --set ... --cache <dir> \
        --iter 15 --lambda 400 --windows medium,short [--jobs 4]

templates.build(win) is replaced, before the worker pool forks, by a loader
returning (W_<win>, meta, window) from the bank; everything else (front-end,
NMF, cache format, settings check) is analyze.py's. The bank path and its
settings are written to <cache>/bank.json and a cache made with another
bank is refused.

--skip-short-nmf: the NMF of the short window returns zeros (P_short,
noise_short = 0; the short spectrum is still computed for the onset flux).
notes.py reads P_medium only (win = off_win = medium, never varied by the
grids), so the events are unchanged and the analysis costs half; the flag is
recorded in bank.json and the caches are not interchangeable with full ones.
"""
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import templates as T  # noqa: E402
import analyze  # noqa: E402


def main():
    argv = sys.argv[1:]
    if '--bank' not in argv:
        raise SystemExit('--bank is required')
    i = argv.index('--bank')
    bank_path = os.path.abspath(argv[i + 1])
    del argv[i:i + 2]
    z = np.load(bank_path)
    meta = json.loads(str(z['meta']))
    settings = json.loads(str(z['settings']))
    banks = {w: z['W_' + w] for w in T.WINDOWS if 'W_' + w in z.files}

    def build(win_name='medium', shape='hann'):
        if win_name not in banks:
            raise SystemExit(f'{bank_path} has no W_{win_name}')
        return banks[win_name], meta, T.window(T.WINDOWS[win_name], shape)

    T.build = build
    analyze.T.build = build
    skip_short = '--skip-short-nmf' in argv
    if skip_short:
        argv.remove('--skip-short-nmf')
        real_nmf = analyze.nmf_run

        def nmf_run(V, W, meta, n_iter=analyze.N_ITER, lam=analyze.LAMBDA):
            if W is banks.get('short'):
                return np.zeros((V.shape[0], W.shape[1]), dtype=np.float32), 0.0
            return real_nmf(V, W, meta, n_iter, lam)

        analyze.nmf_run = nmf_run
    cache = argv[argv.index('--cache') + 1]
    os.makedirs(cache, exist_ok=True)
    marker = os.path.join(cache, 'bank.json')
    info = {'bank': bank_path, 'settings': settings, 'columns': int(banks['medium'].shape[1]), 'skip_short_nmf': skip_short}
    if os.path.exists(marker):
        old = json.load(open(marker))
        if old != info:
            raise SystemExit(f'{marker} holds {old}, not {info}: use another --cache')
    else:
        json.dump(info, open(marker, 'w'), indent=1)
    print(f'bank: {bank_path} ({info["columns"]} columns, {settings})', flush=True)
    sys.argv = [sys.argv[0]] + argv
    analyze.main()


if __name__ == '__main__':
    main()
