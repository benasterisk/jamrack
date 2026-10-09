# MidPluck — ta guitare en MIDI dans Ableton Live (VST3, Windows)

**MidPluck** est un effet VST3 gratuit (licence MIT) qui écoute ta guitare et la
transforme en notes MIDI, avec le pitch bend, pour jouer des synthés et enregistrer
du MIDI. Il contient les deux moteurs de la page web JAMRACK, portés en C++ :

- **MONO** (une note à la fois, avec bend) : le moteur par défaut ;
- **POLY (bêta)** : plusieurs notes ; les notes seules sont meilleures qu'en MONO,
  les doubles et les accords restent « au mieux » (même moteur que la page web).

Version 0.1.0, fabricant **OpenMindLab** dans le navigateur de Live. Plan :
[`docs/plugin-plan.md`](../docs/plugin-plan.md) (jalons M1, M2 et M3 réunis).

**Ce qui est prouvé sans DAW** (détail plus bas) : les deux moteurs C++ donnent les
**mêmes notes, au bit près**, que la page web sur les 36 prises GuitarSet et sur
112 signaux de test ; le plugin installé sort exactement ce que donnent les
moteurs ; pluginval niveau 5 et le validateur Steinberg passent. **Ce qui reste à
juger par toi** : le ressenti dans Live, la latence réelle et l'absence de
craquements.

## Installer (une seule fois)

1. Le plugin est déjà installé par le build : `D:\VST3\MidPluck.vst3`.
2. Dans Live : **Ctrl+,** → onglet **Plug-Ins** → *Use VST3 Plug-In Custom Folder* =
   **On** → *Browse* → `D:\VST3` → **Rescan** (si rien n'apparaît : **Alt** + clic sur
   Rescan). MidPluck apparaît dans **Plug-Ins → OpenMindLab**.
3. **Avant tout nouveau build, ferme Live et REAPER** : tant qu'un hôte a chargé le
   plugin, la DLL est verrouillée et la copie échoue.

## Brancher dans Live (2 pistes, le minimum dans Live)

1. Piste **audio** « Guitare » : *Audio From* = ton entrée guitare (canal GUITAR du
   Gigcaster), *Monitor* = **In**, pose **MidPluck** dessus. Le son de la guitare
   continue de passer tel quel.
2. Piste **MIDI** « Synthé » : un instrument (Operator, Wavetable…) avec sa **plage de
   pitch bend = 2** (la même que le réglage BEND RANGE de MidPluck) ; *MIDI From* =
   **Guitare**, puis **MidPluck** dans le menu du dessous ; *Monitor* = **In**.
3. Joue : le synthé suit la guitare. Pour enregistrer le MIDI : arme la piste Synthé
   et enregistre (ou *Capture MIDI*).

Pourquoi deux pistes : une piste audio ne peut pas contenir d'instrument et Live ne
passe pas le MIDI d'un plugin à l'appareil suivant de la même piste (REAPER, lui, le
fait sur une seule piste).

## Les réglages

| Réglage | Plage / défaut | Effet |
|---|---|---|
| GAIN | 0,1..10 / 1 | multiplie l'entrée avant les moteurs (l'audio qui ressort n'est pas touché) |
| SENS | 0..1 / 0,5 | MONO : seuil d'attaque de −36 (0) à −60 dBFS (1) ; sans effet en POLY |
| DECAY | 0..1 / 0,5 | MONO : la note s'arrête 15 (0) à 45 dB (1) sous sa crête ; POLY fixe |
| DYN | 0..1 / 0,7 | MONO : dynamique de la vélocité (0 = toutes les notes à fond) |
| BEND | on / off | MONO : bend continu (on) ou notes chromatiques redéclenchées (off) |
| BEND RANGE | 2 / 12 / 24 / 48 demi-tons | doit être **la même** que la plage de bend du synthé |
| OCTAVE | −2..2 / 0 | décalage par octave |
| TRANSPOSE | −12..12 / 0 | décalage en demi-tons |
| MODE | MONO / POLY (beta) | change de moteur (les notes en cours sont relâchées) |
| MIDI CH | 1..16 / 1 | canal MIDI de sortie |
| BYPASS | on / off | contourne le plugin (les notes en cours sont relâchées) |

Mêmes réglages et mêmes valeurs que la carte GUITARE de la page web.

## La fenêtre du plugin

Une façade de rack dans le style de la carte GUITARE de JAMRACK (vis, boutons ambrés,
voyants, écran ambré), rafraîchie 30 fois par seconde.

- **En haut** : le voyant d'alimentation (turquoise = actif ; un clic = BYPASS), l'écran
  d'état (« Listening », « Note E3 · 41 ms », « 3 voices · ECO », « POLY loading… »
  pendant les ~50 ms où le moteur POLY se construit, « BYPASSED »), le sélecteur
  **MONO | POLY β** et **BYPASS** (voyant rouge quand il est enclenché).
- **ENGINE** (l'écran de gauche) :
  - MONO : la note entendue en grand (noms de la page web : le do du milieu est C4),
    l'aiguille d'accordeur en cents (turquoise à moins de 5 cents), le vumètre,
    **LAT** = latence attaque → note de la dernière note, **NOTES** = notes envoyées ;
  - POLY : le nombre de **voix** et six voyants, **HOP** = coût d'un pas d'analyse
    face à son budget (en ms), le tag **ECO** (ambré quand il s'allume), NOTES.
- **INPUT, NOTES, MIDI OUT** : les réglages du tableau ci-dessus. Bouton : glisser
  verticalement ou molette ; **double-clic = valeur par défaut** (sur le chiffre pour
  OCTAVE, TRANSPOSE, MIDI CH). En POLY, SENS, DECAY, DYN, BEND et BEND RANGE sont
  grisés (POLY ne s'en sert pas) mais restent réglables. Le voyant **NOTE OUT** clignote
  à chaque note envoyée.
- **En bas à droite** : **`MidPluck 0.1.0 (hash)`** — recopie-la dans ton compte rendu.
  Un `+` après le hash signale un build fait avec des modifications non commitées. À sa
  gauche, en rouge, « bad input samples N » si un échantillon invalide (NaN, infini) est
  arrivé d'un appareil placé avant, « dropped N » si des événements ont été perdus
  (doit rester absent).
- La fenêtre se redimensionne par le coin en bas à droite, de 75 % à 200 %, proportions
  fixes ; elle rouvre à la dernière taille tant que Live reste ouvert.
- **Si le synthé reste muet** : si NOTES augmente (et que NOTE OUT clignote), le plugin
  marche et c'est le routage de Live ; s'il ne bouge pas, le plugin n'entend pas la
  guitare (entrée ou niveau de la piste, réglage SENS).

## Check-list de la soirée MidPluck

Préparation (10 min) :
- Gigcaster : mode **MTK-STREAM**, **MIX MINUS = ON**, canal **GUITAR brut**, effets du
  canal éteints (réglage du 5 octobre).
- Live : Settings → Audio → Driver Type **ASIO**, Audio Device **GCS-5**, tampon **128**.
  **Note la fréquence d'échantillonnage** (44,1 ou 48 kHz). *Input Config* : active
  l'entrée mono du canal GUITAR.
- Les deux pistes ci-dessus ; en jouant fort, le vumètre de la piste Guitare monte vers
  **−12 à −6 dB** ; note ce niveau.

**1. MONO dans Live.**
- [ ] Notes justes, une par attaque ? (oui/non)
- [ ] Bend entendu quand tu tires une corde (plage 2 des deux côtés) ? (oui/non)
- [ ] La note s'arrête quand tu étouffes la corde ? (oui/non)
- [ ] Ressenti comparé à la page web (mieux / pareil / moins bien, en une phrase)

**2. Latence pick → son (méthode téléphone).** Téléphone (dictaphone, WAV si possible)
posé entre les cordes et l'enceinte ; dix notes détachées sur le mi aigu, dix sur le mi
grave ; dans **Audacity**, mesure pour chaque note l'écart entre le bruit du médiator et
le début du son du synthé ; note la **médiane**. Son de synthé court et net.
- [ ] Tampon GCS-5 **64** : mi aigu ___ ms / mi grave ___ ms
- [ ] **128** : ___ / ___ ms
- [ ] **256** : ___ / ___ ms
- [ ] **Page web** (`jamrack.openmindlab.fr`, TAMPON minimum, Live fermé) : ___ / ___ ms
- [ ] Craquements audio à 64 ? à 128 ? (oui/non)

**3. POLY (bêta) dans Live** (MODE = POLY (beta)).
- [ ] Notes seules justes ? (oui/non)
- [ ] Deux notes ensemble : reconnues ? (souvent / parfois / rarement)
- [ ] Accord gratté : combien de notes sur combien ? (attendu : « au mieux »)
- [ ] Tag **ECO** dans la fenêtre à 64 / 128 / 256 ? (attendu sur ce PC : **éteint**
  partout à 44,1 et 48 kHz)
- [ ] Craquements en POLY à 64 / 128 ? (oui/non)

**4. Enregistrement.** Métronome allumé, enregistre un clip MIDI sur « Synthé » en jouant
sur les temps.
- [ ] Décalage du clip par rapport au clic : ___ ms (en avance / en retard)

**5. Armement.** Avec la piste Guitare en *Monitor Auto*, armer « Synthé » coupe-t-il la
guitare ?
- [ ] oui / non (attendu : oui → garder *Monitor In* sur Guitare)

**6. (Facultatif) REAPER** : piste 1, entrée guitare, FX : MidPluck puis ReaSynth (une
seule piste suffit dans REAPER).
- [ ] Notes entendues ? (oui/non)

## Modèle de compte rendu (à recopier et remplir)

```
Compte rendu MidPluck — date : ____
Live : 12.__.__ (Help → About Live)
Pilote : GCS-5, tampon ___, fréquence d'échantillonnage ___ kHz
Niveau crête de la piste Guitare en jouant fort : ___ dB
Build : (ligne lue dans la fenêtre du plugin, ex. « MidPluck 0.1.0 (a1b2c3d) »)

1. MONO : notes justes oui/non ; bend oui/non ; note-off à l'étouffé oui/non ;
   ressenti vs page web : ___
2. Latence (médianes, ms) : 64 ___/___ ; 128 ___/___ ; 256 ___/___ ; page web ___/___ ;
   craquements à 64 oui/non, à 128 oui/non
3. POLY : notes seules oui/non ; deux notes souvent/parfois/rarement ; accord ___ sur ___ ;
   ECO à 64/128/256 : ___ ; craquements oui/non
4. Décalage du clip MIDI : ___ ms (avance/retard)
5. Armement : armer Synthé coupe la guitare oui/non
6. REAPER (facultatif) : notes oui/non

Ce qui a bloqué, dans l'ordre :
-

Fichiers joints : (enregistrement téléphone, capture de Settings → Plug-Ins si le plugin
n'apparaît pas, Log.txt du dossier Preferences de Live si Live plante)
```

## Revenir en arrière

1. Fermer Live et REAPER avant tout build (DLL verrouillée).
2. Avant de remplacer un build qui marche, la session le copie dans
   `D:\VST3-archive\<version>-<hash>\` (jamais dans `D:\VST3`, que Live scanne).
3. Si Live plante ou se bloque : fermer Live, supprimer `D:\VST3\MidPluck.vst3`,
   recopier la dernière archive qui marchait, Settings → Plug-Ins → **Alt + Rescan**.
4. Si MidPluck n'apparaît pas : vérifier que
   `D:\VST3\MidPluck.vst3\Contents\x86_64-win\MidPluck.vst3` existe, **Alt + Rescan**,
   puis envoyer le fichier `Log.txt` du dossier Preferences de Live
   (`%AppData%\Ableton\Live 12.x.x\Preferences`).
5. Les prototypes M0 (JAMRACK GTM Fx / Inst) sont rangés dans `D:\VST3-archive\m0\`.

## Ce qui est mesuré (9 octobre 2026, sans DAW)

Équivalence C++ ↔ JavaScript (le JavaScript reste le moteur de référence) :

| Comparaison | Résultat |
|---|---|
| MONO, GuitarSet 36 prises (24 solo + 12 comp), 44,1 kHz | **3 361 / 3 361 notes identiques au bit près** (aussi à 48 et 96 kHz, et avec des réglages non par défaut) |
| MONO, trames internes | 2 566 383 trames et 605 027 événements comparés octet par octet : 0 différence |
| POLY réglage livré, GuitarSet 36 prises | **4 141 / 4 141 notes identiques** |
| POLY dense (arithmétique du prototype) | 4 174 / 4 174 identiques ; état interne identique sur 410 056 pas d'analyse |
| Corpus synthétique (`test/render-plucks.mjs`, 112 signaux, MONO 44,1/48/96 kHz, POLY) | 673 / 673 notes identiques |
| Scores `test/score.py` (F1 ± 50 ms) | MONO solo 71,8 / comp 9,3 ; POLY solo 80,6 / comp 47,8 : **écart C++ ↔ JS 0,0 point** |

Pour l'identité au bit près, le C++ reprend les fonctions mathématiques exactes de V8
(sin, cos, log, log10, log2, hypot), parce que celles de Windows diffèrent d'un
dernier chiffre sur 1,5 à 42 % des valeurs.

Le plugin installé, passé dans un hôte VST3 de test (`jamrack_vst3_probe`) :
- notes MIDI **identiques échantillon pour échantillon** à celles des moteurs seuls, en
  MONO et en POLY, sur 5 prises, à 44,1 / 48 / 96 kHz, tampons de 32 à 2048 (sauf
  96 kHz / 32, où ECO s'allume et rend POLY dépendant de la charge) ;
- audio rendu à l'identique (0 échantillon différent sur plus de 2 millions), latence
  déclarée 0 ;
- 21 scénarios (bypass, changement de mode, octave, canal ou fréquence en pleine
  note) : chaque note en cours est relâchée, aucune note bloquée ;
- un échantillon invalide (NaN à 5 s, infini à 10 s) : **mêmes notes** qu'avec le
  fichier propre ;
- pluginval niveau 5 : SUCCESS (aussi niveau 10 répété) ; validateur Steinberg :
  47 / 47 ; rappel d'état : 11 réglages sur 11 restitués.
- **Live 12.4.6 Trial l'a scanné** au démarrage du 9 octobre (2 h 43) : sa base de
  plugins contient « MidPluck, OpenMindLab, 0.1.0 », classé effet audio
  (`device:vst3:audiofx`, Fx|Tools), activé. Le test joué dans Live n'a pas pu être
  fait cette nuit : la session Windows s'est verrouillée.

Coût (PC du propriétaire) :
- MONO : 1,6 % d'un cœur (la page web : 2,9 %) ;
- POLY : 0,22 ms par pas d'analyse (JS : 0,47 ms), budget 2,67 ms ; le plugin entier
  ~12 % d'un cœur à 44,1 kHz. **ECO ne s'est jamais allumé** à 44,1 et 48 kHz (tampons
  32 à 256). Il peut s'allumer à 96 kHz avec un tampon de 48 ou moins : les notes
  dépendent alors de la charge du PC, comme sur la page web.
- Le pire bloc observé en POLY à 64 échantillons, sur une machine chargée par d'autres
  compilations, a duré 1,77 ms pour 1,45 ms de budget : à surveiller dans Live à 64.

Repère pour POLY : entre les deux réglages du JavaScript (dense et livré), les notes
diffèrent déjà de 4,3 % (solo) et 34 % (comp) ; le C++ livré, lui, ne diffère du JS
livré d'aucune note.

## Construire et vérifier (pour la session)

```powershell
cmake -S plugin -B plugin/build -G "Visual Studio 17 2022" -A x64
cmake --build plugin/build --config Release          # Live et REAPER fermés : copie dans D:\VST3
ctest --test-dir plugin/build -C Release              # 10 tests des moteurs + oracle synthétique (~3 min)
D:\tools\pluginval\pluginval.exe --strictness-level 5 --validate "D:\VST3\MidPluck.vst3"
D:\tools\vst3sdk\build\bin\Release\validator.exe "D:\VST3\MidPluck.vst3"
```

- Moteurs seuls, sans JUCE : `-DJAMRACK_DSP_ONLY=ON` ; ASan : `-DJAMRACK_ASAN=ON`
  (RelWithDebInfo, jamais Debug).
- `plugin\build\tools\Release\dump_events.exe --mode mono|poly …` : même JSON que
  `test/dump-events.mjs` / `test/poly-dump-events.mjs` ; `node test/diff-events.mjs
  --hop <s> js.json cpp.json` compare note pour note ; références GuitarSet dans
  `D:\poly-out`.
- `node plugin/tools/gen-profile-header.mjs --check` : le profil POLY embarqué est à jour.
- `plugin\build\midpluck_ui_snapshot_artefacts\Release\midpluck_ui_snapshot.exe <dossier>` :
  dessine la fenêtre du plugin en PNG dans chaque état (MONO au repos et en jeu, POLY,
  POLY loading, ECO, BYPASSED, alertes) à 100 % et 150 %, et redimensionnée à 75 % et
  200 %, sans DAW.
- Code : `dsp/` (moteurs sans JUCE), `src/` (le plugin ; `src/ui/` : l'apparence
  JAMRACK), `resources/fonts/` (les trois polices de la page web), `tools/`
  (dump_events, l'hôte de test, le générateur de profil, les captures de la fenêtre).

## Historique : le jalon M0 (7-8 octobre 2026)

Deux prototypes jetables ont mesuré ce que Live accepte avant d'écrire le vrai plugin :
- **REAPER, 7 octobre** (PC portable, micro intégré) : notes, bend et enregistrement MIDI
  d'un effet VST3 : OK.
- **Live 12.4.6, 8 octobre** : un **effet** posé sur la piste guitare est accepté dans
  *MIDI From* et le bend passe → MidPluck est un effet. L'instrument à side-chain
  (3 pistes) a été abandonné. Live refuse tout instrument VST3 sans entrée MIDI, ce que
  ni pluginval ni le validateur Steinberg ne signalent.
- Restent de M0 : l'armement et la latence (points 2 et 5 de la check-list ci-dessus).

Le détail (détecteur d'attaque du prototype, banc `plugin/m0/check_probe.py`, mesures
sur GuitarSet) est dans l'historique git de ce fichier (commit `8f688c0`) et dans
`plugin/m0/`.

## Avis de licence

- Sources de MidPluck : **MIT** (licence du dépôt `jamrack`).
- **JUCE 8.0.15**, sous licence *Starter* (gratuite sous 20 000 USD de revenus annuels,
  dons compris) ; JUCE est téléchargé par CMake au moment du build et n'est pas copié
  dans le dépôt ; toute personne qui recompile accepte le contrat JUCE pour sa copie ;
  les notices de copyright de JUCE sont conservées.
- **SDK VST3** tel qu'embarqué par JUCE 8.0.15
  (`modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt`) : MIT,
  « Copyright (c) 2025, Steinberg Media Technologies GmbH ».
- **Polices** de la fenêtre (Unbounded, Barlow Condensed, Spline Sans Mono, embarquées
  dans le plugin) : **SIL Open Font License 1.1** ; licences et provenance dans
  `plugin/resources/fonts/` (un `OFL.txt` par famille, `README.md`).
- **fdlibm** (fonctions mathématiques reprises de V8, `plugin/dsp/src/poly/v8math.cpp`
  et `mono_tracker.cpp`) : « Copyright (C) 1993 by Sun Microsystems, Inc. All rights
  reserved. Permission to use, copy, modify, and distribute this software is freely
  granted, provided that this notice is preserved. »
- VST is a trademark of Steinberg Media Technologies GmbH.
