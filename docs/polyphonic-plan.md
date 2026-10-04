# JAMRACK POLY — plan de réalisation du moteur polyphonique

Issu d'une étude orchestrée (six recherches, trois architectures jugées
par trois relecteurs, synthèse, critique de complétude) puis corrigé des
erreurs relevées par la critique. Le mono reste tel quel ; POLY est un
second moteur derrière le sélecteur MONO / POLY de la carte GUITARE → MIDI.

## 0. Décision du propriétaire (4 octobre 2026)

**POLY doit fonctionner pour n'importe quel visiteur de la page, sans
rien configurer.** Conséquences sur ce plan :

- La **banque générique** (modèle physique inharmonique, B par corde et
  par case, amplitudes des partiels par défaut) est le chemin principal,
  pas un repli. Elle est validée sur plusieurs guitares et micros :
  GuitarSet (acoustique, six joueurs), GOAT (5,9 h d'électriques en
  direct, CC BY 4.0, plusieurs guitares), et la guitare du propriétaire
  comme une guitare de plus. Les critères d'acceptation des jalons 3 et 6
  portent sur la banque générique.
- La **calibration par guitare** (jalon 5) reste au plan comme option
  « adapter à ma guitare » qui améliore le résultat, jamais comme
  prérequis ; l'état non calibré n'est pas une alerte.
- Les seuils sont réglés pour être robustes entre guitares, pas optimaux
  pour une seule ; le réglage sur la guitare du propriétaire (jalon 6)
  vérifie qu'il ne régresse pas les autres.
- Note-off : en l'absence d'avis contraire, la note **suit la décroissance
  de la corde** (bouton DECAY), comme en MONO.
- **Assistant de calibration intégré, conservé par défaut** (précision du
  5 octobre) : la carte propose un assistant guidé (« jouez la corde de mi
  grave à vide… »), avec avance automatique dès qu'une note stable et juste
  est entendue, reprise d'une note ratée, version rapide (18 pincements,
  ~1 min) et complète (120, ~5 min). Le profil obtenu est **enregistré dans
  le navigateur sans action de l'utilisateur** (IndexedDB, comme les
  échantillons du SAMPLER ; rien n'est envoyé nulle part) et rechargé à la
  visite suivante ; plusieurs profils nommés (une guitare, un micro, un
  accordage chacun), date affichée, recalibration rapide, export et import
  JSON pour passer d'un appareil à l'autre. Tant qu'aucun profil n'existe,
  la banque générique joue : un visiteur n'a jamais à calibrer pour
  commencer.

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
| 1 | ~~Deux décisions~~ **Tranché (section 0)** : pas de calibration requise, banque générique pour tous ; note-off suit la décroissance (DECAY) sauf avis contraire | — | — |
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
| 5 Assistant de calibration | assistant guidé sur la carte (rapide 18 / complète 120 pincements, avance automatique, reprise, vérification ±50 cents), profils nommés enregistrés par défaut dans IndexedDB et rechargés automatiquement, date, recalibration rapide, export / import JSON, douze langues | 2,5 | moyen | vos accords F1 ≥ 0,75 ; complète ≥ rapide ; un visiteur non calibré joue sur la banque générique sans message bloquant |
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


## 11. Tableau de référence (jalon 1)

Banc figé : `test/takes.json` (24 prises solo + 12 prises comp de GuitarSet,
règle de sélection déterministe écrite dans le fichier : pour le joueur p et
le style s, parmi les 6 prises du couple (joueur, style) triées par nom, solo
= styles (p + k) mod 5 pour k = 0..3 et prise (p + s) mod 6 ; comp = styles
(2p) mod 5 et (2p + 1) mod 5 et prise (p + s + 3) mod 6 ; 4 solo et 2 comp
par joueur, chaque style 4-5 fois en solo et 2-3 fois en comp),
`test/dump-events.mjs` (événements du traqueur en JSON) et `test/score.py`
(mir_eval). Audio : mix mono du micro magnétique, 44,1 kHz ; référence : les
six annotations `note_midi` par corde (micro hexaphonique). Durée :
715 s de solo (2185 notes), 378 s de comp
(3119 notes). Commandes exactes (`<guitarset>` = dossier GuitarSet) :

```
node test/dump-events.mjs --guitarset <guitarset> --set solo --out events-mono-solo.json
node test/dump-events.mjs --guitarset <guitarset> --set comp --out events-mono-comp.json
python3 test/score.py --guitarset <guitarset> --basic-pitch solo --out events-basic-pitch-solo.json
python3 test/score.py --guitarset <guitarset> --basic-pitch comp --out events-basic-pitch-comp.json
python3 test/score.py --guitarset <guitarset> events-mono-solo.json events-mono-comp.json \
    events-basic-pitch-solo.json events-basic-pitch-comp.json
```

Appariement : `mir_eval.transcription.match_notes`, un pour un, attaque dans
la tolérance, hauteur ±50 cents ; « avec fins » = fin de note à 20 % de la
durée de référence (≥ 50 ms). Les lignes rappel par taille d'accord,
fantômes, coincées, octaves, notes avant le pincement et latences utilisent
l'appariement ±50 ms sans fins. Taille d'accord = notes de référence dont
l'attaque tombe dans les 50 ms de la première du groupe. Chiffres cumulés sur
les prises (sommes des notes appariées / émises / de référence), tels que
produits par le script.

| Mesure                                                                            | mono (solo)                       | mono (comp)                       | basic-pitch (solo)              | basic-pitch (comp)              |
|-----------------------------------------------------------------------------------|-----------------------------------|-----------------------------------|---------------------------------|---------------------------------|
| Prises / notes de référence / notes émises                                        | 24 / 2185 / 2406                  | 12 / 3119 / 955                   | 24 / 2185 / 2377                | 12 / 3119 / 3278                |
| F1 ±50 ms                                                                         | 71.8 %                            | 9.3 %                             | 81.4 %                          | 72.9 %                          |
| F1 ±20 ms                                                                         | 65.4 %                            | 4.6 %                             | 63.6 %                          | 59.2 %                          |
| F1 ±50 ms avec fins                                                               | 53.8 %                            | 2.7 %                             | 63.2 %                          | 44.3 %                          |
| F1 ±20 ms avec fins                                                               | 49.3 %                            | 1.6 %                             | 50.2 %                          | 37.1 %                          |
| Précision ±50 ms                                                                  | 68.5 %                            | 19.9 %                            | 78.1 %                          | 71.1 %                          |
| Rappel ±50 ms                                                                     | 75.5 %                            | 6.1 %                             | 84.9 %                          | 74.7 %                          |
| Précision ±20 ms                                                                  | 62.4 %                            | 9.8 %                             | 61.0 %                          | 57.8 %                          |
| Rappel ±20 ms                                                                     | 68.7 %                            | 3.0 %                             | 66.4 %                          | 60.7 %                          |
| Rappel accords de 1 note (n réf.)                                                 | 79.2 % (1991)                     | 24.3 % (461)                      | 85.9 % (1991)                   | 67.9 % (461)                    |
| Rappel accords de 2 notes (n réf.)                                                | 38.2 % (170)                      | 7.3 % (562)                       | 71.8 % (170)                    | 72.2 % (562)                    |
| Rappel accords de 3 notes (n réf.)                                                | 33.3 % (24)                       | 3.4 % (783)                       | 95.8 % (24)                     | 75.9 % (783)                    |
| Rappel accords de 4 notes (n réf.)                                                | n/a (0)                           | 0.8 % (956)                       | n/a (0)                         | 77.6 % (956)                    |
| Rappel accords de 5-6 notes (n réf.)                                              | n/a (0)                           | 0.6 % (357)                       | n/a (0)                         | 77.3 % (357)                    |
| Fantômes (< 30 ms, non appariées) / émises                                        | 199 (8.3 %)                       | 138 (14.5 %)                      | 0 (0.0 %)                       | 0 (0.0 %)                       |
| Notes coincées (> 2× réf. + 0,5 s) / appariées                                    | 8 (0.5 %)                         | 1 (0.5 %)                         | 2 (0.1 %)                       | 3 (0.1 %)                       |
| Erreurs d'octave / émises                                                         | 36 (1.5 %)                        | 153 (16.0 %)                      | 80 (3.4 %)                      | 384 (11.7 %)                    |
| Notes avant le pincement (> 20 ms) / appariées                                    | 22 (1.3 %)                        | 7 (3.7 %)                         | 349 (18.8 %)                    | 372 (16.0 %)                    |
| Latence brute vs annotation : médiane (p25 / p75 / p90)                           | +6 ms (+3 ms / +10 ms / +16 ms)   | +19 ms (+9 ms / +31 ms / +41 ms)  | -13 ms (-18 ms / -8 ms / -1 ms) | -10 ms (-17 ms / -2 ms / +6 ms) |
| Retard annotation GuitarSet vs attaque énergie (1449 notes) : médiane (p25 / p75) | +9 ms (+7 ms / +12 ms)            | +9 ms (+7 ms / +12 ms)            | +9 ms (+7 ms / +12 ms)          | +9 ms (+7 ms / +12 ms)          |
| Latence corrigée (brute + retard) : médiane (p25 / p75 / p90)                     | +16 ms (+13 ms / +20 ms / +26 ms) | +28 ms (+18 ms / +40 ms / +50 ms) | -3 ms (-9 ms / +2 ms / +9 ms)   | -0 ms (-7 ms / +8 ms / +16 ms)  |

Répartition des notes de référence par taille d'accord sur les 12 prises
comp : 1 note 15 % (461), 2 notes 18 % (562), 3 notes 25 % (783), 4 notes
31 % (956), 5-6 notes 11 % (357, dont 72 à 6 notes) — plus polyphonique que
les 34 / 22 / 23 / 16 / 6 % cités en section 4, qui portaient sur un autre
regroupement ; c'est cette répartition qui fait foi pour le banc. En solo,
91 % des notes sont seules, 8 % à deux (doubles-notes) et 1 % à trois.

Notes de lecture :

- **Mono** : F1 71,8 % en solo, 9,3 % en comp. Les 72 % / 84 % de
  `docs/guitar-to-midi.md` (précision / rappel) venaient d'une autre liste de
  prises et d'une fenêtre d'appariement asymétrique −60/+150 ms ; avec la
  fenêtre symétrique ±50 ms de mir_eval on lit 68,5 % / 75,5 %. Fantômes 8 %
  en solo, 14,5 % en comp ; erreurs d'octave 16 % des notes émises en comp
  (cordes qui sonnent ensemble) ; 1,3 % des notes appariées partent plus de
  20 ms avant l'attaque annotée (à relire avec le retard d'annotation
  ci-dessous : elles partent en réalité ~10 ms *après* le pincement).
- **Retard d'annotation GuitarSet** : 1449 attaques isolées des 24 prises
  solo (aucune autre attaque annotée dans [−150, +50] ms, montée ≥ 10 dB),
  enveloppe RMS 2 ms / pas 0,5 ms, attaque = premier passage à 20 % de la
  montée (plancher → crête) : l'annotation est **en retard de 9 ms
  (p25 7 / p75 12)** sur l'attaque d'énergie ; 11 ms avec un seuil à 10 %,
  9 ms à 50 % (`--lag-threshold`). Les ≈ 12 ms estimés sur 52 attaques
  (section 4) sont donc revalidés à 9-11 ms. Sur les seules prises comp la
  mesure ne retient que 317 attaques et donne +22 ms (la corde annotée
  n'est pas toujours la première grattée) : elle n'est pas utilisée, la
  colonne comp reprend le retard mesuré en solo.
- **Latence corrigée du mono** : médiane **16 ms** après le pincement en
  solo (p90 26 ms), 28 ms en comp — cohérent avec les 9-36 ms physiques du
  traqueur plus la granularité de bloc (128 échantillons, 2,9 ms).
- **Basic Pitch** (Spotify, modèle ICASSP 2022 en ONNX, CPU, paramètres par
  défaut dont durée minimale de note 127,7 ms, `pip3 install --user
  basic-pitch` déjà présent ; 18-20 s par jeu de prises sur 4 cœurs, deux
  exécutions identiques) : hors ligne et non causal, c'est le **plafond de
  référence**, pas un concurrent. F1 81,4 % en solo et **72,9 % en comp**,
  rappel 72-78 % pour les accords de 2 à 6 notes, précision 71 % ; ses
  attaques tombent 13 ms *avant* l'annotation (3 ms avant le pincement
  réel), d'où ses 16-19 % de « notes avant le pincement » ; zéro fantôme par
  construction (durée minimale) ; 3,4 % / 11,7 % d'erreurs d'octave. Les
  cibles du jalon 3 (comp F1 ≥ 0,55, solo ≥ 0,72, 2-3 notes ≥ 90 %) se
  lisent contre ces deux lignes : un moteur causal à fenêtre de 20-40 ms
  qui atteint 0,55-0,65 en comp est à 75-90 % du plafond hors ligne.
- Prises hexaphoniques : l'archive n'était pas disponible pendant cette
  session (téléchargement incomplet) ; la latence « référencée
  hexaphonique » de la section 4 reste à produire au jalon 1 bis.


## 12. Résultat du jalon 1 bis (prototype de levée de risque, 5 octobre)

Prototype hors ligne : banque générique de 120 gabarits inharmoniques
(valeurs B de Barbancho 2012, B doublé toutes les 6 cases, amplitudes
1/n^1,2) + NMF β = 0,5 parcimonieuse, fenêtre de 43 ms ancrée sur
l'échantillon le plus récent, hop 2,67 ms, règle de note-on sur deux hops.
Code dans `test/poly/`, notation par `test/score.py`, vérifié par un agent
adversarial (reproduction des chiffres, contrôle de fuite, prises
nouvelles, mir_eval refait à la main).

**Ce qui tient, sur des prises jamais vues par le réglage :**

| Jeu (prises nouvelles) | POLY F1 | Mono F1 | Rappel POLY doubles / triades |
|---|---|---|---|
| Solo (12 prises) | 72,8 % | 72,9 % | — |
| Deux solos superposés (mix2b) | 65,0 % | 46,9 % | 52,8 % / — |
| Trois solos superposés (mix3b) | 56,8 % | 30,6 % | 42,7 % / 42,6 % |
| Deux cordes hexaphoniques d'accords (hex2b) | 64,0 % | 40,5 % | 54,2 % / — |
| Trois cordes hexaphoniques (hex3b) | 55,8 % | 24,9 % | 47,3 % / 53,9 % |
| Accords complets (comp2) | 41,0 % | 5 % | 28 % / 38 % (4 notes : 29 %) |

POLY fait jeu égal avec le mono sur les lignes solo, avec ≤ 1 % de notes
fantômes contre 8 à 16 % pour le mono, à +5 ms de latence médiane, et
double le mono sur tout ce qui est polyphonique.

**Ce qui ne tient pas :** la porte « doubles-notes ≥ 80 % de rappel avec
étiquettes exactes » n'est atteinte nulle part (48 à 54 %), et le critère du
jalon 3 (2-3 notes ≥ 90 %) est hors de portée de ce décomposeur : avec la
banque générique, seules 83 % des notes de doubles-notes et 54 % des notes
d'accords obtiennent jamais une activation suffisante (plafond mesuré).

**Corrections du vérificateur :** tous les chiffres viennent du décomposeur
à 6 itérations / λ 0,02 ; la variante annoncée (15 itérations / λ 400),
réglée proprement, est réellement meilleure (hex2b : F1 64 → 75, doubles
54 → 63 %, précision 88 %, 0,9 ms par hop en numpy) et devient le point de
départ ; la séparation DEV / TEST du prototype était contaminée (par joueur
à refaire) ; « POLY meilleur que mono sur solo » ne se généralise pas (égalité).

**Décision appliquée (règle du jalon 1 bis) :** pas de lancement du jalon 3
sur ce décomposeur. Une **session d'amélioration** d'abord, mesurée sur le
même banc avec une séparation propre par joueur, trois pistes classées par
gain attendu : (1) banque de gabarits dérivée des données (spectres moyens
par corde × case des pistes hexaphoniques, réponse du micro incluse) avec
contrôle sur une autre guitare (GOAT, électrique en direct) pour rester
générique ; (2) convergence du décomposeur (plus de gabarits actifs,
démarrage à chaud non nul, 8-15 mises à jour, λ à l'échelle) ; (3) décision
par attaque sur l'activation intégrée sur 40 ms au lieu de deux hops. Porte
de sortie : sur prises nouvelles, doubles ≥ 70 % et triades ≥ 60 % avec
précision ≥ 80 % → jalon 3 avec périmètre « doubles-notes et triades
fiables, accords complets partiels » ; sinon POLY est annoncé comme
« doubles-notes et triades » et le plan est revu.
