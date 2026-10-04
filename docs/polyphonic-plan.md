# JAMRACK POLY — plan de réalisation du moteur polyphonique

Issu d'une étude orchestrée (six recherches, trois architectures jugées
par trois relecteurs, synthèse, critique de complétude) puis corrigé des
erreurs relevées par la critique. Le mono reste tel quel ; POLY est un
second moteur derrière le sélecteur MONO / POLY de la carte GUITARE → MIDI.

## 1. Réponse courte

Un **second moteur polyphonique** à côté du traqueur MONO (intact : 9-36 ms
mesurés, 16 tests verts, précision 72 % / rappel 84 % sur GuitarSet solo),
bâti sur la famille d'algorithmes vers laquelle convergent le brevet de Jam
Origin (US 2012/0132057 A1), Cont 2006 et Dessein 2010 : des **gabarits
spectraux de votre guitare** (calibration de 1 à 5 minutes, banque générique
en attendant), décomposés toutes les 2,7 ms par une **NMF parcimonieuse à
démarrage chaud**, avec attaques détectées par flux spectral et a priori
guitare (six cordes, au plus une note par corde quand l'attribution est
sûre). D'abord un **banc de mesure** qui note chaque étape (démarre sans
rien de votre part), puis un **prototype hors ligne d'une session** qui
décide le périmètre (accords complets, ou doubles-notes et triades) avant
d'investir les quatre sessions du moteur. Le Rust/WASM n'est qu'un
accélérateur conditionnel ; le modèle neuronal et l'utilitaire natif ASIO
sont optionnels. Résultat honnête : notes seules inchangées quand rien
d'autre ne sonne, accords de 2-3 notes fiables, 4-6 notes partiels,
nommage 15-25 ms sur les cordes aiguës et 30-40 ms sur mi/la graves, plus
les tampons du navigateur. **13 à 16 sessions** pour une bêta POLY crédible.

## 2. Ce que vous devez fournir (liste réduite)

| # | Quoi | Sert à | Bloque |
|---|---|---|---|
| 1 | **Deux décisions** : (a) acceptez-vous une calibration par guitare (1 min rapide, 5 min complète, à refaire quand vous changez de cordes) ? (b) note-off : la note suit la décroissance de la corde (bouton DECAY) ou s'arrête après une durée fixe ? | jalons 3-6 | jalon 3 |
| 2 | **Votre rig** : guitare, type de micro (simple/double bobinage, position), interface USB et pilote, Windows 10 ou 11, navigateurs utilisés, iPhone et mode de branchement | écart de domaine, chemin Windows | jalon 4 |
| 3 | **Relevés sur Chrome/PC** avec la carte GUITARE actuelle : IN / OUT / TRK affichés pendant une note tenue, et une mesure aller-retour « clic → micro » que la page fournira au jalon 2 (les chiffres affichés sous-estiment OUT sur certains Windows) | remplace les chiffres extrapolés | jalon 4 |
| 4 | **Enregistrements (≈ 12 min)** : 20 doubles-notes (octaves, quintes, tierces), accords ouverts E A D G C Em Am Dm et barrés F Bm B7, chacun gratté lent, moyen, rapide puis arpégé, un accord avec une corde étouffée, un accord avec bend et vibrato sur la chanterelle seule, 5 minutes de jeu libre. WAV 48 kHz / 24 bits mono, entrée directe de l'interface, micro chevalet, tonalité à fond, pics vers −12 dBFS, un fichier par item. **Le jeu chromatique corde par corde n'est plus demandé** : la page de calibration du jalon 5 l'enregistre elle-même | gabarits, seuils, verdict | jalon 6 |
| 5 | **Acheminement des fichiers** : une « release » GitHub sur votre dépôt, un lien Drive, ou l'enregistreur intégré que la page proposera, à joindre à une issue | — | jalon 6 |
| 6 | **Deux écoutes de 30 min** après les jalons 4 et 6, avec la check-list de la section 10 | porte finale | — |

Décisions reportées aux jalons qu'elles servent : dossier `rust/` avec
binaires WASM commités (jalon 7), utilitaire natif GPLv3 et signature
(jalon 8), GPU loué (jalon 9). Non nécessaires : âge des cordes, relevés
iPhone (iPhone reste en MONO tant que POLY n'a pas de build WASM), micro
hexaphonique (il faudrait aussi une interface à 6 entrées).

## 3. Architecture retenue

Tout tourne dans l'AudioWorklet existant, un seul thread, sans
SharedArrayBuffer (GitHub Pages ne pose pas les en-têtes COOP/COEP ;
vérifié). Chemin : guitare → interface → `getUserMedia` (traitements voix
désactivés) → worklet [front-end commun → MONO | POLY] → événements
`on / off / bend par note / meter` → `js/input/guitar.js` → `main.js`
(nouveau `routeNoteBend` par note) → voix des instruments, qui ont déjà
chacune leur `detune`.

- **Front-end** (existant) : DC, RMS, RMS bande haute, décimation ÷2 vers
  24 kHz ; POLY ajoute un second anneau décimé sans le passe-bas 3 kHz
  (partiels jusqu'à ~9 kHz). Hop 64 échantillons (2,67 ms).
- **Attaques** : flux spectral pondéré en fréquence plus la règle de niveau
  existante ; dépassement du réfractaire limité aux quatre premières trames
  (bug mesuré : onze attaques détectées sur un strum de 30 ms). Chaque
  attaque ouvre une fenêtre de décision de 40 ms (étalement réel des strums
  GuitarSet : médiane 13-16 ms, 90ᵉ centile 34-37 ms).
- **Analyse spectrale** : fenêtres **ancrées sur l'échantillon le plus
  récent**, FFT 2048 à 24 kHz, trois longueurs : COURTE 21 ms, MOYENNE
  43 ms, LONGUE 85 ms, pour le spectre de note-on ; une fenêtre
  **exponentielle courte (τ ≈ 8-12 ms)** pour le niveau des voix et le
  note-off (une fenêtre à plateau de 43 ms ne baisse que de 5 dB après
  24 ms de silence et donnerait des note-off de 60-70 ms).
- **Banque de gabarits** : 120 positions corde × case, versions attaque et
  décroissance, **une par longueur et par forme de fenêtre** (la forme
  change le lobe principal autant que la longueur), partiels inharmoniques
  f_n = n·f0·√(1 + B·n²) avec **B(corde, case) = B_vide(corde) · 2^(case/6)**
  (l'inharmonicité quadruple à la 12ᵉ case ; la calibration rapide aux
  cases 0/5/12 donne trois points par corde pour ajuster B_vide). Un
  désaccord de corde est corrigé en régénérant le gabarit à f0 décalé, pas
  en décalant d'un bin. ≈ 300-600 ko en IndexedDB, profil nommé avec taux
  d'échantillonnage, date et forme de fenêtre, export JSON.
- **Décomposeur** : NMF β = 0,5 multiplicative, gabarits fixes, activations
  à démarrage chaud, pénalité L1, ensemble actif ≤ 40 gabarits par salience
  harmonique. Mesuré : 120 gabarits × 4 itérations = 0,18 ms par hop en JS
  (Node), ×2,5 sur le thread audio de Chromium.
- **Logique de notes** : objets `Voice`, note-off et vélocité **par voix**
  (énergie des partiels propres, pas la RMS globale), bend par voix via la
  fréquence instantanée des deux premiers partiels (retard annoncé
  15-25 ms ; quand une seule voix sonne, la YIN mono garde le bend fin).
  Règle « une note par corde » appliquée seulement quand l'attribution de
  corde est sûre (> 0,8), sinon « une note par hauteur » : l'attribution de
  corde depuis un micro mono n'est juste qu'à ~82 % hors ligne.
- **Chemin rapide MONO** dans POLY : la YIN nomme une note seule
  uniquement si aucune voix ne sonne et confiance ≥ 0,95 ; la NMF vérifie
  et corrige 21-43 ms plus tard. Une note jouée par-dessus un accord qui
  sonne passe par la NMF (10 % des attaques solo de GuitarSet).
- **Accélérateur Rust → WASM** (conditionnel) : bibliothèque `no_std`,
  deux binaires (SIMD et scalaire), module transmis au worklet, mémoire
  statique, JS conservé comme repli et oracle de test, CI qui recompile et
  compare. Gains mesurés : produit matrice-vecteur 5-23× ; FFT et YIN non
  mesurés en WASM (YIN 1,9× sans SIMD).
- **Utilitaire natif** (optionnel, dépôt séparé sous GPLv3 à cause du SDK
  ASIO) : cpal ASIO, repli IAudioClient3, CoreAudio ; transport WebSocket
  local ou port MIDI virtuel (Safari ne tolère pas forcément `ws://` depuis
  une page https).

**Budget de latence, pincement → son, Chrome, 48 kHz**

| Étape | ms | Provenance |
|---|---|---|
| Interface + entrée Chrome | 8-12 | moitié des 19-26 ms aller-retour mesurés sur macOS ; Windows non mesuré |
| Granularité du hop | +1,3 en moyenne | 2,67 ms / 2 |
| Détection d'attaque | 3-5 (hypothèse à mesurer) | précision 0,93-0,95 à 5-8 ms dans la littérature |
| Nommage (physique) | mi aigu 3-6 ; sol 5-10 ; mi grave 12-24 | mono mesuré 9-36 ms |
| Confirmation NMF (2 hops) | 5,3 | règle de conception |
| Saut worklet → thread principal → rack → attaque du synthé | **5-10** | deux quanta minimum plus la gigue du thread principal, invisible au compteur actuel |
| Sortie Chrome | 10-15 | idem première ligne |
| **Total ressenti** | **40-65** ; 30-50 avec utilitaire natif | MIDI Guitar 2 : 21-38 ms mesurés en 2016 avec un tampon de 64 échantillons |

## 4. Données et évaluation

| Jeu | Taille / licence | Usage |
|---|---|---|
| GuitarSet v1.1 (Zenodo 3371780) | CC BY 4.0 ; annotations et mix mono déjà ici ; 360 prises, 62 476 notes | référence solo/comp ; prises hexaphoniques par requêtes partielles pour la vérité par corde |
| Synthétique `test/plucks.mjs` étendu (strum, accords, repick, étouffé, bend) | illimité, exact | **seule latence absolue** ; jamais pour la précision |
| Remix des pistes hexaphoniques (gains et décalages variés) | dérivé | accords étiquetés exactement, pour le prototype |
| Vos enregistrements | ≈ 12 min + calibration | gabarits, seuils, verdict |
| GOAT (5,9 h d'électrique en direct, CC BY 4.0) | — | étape neuronale optionnelle |

Pas d'entraînement dans le plan de base, une grille de cinq seuils.
Évaluation par un script unique (`test/score.py`, mir_eval) : F1 des notes
à ±50 et ±20 ms, hauteur ±50 cents, avec et sans fins de notes ; **rappel
par taille d'accord** (1 / 2 / 3 / 4 / 5-6 notes dans 50 ms ; GuitarSet comp
en contient 34 / 22 / 23 / 16 / 6 %) ; temps de complétion d'accord ;
fantômes (< 30 ms), notes coincées, redéclenchements, erreurs d'octave,
notes avant le pincement ; trois latences imprimées (absolue sur
synthétique, GuitarSet corrigée du retard d'annotation ≈ 12 ms à revalider
sur ≥ 500 notes, référencée hexaphonique). Liste de prises figée et
commitée ; le mono et Basic Pitch sont re-notés sur cette même liste.

## 5. Jalons

| Jalon | Livrable | Sessions | Risque | Acceptation |
|---|---|---|---|---|
| **1 Banc** (démarre maintenant) | `score.py`, liste de prises figée, générateurs strum/accords/repick/étouffé, mesure de latence des fenêtres asymétriques sur synthétique, tableau de référence mono + Basic Pitch | 1,5 | faible | tableau commité, retard d'annotation revalidé |
| **1 bis Levée de risque** | prototype hors ligne (banque générique avec B par case + NMF parcimonieuse + règle de note-on) sur remix hexaphoniques et prises comp, rappel par taille d'accord | 1 | moyen | doubles-notes ≥ 80 % avec étiquettes exactes, sinon périmètre réduit à doubles-notes et triades |
| 2 Plomberie rack | bend par note (`routeNoteBend`, instance, sfz), `state.guitar.mode`, sélecteur MONO / POLY sur la carte, douze langues, refonte `Voice`, mesure aller-retour clic → micro dans la page, correctifs mono (réfractaire, `setParams`) | 2,5 | faible | tests verts, guitarset-eval mono identique |
| 3 POLY v1 JS hors ligne | `poly.js`, `fft.js`, `templates.js`, NMF, règles, YIN sur résidu | 4 (+1 réserve) | **élevé** | comp F1 ≥ 0,55 à 50 ms, solo ≥ 0,72 ; 2-3 notes ≥ 90 % ; complétion p90 < 60 ms ; 0 coincée ; bend p50 ≤ 25 ms ; note-off ≤ 45 ms aigu / ≤ 70 ms grave ; 0 note avant le pincement ; vélocité par voix à ±15 % |
| 4 Intégration navigateur | POLY dans le worklet, lecture CPU et TRK sur la carte, page de test, vos relevés | 1,5 | moyen | bascule live sans note coincée ; porte CPU ci-dessous |
| 5 Calibration | grille 6 × 20, rapide / complète, avance automatique, vérification ±50 cents, IndexedDB, export, enregistreur intégré | 2 | moyen | vos accords F1 ≥ 0,75 ; complète ≥ rapide mesuré |
| 6 Réglage sur votre guitare, bêta | seuils figés, rapport d'échecs, docs | 2 | moyen | fantômes < 5 %, coincées < 1 %, écoute « utilisable » |
| 7 WASM (si la porte CPU l'exige) | `rust/jamrack-dsp`, deux binaires, parité 1e-4, CI | 2 | faible-moyen | voir porte |
| 8 *Opt.* utilitaire natif | dépôt GPLv3, ASIO / IAudioClient3 / CoreAudio | 5 | moyen-élevé | gain IN mesuré chez vous |
| 9 *Opt.* CNN causal | données, modèle 20-60 k paramètres, GPU loué par vous, noyau WASM (~2 ms par pas) | 9 | élevé | bat la NMF en comp sans hausse de latence ni de CPU |

**Porte CPU unique** (99ᵉ centile du temps par bloc, mesuré sous
Playwright pendant un strum, pas la moyenne) : POLY JS peut être livré sur
ordinateur jusqu'à 40 % ; au-delà, jalon 7 ; le WASM doit tenir ≤ 25 %
ordinateur et ≤ 20 % iPhone ; iPhone reste en MONO tant que ce n'est pas
atteint. Coût estimé d'un hop en rafale d'attaque (deux FFT, NMF, YIN sur
résidu) : 2,3-3 ms en JS, soit le quantum entier ; d'où la YIN limitée à
la fenêtre courte ou à un hop sur deux pendant les rafales.

**Exposition** : POLY est livré derrière le sélecteur, MONO par défaut,
`state.guitar.mode` persistant, page de test sur une branche séparée ; la
page principale ne voit aucune régression.

## 6. Résultat attendu, honnêtement

| | Mono aujourd'hui | POLY (cible) | Jam Origin |
|---|---|---|---|
| Note seule, rien d'autre ne sonne | 9-36 ms | inchangé | « généralement < 20 ms » |
| Note seule par-dessus un accord qui sonne | souvent fausse | 15-40 ms | inconnu |
| Note d'accord, cordes aiguës | — | 15-25 ms | inconnu |
| Note d'accord, mi/la graves | — | 30-40 ms (plancher physique) | inconnu |
| Ressenti Chrome | ~30-55 ms | 40-65 ms (30-50 avec utilitaire) | 21-38 ms audio → synthé |
| Solo F1 (GuitarSet, 50 ms) | 0,72 | 0,72-0,79 | non mesuré |
| Comp F1 | 0,16 | 0,55 puis 0,65-0,75 | non mesuré |
| 2-3 notes | 0 | ≈ 85-90 % des notes | « quatre notes à la fois » (forums) |
| 4-6 notes | 0 | ≈ 70-80 %, voix intérieures et doublures d'octave manquées | idem |
| Strums < 10 ms | 0 | une attaque, notes égrenées sur 15-40 ms | point faible connu |

Incertitude : ±0,1 sur les F1, ±5 ms sur les latences ; tout chiffre iPhone
ou Windows est conditionnel à vos relevés.

## 7. Risques et plans B

- Séparation à un seul micro : la littérature rate 15-46 % des notes des
  accords à 4 notes même hors ligne → promettre 2-3 notes, réduire le
  périmètre si le prototype 1 bis le dit.
- Fantômes octave et quinte structurels → parcimonie, gabarits par corde,
  test partiels pairs/impairs ; accepter de courtes notes faibles.
- Gabarits périssables (cordes, accordage, micro) → recalibration rapide,
  date affichée, régénération au désaccord.
- Calibration rapide : la position du micro crée des creux spectraux qui
  se déplacent avec la case ; la banque rapide perdra quelques points,
  mesurés au jalon 5 contre la complète.
- Écart GuitarSet (acoustique) / votre électrique en direct → vos
  enregistrements obligatoires avant le jalon 6.
- CPU du thread audio (JS 2,2-2,7× plus lent que sur le thread principal)
  → porte p99, WASM conditionnel, iPhone en MONO.
- Utilitaire natif jamais exécuté ici (pas de Windows ni macOS) → optionnel,
  hors chemin critique.
- Étape neuronale : aucun modèle publié n'est polyphonique et causal sous
  100 ms ; entraînement CPU ≈ 3 jours par essai → GPU loué par vous, ou
  ne pas lancer.
- Licences : SDK ASIO libre seulement sous GPLv3 (dépôt séparé, JAMRACK
  reste MIT) ; jeux non commerciaux exclus de tout livrable.

## 8. Ce qui reste non vérifié

Aller-retour Chrome sur Windows et sur iOS ; retard d'annotation GuitarSet
(52 attaques, un joueur) ; latence réelle des fenêtres asymétriques (à
mesurer, pas déduite) ; coût de six suivis de voix et de 2-3 FFT par hop
sur le thread audio ; algorithme et latence actuels de MIDI Guitar 3 ;
vitesse JS et WASM dans Firefox et WebKit (Playwright peut installer les
deux ici pour une première mesure) ; `ws://127.0.0.1` depuis une page
https ; type MIME `.wasm` sur GitHub Pages ; valeurs d'inharmonicité par
corde (à mesurer à la calibration).

## 9. Fiche pratique pour vous

**Enregistrement** (quand vous voulez ; il sert au jalon 6) : interface
en 48 kHz / 24 bits, entrée directe, micro chevalet, tonalité à fond, gain
réglé pour des pics vers −12 dBFS, un fichier WAV mono par item, nommés
`01-doubles.wav`, `02-ouverts-lent.wav`, `03-ouverts-moyen.wav`,
`04-ouverts-rapide.wav`, `05-ouverts-arpege.wav`, `06-barres-lent.wav`,
`07-barres-moyen.wav`, `08-barres-rapide.wav`, `09-barres-arpege.wav`,
`10-etouffe.wav`, `11-bend-vibrato.wav`, `12-libre.wav`. Une seconde de
silence entre deux événements. Dites quel accord ou quelle double-note
vous jouez dans un petit fichier texte à côté.

**Mesure aller-retour** (jalon 2 l'intégrera à la page) : haut-parleur et
micro ouverts, un clic émis par la page, le temps jusqu'à son retour dans
le micro ; c'est le vrai IN + OUT, à relever une fois sur Chrome/PC.

**Les deux décisions** : (a) calibration par guitare, oui ou non ; (b)
note-off qui suit la décroissance (DECAY) ou relâchement fixe.

## 10. Check-list d'écoute (30 min, chaque item « utilisable » ou non)

Notes seules sur chaque corde ; doubles-notes ; accords ouverts lents puis
rapides ; barrés ; arpèges ; corde étouffée dans un accord ; bend sur une
corde d'un accord ; hammer-on dans un accord ; repick rapide ; 30 s de jeu
libre.
