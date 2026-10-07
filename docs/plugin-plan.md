# Plan d'exécution : plugin VST3 « JAMRACK Guitar → MIDI » (Windows, tout gratuit)

Plan du 7 octobre 2026, écrit pour être exécuté par une session Claude Code locale
sur le PC du propriétaire (`D:\Jamrack`, mot-clé `ultracode`), un jalon à la fois.
Il découle de l'étude `docs/daw-plugin-feasibility.md` (sources et chiffres) et de
sa note `docs/daw-plugin-research/portability.md` (lecture du code à porter, référencée
ci-dessous comme « note de portabilité »). Aucune ligne de C++ n'existe encore.

## 0. Décisions du propriétaire (figées le 7 octobre 2026)

| Sujet | Décision |
|---|---|
| Format | **VST3 seulement** en v1. Pas d'AU, pas d'AAX, pas de CLAP pour l'instant (CLAP = option future, section 7). |
| Plateforme | **Windows seulement**. Pas de Mac, pas de testeur Mac, pas de signature Apple. |
| Argent | **0 EUR** : Visual Studio Community, JUCE licence *Starter*, pluginval, REAPER en évaluation, GitHub Actions sur dépôt public. Binaire Windows **non signé** (installation par copie dans un dossier, pas d'installateur `.exe`). |
| Framework | **JUCE 8.x** (dernière version 8.0.x publiée, tag à relever sur github.com/juce-framework/JUCE/tags), récupéré par CMake `FetchContent`, **jamais copié dans le dépôt**. Sources du plugin en **MIT** (licence du dépôt) ; avis JUCE reproduit dans `plugin/README.md` (toute personne qui recompile accepte le contrat JUCE pour sa copie). |
| Emplacement | Dossier **`plugin/`** du dépôt `jamrack`, branche **`feature/plugin`**, PR vers `main` à la fin de chaque jalon validé par le propriétaire. L'application web (`js/`, `index.html`) reste intouchée : zéro dépendance, zéro build. |
| Moteur de référence | **Le JavaScript reste canonique.** Le C++ lui est tenu par l'oracle de la section 4 ; aucune amélioration d'algorithme ne se fait côté C++ seul. |
| Hôtes | **Ableton Live 12** (celui du propriétaire) et **REAPER** (évaluation gratuite, hôte le plus permissif pour le MIDI sortant, sert de contre-épreuve). Tout autre DAW : « compile, non testé ». |
| Périmètre v1 | MONO fiable, POLY **bêta** (même réglage que la page : clairsemé τ 1e-3, 8 itérations, mode ÉCO), bend global en MONO, pas de bend par note, pas de calibration dans le plugin (profil générique embarqué ; un profil fait dans le navigateur pourra être importé plus tard, section 7). |

## 1. Prérequis sur le PC (une fois, 30 minutes hors téléchargements)

1. **Visual Studio 2022 Community** (gratuit) avec la charge de travail « Développement Desktop en C++ » : inclut MSVC v143, le SDK Windows, **CMake et Ninja**. Vérifier dans « Developer PowerShell for VS 2022 » : `cmake --version` (≥ 3.22) et `cl` (répond avec sa version).
2. **Git, Node 22, Python 3.11** : déjà présents (`docs/local-setup.md`). `mir_eval` installé, GuitarSet complet dans `D:\guitarset`.
3. **pluginval** (gratuit, Tracktion) : télécharger le binaire Windows depuis les *releases* GitHub de `Tracktion/pluginval` dans `D:\tools\pluginval\pluginval.exe`.
4. **REAPER** (évaluation complète, gratuite 60 jours puis rappel) : installer, Options → Preferences → Plug-ins → VST : ajouter `D:\VST3` au chemin, re-scanner.
5. **Dossier de plugins** : créer `D:\VST3`. Dans Live 12 : Options → Preferences → Plug-Ins → **Use VST3 Plug-In Custom Folder = On**, dossier `D:\VST3`, puis *Rescan*. Aucun droit administrateur nécessaire (on évite `C:\Program Files\Common Files\VST3`).
6. **Gigcaster 5** : réglage validé le 5 octobre (`docs/guitar-to-midi.md`, « Interfaces de streaming ») : mode **MTK-STREAM**, **MIX MINUS = ON**, canal **GUITAR brut**, effets du canal éteints, tampon du pilote GCS-5 au cran qui ne craque pas. Live : Preferences → Audio → pilote **ASIO « GCS-5 »**, tampon 128 pour commencer.
7. **Disque** : ~3 Go pour Visual Studio, ~1,5 Go pour JUCE + build.

## 2. Architecture

```
plugin/
  CMakeLists.txt               projet "jamrack-plugin", C++17, FetchContent JUCE 8.0.x
  README.md                    construire, installer, tester ; avis de licence JUCE ; table de latence mesurée
  dsp/                         bibliothèque statique SANS JUCE (portable, testable seule)
    include/jamrack/mono_tracker.h       port de js/audio/guitar/tracker.js (classe GuitarTracker, struct Tuning)
    include/jamrack/poly/fft.h resample.h bank.h profile.h nmf.h notes.h engine.h   port de js/audio/guitar/poly/*.js
    include/jamrack/events.h             struct Event { type on/off/bend/meter ; midi ; vel ; semis ; sampleOffset ; ... } + FIFO fixe
    src/...
    src/profile_data.h          profil générique, GÉNÉRÉ par plugin/tools/gen-profile-header.mjs depuis js/audio/guitar/poly/profile.js (committé, vérifié par la CI)
    tests/                      exécutables CTest sans framework de test (asserts) : réponses fermées (biquad, YIN sur sinus, FFT vs DFT, noyau du rééchantillonneur, normes de la banque)
  tools/
    dump_events.cpp             CLI : WAV 16 bits (lecteur identique à test/dump-events.mjs) → JSON au format de test/dump-events.mjs
                                options : --mode mono|poly  --block 128  --stamp block|sample  --dense  --align live  --tuning '{...}'  --decomp '{...}'  --rule '{...}'
                                imprime le coût par hop (moyenne, p99, max) comme test/poly-dump-events.mjs
  src/                          coque JUCE : PluginProcessor.{h,cpp}, PluginEditor.{h,cpp} (éditeur générique jusqu'à M3), Params.h, Sounding.h
.github/workflows/plugin.yml   Windows : configure + build Release + ctest + pluginval + équivalence synthétique, artefact .vst3 zippé
                               Linux : build dsp + tools avec clang -fsanitize=address,undefined, équivalence synthétique
test/
  render-plucks.mjs            NOUVEAU : rend le corpus synthétique de test/plucks.mjs en WAV 16 bits (dossier temporaire) + dump JS attendu
  diff-events.mjs              NOUVEAU : diff note pour note de deux JSON dump-events (sections 4.3)
```

Règles : `dsp/` ne dépend d'aucun en-tête JUCE (il doit compiler avec `cmake -DJAMRACK_DSP_ONLY=ON` sans télécharger JUCE) ; les commentaires de code en anglais, la documentation en français ; `plugin/build*/` et `D:\VST3` dans `.gitignore` ; aucun binaire committé ; publication par **GitHub Releases** (zip du dossier `.vst3` + checksum + `plugin/README.md`).

Identité du plugin (`juce_add_plugin`) : `PRODUCT_NAME "JAMRACK Guitar MIDI"`, `COMPANY_NAME "JAMRACK"`, `PLUGIN_MANUFACTURER_CODE Jamr`, `PLUGIN_CODE Gtm1`, `FORMATS VST3`, `IS_SYNTH FALSE`, `NEEDS_MIDI_INPUT FALSE`, `NEEDS_MIDI_OUTPUT TRUE`, `IS_MIDI_EFFECT FALSE`, `VST3_CATEGORIES "Fx" "Tools"` (jamais « Analyzer », non insérable), `COPY_PLUGIN_AFTER_BUILD TRUE`, `VST3_COPY_DIR "D:/VST3"` (surchargeable par `-DJAMRACK_VST3_DIR=`).

## 3. Contrats

### 3.1 Paramètres (ids stables, `AudioProcessorValueTreeState`)

| id | Libellé | Plage / défaut | Effet |
|---|---|---|---|
| `gain` | GAIN | 0,1..10 / 1 | multiplie le bloc d'entrée avant les moteurs |
| `sens` | SENS | 0..1 / 0,5 | MONO : porte −36..−60 dBFS ; sans effet en POLY |
| `decay` | DECAY | 0..1 / 0,5 | MONO : note-off 15..45 dB sous la crête ; POLY fixe (20 dB) |
| `dyn` | DYN | 0..1 / 0,7 | loi de vélocité MONO |
| `bend` | BEND | on/off / on | MONO : bend continu ou redéclenchement chromatique |
| `range` | BEND RANGE | 2 / 12 / 24 / 48 demi-tons / 2 | conversion du bend en molette 14 bits : `8192 + round(semis / range × 8192)` borné 0..16383 ; l'instrument récepteur doit avoir la même plage |
| `octave` | OCTAVE | −2..2 / 0 | décalage ×12 |
| `transpose` | TRANSPOSE | −12..12 / 0 | décalage |
| `mode` | MODE | mono / poly / mono | bascule de moteur avec *flush* ; libellé « POLY (bêta) » ; **absent jusqu'à M3** |
| `channel` | MIDI CH | 1..16 / 1 | canal de sortie (Live fusionne de toute façon) |
| bypass | (paramètre de bypass JUCE) | — | *flush* + audio inchangé |

Entrée : bus mono ou stéréo ; le moteur lit le **canal 0** ; l'audio ressort **inchangé** (passe-tout) pour que la piste guitare continue de sonner. Tous les paramètres sont lus sur le fil audio par `std::atomic` / `getRawParameterValue`, jamais par verrou. `prepareToPlay(sr, block)` reconstruit les moteurs à la fréquence de l'hôte (44,1 / 48 / 96 kHz acceptés : MONO décime par `round(sr/24000)`, POLY rééchantillonne à 24 000 Hz exactement, note de portabilité § 2.2) et *flush*.

### 3.2 MIDI sortant

- **Note-on** : `midi` borné 0..127 après décalage (MONO ne borne pas aujourd'hui, la coque le fait), vélocité `clamp(round(v × 127), 1, 127)`.
- **Note-off** : toujours explicite. La coque tient un ensemble **`sounding`** (comme `js/input/guitar.js`) et, en MONO, relâche tout ce qui sonne si un note-off nomme une hauteur inconnue (changement de décalage en cours de note).
- **Pitch bend** (MONO seulement) : molette 14 bits selon `range`, remise à 8192 à chaque note-on et note-off, pas de MPE.
- **Flush** (note-off de tout `sounding` + bend à 8192) : à `prepareToPlay`, `reset`, `releaseResources`, passage en bypass, changement de `mode`, de `octave`/`transpose` en cours de note, et changement de profil. **Pas** de flush sur stop du transport : le plugin suit la guitare, pas la lecture.
- **Horodatage** : MONO décide dans `_frame` tous les 64 échantillons décimés ; le port ajoute un compteur d'échantillons et horodate l'événement à l'échantillon du bloc hôte où la décision tombe (`--stamp sample`), ou à la fin du bloc de 128 (`--stamp block`, ce que fait le JS, pour l'oracle). POLY horodate à l'échantillon d'entrée qui a complété le hop. `MidiBuffer::addEvent(msg, sampleOffset)`.
- **Latence déclarée : 0** (`setLatencySamples(0)`). La détection (9-36 ms) n'est pas un retard compensable ; la latence mesurée s'affiche dans l'interface (`lastLatMs` du moteur MONO) et dans `plugin/README.md` par taille de tampon.
- **Mesures** (niveau, accordeur, CPU, ÉCO, voix) : fil audio → interface par `juce::AbstractFifo`, jamais en MIDI.

### 3.3 Règles temps réel (revue obligatoire avant chaque PR)

1. Dans `processBlock` : zéro allocation, zéro verrou, zéro `std::function`, zéro `sort` dynamique ; `juce::ScopedNoDenormals` en tête.
2. Les trois `Array.from().sort()` par hop de `nmf.js` deviennent des tableaux d'indices préalloués + `std::partial_sort`, puis **remise en ordre croissant conservée** (elle fixe l'ordre de sommation, nécessaire à l'équivalence).
3. Les `Map` de voix (`notes.js`) et `_sent` (`engine.js`) deviennent des tableaux fixes (44 hauteurs, 6 voix, ordre d'insertion conservé).
4. Arithmétique **double** partout, stockage **float** exactement là où le JS a des `Float32Array` (MONO : `rmsHist`, `hfHist`, `midiHist`, `ring`, `win`, `d`, `cmnd` ; POLY : banque `W` et sa copie creuse). Un port tout-`float` est interdit : il dérive par le warm start de la NMF (note de portabilité § 2.1).
5. Banque POLY (122 × 1025 float32, ~50 ms à rendre) : rendue sur un fil de fond (`juce::Thread`) au chargement et au changement de profil, échangée par pointeur atomique, ancienne banque libérée sur le fil des messages, *flush* après l'échange. Jamais dans le rappel audio.
6. Budget ÉCO : le contrôleur compare le coût du hop à `min(2,67 ms, durée du tampon hôte)` (un tampon de 64 à 48 kHz donne 1,33 ms), pas au hop seul. Si la mesure M3 montre que le pire cas ne tient pas à 128, le décomposeur passe sur un fil de travail temps réel avec +1 hop de latence (décision prise sur mesure, pas avant).
7. Lectures hors bornes : silencieuses en JS, indéfinies en C++. Une passe **ASan** (MSVC `/fsanitize=address`) sur le corpus synthétique à chaque jalon ; **UBSan + ASan** (clang) dans le job Linux de la CI.
8. `Tuning`, `NoteRule`, `Decomposer` restent des structs modifiables à l'exécution (le harnais en a besoin : mode dense pour l'arithmétique exacte).

## 4. Oracle d'équivalence (le cœur du travail)

### 4.1 Corpus synthétique (sans matériel, à chaque commit)

`test/render-plucks.mjs` rend, avec le générateur déterministe de `test/plucks.mjs`, les signaux des tests existants (plucks par corde, bruit, coup, repick, legato, bend, chromatique, décroissance, accords et strums, 44,1 / 48 / 96 kHz) en WAV 16 bits dans un dossier temporaire, et écrit à côté le dump JS attendu (`dump-events.mjs` et `poly-dump-events.mjs --align live`, horodatage fin de bloc 128). La CI et `ctest` exécutent ensuite `tools/dump_events` sur les mêmes WAV et `test/diff-events.mjs` compare. Rien n'est committé en WAV.

### 4.2 Référence GuitarSet (sur le PC du propriétaire, avant chaque PR)

1. Dumps JS, à régénérer une fois (non versionnés, 4,3 Go de données) : `node test/dump-events.mjs --guitarset D:\guitarset --set solo --out D:\poly-out\js-mono-solo.json` (idem `comp`) ; `node test/poly-dump-events.mjs --guitarset D:\guitarset --set solo --align live --out D:\poly-out\js-poly-solo.json` (idem `comp`) ; et le mode dense : `DECOMP='{"sparse":0,"iter":15}'` (dans PowerShell : `$env:DECOMP='{"sparse":0,"iter":15}'`).
2. **Établir d'abord la barre** : `node test/diff-events.mjs js-poly-dense-solo.json js-poly-solo.json` donne l'écart JS dense ↔ JS livré (attendu : quelques notes sur plusieurs milliers, onsets à ± 1 hop). Ce chiffre, consigné dans `plugin/README.md`, devient la barre d'acceptation du C++.
3. Dumps C++ : `plugin\build\tools\dump_events.exe --mode poly --align live --guitarset D:\guitarset --set solo --out D:\poly-out\cpp-poly-solo.json` (même format, mêmes options).
4. Score : `python test/score.py --guitarset D:\guitarset D:\poly-out\cpp-poly-solo.json` → F1 à comparer au JS (écart ≤ 0,5 point).
5. Six jeux (M3) : les mélanges de `test/poly/make_mixes.py` (hex compris, présents dans `D:\guitarset`) comme dans `docs/poly-implementation.md` ; 13 624 notes.

### 4.3 `test/diff-events.mjs` (≈ 80 lignes, écrit en M1)

Entrée : deux JSON dump-events. Pour chaque prise, apparie les notes par `midi` et onset à ± `--hop` (défaut 0,002667 s ; ± 1 bloc de 128 échantillons, soit 0,00267 s à 48 kHz, pour MONO) ; compte **identiques**, **décalées** (onset ou offset à ± 1 hop), **manquantes**, **en trop**, et les écarts de vélocité (`--vel`, défaut 1/127). Sortie : tableau par prise et total ; code de retour 1 si manquantes + en trop > `--max-missing` ou décalées > `--max-shifted` (en nombre ou en %). Sert aussi bien au JS ↔ JS qu'au JS ↔ C++.

### 4.4 Barres d'acceptation

| Comparaison | Barre |
|---|---|
| C++ MONO ↔ JS MONO, corpus synthétique et GuitarSet 36 prises | mêmes notes (0 manquante, 0 en trop), onsets/offsets à ± 1 bloc, vélocité à ± 1/127 ; `why` identique sur ≥ 99 % |
| C++ POLY dense ↔ JS POLY dense | 0 manquante, 0 en trop ; décalées ≤ 0,5 % (ulp de `libm`) |
| C++ POLY livré ↔ JS POLY livré | écart ≤ la barre JS dense ↔ JS livré établie en 4.2 |
| Scores `score.py` | F1 ± 50 ms à ± 0,5 point du JS sur chaque jeu |
| Coût (`dump_events` sur `00_BN1-129-Eb_solo_mix.wav`) | POLY livré : moyenne ≤ 1,1 ms/hop, max à imprimer ; MONO : ≤ 15 % d'un cœur (le JS fait 13,6 %) ; chiffres JS à relever sur le même PC le même jour |

## 5. Jalons

« Session » = une session d'agent locale ; « soirée » = essai du propriétaire au Gigcaster, avec une check-list fournie par la session et un compte rendu qui nourrit la session suivante (un agent n'entend pas la guitare). Branche `feature/plugin` ; une PR vers `main` par jalon validé.

### M0 — Prototype jetable qui mesure Live (1 session, 1 soirée)

But : répondre par la mesure aux inconnues de l'étude avant d'écrire du DSP.

Tâches :
1. `plugin/CMakeLists.txt` + JUCE `FetchContent` ; **deux cibles** construites depuis la même source : `JAMRACK GTM Fx` (effet audio, `IS_SYNTH FALSE`) et `JAMRACK GTM Inst` (`IS_SYNTH TRUE` avec un bus d'entrée side-chain) — pour trancher par l'essai le type de piste que Live accepte pour le routage *MIDI From* (l'article Ableton parle de piste MIDI, Jam Origin pose son VST sur la piste audio ; non vérifié).
2. Processeur minimal : passe-tout audio ; détecteur d'attaque rudimentaire (enveloppe RMS, seuil −30 dBFS, réarmement sous −40 dBFS) qui émet **note-on 60 vélocité 100 à l'échantillon de l'attaque**, note-off 150 ms plus tard, et une **rampe de pitch bend** 8192 → 12288 → 8192 sur 300 ms après chaque note (test du bend VST3 dans Live 12.4) ; paramètre `channel`.
3. Éditeur générique ; `COPY_PLUGIN_AFTER_BUILD` vers `D:\VST3` ; `README.md` avec la procédure Live (section 2.2 de l'étude) et la check-list ci-dessous.
4. `pluginval --strictness-level 5 --validate D:\VST3\JAMRACK_GTM_Fx.vst3` propre ; CI Windows qui construit et valide.

Soirée (check-list, tout à noter) :
- REAPER : piste audio guitare, plugin inséré, « Record: output (MIDI) » armée → les notes arrivent-elles ? (contre-épreuve : si oui, le plugin émet bien).
- Live 12 : (a) piste audio + `GTM Fx`, piste MIDI « Synthé » avec *MIDI From* = Guitare → `JAMRACK GTM Fx`, Monitor In : notes reçues ? bend entendu sur l'instrument (plage 2) ? (b) même chose avec `GTM Inst` sur une piste MIDI et le side-chain = piste Guitare. Noter laquelle des deux Live liste dans *MIDI From*.
- Armement : avec Guitare en *Auto*, armer Synthé coupe-t-il l'entrée ? (attendu : oui → Monitor In ou Ctrl-clic).
- Latence pick → note : enregistrer simultanément la guitare (piste audio) et la sortie de l'instrument déclenché (piste audio « resample ») à tampon **64, 128, 256** ; mesurer dans l'arrangement l'écart entre le transitoire de la guitare et le début du son ; faire la même mesure sur la page web (`jamrack.openmindlab.fr`, réglage identique) le même soir.
- Décalage d'un clip MIDI enregistré sur « Synthé » par rapport au clic du métronome.

Acceptation : les cinq réponses consignées dans `plugin/README.md` (section « Mesures M0 »). Le code de M0 est jeté ensuite, sauf le CMake et la CI.

### M1 — Port MONO + oracle (1 à 2 sessions, 0 soirée)

Tâches :
1. `dsp/mono_tracker` : port ligne à ligne de `tracker.js` (550 lignes) avec les points de rondeur float32 de la règle 3.3-4, `Tuning` modifiable, FIFO d'événements avec offset d'échantillon, `flush`, `setParams` ; `onFrame` conservé comme pointeur de fonction optionnel (traçage).
2. `tools/dump_events.cpp` (mode mono) ; `test/render-plucks.mjs` ; `test/diff-events.mjs`.
3. Tests CTest fermés : biquads (réponse à 1 kHz à 1e-9 du JS), YIN sur sinus pur (période exacte), décimation ; puis corpus synthétique diff = 0.
4. ASan sur le corpus ; job Linux ASan + UBSan.
5. GuitarSet 36 prises : diff et `score.py`, chiffres dans `plugin/README.md`.

Acceptation : `ctest` vert ; `node test/diff-events.mjs js-mono-solo.json cpp-mono-solo.json` → 0 manquante, 0 en trop, décalées ≤ 1 % ; `score.py` à ± 0,5 point ; sanitizers propres.

### M2 — Coque VST3 MONO utilisable dans Live (2 à 3 sessions, 2 soirées)

Tâches :
1. Processeur réel : moteur MONO, paramètres de 3.1 (sans `mode`), `sounding`, *flush* selon 3.2, `ScopedNoDenormals`, latence 0, FIFO de mesures ; éditeur générique + une étiquette texte (note / cents / niveau / latence mesurée / CPU).
2. État sauvegardé/rechargé (`getStateInformation`), test automatisé de rappel d'état ; pluginval 5 ; CI publie l'artefact zip.
3. `plugin/README.md` : installation (dézipper dans `D:\VST3`, rescan), procédure Live pas à pas, procédure REAPER, table de latence à remplir, limites connues.
4. Release GitHub `plugin-v0.1.0-mono` (zip + checksum).

Soirées : (1) Live 12 de bout en bout avec un instrument Live et un Instrument Rack, comparaison de « sensation » avec la page web, latence à 64/128/256 par la méthode M0, bend, note-off ; (2) après corrections, même parcours + REAPER, enregistrement d'un clip MIDI calé sur le métronome.

Acceptation : le propriétaire écrit « MONO utilisable dans Live » ; table de latence remplie ; pluginval 5 ; PR fusionnée.

### M3 — Port POLY (2 à 3 sessions, 1 à 2 soirées)

Tâches :
1. `plugin/tools/gen-profile-header.mjs` → `dsp/src/profile_data.h` (CI vérifie qu'il est à jour).
2. Port des sept fichiers (`fft`, `resample`, `bank`, `profile`, `nmf`, `notes`, `engine`, 1 084 lignes) avec les règles 3.3-2/3/4/5 ; mode dense et clairsemé ; `Decomposer`/`NoteRule` modifiables.
3. Tests fermés : FFT vs DFT direct à 1e-9, noyau du rééchantillonneur et passage d'un 1 kHz, normes et pics de la banque, décomposition d'un gabarit seul (les quatre premiers tests de `test/poly-engine.test.mjs`, reproduits tels quels).
4. Oracle : barre JS dense ↔ JS livré (4.2-2), puis C++ dense ↔ JS dense, puis C++ livré ↔ JS livré sur 36 prises, puis les six jeux ; coûts par hop.
5. Coque : paramètre `mode` (« POLY (bêta) »), bascule avec *flush*, banque sur fil de fond, ÉCO selon la règle 3.3-6, mesures CPU/voix/ÉCO dans l'étiquette.
6. Release `plugin-v0.2.0-poly-beta`.

Soirée : POLY dans Live (notes seules, deux notes, accords grattés : attentes de la section 13 du plan POLY, pas plus), tag ÉCO à 64/128/256, CPU de Live affiché, comparaison avec la page web.

Acceptation : barres de 4.4 tenues ; coût ≤ 1,1 ms/hop en moyenne sur le PC du propriétaire ; pas de décrochage audio à 128 dans Live ; le propriétaire écrit « POLY bêta = la page web ».

### Total v1 (Windows, gratuit)

| | Sessions | Soirées | Argent |
|---|---|---|---|
| M0 + M1 + M2 + M3 | **6 à 9** | **4 à 5** | **0 EUR** |

Hypothèses : le C++ est écrit et testé par les agents sur le PC (compilation locale, GuitarSet local) ; les soirées sont le goulot ; « 1 à 2 mois » est une borne haute dictée par les soirées.

## 6. Mode d'emploi pour la session locale

Message à coller dans la conversation locale (`D:\Jamrack`) :

```
ultracode — exécute docs/plugin-plan.md, jalon M0. Branche feature/plugin depuis main.
Respecte les décisions de la section 0, les contrats de la section 3 et les règles temps réel 3.3.
Termine par : plugin/README.md avec la check-list de ma soirée, le build qui copie dans D:\VST3, pluginval propre, et la PR.
```

Puis, après chaque soirée : « voici mon compte rendu M0 : … ; passe au jalon M1 ». Chaque jalon suivant se lance de la même façon (« jalon M1 », « jalon M2 », « jalon M3 »).

Conventions pour les agents : lire `CLAUDE.md` (mémoire du projet) ; ne jamais modifier `js/` pour les besoins du plugin, sauf les deux scripts de test nouveaux ; commiter des points d'étape après `cmake --build` et `ctest` ; messages de commit par heredoc ; mettre à jour `CLAUDE.md` (section « Plugin ») à la fin de chaque jalon ; toute mesure du propriétaire va dans `plugin/README.md` avec la date, le tampon et le pilote.

## 7. Hors v1 (à décider plus tard)

- **Calibration dans le plugin** (port de `calibrate.js` + dialogue, fichiers de profil JSON au format de `profiles.js`) ; en attendant, un profil exporté du navigateur pourra être chargé par un paramètre `profile` (fichier) — à ajouter en M3 si le temps le permet, sinon après.
- **Interface WebView** (carte HTML existante dans `WebBrowserComponent`, 12 langues).
- **CLAP** via `clap-juce-extensions` (compatibilité JUCE 9 non vérifiée ; rester en JUCE 8 si CLAP est voulu).
- **Application autonome + port MIDI virtuel** (loopMIDI ou Windows MIDI Services) : **sans ASIO** (WASAPI exclusif, binaire MIT) ou avec ASIO et binaire GPLv3 annoncé.
- **macOS / AU**, **Linux**, **signature Windows** (SignPath pour l'open source).

## 8. Risques et parades (rappel)

1. Live ne liste pas l'effet dans *MIDI From* → M0 le mesure avec les deux cibles ; repli : cible instrument + side-chain, ou port MIDI virtuel (section 7).
2. Pire cas CPU POLY à tampon 64 → règle 3.3-6, mesuré en M3 ; repli : décomposeur sur fil de travail (+1 hop).
3. Dérive numérique → double + float32 aux mêmes endroits, barres de 4.4 ; UB → sanitizers.
4. Mises à jour de Live / JUCE qui cassent → une session par casse, issue GitHub comme canal.
5. Pitch bend VST3 dans Live 12.4 jamais revérifié → M0 (rampe de bend) ; repli : BEND off (redéclenchement chromatique).
