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
  test/poly-engine.test.mjs test/poly-calibrate.test.mjs` (44 tests, ~30 s,
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
  détecteur d'attaque joue un do 60 tenu 600 ms avec un bend d'un demi-ton
  aller-retour entre 100 et 400 ms) pour mesurer Live 12 : **JAMRACK GTM Fx**
  (`Gtm0`, effet, audio passe-tout) et **JAMRACK GTM Inst** (`Gtm9`,
  instrument dont la seule entrée est un bus *side-chain* auxiliaire,
  `getPluginHasMainInput() = false`). `plugin/CMakeLists.txt` : JUCE 8.0.15
  par FetchContent (jamais copié), runtime MSVC statique, `/fp:precise`,
  version + hash git court (calculé à chaque build par `plugin/cmake/git_hash.cmake`, « + » = modifications non commitées) dans l'éditeur, copie
  dans `D:\VST3` (`JAMRACK_COPY_AFTER_BUILD`). pluginval 1.0.4 niveau 5 :
  SUCCESS sur les deux. Revue adversariale (5 angles, 3 sceptiques par constat) : 13 constats confirmés, tous corrigés. CI `.github/workflows/plugin.yml` (Windows : build +
  pluginval + artefact). `plugin/README.md` : check-list de la soirée M0,
  modèle de compte rendu, retour arrière, avis de licence.
- **Outillage local (installé le 7 octobre)** : CMake 4.4.4 (winget, portée
  utilisateur, sur le PATH utilisateur ; une fenêtre ouverte avant
  l'installation ne le voit pas), MSVC des **Build Tools 2022** (la
  Community 2022 n'a pas la charge C++) → générateur **« Visual Studio 17
  2022 »**, pluginval 1.0.4 dans `D:\tools\pluginval\`, dossiers `D:\VST3` et
  `D:\VST3-archive`. Build : `cmake -S plugin -B plugin/build -G "Visual
  Studio 17 2022" -A x64` puis `cmake --build plugin/build --config Release`
  (Live et REAPER **fermés**). Live 12.4.6 **Trial** installé depuis
  (`C:\ProgramData\Ableton\Live 12 Trial`, bibliothèque utilisateur
  `C:\Users\benas\OneDrive\Documents\Ableton\User Library`) ; le 8 octobre
  sa base de plugins était vide (dossier VST3 `D:\VST3` pas encore déclaré).
  Validateur officiel Steinberg : SDK VST3 3.8.1 cloné et compilé dans
  `D:\tools\vst3sdk` (`build\bin\Release\validator.exe <bundle>`).
- **Contre-épreuve REAPER faite par le propriétaire le 7 octobre** (PC
  portable, micro intégré, WASAPI) : do, bend et enregistrement MIDI OK.
- **Live 12.4.6 Trial, 8 octobre : test 1 réussi** (propriétaire) : l'**effet**
  JAMRACK GTM Fx est accepté dans *MIDI From* et le synthé joue → M2 garde la
  forme « effet audio avec sortie MIDI ». Bend entendu (micro et casque du
  PC). Test 2 : Live a refusé l'Inst (« no valid event input bus » :
  Live exige une entrée MIDI sur tout instrument VST3, pluginval et le
  validateur Steinberg non) → `NEEDS_MIDI_INPUT` pour l'Inst (d33d256), à
  réessayer (section Sidechain de l'appareil à allumer, Mix 100 %).
- **Décision du propriétaire, 8 octobre : on reste sur l'EFFET (Fx).** Test 2
  (Inst, 3 pistes, « usine à gaz ») abandonné ; pas d'hôte de synthé dans le
  plugin pour l'instant (option possible après M2 : seulement des VST tiers,
  pas les instruments de Live). Dans Live, 2 pistes est le minimum (une piste
  audio ne contient pas d'instrument et Live ne passe pas le MIDI d'un plugin
  à l'appareil suivant). Restent pour M0 : armement, latence.
- **Nom choisi le 8 octobre : MidPluck** (recherche web rapide : aucun
  produit de ce nom ; pas de vérification de marque). Identité prévue :
  `PRODUCT_NAME "MidPluck"`, `COMPANY_NAME "OpenMindLab"`, codes `Omlb` /
  `Mdpk`, version 0.1.0, effet (Fx). Le propriétaire a demandé MONO **et**
  POLY ensemble : port C++ des deux moteurs (`plugin/dsp/`, contrats
  `events.h`, `jsmath.h`, `tools/dump_common.h`), oracle note pour note
  (`test/render-plucks.mjs`, `test/diff-events.mjs`, dumps JS dans
  `D:\poly-out`) et coque VST3 (`plugin/src/`) lancés en parallèle (workflow
  `midpluck-mono-poly`).
- **Nuit du 7 au 8 octobre (session autonome ; Live non pilotable, l'accès
  au bureau demande un clic)** : hôte VST3 de test en ligne de commande
  `plugin/tools/vst3_probe.cpp` (cible `jamrack_vst3_probe` ; options bypass
  et `--reprepare-at`) + `plugin/m0/check_probe.py` (**62/62**, détail dans
  le README) ; deux revues adversariales (63 puis 6 agents). Corrigé :
  détecteur M0 devenu un détecteur de **montée** (énergie 5 ms > −45 dBFS et
  > 2 × énergie 30 ms, réarmement sous 1,25 ×, enveloppes conservées au
  reset/bypass, garde NaN) ; l'ancien (−30 dBFS, réarmement sous −40)
  bloquait une corde qui sonne (1 note sur 10) et devenait muet sous −24 dBFS
  de crête. GuitarSet (`plugin/m0/sim_detector.py`) : 72/73/71/61 %
  d'attaques attrapées à −6/−12/−18/−24 dBFS contre 39/37/13/0 %. Programme
  par défaut nommé (validateur Steinberg 47/47). Check-list : étape
  « niveau » et diagnostic par le compteur « notes sent ».
- **CI jamais exécutée** : Actions activé sur le dépôt, YAML valide, mais
  GitHub ne crée aucune exécution (pas de suite Actions sur la PR #6, et les
  suites `github-pages` restent « queued ») : blocage probable côté compte,
  à regarder par le propriétaire sur github.com/benasterisk/jamrack/actions.
- **MidPluck 0.1.0 fait (nuit du 8 au 9 octobre, jalons M1 + M2 + M3 réunis,
  build `be2b6b7` installé dans `D:\VST3\MidPluck.vst3`)** : moteurs C++ dans
  `plugin/dsp/` (MONO `mono_tracker`, POLY `poly/*` avec `PolyEngineState`),
  **identiques au JS au bit près** (fonctions de V8 reprises dans
  `poly/v8math.h` : sin, cos, log, log10, hypot, plus log2 dans
  `mono_tracker.cpp`) : MONO GuitarSet 3 361 / 3 361 notes, POLY livré
  4 141 / 4 141, dense 4 174 / 4 174, corpus synthétique 673 / 673, écart
  `score.py` 0,0 point. Outils : `tools/dump_events` (même JSON que les dumps
  JS), `test/render-plucks.mjs` (112 signaux, ctest `oracle_synthetic`),
  `test/diff-events.mjs`, `tools/gen-profile-header.mjs --check`. Coque
  `plugin/src/` : réglages du plan 3.1 avec MODE (MONO / POLY bêta) et BYPASS,
  OCTAVE/TRANSPOSE/MIDI CH à crans (`SteppedInt`), flushs du plan 3.2, état
  POLY construit sur un fil de fond, entrée NaN/Inf remplacée par 0 avant les
  moteurs. pluginval 5 SUCCESS, Steinberg 47/47, rappel d'état OK, sortie du
  plugin = sortie des moteurs (vst3_probe étendu : `--param`, `--param-at`,
  `--state-test`, `--warmup-ms`). Coût : MONO 1,6 % d'un cœur, POLY 0,22 ms
  par pas (budget 2,67 ms) : ECO éteint à 44,1/48 kHz. Prototypes M0 rangés
  dans `D:\VST3-archive\m0`, plus construits (sources gardées). Écart voulu :
  le passage POLY → MONO remet MONO à zéro dans le plugin, pas dans
  `worklet.js` (décision du propriétaire à prendre).
- **Interface au style JAMRACK (9 octobre, build `7ac59fb` installé ; l'ancien
  `be2b6b7` est dans `D:\VST3-archive`)** : demandée par le propriétaire
  (« design pas beau »). `plugin/src/ui/` (LookAndFeel, widgets, écran
  TUNER/VOICES), façade 960 x 240 (4:1, 100-200 %) : vis de rack, LCD ambre
  avec des états en mots simples, potards ambre lumineux, sélecteur
  MONO | POLY β, BYPASS, voyant NOTE OUT, ↺ par section, réglages MONO seuls
  grisés en POLY ; polices OFL embarquées (`plugin/resources/fonts/`, licences
  copiées dans le bundle). Textes côté hôte : GAIN en dB, SENS/DECAY/DYN 0-100.
  Molette désactivée sur MODE et MIDI CH (choix réversible). Outil
  `tools/ui_snapshot.cpp` (cible `midpluck_ui_snapshot`) : 22 captures hors
  écran et 107 vérifications. pluginval 5 avec tests d'interface SUCCESS,
  Steinberg 47/47, MIDI identique à la version précédente. Fenêtre pas encore
  ouverte dans un vrai DAW par la session.
- **Suite** : soirée MidPluck du propriétaire (check-list de
  `plugin/README.md` : MONO, latence au téléphone 64/128/256 contre la page
  web, POLY et tag ECO, décalage d'enregistrement, armement), compte rendu,
  puis PR vers `main` et Release GitHub `plugin-v0.1.0` (la CI n'a encore
  jamais tourné côté GitHub).

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
