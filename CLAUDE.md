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
- Tests : `node --test test/guitar-tracker.test.mjs test/looper-core.test.mjs
  test/poly-engine.test.mjs test/poly-calibrate.test.mjs` (43 tests, ~30 s,
  Node 20+).
- Le propriétaire (benasterisk) ne code pas lui-même : il dirige, teste à la
  guitare et décide du périmètre. Répondre **en français**, sans jargon
  inutile, avec des chiffres mesurés plutôt que des promesses.

## Branches

- `main` : site public jamrack.openmindlab.fr. Contient tout le travail
  guitare/looper (PR #1, `52b6cc8`) **et POLY bêta + calibration + tampon
  audio (PR #2, `0c10765`, 5 octobre 2026)**. Les fusions vers `main`
  passent par une PR.
- `feature/poly` : branche de travail POLY, fusionnée dans `main` par la
  PR #2 ; la suite du travail POLY continue ici (`git push -u origin
  feature/poly`) et repasse par une PR.
- `feature/guitar-to-midi` : fusionnée dans `main` par la PR #1 ; ne plus y
  commiter.
- `gh-pages` : page de test HTTPS https://benasterisk.github.io/jamrack/
  (micro autorisé, contrairement aux artefacts). Synchronisée avec `main` au
  commit `52b6cc8` (pas encore avec la PR #2). Rafraîchir : `git checkout gh-pages && git merge main &&
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

## Étude plugin DAW (7 octobre 2026)

- Question du propriétaire : faire de GUITARE → MIDI un plugin pour les DAW
  (Ableton Live d'abord) qui ressort du MIDI vers les pistes ou une sortie
  MIDI. Rapport sourcé : `docs/daw-plugin-feasibility.md` (notes de
  recherche dans `docs/daw-plugin-research/`). Conclusions : faisable (1 634
  lignes de DSP pur à porter en C++) ; ça existe déjà (Jam Origin MIDI
  Guitar, fermé, 149,95 USD, sans mesure publiée) ; recommandation **VST3
  « effet audio avec sortie MIDI »** (pas un instrument : il faut l'entrée
  audio de la piste guitare), **JUCE** licence Starter + sources MIT,
  **Windows + Live 12** d'abord (routage *MIDI From* → piste guitare →
  plugin ; AU ignoré par Live pour le MIDI), CLAP en sous-produit, AU puis
  autonome ensuite, pas d'AAX ; gain de latence honnête 8-15 ms (tampons
  navigateur), la détection 9-36 ms reste ; effort 10-14 sessions + 6-8
  soirées d'essai, 0 EUR pour la bêta Windows. **Aucun code écrit** ;
  cinq décisions du propriétaire en §7 du rapport, puis jalon M0 (prototype
  jetable qui mesure routage, bend et latence dans Live 12.4).
- **Décisions prises le 7 octobre : VST3 seulement, Windows seulement, tout
  gratuit (JUCE Starter, VS Community, pluginval, REAPER évaluation), pas de
  Mac, dossier `plugin/`, branche `feature/plugin`.** Plan d'exécution jalon
  par jalon (M0 prototype de mesure → M1 port MONO + oracle → M2 coque VST3
  MONO → M3 POLY bêta) : `docs/plugin-plan.md`, section 6 pour le message à
  coller dans la session locale. Le JS reste le moteur canonique ; le C++
  lui est tenu par l'oracle note pour note (section 4 du plan).

## Plugin VST3 — état exact

- **Jalon M0 fait (7 octobre 2026, branche `feature/plugin`)** : deux
  prototypes **jetables** dans `plugin/m0/` (aucun DSP de guitare : un
  détecteur d'attaque joue un do 60 tenu 600 ms avec un bend d'un ton aller-
  retour entre 100 et 400 ms) pour mesurer Live 12 : **JAMRACK GTM Fx**
  (`Gtm0`, effet, audio passe-tout) et **JAMRACK GTM Inst** (`Gtm9`,
  instrument dont la seule entrée est un bus *side-chain* auxiliaire,
  `getPluginHasMainInput() = false`). `plugin/CMakeLists.txt` : JUCE 8.0.15
  par FetchContent (jamais copié), runtime MSVC statique, `/fp:precise`,
  version + hash git court (rafraîchi à chaque commit) dans l'éditeur, copie
  dans `D:\VST3` (`JAMRACK_COPY_AFTER_BUILD`). pluginval 1.0.4 niveau 5 :
  SUCCESS sur les deux. CI `.github/workflows/plugin.yml` (Windows : build +
  pluginval + artefact). `plugin/README.md` : check-list de la soirée M0,
  modèle de compte rendu, retour arrière, avis de licence.
- **Outillage local (installé le 7 octobre)** : CMake 4.4.4 (winget, portée
  utilisateur, sur le PATH utilisateur ; une fenêtre ouverte avant
  l'installation ne le voit pas), MSVC des **Build Tools 2022** (la
  Community 2022 n'a pas la charge C++) → générateur **« Visual Studio 17
  2022 »**, pluginval 1.0.4 dans `D:\tools\pluginval\`, dossiers `D:\VST3` et
  `D:\VST3-archive`. Build : `cmake -S plugin -B plugin/build -G "Visual
  Studio 17 2022" -A x64` puis `cmake --build plugin/build --config Release`
  (Live et REAPER **fermés**). Live 12 non trouvé aux emplacements standard
  de `C:` lors de la vérification.
- **Suite** : soirée M0 du propriétaire (check-list du README), compte rendu,
  puis M1 (port MONO + oracle). Le code `plugin/m0/` est jeté en M2 (garder
  le CMake et la CI) ; supprimer les deux `.vst3` M0 de `D:\VST3` avant
  d'installer M2.

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
- **Fait le 5 octobre (jalons 1 et 2, première partie), détail et chiffres
  dans `docs/poly-implementation.md`** : moteur POLY JS dans
  `js/audio/guitar/poly/` (profil mesuré, FFT, rééchantillonneur identique à
  scipy, banque fitatt rendue dans le navigateur, β-NMF, règle de notes,
  `PolyTracker` dans `engine.js`). Équivalence prouvée sur les **six jeux** du banc
  (solo, comp, mix2, mix3, hex2, hex3 ; 13 624 notes) : le décomposeur dense
  donne **13 624 notes sur 13 624 identiques** au prototype Python (onsets,
  fins, vélocités, mêmes scores) ; la porte §12 reste non franchie, comme
  pour le prototype (doubles 57,9 %, triades 45,2 %, précision 78,2 %). Coût : le dense et même le clairsemé 1e-4 / 15 it. ont
  saturé le fil audio en usage réel (2,65-4,05 ms par hop de 2,67 ms, synthé
  saccadé, constaté par le propriétaire le 5 octobre au matin). **Livré :
  clairsemé τ = 1e-3, 8 itérations** (`DECOMPOSER`, 1,1 ms Node / 1,62 ms
  worklet = 61 %, −0,1 pt F1 solo, −1,6 pt triades sur le banc) + **mode ÉCO
  automatique** (5 it. / actif 40 quand le hop dépasse 80 % du budget une
  seconde, retour sous 45 %, tag ÉCO sur la carte ; coût 0,39 ms, −1 pt F1 solo,
  −5 pt triades : secours seulement). POLY ne réagit qu'aux
  attaques pincées (une voix ne déclenche rien : normal, documenté). `state.guitar.mode` ('mono' par défaut |
  'poly'), sélecteur MONO / POLY β sur la carte, worklet qui bascule de
  moteur (banque rendue sur le fil principal), CPU et nombre de voix
  affichés, manuel et i18n 12 langues. Outils : `test/poly-dump-events.mjs`
  (moteur JS sur GuitarSet, format dump-events, coût par hop),
  `test/poly-engine.test.mjs` (8 tests ; 39 au total avec les deux autres
  fichiers), `test/poly/export-profile.py` (régénère `profile.js`).
- **Jalon 3 fait (5-6 octobre)** : assistant de calibration
  (`js/audio/guitar/poly/calibrate.js` + `js/ui/calibration.js`), profils
  IndexedDB (`profiles.js`, export/import), menu PROFIL et bouton CALIBRER
  sur la carte, `state.guitar.profileId`, relais audio et échange de banque
  dans le worklet. Non testé avec un vrai micro (session automatisée).
- **Essai du propriétaire, 5 octobre (guitare dans une BOSS Gigcaster 5)** :
  après coupure de la boucle USB (voir `docs/guitar-to-midi.md`, « Interfaces
  de streaming »), MONO OK et **POLY « mieux qu'espéré »**. Les symptômes du
  matin (précision nulle, notes qui tournent toutes seules, même sur le site
  public) venaient de la Gigcaster qui renvoyait la sortie du PC dans
  l'entrée, pas du code. Craquements réglés par le tampon (TAMPON sur la
  carte + panneau « GCS-5 Driver Settings »). Calibration pas encore essayée
  avec la guitare.
- **Pas fait** : bend par note (le prototype ne donne pas de hauteur continue
  par voix, POLY n'émet aucun bend), PR vers `main` (décision du
  propriétaire : la bêta est jugée utilisable).
- **Décision prise le 5 octobre (plan §14) : option 2.** POLY « bêta »
  derrière le sélecteur, MONO par défaut, « notes seules mieux que MONO,
  doubles et triades au mieux ». Branche `feature/poly`, PR vers `main`
  quand la bêta est utilisable.
- Données locales du propriétaire : GuitarSet complet (hex compris) dans
  `D:\guitarset` ; `mir_eval` installé.

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
