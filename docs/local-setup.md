# Reprendre le travail en local (PC avec plus de cœurs, guitare branchée)

Tout ce qui compte est dans le dépôt : le code, les tests, les documents de
conception (`docs/guitar-to-midi.md`, `docs/looper.md`,
`docs/polyphonic-plan.md` avec ses sections 11 et 12 de résultats) et les
outils de mesure (`test/`). Seules les données GuitarSet et les caches de
calcul ne sont pas versionnés ; le script ci-dessous les retélécharge.

## Pourquoi en local

- L'orchestrateur d'agents lance au plus `cœurs − 2` agents en parallèle
  (plafond 16). Le conteneur cloud en a 4, donc 2 agents à la fois ; un PC à
  8-16 cœurs en fait tourner 6 à 14.
- La guitare et l'interface sont branchées : les essais se font sur du vrai
  signal, et la carte GUITARE → MIDI affiche vos vraies latences IN / OUT.
- Pas de proxy : les polices, le CDN des banques de sons et les jeux de
  données passent sans détour.

## Installation (Windows : passer par WSL2 Ubuntu, les scripts sont bash,
## Node et Python ; macOS et Linux : tel quel)

```bash
git clone https://github.com/benasterisk/jamrack.git
cd jamrack
git checkout feature/guitar-to-midi
# Node 20+ (22 ici) et Python 3.10+
node --version && python3 --version
pip3 install --user numpy scipy soundfile mir_eval
# optionnel, pour la ligne de référence hors ligne (TensorFlow/ONNX, ~1 Go) :
pip3 install --user basic-pitch
# données (≈ 720 Mo ; avec --hex ≈ 4,3 Go, nécessaire pour les remix d'accords) :
python3 test/fetch-guitarset.py ~/guitarset --hex
```

## Vérifier que tout marche

```bash
node --test test/guitar-tracker.test.mjs test/looper-core.test.mjs   # 31 tests
node serve.mjs 4173     # puis http://localhost:4173 dans Chrome, guitare branchée
node test/guitarset-eval.mjs ~/guitarset 24 solo                        # mono sur vraie guitare
node test/dump-events.mjs --guitarset ~/guitarset --set solo --out /tmp/mono-solo.json
python3 test/score.py --guitarset ~/guitarset /tmp/mono-solo.json     # tableau mir_eval
```

## Reprendre le plan polyphonique

`docs/polyphonic-plan.md`, section 12, donne l'état exact : le prototype
hors ligne est dans `test/poly/` (lire `test/poly/README.md` quand il
existe, sinon `run.sh`), la porte à passer avant le jalon 3 et les trois
pistes d'amélioration. Dans une session Claude Code locale, le mot-clé
`ultracode` active l'orchestration multi-agents ; demander simplement de
« reprendre le jalon suivant de docs/polyphonic-plan.md ».

## Ce qui ne se transfère pas

La conversation de la session cloud. Les décisions et les chiffres qu'elle a
produits sont tous dans les documents ci-dessus et dans l'historique git
(`git log --oneline`), qui suffit comme contexte à une nouvelle session.
