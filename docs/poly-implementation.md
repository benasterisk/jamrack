# POLY dans le navigateur — implémentation et mesures (jalons 1 et 2 du plan §14)

Document de travail de la branche `feature/poly`. Il décrit ce qui a été
construit à partir du prototype Python (`test/poly/`, configuration fusionnée
`params-merged.json`), comment c'est vérifié, et ce qui reste. Les chiffres
sont mesurés, pas déduits ; chaque commande qui les produit est donnée.

## 1. Ce qui est dans l'application

`js/audio/guitar/poly/` — moteur POLY, JavaScript pur sur tableaux typés
(aucune dépendance, même code dans l'AudioWorklet, le repli ScriptProcessor et
les tests Node) :

| fichier | rôle | source Python portée |
|---|---|---|
| `profile.js` | loi d'inharmonicité B et profils de partiels par corde, mesurés sur les joueurs DEV de GuitarSet (généré par `test/poly/export-profile.py`) | `test/poly/bank/profile-dev.json` |
| `fft.js` | FFT réelle radix-2 (2048 par hop, 65 536 une fois pour le noyau de fenêtre) | `np.fft.rfft` |
| `resample.js` | rééchantillonnage polyphasé vers 24 kHz, noyau identique à `scipy.signal.resample_poly` (Kaiser β 5, 10 × max(up, down) demi-taps), causal | `resample_poly` |
| `bank.js` | banque « fitatt » : 120 gabarits (6 cordes × 20 frettes) rendus dans le navigateur + 2 gabarits de bruit, colonnes L2 | `bank/make_bank.py` kind `fit`, `templates.py` |
| `nmf.js` | β-NMF (β 0,5), ensemble actif 60, 15 itérations, λ 400, démarrage à chaud ; stockage dense ou clairsemé (CSR) | `decomp/decomp.py` |
| `notes.js` | règle de note par hop, causale, seuils de `params-merged.json` | `notes.py` run_take |
| `engine.js` | `PolyTracker` : front-end (niveau, deux fenêtres Hann 1024 / 512, flux spectral, gardes d'octave), décomposeur, règle, événements | `analyze.py`, `merge.py` |

Intégration (jalon 2, première partie) :

- `state.guitar.mode` : `'mono'` par défaut, `'poly'` (bêta), persisté.
- `js/audio/guitar/worklet.js` héberge les deux moteurs ; le message
  `{ mode, bank }` change de moteur (les notes tenues sont relâchées, un
  événement `mode` le confirme). La banque (500 Ko) est rendue sur le fil
  principal (`js/input/guitar.js`, une fois par page, ~50 ms) et passée au
  worklet : la rendre sur le fil audio bloquerait le son au démarrage.
- Carte GUITARE → MIDI : sélecteur **MONO / POLY β** dans l'en-tête ; en POLY
  les réglages propres à MONO (SENS, DECAY, DYN, BEND) sont grisés, l'accordeur
  laisse place au nombre de voix et au **CPU** (coût d'un hop rapporté au
  budget de 2,67 ms) ; le LCD indique « · POLY β ». Manuel : paragraphe POLY.
  Chaînes dans les 12 langues (`gtrTitleMode`, `gtrCpu`, `helpPolyTitle`,
  `helpPolyText`).

Pas encore fait (plan §14) : bend par note (`routeNoteBend`) — le prototype
ne produit pas de hauteur continue par voix, le moteur n'émet donc aucun
bend en POLY ; assistant de calibration (jalon 3) ; évaluation GuitarSet
via le moteur JS sur les six jeux de mixes, revue adversariale et PR
(jalon 4).

## 2. Équivalence avec le prototype Python

Méthode. Les mêmes prises WAV passent dans les deux chaînes :

```sh
# Python (référence) : analyse fusionnée puis événements, sets solo et comp de test/takes.json
python test/poly/merge.py analyze --config test/poly/params-merged.json --guitarset D:/guitarset \
  --takes test/takes.json --set solo,comp --cache OUT/cache --features OUT/features --jobs 8
python test/poly/merge.py events  --config test/poly/params-merged.json --cache OUT/cache \
  --takes test/takes.json --set solo --out OUT/events-py-solo.json      # idem comp
# JavaScript : le moteur sur les mêmes prises, hops alignés sur le rééchantillonnage de scipy
node test/poly-dump-events.mjs --guitarset D:/guitarset --set solo --out OUT/events-js-solo.json
# comparaison note à note (même midi, onset à ±1 hop) et score mir_eval
python test/score.py --guitarset D:/guitarset --takes test/takes.json OUT/events-py-solo.json OUT/events-js-solo.json
```

Briques (vérifiées contre numpy/scipy, 5 octobre 2026) :

- banque : 122 colonnes, écart maximal **3·10⁻⁸** avec la banque Python
  (float32), ordre des colonnes identique ; construction 53-56 ms ;
- rééchantillonneur : écart maximal **7·10⁻¹⁶** avec `resample_poly` à
  44,1 kHz (80/147, 2941 taps) comme à 48 kHz (1/2, 41 taps) ; retard de
  groupe 0,42 ms dans les deux cas (le noyau vit sur la grille suréchantillonnée) ;
- FFT : égale à une DFT directe à 10⁻⁹.

Événements, décomposeur **dense** (arithmétique du prototype) :

| set | prises | notes Python | notes JS | onsets identiques | fins ≠ | vélocités ≠ | manquantes | en trop |
|---|---|---|---|---|---|---|---|---|
| solo | 24 | 2283 | 2283 | **2283 (100 %)** | 0 | 0 | 0 | 0 |
| comp | 12 | 1585 | 1585 | **1585 (100 %)** | 0 | 0 | 0 | 0 |

Score mir_eval (F1 ±50 ms / précision / rappel / fantômes / octaves) : solo
Python 80,8 / 79,1 / 82,7 / 0,3 % / 0,1 %, comp 47,2 / 70,1 / 35,6 / 1,0 % /
4,7 % ; JS dense **identique** sur les deux sets.

## 3. Coût par hop et décomposeur clairsemé

Budget : un hop d'analyse = 64 échantillons à 24 kHz = **2,667 ms**.

Mesures (PC du propriétaire, 12 cœurs, Node 25 ; `test/poly-dump-events.mjs`
imprime le coût par prise ; numpy mono-fil mesuré par `merge.py time`) :

| décomposeur | ms/hop NMF (Node, machine au repos) | ms/hop dans le worklet Chrome (48 kHz, repos) | équivalence sur les 3868 notes solo + comp |
|---|---|---|---|
| Python numpy (référence) | 0,95-1,05 | — | — |
| JS dense | 1,69-1,88 | **2,60** (hop complet) | 3868 identiques |
| JS clairsemé τ = 1e-5 (645 bins/colonne) | 1,25 | 2,29 | 3867 identiques, 1 à ±1 hop, 1 fin ≠ |
| JS clairsemé τ = 1e-4 (417 bins/colonne) | 0,91 | **1,52** | 3859 identiques, 8 à ±1 hop, 1 manquante, 40 vélocités ≠ (< 1e-3) ; F1 / P / R inchangés |
| JS clairsemé τ = 1e-3 (248 bins/colonne) | 0,63 | — | non retenu : dérive des activations jusqu'à 24 % |

**Réglage retenu pour l'application : clairsemé τ = 1e-4** (`DECOMPOSER.sparse`
dans `engine.js`), 57 % du budget sur ce PC ; le dense coûte le budget entier
dans le worklet et ne laisse rien aux téléphones. Le dense reste disponible
(`DECOMP='{"sparse":0}' node test/poly-dump-events.mjs …`) pour l'équivalence
exacte.

« Clairsemé τ » : seules les cases d'une colonne au-dessus de τ fois son
maximum sont gardées (les lobes secondaires du noyau de Hann décroissent en
1/x³). Mesure worklet : bicorde tenu (sol2 + mi3 en dents de scie) pendant
1,8 s, moyenne des 20 derniers relevés ; sous forte charge (trois bancs
d'essai en parallèle) le dense montait à 4,0 ms.

## 4. Vérifications dans le navigateur

- `addModule` du worklet : 17 ms. Une dent de scie à 220 Hz envoyée dans le
  worklet en mode POLY donne exactement `on 57` / `off 57` (La3) ; le
  message `{ mode: 'mono' }` répond par un événement `mode`.
- Carte : sélecteur, grisage, manuel, persistance vérifiés ; aucune erreur
  console. Le micro n'est pas testable depuis la session automatisée : essai à
  la guitare par le propriétaire à faire (voir §6).

## 5. Tests

`node --test test/guitar-tracker.test.mjs test/looper-core.test.mjs test/poly-engine.test.mjs`
— 39 tests. `test/poly-engine.test.mjs` : FFT contre DFT, noyau du
rééchantillonneur, normes et pics de la banque, décomposeur sur un gabarit
isolé, silence et souffle, pincements synthétiques sur huit cordes (latence
20-36 ms, note-off 43-46 ms après l'étouffement), bicorde, coût (informatif).

## 6. À faire ensuite

1. Essai à la guitare par le propriétaire : POLY sur notes seules, doubles,
   accords ; chiffre CPU affiché ; absence de notes fantômes ; le mode MONO
   doit rester identique à avant.
2. Jalon 3 : assistant de calibration (profil de partiels et B par corde,
   IndexedDB, profils nommés, export/import, « générique » toujours
   disponible) — `bank.js` accepte déjà un profil `{ bLaw, prof }`.
3. Jalon 4 : évaluation des six jeux de mixes via le moteur JS, revue
   adversariale, PR vers `main`.
