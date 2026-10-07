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

Assistant de calibration (jalon 3) :

- `js/audio/guitar/poly/calibrate.js` porte l'extracteur Python
  (`test/poly/bank/extract.py`) : détection de l'attaque, spectre d'attaque
  10-40 ms, ajustement de l'inharmonicité B sur un spectre à 0,37 Hz (pics
  paraboliques, trois tours, rejet des partiels à plus de 1 %), profil de
  partiels unitaire, accord en cents ; refus nommés (trop faible, pas de
  partiels, mauvaise corde avec la note entendue). Tests
  `test/poly-calibrate.test.mjs` sur cordes synthétiques : B retrouvé à
  < 12 %, f0 à < 3 cents.
- `js/ui/calibration.js` : bouton **CALIBRER** de la carte (POLY) → une
  corde à vide à la fois (puis, au choix, la 12e frette pour la pente de la
  loi B), niveau en direct, résultat par corde, résumé, profil nommé.
  Profils dans IndexedDB (`profiles.js`), export / import JSON, menu
  **PROFIL** sur la carte (Générique toujours disponible) ; le worklet
  reconstruit son moteur sur la nouvelle banque sans arrêter l'entrée. Le
  signal brut est relayé par le worklet (`{ capture }`, blocs de 2048).

À savoir : POLY n'ouvre une décision que sur une **attaque** (hausse de
niveau de 6 dB en 8-16 ms + flux spectral) : une voix chantée ou une note
frottée ne déclenche rien, par construction (MONO a une détection « swell »
pour cela). Le manuel le dit.

Pas encore fait (plan §14) : bend par note (`routeNoteBend`) — le prototype
ne produit pas de hauteur continue par voix, le moteur n'émet donc aucun
bend en POLY ; essai à la guitare par le propriétaire (calibration comprise :
le navigateur de la session automatisée n'a pas de micro) ; PR.

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

Événements sur les **six jeux** du banc (mixes construits par
`test/poly/make_mixes.py` dans un dossier de travail, `--mixdir`), 13 624 notes :

| set | prises | notes Python | JS dense (arithmétique exacte) | JS clairsemé τ = 1e-4 (livré) |
|---|---|---|---|---|
| solo | 24 | 2283 | **2283 identiques** | 2281 identiques, 2 à ±1 hop |
| comp | 12 | 1585 | **1585 identiques** | 1578 identiques, 6 à ±1 hop, 1 manquante |
| mix2 | 24 | 3418 | **3418 identiques** | 3409 identiques, 9 à ±1 hop |
| mix3 | 12 | 2134 | **2134 identiques** | 2130 identiques, 3 à ±1 hop, 1 manquante, 1 en trop |
| hex2 | 36 | 2332 | **2332 identiques** | 2325 identiques, 7 à ±1 hop |
| hex3 | 24 | 1872 | **1872 identiques** | 1868 identiques, 4 à ±1 hop |

« Identique » = même note, même onset, même fin, même vélocité. Le moteur
dense reproduit donc le prototype à la note près sur l'ensemble du banc ; le
clairsemé livré en diffère sur 2 notes manquantes et 1 en trop sur 13 624,
plus 31 onsets décalés d'un hop (2,67 ms) et quelques vélocités à < 10⁻³.

Scores mir_eval du moteur **livré** (`test/poly/gate.py` sur `score.py --json`,
tous les joueurs ; latence corrigée du retard d'annotation +9,4 ms) :

| set | F1 ±50 | F1 ±20 | P | R | rappel 1 note | 2 notes | 3 notes | 4 notes | fantômes | octave | latence médiane (p25/p75/p90) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| solo | 80,8 | 67,6 | 79,1 | 82,7 | 85,0 | 55,3 | 83,3 | – | 0,3 % | 0,1 % | +23 (20/27/33) ms |
| comp | 47,2 | 21,9 | 70,1 | 35,6 | 40,6 | 42,7 | 42,0 | 29,9 | 1,0 % | 4,7 % | +30 (25/37/45) |
| mix2 | 76,4 | 57,3 | 77,4 | 75,3 | 79,0 | 69,1 | 52,8 | – | 0,3 % | 0,5 % | +25 (22/29/35) |
| mix3 | 71,2 | 49,4 | 75,1 | 67,8 | 73,0 | 65,1 | 50,8 | 45,8 | 0,4 % | 1,1 % | +26 (23/30/36) |
| hex2 | 68,5 | 40,5 | 84,8 | 57,4 | 58,2 | 56,6 | – | – | 0,4 % | 0,8 % | +27 (23/34/41) |
| hex3 | 61,2 | 30,6 | 81,6 | 49,0 | 45,2 | 52,3 | 46,3 | – | 0,6 % | 1,9 % | +29 (24/36/43) |

Les colonnes Python sont identiques à ±0,1 point. La porte de la section 12
du plan (doubles ≥ 70 %, triades ≥ 60 %, précision ≥ 80 % sur les jeux
groupés) reste **non franchie** par le moteur JS comme par le prototype
(57,9 % / 45,2 % / 78,2 %) : c'est le périmètre « au mieux » annoncé en §14,
et la raison pour laquelle POLY est une bêta derrière MONO.

## 3. Coût par hop et décomposeur clairsemé

Budget : un hop d'analyse = 64 échantillons à 24 kHz = **2,667 ms**.

Mesures (PC du propriétaire, 12 cœurs, Node 25 ; `test/poly-dump-events.mjs`
imprime le coût par prise ; numpy mono-fil mesuré par `merge.py time`) :

| décomposeur | ms/hop NMF (Node, machine au repos) | ms/hop dans le worklet Chrome (48 kHz, repos) | équivalence sur les 3868 notes solo + comp |
|---|---|---|---|
| Python numpy (référence) | 0,95-1,05 | — | — |
| JS dense | 1,69-1,88 | **2,60** (hop complet) | 13 624 / 13 624 identiques (six jeux) |
| JS clairsemé τ = 1e-5 (645 bins/colonne) | 1,25 | 2,29 | 3867 / 3868 identiques (solo + comp), 1 à ±1 hop |
| JS clairsemé τ = 1e-4 (417 bins/colonne) | 0,91 (0,96 sur les 24 solo) | **1,52** | 13 591 identiques, 31 à ±1 hop, 2 manquantes, 1 en trop ; scores identiques à ±0,1 pt |
| JS clairsemé τ = 1e-3 (248 bins/colonne) | 0,63 | — | non retenu : dérive des activations jusqu'à 24 % |

**Premier réglage livré : clairsemé τ = 1e-4, 15 itérations** (57 % du
budget au repos). **Insuffisant en usage réel** : le 5 octobre au matin, le
propriétaire (voix dans le micro, synthé qui joue) a vu le CPU dépasser
100 % et le synthé saccader ; remesuré alors dans le worklet : **2,65 à
4,05 ms par hop** (la machine n'était plus au repos, et le synthé, la
réverb et le looper partagent le même fil audio, que le chiffre de la nuit
ne comptait pas). Six variantes moins coûteuses ont donc été scorées sur
les six jeux (même banc, 13 624 notes) :

| variante | ms/hop (Node, même session) | F1 solo | doubles groupés | triades | précision |
|---|---|---|---|---|---|
| τ 1e-4, 15 it. (1er livré) | 2,65 | 80,8 | 57,9 | 45,2 | 78,2 |
| τ 1e-3, 15 it. | 1,86 | 80,8 | 57,8 | 44,9 | 78,2 |
| τ 1e-4, 10 it. | 1,86 | 80,7 | 57,4 | 44,6 | 78,0 |
| τ 1e-4, 8 it. | 1,61 | 80,7 | 57,1 | 43,8 | 77,7 |
| τ 1e-4, 15 it., actif 40 | 2,04 | 81,0 | 57,9 | 43,9 | 77,1 |
| **τ 1e-3, 8 it. (livré)** | **1,10** | 80,7 | 57,0 | 43,6 | 77,8 |
| τ 1e-4, 10 it., actif 40 | 1,47 | 80,6 | 57,2 | 42,5 | 76,3 |
| ÉCO : τ 1e-3, 5 it., actif 40 | 0,39 (0,82 pour le livré, 2,30 pour le 1er livré, machine moins chargée) | 79,7 | 54,2 | 38,3 | 74,3 |

**Réglage retenu : clairsemé τ = 1e-3, 8 itérations** (`DECOMPOSER` dans
`engine.js`) : coût divisé par 2,4 pour −0,1 point de F1 en solo et
−1,6 point sur les triades. Dans le worklet, au même moment : **1,62 ms par
hop (61 %)** là où le réglage précédent coûtait 4,05 ms (152 %).

**Mode ÉCO automatique** (`PolyTracker._loadControl`) : si le coût moyen
d'un hop reste au-dessus de 80 % du budget pendant une seconde, le
décomposeur passe à 5 itérations / ensemble actif 40 (1,0 ms, 38 %) et
revient sous 45 % ; la carte affiche **ÉCO** en ambre à côté du CPU. Mesuré
dans le worklet surchargé : bascule au hop ~390, coût 3,7 → 1,1 ms. Son prix
sur le banc : −1,0 point de F1 en solo, −2,8 sur les doubles, −5,3 sur les
triades, −3,5 de précision par rapport au réglage livré — un mode de
secours, préférable aux décrochages audio, pas un réglage de croisière.

Le dense reste disponible (`DECOMP='{"sparse":0,"iter":15}' node
test/poly-dump-events.mjs …`) pour l'équivalence exacte.

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
  console. Calibration : dialogue, refus propre sans micro, relais du signal
  (blocs de 2048 avec signal), échange de banque en cours de route (sol2
  re-détecté), import d'un profil exporté → sélectionné et persisté,
  fichier étranger refusé, suppression. Vue téléphone (375 px) : aucun
  débordement, commandes à la taille du doigt. Revue de code (effort
  élevé) : 3 constats corrigés. Le micro n'est pas testable depuis la session automatisée : essai à
  la guitare par le propriétaire à faire (voir §6).

## 5. Tests

`node --test test/guitar-tracker.test.mjs test/looper-core.test.mjs test/poly-engine.test.mjs test/poly-calibrate.test.mjs`
— 43 tests. `test/poly-engine.test.mjs` : FFT contre DFT, noyau du
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
