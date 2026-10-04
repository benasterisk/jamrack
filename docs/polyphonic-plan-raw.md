# Annexe : plan POLY tel que synthétisé par l'orchestration (brut, avant corrections)

# JAMRACK POLY — plan final

## 1. Réponse courte

Il faut un **second moteur, polyphonique, à côté du traqueur MONO actuel** (qu'on ne touche pas : 9-36 ms mesurés, 16/16 tests verts), construit sur la famille d'algorithmes vers laquelle convergent le brevet de Jam Origin (US 2012/0132057 A1), Cont 2006 et Dessein 2010 : **des gabarits spectraux appris sur votre guitare** (calibration de 1 à 5 minutes), décomposés toutes les 2,7 ms par une **NMF parcimonieuse à démarrage chaud**, avec attaques détectées par flux spectral et a priori guitare (≤ 6 notes, une par corde). Il faut **d'abord un banc de mesure** (mir_eval, latence absolue sur signaux synthétiques, GuitarSet corrigé) qui note chaque étape avec des chiffres qu'un guitariste comprend ; ce banc démarre aujourd'hui sans rien de votre part. Il faut **vos enregistrements DI** (25 min) et vos relevés IN/OUT, car GuitarSet est une guitare acoustique à micro hexaphonique pincé et ce conteneur n'a pas d'audio. Résultat honnête : notes seules inchangées, accords de 2-3 notes fiables, 4-6 notes partiels, **latence de nommage 15-25 ms (cordes aiguës) / 30-40 ms (mi-la graves) + ~20 ms d'E/S navigateur** ; le Rust/WASM n'est qu'un accélérateur conditionnel, le modèle neuronal et le helper natif ASIO des étapes optionnelles. Environ **10-12 sessions d'agents** pour une bêta POLY crédible.

## 2. Ce que vous devez fournir

| # | Quoi | Pourquoi | Peut démarrer avant |
|---|---|---|---|
| 1 | **Votre rig** : guitare, micro (simple/double bobinage, chevalet ?), âge des cordes, interface USB (pilote ASIO natif ?), Windows 10/11 (24H2+ ?), iPhone + mode de connexion, navigateurs | Fixe l'écart de domaine et le chemin Windows | Jalons 1-3 |
| 2 | **Relevés IN / OUT / TRK** de la carte GUITAR actuelle, Chrome/PC et Safari/iPhone, plus CPU affiché pendant une note tenue | Remplace tous les chiffres navigateur extrapolés (Windows et iOS jamais mesurés) | Jalons 1-3 |
| 3 | **Enregistrements set 1** (≈ 25 min, WAV 48 kHz/24 bit mono, DI de l'interface, micro chevalet, tonalité à fond, un fichier par item) : chaque corde chromatique du vide à la case 19 (1,5 s par note) ; 20 doubles-notes (octaves, quintes, tierces) ; accords ouverts E A D G C Em Am Dm, barrés F Bm B7, chacun gratté lent/moyen/rapide puis arpégé ; un accord avec une corde étouffée ; un accord avec bend/vibrato sur la chanterelle seule ; 5 min de jeu libre | Seule vérité terrain qui ressemble à votre micro ; sert aux gabarits ET au réglage des seuils | Jalons 1-3 ; bloque le jalon 6 |
| 4 | **Calibration dans l'appli** (rapide : 18 pincements, ~1 min ; complète : 120, ~5 min) puis set 2 (accords + jeu libre) | Les gabarits sont propres à guitare/micro/cordes/accordage | Jalon 5 livré |
| 5 | **Décisions** : (a) accepter une calibration par guitare ; (b) note-off : suivre la décroissance ou relâchement fixe ; (c) accepter un dossier `rust/` avec binaires `.wasm` commités et vérification CI (« pas de build pour déployer » reste vrai) ; (d) helper natif = programme séparé **GPLv3** (SDK ASIO gratuit sous GPLv3 depuis 2.3.4, oct. 2025), non signé au départ, ou budget 99 $/an Apple + certificat Windows | — | — |
| 6 | **Deux sessions d'écoute** de 30 min (après jalons 4 et 6) avec une check-list | Vos oreilles sont la porte finale | — |
| 7 | *Optionnel* : essai MIDI Guitar 2 / bêta MG3 dans Reaper pour le protocole loopback ; GPU loué (≈ 10-30 $/run) si l'étape neuronale est lancée ; micro hexaphonique (~170 $) pour une vérité par corde | Rend la comparaison Jam Origin mesurée, pas supposée | — |

## 3. Architecture retenue

Tout tourne **dans l'AudioWorklet existant** (`js/audio/guitar/worklet.js`), un seul thread, sans SharedArrayBuffer (GitHub Pages ne peut pas poser COOP/COEP — vérifié : `DataCloneError`). Chemin : guitare → interface → `getUserMedia` (EC/NS/AGC off) → worklet [front-end commun → MONO | POLY] → événements `{on, off, bend(midi), meter}` → `js/input/guitar.js` → `main.js` (`routeNoteOn/Off/NoteBend`) → voix des instruments (chaque voix a déjà son `detune`).

Composants :
- **Front-end** (existant, `tracker.js:192-216`) : DC, RMS, RMS 2,5 kHz, décimation ÷2 → 24 kHz ; POLY ajoute un second anneau décimé sans le passe-bas 3 kHz (partiels jusqu'à ~9 kHz). Hop = 64 éch. (2,67 ms) ; `process()` accepte toute taille de bloc (Chrome 153 `renderSizeHint`).
- **Attaques** : flux spectral pondéré en fréquence (Σ max(0,|X_t|−|X_{t−1}|)·k) + règle de niveau existante ; override du réfractaire limité aux 4 premières trames (le bug mesuré : 11 attaques sur un strum à 30 ms). Chaque attaque ouvre une **fenêtre de 40 ms** (étalement réel des strums GuitarSet : p50 13-16 ms, p90 34-37 ms).
- **Analyse spectrale** : fenêtres **ancrées sur l'échantillon le plus récent et asymétriques** (montée courte, plateau jusqu'au présent, ou décroissance exponentielle) — une Hann symétrique de 21 ms a son centroïde 10,6 ms en arrière et celle de 85 ms 42,6 ms, ce qui invalidait les latences de bend/note-off des plans d'origine. Trois longueurs : COURTE 21 ms, MOYENNE 43 ms, LONGUE 85 ms, FFT 2048.
- **Banque de gabarits W** : 120 positions corde×case × {attaque, décroissance} × **une version par longueur de fenêtre** (correction du défaut fatal « W et V à des résolutions différentes »), ~100 bins non nuls chacun, partiels inharmoniques f_n = n·f0·√(1+B·n²), ≈ 300-600 ko en IndexedDB, profil nommé + taux d'échantillonnage + export JSON.
- **Décomposeur** : NMF β=0,5 (Dessein 2010) multiplicative, W fixe, h à démarrage chaud, pénalité L1 (Cont 2006 : précision 33-50 % → 71-78 %), ensemble actif ≤ 40 gabarits via salience harmonique (Klapuri). Mesuré : 120 gabarits × 4 itérations = 0,18 ms/hop en JS Node, ×2,5 sur le thread audio de Chromium.
- **Logique de notes** : objets `Voice` (refonte de l'état mono-note `tracker.js:131-145`), échelle de fenêtres par corde, note-off **par voix**, vélocité **par voix** (énergie propre des partiels, pas la RMS globale gonflée par les cordes qui sonnent).
- **Chemin rapide MONO** : la YIN existante nomme une note isolée seulement si **aucune voix ne sonne et confiance ≥ 0,95** (les fausses notes mesurées sur A2+E3 et C4+E4 étaient à 0,90-0,91, au-dessus du CONF_ON 0,85 actuel) ; la NMF vérifie 21-43 ms plus tard et corrige. Désactivable sur la carte.
- **Accélérateur Rust→WASM** (conditionnel) : cdylib `no_std`, deux binaires (simd128 + scalaire, choisis par `WebAssembly.validate`), Module passé par `processorOptions`, instanciation synchrone (1 ms vérifiée), arène statique sans `memory.grow`, JS conservé en repli et oracle de test, CI rebuild-and-diff.
- **Helper natif** (optionnel, dépôt GPLv3 séparé) : cpal ASIO (build MSVC via cargo-xwin, compilé et lié ici, **jamais exécuté**), IAudioClient3 en repli, CoreAudio via runner macOS GitHub ; événements en `ws://127.0.0.1` (à vérifier depuis l'origine https).

**Budget de latence par étape (navigateur, 48 kHz, pincement → son)**

| Étape | ms | Provenance |
|---|---|---|
| Interface + entrée Chrome | 8-12 | moitié des 19-26 ms aller-retour mesurés sur macOS ([jefftk](https://www.jefftk.com/p/browser-audio-latency), [Chrome 153/M4](https://dev.classmethod.jp/en/articles/chrome-153-webaudio-render-size-hint-latency/)) ; **Windows non mesuré** |
| Granularité hop | +1,3 moy. | 2,67 ms/2 |
| Détection d'attaque | 3-5 | flux HF, Kehling 2014 F=0,95 à 5 ms |
| Nommage (physique : 1-2 périodes + 3-5 ms de transitoire) | mi aigu 3-6 ; sol 5-10 ; mi grave 12-24 | mono mesuré 9-36 ms (`docs/guitar-to-midi.md`) |
| Confirmation NMF (2 hops) | 5,3 | règle de design |
| Rack + attaque synthé | 0-2,7 | prochain quantum |
| Sortie Chrome | 10-15 | idem ligne 1 |
| **Total ressenti** | **35-60** ; 25-45 avec helper natif | MIDI Guitar 2 : 21-38 ms audio-synthé, VST 64 éch. ([Premier Guitar 2016](https://www.premierguitar.com/articles/23660-decoding-modern-midi-guitar)) |

## 4. Algorithmes

**Réutilisé du mono** : front-end, biquads, détecteur d'attaque (corrigé), échelle YIN `_pitchAtOnset/_yin` (sur le **résidu** = mixage − partiels des voix actives, une fois par hop pendant une fenêtre d'attaque), `_vel`, lissage `_bend`, vote legato, gardes grille/accord.

**Gabarits** : par position, gabarit d'attaque = moyenne des spectres COURTS 10-40 ms après l'attaque ; décroissance = moyenne des spectres LONGS 200-600 ms ; B(corde) ajusté par moindres carrés sur les partiels 2-12 (Barbancho 2012). Calibration rapide (vide/5/12) : autres cases synthétisées en déplaçant le motif inharmonique. Sans calibration : banque générique (amplitudes 1/n^1,2, B par défaut), état « non calibré » affiché. Décalage de ±1 bin piloté par l'accordeur si une corde est > 15 cents à côté.

**Note-on** (gabarit k, dans une fenêtre d'attaque) : h_k > seuil absolu calibré **et** > 12 % de Σh sur 2 hops consécutifs **et** ≥ 3× sa valeur 4 hops avant l'attaque (pas de re-déclenchement d'une corde qui sonne) ; une seule note par corde (sinon off+on) ; étendue ≤ 5 cases hors cordes à vide ; garde d'octave par énergie partiels impairs/pairs (Kehling 2014) ; deux candidats à un demi-ton → le plus fort ; ≤ 6 voix. **Échelle par corde** : G/B/mi aigu et ré fretté décident sur COURTE ; **mi/la graves sur MOYENNE** — à 21 ms le lobe de Hann fait ±94 Hz, E2/F2 ne se séparent qu'au ~20ᵉ partiel ; la LONGUE peut ajouter des voix intérieures tardives.

**Suivi, bend, note-off par voix** : fréquence instantanée (vocodeur de phase) des partiels 1-2 entre FFT MOYENNES consécutives, fenêtre asymétrique → **retard de bend ≈ 15-25 ms** (annoncé tel quel ; quand une seule voix sonne, la YIN mono garde le bend fin). Niveau de voix = énergie des 3 premiers partiels ; off quand < pic − RELEASE dB pendant 24 ms, ou nouvelle attaque sur la même corde, ou silence global 100 ms (filet anti-note-coincée). **Note-off attendu 40-60 ms après étouffement sur les graves**, pas 21-40. Pas de résonateurs Q≈12 par voix : τ = 46 ms à 82 Hz, incompatible.

**Budget CPU par bloc (thread audio Chromium)** : mono 0,45-0,5 ms mesurés ; POLY JS v1 ≈ 1,3-1,6 ms (50-60 %, A/B bureau seulement) ; WASM SIMD ≈ 0,45-0,6 ms (17-22 %, FFT et NMF 5-23× plus rapides, mesuré). Porte = **p99** sous Playwright (DAFx26 : le débit isolé cache 5-7× de dépassements en callback réel), ≤ 30 % bureau, ≤ 20-25 % iPhone.

## 5. Données, entraînement ou calibration, évaluation

| Jeu | Taille / licence | Usage |
|---|---|---|
| GuitarSet v1.1.0 ([Zenodo 3371780](https://zenodo.org/records/3371780)) | 8,2 Go, CC BY 4.0 ; annotation + mix déjà décompressés ici (924 Mo) ; 360 extraits, 62 476 notes | Référence F1 solo/comp ; prises hex par requêtes Range (10-25 Mo) pour la latence physique |
| Synthétique `test/plucks.mjs` étendu (strum, accords, repick, bend, étouffé) | illimité, exact | **Seule latence absolue** ; jamais benchmark de précision |
| Remix de stems hex (gains −9..+3 dB, recombinaison 0/5/10/15/30 ms) | dérivé | Stress-test polyphonique étiqueté exactement |
| Vos enregistrements | 25 + 15 min | Gabarits, seuils, verdict |
| GOAT (5,9 h DI électrique, CC BY 4.0) ; IDMT-SMT (CC BY-NC-ND, évaluation seule) | — | Étape neuronale optionnelle uniquement |

Pas d'entraînement dans le plan de base : **grille de 5 seuils** (minutes sur 4 cœurs). Évaluation = un script (`test/score.py`, mir_eval 0.8.2 installé en 7 s) : F1 notes à ±50 et ±20 ms, pitch ±50 cents, avec/sans offsets ; **rappel par taille de cluster** (1/2/3/4/5-6 notes dans 50 ms ; comp GuitarSet = 34/22/23/16/6 %) ; temps de complétion d'accord ; fantômes (< 30 ms), notes coincées, re-déclenchements, erreurs d'octave, « note avant le pincement ». **Trois latences imprimées** : absolue (synthétique), GuitarSet corrigée (les onsets annotés sont ~12 ms en retard, n = 52 — à revalider sur ≥ 500 notes), référencée hex (seuil −20 dB par corde). Split par joueur, errata #4/#5 appliqués. Les scores GuitarSet mesurent une banque **interpolée**, pas une calibration complète ; dit tel quel. Bases : mono F 0,724 solo / 0,163 comp ; Basic Pitch hors ligne 0,791 / 0,732.

## 6. Jalons

| Jalon | Livrable | Sessions | Risque | Critère d'acceptation |
|---|---|---|---|---|
| **1 Banc** (démarre maintenant, zéro entrée utilisateur) | `score.py`, dump événements, générateur strum/accords, listes de prises fixes, onsets hex corrigés, porte Playwright µs/bloc, tableau de référence | 1,5 | faible | Tableau mono + Basic Pitch commité ; lag d'annotation revalidé |
| 2 Plomberie rack | bend taggé `midi`, `routeNoteBend/setNoteBend` (instance, sfz, composition molette + note), `state.guitar.mode`, bascule carte, 12 langues, refonte `Voice`, correctifs (réfractaire, `setParams`, bloc nul, accordeur) | 2,5 | faible | 16 tests verts, guitarset-eval identique |
| 3 POLY v1 JS hors ligne | `poly.js`, `fft.js`, `templates.js` (banque par fenêtre), NMF, règles, résidu YIN | 4 (+1 réserve) | **élevé** | comp F1 ≥ 0,55 à 50 ms, solo ≥ 0,72 ; 2-3 notes ≥ 90 % ; complétion p90 < 60 ms ; coincées 0 |
| 4 Intégration navigateur | POLY dans le worklet, lecture CPU/TRK, déploiement gh-pages, vos relevés | 1,5 | moyen | p99 ≤ 60 % JS bureau ; bascule live sans note coincée |
| 5 Calibration | grille 6×20, rapide/complète, auto-avance, vérification ±50 cents, IndexedDB, export, page d'étiquetage | 2 | moyen | vos accords F1 ≥ 0,75 |
| 6 Réglage sur votre guitare + bêta | seuils figés, rapport d'échecs, docs 12 langues | 2 | moyen | fantômes < 5 %, coincées < 1 %, écoute « utilisable » |
| 7 WASM (si p99 > 25-30 % ou iPhone) | `rust/jamrack-dsp`, 2 binaires, parité 1e-4, CI | 2 | faible-moyen | p99 ≤ 25 % bureau |
| 8 *Opt.* helper natif | dépôt GPLv3, ASIO/IAudioClient3/CoreAudio, WebSocket, `--loopback-test` | 5 | moyen-élevé | gain IN mesuré sur votre interface |
| 9 *Opt.* CNN causal | pipeline données, modèle 20-60k, GPU loué, noyau WASM (compter ~2 ms/pas, 1,16 GMAC/s mesurés, pas 0,5) | 9 | élevé | bat la NMF en comp F1 à 20 ms sans hausse de latence/CPU |

Si le jalon 3 cale : réduire à « POLY = doubles-notes et triades », déjà un saut énorme sur 0,163.

## 7. Résultat attendu, honnêtement

| | Mono aujourd'hui | POLY (cible) | Jam Origin |
|---|---|---|---|
| Note seule, pincement → note-on | 9-36 ms | inchangé (chemin rapide) | MIDI « généralement < 20 ms » (PG 2016) |
| Note d'accord, cordes aiguës | — | 15-25 ms | inconnu |
| Note d'accord, mi/la graves | — | 30-40 ms (plancher physique) | inconnu |
| Ressenti Chrome | ~30-55 | 35-60 (25-45 helper) | 21-38 audio-synthé, VST 64 éch. |
| Solo F1 (GuitarSet, 50 ms) | 0,72 | 0,72-0,79 (plafond réaliste ; 0,85 n'a pas de mécanisme) | non mesuré |
| Comp F1 | 0,16 | 0,55 → 0,65-0,75 | non mesuré |
| 2-3 notes | 0 | ≈ 85-90 % des notes | « quatre notes à la fois » (forums) |
| 4-6 notes | 0 | ≈ 70-80 %, voix intérieures/doublures d'octave manquées | idem |
| Strums < 10 ms | 0 | une attaque, notes égrenées sur 15-40 ms | point faible connu |

Incertitude : ±0,1 sur les F1, ±5 ms sur les latences ; tous les chiffres iPhone et Windows sont conditionnels à vos relevés.

## 8. Risques et plans B

- **Séparation à un seul micro** : Barbancho 2012 rate 24-46 % des notes d'accords à 4 notes avec 300 ms d'audio ; Klapuri 15-18 % à 4 voix. → Promettre 2-3 notes, réduire le périmètre si besoin.
- **Fantômes octave/quinte** structurels → parcimonie, gabarits par corde, test impair/pair ; accepter de courtes notes faibles résiduelles.
- **Gabarits périssables** (cordes, accordage, micro) → recalibration rapide d'1 min, date affichée, décalage accordeur.
- **Écart GuitarSet / votre DI** → vos enregistrements obligatoires avant le jalon 6.
- **CPU thread audio** (JS 2,2-2,7× plus lent que le main thread, cause inconnue) → WASM conditionnel, porte p99, iPhone en MONO par défaut.
- **Helper natif jamais exécuté** (pas de Windows/macOS ici), SmartScreen/Gatekeeper, gain dépendant de l'interface (RME 5,3 ms, Forte 11,8 ms) → optionnel, hors chemin critique.
- **Étape neuronale** : aucun modèle publié n'est polyphonique et causal < 100 ms ; entraînement CPU ≈ 3 jours/run contre 2 h max par tâche de fond → GPU loué + reprise sur checkpoint, ou ne pas lancer.
- **Licences** : SDK ASIO libre seulement sous GPLv3 (dépôt séparé, JAMRACK reste MIT) ; IDMT/SynthTab/GAPS non commerciaux exclus de tout livrable.

## 9. Ce qui reste non vérifié

- Aller-retour Chrome sur **Windows** et **iOS** (débit wasm SIMD Safari, marge AudioWorklet, bugs de dropouts iOS) — à lire sur votre carte.
- Lag d'onset GuitarSet (~12 ms) mesuré sur 52 attaques d'un joueur.
- Effet réel des fenêtres asymétriques : la table de latence doit être **remesurée** sur `plucks.mjs`, pas déduite.
- Coût des 6 suivis de voix + 2-3 FFT par hop en JS sur le thread audio (extrapolé des noyaux mesurés).
- Algorithme et latence actuels de **MIDI Guitar 3** : seul un brevet abandonné (priorité 2009), une mesure de 2016 et un post de forum (25 ms) existent ; « aussi bon que MG3 » ne se vérifie que par votre protocole loopback.
- `ws://127.0.0.1` depuis l'origine https de gh-pages ; exécution d'asio-sys compilé par clang-cl ; MIME `.wasm` sur GitHub Pages.
- Valeurs par défaut d'inharmonicité B par corde (ordres de grandeur de la littérature, à mesurer à la calibration).