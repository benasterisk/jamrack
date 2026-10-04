# JAMRACK — mémoire de projet pour Claude Code

Lu automatiquement à chaque session (cloud ou locale, par exemple `D:\Jamrack`).
Tenir ce fichier à jour à chaque jalon ; c'est la seule mémoire qui survit
aux changements de session. Détails : `docs/local-setup.md`.

## Le projet et son propriétaire

- Rack de synthés dans le navigateur : HTML/CSS/JS + Web Audio, **aucun
  build, zéro dépendance**, hébergement statique. `index.html`, `js/`,
  `css/style.css`, `sfz/`, serveur de dev `node serve.mjs 4173`.
- Interface en **12 langues** : `js/i18n/strings.js`. Toute chaîne nouvelle
  va dans les 12 langues ; le manuel intégré est dans `js/ui/dialogs.js`.
- Cartes du rack : `js/ui/rack.js`. État persisté : `js/state.js`
  (localStorage ; IndexedDB pour les samples). Routage des notes :
  `routeNoteOn / routeNoteOff / routeBend` dans `js/main.js` (bend global
  seulement, pour l'instant). Moteur : `js/audio/engine.js` (bus `dry` →
  master ; `latencyHint: 0` sur desktop).
- Tests : `node --test test/guitar-tracker.test.mjs test/looper-core.test.mjs`
  (31 tests, ~16 s, Node 20+).
- Le propriétaire (benasterisk) ne code pas lui-même : il dirige, teste à la
  guitare et décide du périmètre. Répondre **en français**, sans jargon
  inutile, avec des chiffres mesurés plutôt que des promesses.

## Branches

- `main` : site public jamrack.openmindlab.fr. Contient **tout** le travail
  guitare/looper depuis la fusion de la PR #1 (`52b6cc8`, 5 octobre 2026).
  Les fusions vers `main` passent par une PR.
- `feature/poly` : branche de travail de l'option 2 (moteur POLY temps réel).
  Toujours commiter et pousser ici (`git push -u origin feature/poly`).
- `feature/guitar-to-midi` : fusionnée dans `main` par la PR #1 ; ne plus y
  commiter.
- `gh-pages` : page de test HTTPS https://benasterisk.github.io/jamrack/
  (micro autorisé, contrairement aux artefacts). Synchronisée avec `main` au
  commit `52b6cc8`. Rafraîchir : `git checkout gh-pages && git merge main &&
  git push && git checkout -`.

## Livré (sur `main`)

- **GUITARE → MIDI monophonique temps réel** : `js/audio/guitar/tracker.js`
  (DSP pur, constantes réglables dans `TUNING`), `worklet.js` (AudioWorklet
  `jamrack-guitar-tracker`), `js/input/guitar.js` (capture brute sans
  EC/NS/AGC ; refcount iOS `captureOpened/captureClosed` dans `js/util.js`),
  carte `buildGuitar()` et câblage dans `js/main.js`. Mesuré sur GuitarSet
  (solo) : précision ≈ 72 %, rappel ≈ 84 %. La latence est bornée par la
  physique (1-2 périodes de la note) plus les tampons du navigateur ; un
  pont natif ne gagnerait que sur les tampons (`docs/guitar-to-midi.md`).
- **LOOPER 6 pistes** : `js/audio/looper/core.js` (LooperCore, testé),
  `worklet.js` (`jamrack-looper`), `index.js` (`createLooper`), carte
  `buildLooper()`. Sources rack / module / entrée audio, SYNC sur le BPM du
  METRO, 5 min par piste (1 min sur téléphone), undo incrémental, reverse,
  demi-vitesse, mixeur et envois reverb/delay. Validé par le propriétaire
  (`docs/looper.md`).
- Captures : `docs/screenshots/`. README à jour. Annonce LinkedIn (anglais,
  non technique, sans promesse de polyphonie) :
  https://claude.ai/artifact/SoYz1iuPrbftoJbeS4rNbf

## POLY (polyphonie) — état exact

- **Décisions du propriétaire** (plan §0) : sélecteur **MONO / POLY** sur la
  carte guitare, persisté dans `state.guitar.mode` ; POLY doit marcher pour
  **n'importe quel visiteur sans calibration** (banque générique par
  défaut) ; **assistant de calibration** optionnel mais profil **conservé
  par défaut** (IndexedDB, profils nommés, export/import) ; note-off suit la
  décroissance de la corde (DECAY) ; bend par note à ajouter
  (`routeNoteBend`, instance/sfz).
- **Prototype hors ligne** (Python) dans `test/poly/` (README anglais
  détaillé, runner `run.sh`), configuration retenue
  `test/poly/params-merged.json`. Verdict vérifié par un agent adversarial
  (`docs/polyphonic-plan.md` §13) : notes seules **meilleures que MONO**
  (F1 81 % contre 74 %), fantômes ≤ 1 %, deux lignes superposées 79 %, mais
  la porte auto-imposée **échoue** : doubles 55,8 % (seuil 70), triades
  53,5 % (seuil 60), précision 81,8 % (réussi). Limite identifiée : la règle
  de note par hop (`test/poly/notes.py`), pas le décomposeur (99 % des notes
  de doubles reçoivent une activation suffisante).
- **Rien de POLY n'est dans l'application** (`js/`) : ni sélecteur, ni
  moteur JS, ni assistant.
- **Décision prise le 5 octobre (plan §14) : option 2.** Construire le
  moteur POLY temps réel maintenant avec un périmètre honnête : POLY
  « bêta » derrière le sélecteur, MONO par défaut, « notes seules mieux que
  MONO, doubles et triades au mieux ». Travail sur la branche `feature/poly`,
  PR vers `main` quand la bêta est utilisable.
- Jalons (plan §14) : 1. portage JS de la configuration fusionnée dans le
  worklet (`fft.js`, `templates.js`, NMF β 0,5, 60 gabarits actifs,
  15 itérations, λ 400 ; coût numpy 1,17 ms par hop, **à mesurer en JS**,
  budget 2,67 ms) avec équivalence Python ↔ JS ; 2. intégration
  (`state.guitar.mode` + sélecteur, bend par note, CPU/TRK, i18n) ;
  3. assistant de calibration ; 4. vérification puis PR.

## Conventions et pièges

- Pas de dépendance npm dans `js/`. Python (numpy, scipy, soundfile,
  mir_eval) uniquement sous `test/`.
- Messages de commit par heredoc (`git commit -F - <<'EOF'`) : les backticks
  dans `-m` sont exécutés par le shell.
- Un hook de fin de tour exige que toute modification soit **commitée et
  poussée** : commiter des points d'étape après `node --check`,
  `python3 -m py_compile` ou les tests.
- Ne jamais affirmer de polyphonie dans une communication tant que POLY
  n'est pas livré (« une note à la fois »).
- Orchestration multi-agents (mot-clé `ultracode`) : au plus
  `min(16, cœurs − 2)` agents en parallèle. Données GuitarSet non
  versionnées : `python3 test/fetch-guitarset.py <dir> --hex` (≈ 4,3 Go).
  IDMT-SMT-Guitar : évaluation seule (CC BY-NC-ND).
- Sur iOS : session audio `play-and-record`, le micro ne marche que sur une
  vraie page HTTPS (gh-pages), pas dans un artefact.
