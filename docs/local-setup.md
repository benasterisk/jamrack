# Reprendre le travail en local (PC avec plus de cœurs, guitare branchée)

Tout ce qui compte est dans le dépôt : le code, les tests, les documents de
conception (`docs/guitar-to-midi.md`, `docs/looper.md`,
`docs/polyphonic-plan.md` avec ses sections 11 à 13 de résultats), les
outils de mesure (`test/`) et la mémoire de projet `CLAUDE.md` à la racine,
que Claude Code lit automatiquement au démarrage d'une session. Seules les
données GuitarSet et les caches de calcul ne sont pas versionnés ; le script
ci-dessous les retélécharge.

## Pourquoi en local

- L'orchestrateur d'agents lance au plus `cœurs − 2` agents en parallèle
  (plafond 16). Le conteneur cloud en a 4, donc 2 agents à la fois ; un PC à
  8-16 cœurs en fait tourner 6 à 14.
- La guitare et l'interface sont branchées : les essais se font sur du vrai
  signal, et la carte GUITARE → MIDI affiche vos vraies latences IN / OUT.
- Pas de proxy : les polices, le CDN des banques de sons et les jeux de
  données passent sans détour.

## Installation

Le dossier local attendu est `D:\Jamrack` (ou n'importe quel clone). Deux
façons de travailler sous Windows :

**Windows natif (le plus simple).** Node 20+ (22 testé) et Python 3.10+
(3.11 testé) depuis nodejs.org et python.org, plus Git for Windows. Les tests
Node et les scripts Python tournent tels quels ; seuls les lanceurs
`test/poly/run.sh` et `test/poly/bank/run.sh` sont en bash : les exécuter
depuis Git Bash, ou lancer à la main les commandes Python qu'ils contiennent
(`test/poly/README.md` les détaille). Dans PowerShell, `export X=1` devient
`$env:X = 1`.

**WSL2 Ubuntu.** Tout marche comme sous Linux, mais cloner le dépôt et
poser GuitarSet **dans le système de fichiers WSL** (`~/jamrack`,
`~/guitarset`), pas sous `/mnt/d` : les caches d'analyse y sont 5 à 10 fois
plus lents.

```bash
cd D:/Jamrack            # ou : git clone https://github.com/benasterisk/jamrack.git
git fetch origin && git checkout feature/poly && git pull
node --version && python3 --version
pip3 install --user numpy scipy soundfile mir_eval
# optionnel, pour la ligne de référence hors ligne (TensorFlow/ONNX, ~1 Go) :
pip3 install --user basic-pitch
# données (≈ 720 Mo ; avec --hex ≈ 4,3 Go, nécessaire pour les remix d'accords) :
python3 test/fetch-guitarset.py D:/guitarset --hex
```

## Vérifier que tout marche

```bash
node --test test/guitar-tracker.test.mjs test/looper-core.test.mjs   # 31 tests
node serve.mjs 4173     # puis http://localhost:4173 dans Chrome, guitare branchée
node test/guitarset-eval.mjs D:/guitarset 24 solo                       # mono sur vraie guitare
node test/dump-events.mjs --guitarset D:/guitarset --set solo --out mono-solo.json
python3 test/score.py --guitarset D:/guitarset mono-solo.json         # tableau mir_eval
```

## Reprendre le plan polyphonique

`CLAUDE.md` résume l'état ; `docs/polyphonic-plan.md` section 13 donne les
chiffres vérifiés et le diagnostic, section 14 la décision du propriétaire
(option 2 : moteur temps réel à périmètre honnête, branche `feature/poly`)
et ses quatre jalons ; `test/poly/README.md` explique comment rejouer la
configuration retenue (`test/poly/params-merged.json`) et la porte
(`test/poly/gate.py`). Dans une session Claude Code locale, le mot-clé
`ultracode` active l'orchestration multi-agents ; demander simplement
« reprends le jalon suivant de docs/polyphonic-plan.md §14 ».

## Ce qui ne se transfère pas

La conversation de la session cloud. Les décisions et les chiffres qu'elle a
produits sont tous dans `CLAUDE.md`, les documents ci-dessus et l'historique
git (`git log --oneline`), qui suffisent comme contexte à une nouvelle
session.
