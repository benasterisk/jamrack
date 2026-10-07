# Plan d'exécution : plugin VST3 « JAMRACK Guitar → MIDI » (Windows, tout gratuit)

Plan du 7 octobre 2026, écrit pour être exécuté par une session Claude Code locale
sur le PC du propriétaire (`D:\Jamrack`, mot-clé `ultracode`), un jalon à la fois.
Il découle de l'étude `docs/daw-plugin-feasibility.md` (sources et chiffres) et de
sa note `docs/daw-plugin-research/portability.md` (lecture du code à porter, référencée
ci-dessous comme « note de portabilité »). Aucune ligne de C++ n'existe encore.
Révisé le 7 octobre 2026 après relecture adversariale (deux critiques : exécutabilité
et propriétaire) ; les faits d'outillage cités ci-dessous ont été revérifiés ce jour-là
(tags JUCE, générateur CMake de Visual Studio 2026, `getBusInfo` de JUCE 8.0.15,
options de pluginval, `/fsanitize=address` et `/fp` de MSVC).

## 0. Décisions du propriétaire (figées le 7 octobre 2026)

| Sujet | Décision |
|---|---|
| Format | **VST3 seulement** en v1. Pas d'AU, pas d'AAX, pas de CLAP pour l'instant (CLAP = option future, section 7). |
| Plateforme | **Windows seulement**. Pas de Mac, pas de testeur Mac, pas de signature Apple. |
| Argent | **0 EUR** : Visual Studio Community, JUCE licence *Starter*, pluginval, REAPER en évaluation (**60 jours** d'évaluation complète, puis la licence demande 60 USD, hors budget : les soirées M0 et M2 doivent tenir dans ces 60 jours, sinon REAPER est abandonné comme contre-épreuve et Live suffit), GitHub Actions sur dépôt public. Binaire Windows **non signé** (installation par copie dans un dossier, pas d'installateur `.exe`). |
| Framework | **JUCE 8.0.15** (dernier 8.0.x, 21 juillet 2026 ; JUCE 9.0.0 à 9.0.3 existent depuis juillet-septembre 2026 mais `clap-juce-extensions` n'y est pas vérifié et la doc CMake y diffère), récupéré par CMake `FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)`, **jamais copié dans le dépôt** ; toute lecture de la doc CMake se fait **au tag 8.0.15** (`docs/CMake API.md`), pas sur `master` (qui est JUCE 9). JUCE ≥ 8.0.11 est requis pour Visual Studio 2026 (exporteur ajouté en décembre 2025). Sources du plugin en **MIT** (licence du dépôt) ; avis de licence énumérés dans `plugin/README.md` (section 2). |
| Emplacement | Dossier **`plugin/`** du dépôt `jamrack`, branche **`feature/plugin`**, PR vers `main` à la fin de chaque jalon validé par le propriétaire. L'application web (`js/`, `index.html`) reste intouchée : zéro dépendance, zéro build. |
| Moteur de référence | **Le JavaScript reste canonique.** Le C++ lui est tenu par l'oracle de la section 4 ; aucune amélioration d'algorithme ne se fait côté C++ seul. |
| Hôtes | **Ableton Live 12** (celui du propriétaire) et **REAPER** (évaluation gratuite, hôte le plus permissif pour le MIDI sortant, sert de contre-épreuve). Tout autre DAW : « compile, non testé ». |
| Périmètre v1 | MONO fiable, POLY **bêta** (même réglage que la page : clairsemé τ 1e-3, 8 itérations, mode ÉCO), bend global en MONO, pas de bend par note, pas de calibration dans le plugin (profil générique embarqué, **aucun paramètre `profile` en v1** ; un profil fait dans le navigateur pourra être importé plus tard, section 7, sur décision du propriétaire). |

## 1. Prérequis sur le PC (une fois, 30 minutes hors téléchargements)

1. **Visual Studio Community 2026** (gratuit ; c'est la version que télécharge visualstudio.microsoft.com depuis novembre 2025, la 2022 convient aussi si elle est déjà installée) avec la charge de travail « Développement Desktop en C++ » : inclut MSVC v145 (v143 pour 2022), le SDK Windows, CMake et Ninja. Installer **aussi CMake depuis cmake.org** (installateur Windows, cocher « Add CMake to the system PATH for all users ») : la session Claude Code tourne dans un PowerShell ordinaire, où le CMake de Visual Studio n'est **pas** sur le PATH. Vérifier dans ce PowerShell ordinaire : `cmake --version` (≥ 3.22 ; **≥ 4.2 avec VS 2026**, seule version qui connaît le générateur « Visual Studio 18 2026 »). `cl` n'a pas besoin d'être sur le PATH : avec le générateur Visual Studio, CMake trouve MSVC tout seul. Générateur : `-G "Visual Studio 18 2026" -A x64` (2026) ou `-G "Visual Studio 17 2022" -A x64` (2022).
2. **Git, Node 22, Python 3.11** : déjà présents (`docs/local-setup.md`). `mir_eval` installé, GuitarSet complet dans `D:\guitarset`.
3. **pluginval** (gratuit, Tracktion) : télécharger le binaire Windows depuis les *releases* GitHub de `Tracktion/pluginval` dans `D:\tools\pluginval\pluginval.exe` ; vérifier `D:\tools\pluginval\pluginval.exe --version`.
4. **REAPER** : **installer le soir de M0, pas avant** (évaluation complète 60 jours, décision « Argent » de la section 0). Réglage : Options → Preferences → Plug-ins → VST → champ « VST plug-in paths », ajouter `;D:\VST3` à la fin, bouton **Re-scan**.
5. **Dossier de plugins** : créer `D:\VST3` (dossier dédié : ni `C:\Program Files\Common Files\VST3`, ni le dossier VST2, Ableton le demande) et `D:\VST3-archive` (anciens builds, section 5 M2). Dans Live 12 : Options → **Settings** (Ctrl+, ; la fenêtre s'appelait « Preferences » dans Live 11) → onglet **Plug-Ins** → section **Plug-In Sources** → **Use VST3 Plug-In Custom Folder = On**, Browse → `D:\VST3`, puis **Rescan** (bouton en haut de l'onglet) ; si le plugin n'apparaît pas, maintenir **Alt** en cliquant Rescan (rescan profond). Aucun droit administrateur nécessaire.
6. **Gigcaster 5** : réglage validé le 5 octobre (`docs/guitar-to-midi.md`, « Interfaces de streaming ») : mode **MTK-STREAM**, **MIX MINUS = ON**, canal **GUITAR brut**, effets du canal éteints, tampon du pilote GCS-5 au cran qui ne craque pas. Live : Settings → Audio → Driver Type **ASIO**, Audio Device **GCS-5**, bouton *Hardware Setup* pour le tampon (128 pour commencer), noter la fréquence d'échantillonnage affichée (44,1 ou 48 kHz : elle fixe le budget ÉCO, règle 3.3-6), puis **Input Config** : activer l'entrée mono du canal GUITAR (son numéro tel qu'affiché par le pilote en MTK-STREAM, le même que celui choisi dans Chrome le 5 octobre). Sans cette étape la piste guitare ne voit pas le canal.
7. **Disque** : 8 à 10 Go pour Visual Studio (chiffre affiché par l'installateur), ~1,5 Go pour JUCE + build, `D:\VST3-archive` pour les anciens builds.

## 2. Architecture

```
plugin/
  CMakeLists.txt               projet "jamrack-plugin", C++17, FetchContent JUCE 8.0.15 (GIT_SHALLOW)
  README.md                    construire, installer, tester ; avis de licence (ci-dessous) ; table de latence mesurée ;
                               section « Revenir en arrière » ; modèle de compte rendu (section 5)
  dsp/                         bibliothèque statique SANS JUCE (portable, testable seule)
    include/jamrack/mono_tracker.h       port de js/audio/guitar/tracker.js (classe GuitarTracker, struct Tuning)
    include/jamrack/poly/fft.h resample.h bank.h profile.h nmf.h notes.h engine.h   port de js/audio/guitar/poly/*.js
    include/jamrack/events.h             struct Event { type on/off/bend/meter ; midi ; vel ; semis ; sampleOffset ; ... } + FIFO fixe
    src/...
    src/profile_data.h          profil générique, GÉNÉRÉ par plugin/tools/gen-profile-header.mjs depuis js/audio/guitar/poly/profile.js (committé, vérifié par la CI)
    tests/                      exécutables CTest sans framework de test (asserts) : réponses fermées (biquad, YIN sur sinus, FFT vs DFT, noyau du rééchantillonneur, normes de la banque)
  tools/
    dump_events.cpp             CLI : WAV 16 bits (lecteur identique à test/dump-events.mjs : int16 / 32768, premier canal) → JSON au format de test/dump-events.mjs
                                options : --mode mono|poly  --block 128  --stamp block|sample  --dense  --align live  --tuning '{...}'  --decomp '{...}'  --rule '{...}'
                                          <fichier.wav> [sortie.json]  ou  --guitarset <dir>|--mixdir <dir> --set <nom> --takes <fichier> --out <json> [--label <nom>]
                                --takes = liste de prises, UN NOM PAR LIGNE (pas de parseur JSON en C++), produite par
                                  node -e "console.log(require('./test/takes.json').solo.join('\n'))" | Out-File -Encoding ascii D:\poly-out\solo.txt
                                  (le lecteur ignore BOM, CR et lignes vides)
                                sortie JSON identique à dump-events.mjs : label, source, set, align, takes[prise].duration / events[{onset, offset, midi, velocity, why}],
                                nombres imprimés avec 17 chiffres significatifs, vélocités 0..1 non quantifiées ; POLY horodaté (hop + 1) × 64 / 24000 comme
                                poly-dump-events.mjs (le --stamp sample ne concerne que la coque MIDI) ; MONO horodaté fin de bloc 128 (--stamp block, défaut)
                                imprime le coût par hop (moyenne, p99, max) comme test/poly-dump-events.mjs, et en MONO le temps total et le ratio temps / durée
  src/                          coque JUCE : PluginProcessor.{h,cpp}, PluginEditor.{h,cpp} (éditeur générique jusqu'à M3 + étiquette version/hash), Params.h, Sounding.h
.github/workflows/plugin.yml   Windows : configure + build Release + ctest + pluginval + équivalence synthétique, artefact .vst3 zippé
                               Linux : build dsp + tools avec clang -fsanitize=address,undefined -fno-sanitize-recover=all, équivalence synthétique
test/
  render-plucks.mjs            NOUVEAU : corpus synthétique (liste de recettes propre, section 4.1) → WAV 16 bits (dossier temporaire) + dump JS attendu calculé sur le WAV relu
  diff-events.mjs              NOUVEAU : diff note pour note de deux JSON dump-events (section 4.3)
```

Règles : `dsp/` ne dépend d'aucun en-tête JUCE (il doit compiler avec `cmake -DJAMRACK_DSP_ONLY=ON` sans télécharger JUCE) ; les commentaires de code en anglais, la documentation en français ; `plugin/build*/` dans `.gitignore` (`D:\VST3` est hors dépôt, rien à ignorer) ; aucun binaire committé ; `ctest` cherche Node par `find_program(NODE node REQUIRED)` ; `COPY_PLUGIN_AFTER_BUILD` est piloté par l'option CMake **`JAMRACK_COPY_AFTER_BUILD`** (ON par défaut sur le PC, OFF dans la CI) ; la CI valide `plugin/build/<cible>_artefacts/Release/VST3/<PRODUCT_NAME>.vst3` (`JUCE_PLUGIN_ARTEFACT_FILE`) avec `pluginval --strictness-level 5 --timeout-ms 60000 --skip-gui-tests --output-dir plugin/build/pluginval` et publie ce dossier `.vst3` zippé ; `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"` (runtime statique : un `.vst3` téléchargé se charge sans redistribuable VC++, sinon Live l'ignore en silence) ; la version (`JucePlugin_VersionString`) et le **hash git court** (`git rev-parse --short HEAD` à la configuration CMake, injecté en `JAMRACK_GIT_HASH`) sont affichés dans l'éditeur, pour que chaque compte rendu nomme le build testé.

Publication par **GitHub Releases** : la session pose le tag `plugin-vX.Y.Z-…` ; le workflow `plugin.yml` construit sur ce tag et attache `JAMRACK-Guitar-MIDI-vX.Y.Z-win64.zip` (dossier `.vst3` + `plugin/README.md`) et `SHA256SUMS.txt` à une Release **brouillon** ; le propriétaire la publie d'un clic sur github.com/benasterisk/jamrack/releases (« Publish release »), ou la session le fait par `gh release create`. Le propriétaire n'a jamais besoin de la Release pour lui-même : le build local copie dans `D:\VST3`.

Avis de licence à reproduire dans `plugin/README.md` : (a) sources du plugin MIT (licence du dépôt) ; (b) JUCE sous licence *Starter* (gratuite sous 20 000 USD de revenus, dons compris ; toute personne qui recompile accepte le contrat JUCE pour sa copie ; notices de copyright JUCE conservées) ; (c) texte de licence du SDK VST3 tel qu'embarqué par JUCE 8.0.15 : `modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt` (MIT, « Copyright (c) 2025, Steinberg Media Technologies GmbH », vérifié au tag) ; (d) « VST is a trademark of Steinberg Media Technologies GmbH ».

Identité du plugin réel (`juce_add_plugin`, M2) : `PRODUCT_NAME "JAMRACK Guitar MIDI"` (le bundle s'appelle donc `JAMRACK Guitar MIDI.vst3`, avec espaces), `COMPANY_NAME "JAMRACK"`, `PLUGIN_MANUFACTURER_CODE Jamr`, `PLUGIN_CODE Gtm1`, `FORMATS VST3`, `IS_SYNTH FALSE`, `NEEDS_MIDI_INPUT FALSE`, `NEEDS_MIDI_OUTPUT TRUE`, `IS_MIDI_EFFECT FALSE`, `VST3_CATEGORIES "Fx" "Tools"` (jamais « Analyzer », non insérable), `COPY_PLUGIN_AFTER_BUILD` selon `JAMRACK_COPY_AFTER_BUILD`, `VST3_COPY_DIR "D:/VST3"` (surchargeable par `-DJAMRACK_VST3_DIR=`). **Chaque `juce_add_plugin` a son propre `PLUGIN_CODE`** : JUCE dérive l'identifiant de classe VST3 des codes fabricant + plugin (`VST3Interface::jucePluginId(manufacturerCode, pluginCode)`, `juce_VST3Interface.h`), deux cibles de même code entrent en collision dans le scan de Live. `Gtm1` est réservé au plugin réel ; les prototypes M0 prennent `Gtm0` et `Gtm9` pour ne jamais le masquer.

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
| `mode` | MODE | mono / poly / mono | bascule de moteur avec *flush* + `reset` (3.2) ; libellé « POLY (bêta) » ; **absent jusqu'à M3** |
| `channel` | MIDI CH | 1..16 / 1 | canal de sortie (Live fusionne de toute façon) |
| `bypass` | BYPASS | on/off / off | `getBypassParameter()` renvoie cet `AudioParameterBool` (sans lui, l'enveloppe VST3 de JUCE crée son propre bypass et appelle `processBlockBypassed`, où le *flush* ne serait pas) ; dans `processBlock`, à la transition vers bypass : *flush* une fois, puis audio inchangé et aucun moteur alimenté ; `processBlockBypassed` non utilisé |

Entrée : bus mono ou stéréo ; le moteur lit le **canal 0** ; l'audio ressort **inchangé** (passe-tout) pour que la piste guitare continue de sonner. Tous les paramètres sont lus sur le fil audio par `std::atomic` / `getRawParameterValue`, jamais par verrou. `prepareToPlay(sr, block)` reconstruit à la fréquence de l'hôte ce qui en dépend (44,1 / 48 / 96 kHz acceptés : MONO décime par `round(sr/24000)` et recrée ses biquads, POLY recrée seulement son rééchantillonneur à 24 000 Hz exactement, note de portabilité § 2.2), vide les histoires, recalcule le budget ÉCO (règle 3.3-6) et *flush*. **`prepareToPlay` ne touche pas à la banque POLY** (rendue à 24 kHz, indépendante du taux hôte) et n'attend jamais le fil de fond.

### 3.2 MIDI sortant

- **Note-on** : `midi` borné 0..127 après décalage (MONO ne borne pas aujourd'hui, la coque le fait), vélocité `clamp(round(v × 127), 1, 127)`.
- **Note-off** : toujours explicite. La coque tient un ensemble **`sounding`** (comme `js/input/guitar.js`) et, en MONO, relâche tout ce qui sonne si un note-off nomme une hauteur inconnue (changement de décalage en cours de note).
- **Pitch bend** (MONO seulement) : molette 14 bits selon `range`, remise à 8192 à chaque note-on et note-off, pas de MPE.
- **Flush** (note-off de tout `sounding` + bend à 8192) : à `prepareToPlay`, `reset`, `releaseResources`, passage en bypass, changement de `mode`, de `octave`/`transpose` en cours de note, et changement de profil. **Pas** de flush sur stop du transport : le plugin suit la guitare, pas la lecture. **Changement de `mode`** : flush du moteur sortant, puis `reset()` du moteur entrant (POLY comme `worklet.js:69` : anneau, histoires, activations, voix ; MONO n'a pas de `reset()` en JS, la page le reconstruit : le port lui en donne un qui remet à zéro `ring`, `rmsHist`, `hfHist`, `midiHist` (à NaN), biquads, bloqueur DC, compteurs et états de note).
- **Horodatage** : MONO décide dans `_frame` tous les 64 échantillons décimés ; le port ajoute un compteur d'échantillons et horodate l'événement à l'échantillon du bloc hôte où la décision tombe (`--stamp sample`), ou à la fin du bloc de 128 (`--stamp block`, ce que fait le JS, pour l'oracle). POLY horodate à l'échantillon d'entrée qui a complété le hop. `MidiBuffer::addEvent(msg, sampleOffset)`.
- **Latence déclarée : 0** (`setLatencySamples(0)`). La détection (9-36 ms) n'est pas un retard compensable ; la latence mesurée s'affiche dans l'interface (`lastLatMs` du moteur MONO) et dans `plugin/README.md` par taille de tampon.
- **Mesures** (niveau, accordeur, CPU, ÉCO, voix) : fil audio → interface par `juce::AbstractFifo`, jamais en MIDI.

### 3.3 Règles temps réel (revue obligatoire avant chaque PR)

1. Dans `processBlock` : zéro allocation, zéro verrou, zéro `std::function`, zéro `sort` dynamique ; `juce::ScopedNoDenormals` en tête.
2. Les trois `Array.from().sort()` par hop de `nmf.js` deviennent des tableaux d'indices préalloués triés **en entier et de façon stable** par (valeur décroissante, **index croissant**) sur ≤ 122 entrées (V8 trie de façon stable : à égalité de `h` ou de `sal`, fréquente — colonnes démarrées au même plancher `1e-4·vmax`, salience nulle en silence —, l'ordre d'index décide de l'ensemble actif retenu à la coupure `nmax`) ; aucune sélection non stable (`std::partial_sort`, `std::nth_element` interdits ici) ; puis **remise en ordre croissant conservée** (elle fixe l'ordre de sommation, nécessaire à l'équivalence).
3. Les `Map` de voix (`notes.js`) et `_sent` (`engine.js`) deviennent des tableaux fixes (44 hauteurs, 6 voix, ordre d'insertion conservé).
4. Arithmétique **double** partout, stockage **float** exactement là où le JS a des `Float32Array` (MONO : `rmsHist`, `hfHist`, `midiHist`, `ring`, `win`, `d`, `cmnd` ; POLY : banque `W` et sa copie creuse). Un port tout-`float` est interdit : il dérive par le warm start de la NMF (note de portabilité § 2.1).
5. Banque POLY (122 × 1025 float32 + copie CSR, ~50 ms à rendre) : rendue sur un `juce::Thread` au chargement et au changement de profil, **avec** son `Decomposer` (CSR, `h` à zéro) et une `NoteRule` neuve, dans un objet `PolyEngineState` construit en entier sur le fil de fond (comme `worklet.js:39-47` reconstruit l'engin POLY entier, pas seulement `W`) ; publié par `std::atomic<PolyEngineState*> next` ; le fil audio, en tête de `processBlock`, fait `swap(current, next)` s'il y a un `next`, émet le *flush* de l'ancien état, puis pousse l'ancien pointeur dans une `AbstractFifo` de retrait que le fil des messages vide (timer 100 ms) pour libérer. **Jamais de `delete` d'un état sans qu'il soit passé par cette file** (un `delete` sur le fil des messages juste après l'échange serait une lecture après libération dans le rappel audio encore dans son `step()`). Jamais de rendu dans le rappel audio.
6. Budget ÉCO : à `prepareToPlay`, `hopIn = 64 × sr / 24000` échantillons d'entrée (117,6 à 44,1 kHz, 128 à 48 kHz), `hopsMax = ceil(blockSize / hopIn)` (nombre de hops qui peuvent se terminer dans un même rappel), `budgetMs = 1000 × blockSize / sr / hopsMax` (48 kHz : 64 → 1,33 ms, 128 → 2,67 ms, 256 → 2,67 ms ; 44,1 kHz : 64 → 1,45 ms, 128 → 1,45 ms, 256 → 1,93 ms). Le contrôleur compare la moyenne exponentielle du coût par hop à ce `budgetMs` (seuils 80 % / 45 %, maintien 375 hops, inchangés par rapport à `engine.js`). Les hôtes qui appellent avec des blocs plus courts que `blockSize` (Live aux points d'automation, pluginval) ne changent pas le budget. Conséquence à connaître : à 44,1 kHz / 128 ou à 48 kHz / 64, le coût moyen livré (1,1-1,6 ms sur le PC du propriétaire) dépasse 80 % du budget et ÉCO reste allumé par construction. Si la mesure M3 montre que le pire cas ne tient pas à 48 kHz / 128, le décomposeur passe sur un fil de travail temps réel avec +1 hop de latence (décision prise sur mesure, pas avant).
7. Lectures hors bornes : silencieuses en JS, indéfinies en C++. Une passe **ASan** sur le corpus synthétique à chaque jalon, en configuration séparée : `cmake -S plugin -B plugin/build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DJAMRACK_DSP_ONLY=ON -DJAMRACK_ASAN=ON` ; `JAMRACK_ASAN` ajoute `/fsanitize=address /Zi` et retire `/RTC1`, `/ZI` et `/INCREMENTAL` (`string(REPLACE "/RTC1" "" ...)`, `/INCREMENTAL:NO`) parce que MSVC les déclare incompatibles avec ASan (doc Microsoft, vérifiée) et que CMake les met par défaut en Debug : **jamais en Debug** ; exécuter depuis le « Developer PowerShell » de Visual Studio (la DLL `clang_rt.asan_dynamic-x86_64.dll` doit être trouvée) ; `ctest` passe `ASAN_OPTIONS=halt_on_error=1`. MSVC n'a pas d'UBSan : **UBSan + ASan** seulement dans le job Linux clang de la CI (`-fsanitize=address,undefined -fno-sanitize-recover=all`).
8. `Tuning`, `NoteRule`, `Decomposer` restent des structs modifiables à l'exécution (le harnais en a besoin : mode dense pour l'arithmétique exacte).
9. Portabilité numérique (sémantique JS → C++ et options du compilateur) : MSVC `/fp:precise` (défaut, sans contraction FMA depuis VS 2022, vérifié) et **jamais** `/fp:fast` ni `/arch:AVX2` ; clang/gcc `-ffp-contract=off` et jamais `-ffast-math` ; aucun `-march` au-delà de x86-64 de base (V8 ne fusionne jamais `a*b+c` ; une FMA changerait la somme YIN, les biquads et la NMF). `Math.round(x)` → `r = std::floor(x); return (x - r >= 0.5) ? r + 1 : r;` (demis arrondis vers +∞ comme en JS ; `std::round` arrondit −2,5 en −3, `std::floor(x + 0.5)` se trompe sur 0,49999999999999994). `Math.max/min` propagent NaN : utiliser `std::max/min` seulement sur des valeurs jamais NaN, et là où le JS reçoit NaN (`midiHist`, sentinelle NaN) tester `std::isnan` explicitement comme le fait `Number.isNaN` ; jamais `std::fmax/fmin` (qui ignorent NaN). `Math.hypot` → `std::hypot`. `|0` → `static_cast<int>`. `(k - i) & HM` et `& 7` sur des différences qui peuvent être négatives : `int` (complément à deux, comme les entiers 32 bits de JS), jamais `size_t`.

## 4. Oracle d'équivalence (le cœur du travail)

### 4.1 Corpus synthétique (sans matériel, à chaque commit)

`test/render-plucks.mjs` contient sa **propre liste de recettes** (copie explicite des signaux construits dans les corps de test de `guitar-tracker.test.mjs` et `poly-engine.test.mjs`, qui ne sont pas exportés : plucks des 10 cordes, bruit, coup, repick ×5, legato, bend, chromatique, fondamentale absente, décroissance, accords et strums fast/medium/slow, hiss ; MONO à 44,1 / 48 / 96 kHz, POLY à 48 kHz), rendues avec le générateur déterministe de `test/plucks.mjs`. Il écrit chaque signal en WAV PCM 16 bits mono par `s = max(-32768, min(32767, Math.round(x × 32768)))` dans un dossier temporaire, puis **relit chaque WAV avec `readWav` de `dump-events.mjs`** et produit le dump JS attendu par `trackNotes` (MONO, horodatage fin de bloc 128) et `trackNotesPoly(wav, 'live')` (POLY) **sur ces échantillons relus** (int16 / 32768, exactement représentables en float32) : le WAV quantifie le signal (pas de 3·10⁻⁵, du même ordre que le plancher de bruit 1e-4 des générateurs), un dump calculé sur le `Float32Array` d'origine ne serait pas comparable. Le C++ lit le WAV avec la même conversion (/ 32768, premier canal). La CI et `ctest` exécutent ensuite `tools/dump_events` sur les mêmes WAV et `test/diff-events.mjs` compare. Rien n'est committé en WAV.

### 4.2 Référence GuitarSet (sur le PC du propriétaire, avant chaque PR)

1. Dumps JS, à régénérer une fois (non versionnés, 4,3 Go de données), depuis `D:\Jamrack`, **toujours avec `--takes` explicite** (`dump-events.mjs` calcule son chemin par défaut avec `new URL(...).pathname`, qui donne `/D:/...` sur Windows et lève ENOENT) : `node test/dump-events.mjs --guitarset D:\guitarset --set solo --takes test\takes.json --out D:\poly-out\js-mono-solo.json` (idem `comp`) ; `node test/poly-dump-events.mjs --guitarset D:\guitarset --set solo --takes test\takes.json --align live --out D:\poly-out\js-poly-solo.json` (idem `comp`) ; mode dense : `$env:DECOMP='{"sparse":0,"iter":15}'; node test/poly-dump-events.mjs --guitarset D:\guitarset --set solo --takes test\takes.json --align live --out D:\poly-out\js-poly-dense-solo.json; Remove-Item Env:DECOMP` (idem `comp`). La session vérifie l'absence de `DECOMP` avant chaque dump livré (`Get-ChildItem Env:DECOMP` doit échouer) : une variable oubliée rendrait dense tout dump lancé ensuite dans la même fenêtre.
2. **Établir d'abord la barre** : `node test/diff-events.mjs --hop 0.0026667 D:\poly-out\js-poly-dense-solo.json D:\poly-out\js-poly-solo.json` donne l'écart JS dense ↔ JS livré (attendu : quelques notes sur plusieurs milliers, onsets à ± 1 hop). Ce chiffre, consigné dans `plugin/README.md`, devient la barre d'acceptation du C++.
3. Liste de prises pour le C++ : `node -e "console.log(require('./test/takes.json').solo.join('\n'))" | Out-File -Encoding ascii D:\poly-out\solo.txt` (idem `comp`). Dumps C++ : `plugin\build\tools\Release\dump_events.exe --mode poly --align live --guitarset D:\guitarset --set solo --takes D:\poly-out\solo.txt --out D:\poly-out\cpp-poly-solo.json` (même format JSON, section 2) ; MONO : `--mode mono --out D:\poly-out\cpp-mono-solo.json`.
4. Score : `python test\score.py --guitarset D:\guitarset --takes test\takes.json D:\poly-out\js-poly-solo.json D:\poly-out\cpp-poly-solo.json` → F1 des deux colonnes dans le même tableau, écart ≤ 0,5 point.
5. Six jeux (M3) : les mélanges ne sont **pas** dans `D:\guitarset` (seuls les stems hexaphoniques y sont) ; construire une fois `python test\poly\make_mixes.py --guitarset D:\guitarset --out D:\poly-mixes` (dossier qui imite GuitarSet avec son propre `takes.json` ; copie les prises solo/comp si Windows refuse les liens symboliques, ~1 Go), puis pour chaque `set` de `solo comp mix2 mix3 hex2 hex3` : `node test/poly-dump-events.mjs --mixdir D:\poly-mixes --set <set> --takes D:\poly-mixes\takes.json --align live --out D:\poly-out\js-poly-<set>.json` (dense idem avec `$env:DECOMP` puis `Remove-Item`), liste `node -e "console.log(require('D:/poly-mixes/takes.json').<set>.join('\n'))" | Out-File -Encoding ascii D:\poly-out\<set>.txt`, `dump_events.exe --mode poly --align live --mixdir D:\poly-mixes --set <set> --takes D:\poly-out\<set>.txt --out D:\poly-out\cpp-poly-<set>.json`, diff, et `python test\score.py --guitarset D:\poly-mixes --takes D:\poly-mixes\takes.json D:\poly-out\js-poly-<set>.json D:\poly-out\cpp-poly-<set>.json` ; 13 624 notes au total, comme dans `docs/poly-implementation.md`.

### 4.3 `test/diff-events.mjs` (≈ 100 lignes, écrit en M1)

Entrée : deux JSON dump-events (JS puis C++, ou JS puis JS). Options : `--hop <s>` **obligatoire** = durée d'une trame de décision (MONO : une trame = 64 échantillons décimés = `64 × round(rate/24000) / rate`, soit 0,0029025 à 44,1 kHz, 0,0026667 à 48 kHz et à 96 kHz ; un bloc de 128 à 44,1 kHz dure 2,902 ms, plus qu'un hop POLY : une tolérance de 2,667 ms y compterait manquante + en trop toute note décalée d'une trame ; POLY : 0,0026667) ; `--vel` (défaut 1/127 ; les vélocités du dump sont en 0..1, non quantifiées). Appariement par prise : notes triées par onset ; pour chaque note JS, la note C++ **non encore appariée** de même `midi` dont |Δonset| ≤ 1,5 × hop est la plus proche (gloutonne, une à une). Catégories : **identique** si |Δonset| ≤ 1e-6 s, |Δoffset| ≤ 1e-6 s, |Δvel| ≤ `--vel` et `why` égal ; **décalée** si appariée avec |Δonset| ≤ 1,5 hop et |Δoffset| ≤ 1,5 hop ; **offset divergent** si appariée et |Δoffset| > 1,5 hop ; **`why` différent** compté à part ; **manquante** (JS sans C++) ; **en trop** (C++ sans JS). Sortie : tableau par prise et total. Seuils : `--max-missing N` et `--max-shifted N` acceptent un entier (nombre) ou `N%` (pour cent des notes JS) ; les offsets divergents comptent avec les décalées ; code de retour 1 au-delà. Sert aussi bien au JS ↔ JS qu'au JS ↔ C++.

### 4.4 Barres d'acceptation

| Comparaison | Barre |
|---|---|
| C++ MONO ↔ JS MONO, corpus synthétique et GuitarSet 36 prises | mêmes notes visées (0 manquante, 0 en trop), onsets/offsets à ± 1 trame (2,9 ms à 44,1 kHz), vélocité à ± 1/127, `why` identique sur ≥ 99 % ; au-delà de 0/0, chaque note divergente doit être expliquée trame à trame par `onFrame` des deux moteurs (`rmsDb`, `conf`, `midiF`) comme une bascule de seuil à l'ulp de `libm` (`log10`, `pow` à la frontière `conf >= CONF_ON` ou `rmsDb > gate`), sinon c'est un bug ; **barre dure ≤ 0,1 % de manquantes + en trop** (le précédent 13 624/13 624 concerne POLY dense, MONO n'a pas de précédent mesuré) |
| C++ POLY dense ↔ JS POLY dense | 0 manquante, 0 en trop ; décalées ≤ 0,5 % (ulp de `libm`) |
| C++ POLY livré ↔ JS POLY livré | écart ≤ la barre JS dense ↔ JS livré établie en 4.2 |
| Scores `score.py` | F1 ± 50 ms à ± 0,5 point du JS sur chaque jeu |
| Coût (`dump_events` sur `00_BN1-129-Eb_solo_mix.wav`) | POLY livré : moyenne ≤ 1,1 ms/hop, max à imprimer ; MONO : ≤ 15 % d'un cœur, référence JS mesurée sur le même WAV par `Measure-Command { node test/dump-events.mjs D:\guitarset\audio_mono-pickup_mix\00_BN1-129-Eb_solo_mix.wav D:\poly-out\t.json }` rapporté à la durée de la prise (le « 13,6 % » du dépôt vient d'un test synthétique dans un conteneur, pas de ce WAV) ; C++ : `dump_events --mode mono` imprime le temps total et le ratio ; chiffres JS à relever sur le même PC le même jour |
| Latence pick → son (M0, M2) | méthode téléphone de la section 5 (médiane sur 10 notes par corde), même méthode pour le plugin et la page web |

## 5. Jalons

« Session » = une session d'agent locale (elle dure tant que le propriétaire est joignable pour une question, en pratique une demi-journée) ; « soirée » = essai du propriétaire au Gigcaster, avec une check-list fournie par la session et un compte rendu **selon le modèle du README** (ci-dessous) qui nourrit la session suivante (un agent n'entend pas la guitare). Branche `feature/plugin` ; une PR vers `main` par jalon validé.

**Modèle de compte rendu** (à recopier dans `plugin/README.md` dès M0) : date ; Live 12.x.y (Help → About Live) ; pilote GCS-5, tampon, fréquence d'échantillonnage ; build : version + hash lus dans l'éditeur du plugin ; pour chaque point de la check-list : réponse oui/non et chiffre ; ce qui a bloqué, dans l'ordre ; fichiers joints (enregistrement téléphone, capture d'écran de Settings → Plug-Ins si le plugin n'apparaît pas, `Log.txt` du dossier Preferences de Live si Live plante).

### M0 — Prototype jetable qui mesure Live (1 session, 1 soirée)

But : répondre par la mesure aux inconnues de l'étude avant d'écrire du DSP.

Tâches :
1. `plugin/CMakeLists.txt` + JUCE `FetchContent` (8.0.15, section 0) ; **deux cibles** construites depuis la même source, chacune avec son `PLUGIN_CODE` (section 2) : `JAMRACK GTM Fx` (`PLUGIN_CODE Gtm0`, `IS_SYNTH FALSE`, effet audio) et `JAMRACK GTM Inst` (`PLUGIN_CODE Gtm9`, `IS_SYNTH TRUE`, bus d'entrée unique « Sidechain » stéréo déclaré **auxiliaire** : le processeur surcharge `juce::VST3ClientExtensions::getPluginHasMainInput()` → `false` via `getVST3ClientExtensions()`, sinon JUCE présente le premier bus d'entrée comme entrée principale — vérifié dans `juce_audio_plugin_client_VST3.cpp` au tag 8.0.15, `getBusInfo` : premier bus = `getPluginHasMainInput() ? kMain : kAux`, défaut `true` — et Live n'offrirait pas d'*Audio From*) — pour trancher par l'essai le type de piste que Live accepte pour le routage *MIDI From* (l'article Ableton parle de piste MIDI, Jam Origin pose son VST sur la piste audio ; non vérifié).
2. Processeur minimal : passe-tout audio ; détecteur d'attaque rudimentaire (enveloppe RMS 5 ms au-dessus de −30 dBFS **et** montée de 3 dB sur une enveloppe 30 ms ; réarmement quand la montée est retombée, pour qu'une corde qui sonne encore ne bloque pas la suivante — le réarmement initial « sous −40 dBFS » donnait 1 note pour 10 attaques, corrigé le 8 octobre) qui émet **note-on 60 vélocité 100 à l'échantillon de l'attaque, tenu 600 ms**, et **pendant la note** une rampe de pitch bend 8192 → 12288 (de 100 à 250 ms après l'attaque) puis 12288 → 8192 (de 250 à 400 ms), un message de molette tous les 64 échantillons, puis **note-off à 600 ms** avec la molette remise à 8192 au même échantillon (test du bend VST3 dans Live 12.4 : une rampe jouée après le note-off ne modulerait aucune voix) ; paramètre `channel`.
3. Éditeur générique + étiquette `JucePlugin_VersionString` + hash git court (section 2) ; `COPY_PLUGIN_AFTER_BUILD` vers `D:\VST3` ; `README.md` avec la procédure Live (section 2.2 de l'étude), le modèle de compte rendu, la section « Revenir en arrière » (M2 tâche 3, dès M0 sous forme courte : fermer Live et REAPER avant tout build, la DLL est verrouillée tant qu'un hôte l'a chargée) et la check-list ci-dessous.
4. `D:\tools\pluginval\pluginval.exe --strictness-level 5 --validate "D:\VST3\JAMRACK GTM Fx.vst3"` (guillemets : le nom contient des espaces ; JUCE nomme le bundle d'après `PRODUCT_NAME`) et idem pour `"D:\VST3\JAMRACK GTM Inst.vst3"`, propres (options vérifiées dans `Source/CommandLine.cpp` de pluginval ; `--skip-gui-tests` en CI) ; CI Windows qui construit et valide (section 2).

Soirée (check-list, tout à noter) :
- REAPER (installé ce soir-là) : piste 1 : Input = canal GUITAR du GCS-5 (mono), monitoring activé ; FX : `JAMRACK GTM Fx` puis **ReaSynth** derrière lui dans la même chaîne (REAPER passe le MIDI d'un FX au suivant) ; clic droit sur le bouton d'armement → **Record: output → Record: output (MIDI)** ; armer, enregistrer 10 notes. À noter oui/non : on entend ReaSynth pendant le jeu ; après l'arrêt, un **item MIDI contenant des notes** apparaît sur la piste (contre-épreuve : si oui, le plugin émet bien).
- Live 12 : (a) piste audio + `GTM Fx`, piste MIDI « Synthé » avec *MIDI From* = Guitare → `JAMRACK GTM Fx`, Monitor In, instrument réglé sur **plage de bend 2** : notes reçues ? **bend entendu pendant la note** (la note doit monter d'un demi-ton puis redescendre ; si elle monte de trois tons, six demi-tons, l'instrument est resté à plage 12) ? (b) `GTM Inst` sur une piste MIDI : le panneau du plugin montre-t-il un sélecteur ***Audio From*** (side-chain) ? side-chain = piste Guitare : notes reçues ? Noter laquelle des deux Live liste dans *MIDI From*.
- Armement : avec Guitare en *Auto*, armer Synthé coupe-t-il l'entrée ? (attendu : oui → Monitor In ou Ctrl-clic).
- **Latence pick → son, même méthode pour le plugin et la page web** : poser un téléphone (application dictaphone, format WAV ou le meilleur disponible) entre les cordes et l'enceinte ; jouer dix notes détachées sur le mi aigu et dix sur le mi grave ; ouvrir le fichier dans Audacity (gratuit) et lire, pour chaque note, l'écart entre le bruit du médiator et le début du son du synthé ; noter la médiane. Même son déclenché des deux côtés (un son percussif court à attaque nette : dans Live un instrument de ce type, sur la page le même type de son) ; faire la mesure à tampon GCS-5 **64, 128, 256** pour le plugin, puis sur la page web (`jamrack.openmindlab.fr`, BUFFER min) le même soir, même téléphone, même position. (La page web ne peut pas être enregistrée dans le même Set Live : le pilote GCS-5 est tenu par Live en ASIO.) **Mesure secondaire, plugin seul** : enregistrer dans Live la piste guitare et une piste audio en *Resampling*, mesurer l'écart dans l'arrangement ; la noter comme « écart interne Live », sans la comparer à la page.
- Décalage d'un clip MIDI enregistré sur « Synthé » par rapport au clic du métronome.

Acceptation : les cinq réponses consignées dans `plugin/README.md` (section « Mesures M0 ») avec le modèle de compte rendu rempli. Le code de M0 est jeté ensuite, sauf le CMake et la CI ; **avant d'installer M2, supprimer les deux `.vst3` du prototype M0** de `D:\VST3`.

### M1 — Port MONO + oracle (1 à 2 sessions, 0 soirée)

Tâches :
1. `dsp/mono_tracker` : port ligne à ligne de `tracker.js` (550 lignes) avec les points de rondeur float32 de la règle 3.3-4 et la sémantique de la règle 3.3-9, `Tuning` modifiable, FIFO d'événements avec offset d'échantillon, `flush`, `reset` (3.2), `setParams` ; `onFrame` conservé comme pointeur de fonction optionnel (traçage).
2. `tools/dump_events.cpp` (mode mono, options de la section 2) ; `test/render-plucks.mjs` ; `test/diff-events.mjs`.
3. Tests CTest fermés : biquads (réponse à 1 kHz à 1e-9 du JS), YIN sur sinus pur (période exacte), décimation ; puis corpus synthétique diff = 0.
4. ASan sur le corpus (configuration `build-asan` de la règle 3.3-7) ; job Linux ASan + UBSan.
5. GuitarSet 36 prises : diff et `score.py`, chiffres dans `plugin/README.md`.

Acceptation : `ctest` vert ; `node test/diff-events.mjs --hop 0.0029025 D:\poly-out\js-mono-solo.json D:\poly-out\cpp-mono-solo.json` (GuitarSet est à 44,1 kHz) → manquantes + en trop ≤ 0,1 % (0 visé, note divergente expliquée trame à trame sinon), décalées ≤ 1 % ; `score.py` à ± 0,5 point ; sanitizers propres.

### M2 — Coque VST3 MONO utilisable dans Live (2 à 3 sessions, 2 soirées)

Tâches :
1. Processeur réel (`PLUGIN_CODE Gtm1`) : moteur MONO, paramètres de 3.1 (sans `mode`), `sounding`, *flush* et bypass selon 3.1-3.2, `ScopedNoDenormals`, latence 0, FIFO de mesures ; éditeur générique + une étiquette texte (note / cents / niveau / latence mesurée / CPU / version + hash).
2. État sauvegardé/rechargé (`getStateInformation`), test automatisé de rappel d'état ; pluginval 5 ; CI publie l'artefact zip.
3. `plugin/README.md` : installation (dézipper dans `D:\VST3`, rescan), procédure Live pas à pas, procédure REAPER, table de latence à remplir (méthode téléphone), limites connues, avis de licence (section 2), et la section **« Revenir en arrière »** : 1. Fermer Live et REAPER avant tout `cmake --build` (la DLL `.vst3` est verrouillée tant qu'un hôte l'a chargée : la copie échoue par « Permission denied » / LNK1104). 2. Avant chaque nouveau build, la session copie le dossier `.vst3` courant dans `D:\VST3-archive\<version>-<hash>\` (jamais dans `D:\VST3`, que Live scanne avec ses sous-dossiers). 3. Si Live plante ou se bloque : fermer Live, supprimer `D:\VST3\JAMRACK Guitar MIDI.vst3`, recopier la dernière archive qui marchait, Options → Settings → Plug-Ins → **Alt + Rescan**. 4. Si le plugin n'apparaît pas dans le navigateur de Live : vérifier que `D:\VST3\JAMRACK Guitar MIDI.vst3\Contents\x86_64-win\JAMRACK Guitar MIDI.vst3` existe, Alt + Rescan, puis envoyer à la session le fichier `Log.txt` du dossier Preferences de Live. 5. Avant d'installer M2, supprimer les deux `.vst3` du prototype M0.
4. Release GitHub `plugin-v0.1.0-mono` selon le processus de la section 2 (tag, Release brouillon construite par la CI, zip + `SHA256SUMS.txt`, publication par le propriétaire ou `gh release create`).

Soirées : (1) Live 12 de bout en bout avec un instrument Live et un Instrument Rack, comparaison de « sensation » avec la page web, latence à 64/128/256 par la méthode téléphone de M0 (médiane sur 10 notes par corde), bend, note-off ; (2) après corrections, même parcours + REAPER, enregistrement d'un clip MIDI calé sur le métronome.

Acceptation : le propriétaire écrit « MONO utilisable dans Live » ; table de latence remplie ; pluginval 5 ; PR fusionnée.

### M3 — Port POLY (2 à 3 sessions, 1 à 2 soirées)

Tâches :
1. `plugin/tools/gen-profile-header.mjs` → `dsp/src/profile_data.h` (CI vérifie qu'il est à jour).
2. Port des sept fichiers (`fft`, `resample`, `bank`, `profile`, `nmf`, `notes`, `engine`, 1 084 lignes) avec les règles 3.3-2/3/4/5/9 ; mode dense et clairsemé ; `Decomposer`/`NoteRule` modifiables.
3. Tests fermés : FFT vs DFT direct à 1e-9, noyau du rééchantillonneur et passage d'un 1 kHz, normes et pics de la banque, décomposition d'un gabarit seul (les quatre premiers tests de `test/poly-engine.test.mjs`, reproduits tels quels).
4. Oracle : barre JS dense ↔ JS livré (4.2-2), puis C++ dense ↔ JS dense, puis C++ livré ↔ JS livré sur 36 prises, puis les six jeux construits par `make_mixes.py` (4.2-5) ; coûts par hop.
5. Coque : paramètre `mode` (« POLY (bêta) »), bascule avec *flush* + `reset`, `PolyEngineState` sur fil de fond (règle 3.3-5), ÉCO selon la règle 3.3-6, mesures CPU/voix/ÉCO dans l'étiquette (aucun paramètre `profile` : profil générique seulement, section 0).
6. Release `plugin-v0.2.0-poly-beta`.

Soirée : POLY dans Live (notes seules, deux notes, accords grattés : attentes de la section 13 du plan POLY, pas plus), tag ÉCO à 64/128/256 (attendu à 48 kHz : allumé à 64, éteint à 128 et 256 ; à 44,1 kHz, allumé aussi à 128, règle 3.3-6), CPU de Live affiché, comparaison avec la page web.

Acceptation : barres de 4.4 tenues ; coût ≤ 1,1 ms/hop en moyenne sur le PC du propriétaire ; pas de décrochage audio à 128 dans Live ; **à 48 kHz et tampon 128, tag ÉCO éteint**, le propriétaire écrit « POLY bêta = la page web ». À 64 (ou à 44,1 kHz / 128), ÉCO allumé est le comportement attendu (budget inférieur au coût moyen) et n'est pas un défaut ; le noter dans le README.

### Total v1 (Windows, gratuit)

| | Sessions | Soirées | Argent |
|---|---|---|---|
| M0 + M1 + M2 + M3 | **6 à 9** | **4 à 5** | **0 EUR** |

Hypothèses : le C++ est écrit et testé par les agents sur le PC (compilation locale, GuitarSet local) ; les soirées sont le goulot ; « 1 à 2 mois » est une borne haute dictée par les soirées, à tenir dans les 60 jours d'évaluation de REAPER (section 0).

## 6. Mode d'emploi pour la session locale

Message à coller dans la conversation locale (`D:\Jamrack`) :

```
ultracode — git checkout main && git pull, puis exécute docs/plugin-plan.md, jalon M0,
sur une branche feature/plugin créée depuis ce main à jour.
Commence par vérifier les prérequis de la section 1 (cmake --version, node --version,
python --version, D:\tools\pluginval\pluginval.exe --version, dossiers D:\VST3 et
D:\VST3-archive) et dis-moi ce qui manque avant d'écrire du code.
Propose-moi en une seule fois la liste des commandes à autoriser pour la durée du projet.
Respecte les décisions de la section 0, les contrats de la section 3 et les règles temps réel 3.3.
Termine par : plugin/README.md avec la check-list de ma soirée et le modèle de compte rendu,
le build qui copie dans D:\VST3 (Live et REAPER fermés), pluginval propre, la version + hash git
visibles dans l'éditeur du plugin, CLAUDE.md mis à jour, et la PR.
```

Si la PR du plan n'est pas encore fusionnée, remplacer `main` par `docs/plugin-plan` dans la première ligne (le clone local est sur `feature/poly` d'après `docs/local-setup.md` : sans `git pull`, un `main` local n'a ni `docs/plugin-plan.md` ni la section « Plugin » de `CLAUDE.md`).

Puis, après chaque soirée : « voici mon compte rendu M0 (modèle du README rempli) : … ; passe au jalon M1 ». Chaque jalon suivant se lance de la même façon (« jalon M1 », « jalon M2 », « jalon M3 »).

Conventions pour les agents : lire `CLAUDE.md` (mémoire du projet) ; la session M0 commence par proposer au propriétaire, en une seule fois, la liste des commandes à autoriser pour la durée du projet (`cmake`, `ctest`, `node`, `python`, `git`, `gh`, `D:\tools\pluginval\pluginval.exe`) et l'enregistre dans `.claude/settings.local.json` (non versionné : une session `ultracode` qui s'arrête à chaque invite n'avance pas quand le propriétaire n'est pas devant l'écran) ; ne jamais modifier `js/` pour les besoins du plugin, sauf les deux scripts de test nouveaux ; commiter des points d'étape après `cmake --build` et `ctest` ; messages de commit par heredoc ; mettre à jour `CLAUDE.md` (section « Plugin ») à la fin de chaque jalon ; toute mesure du propriétaire va dans `plugin/README.md` avec la date, le tampon, la fréquence d'échantillonnage, le pilote et le build (version + hash).

## 7. Hors v1 (à décider plus tard)

- **Calibration dans le plugin** (port de `calibrate.js` + dialogue, fichiers de profil JSON au format de `profiles.js`) ; en attendant, un profil exporté du navigateur pourra être chargé par un paramètre `profile` (fichier) — **jalon séparé après M3, sur décision du propriétaire** (pas « si le temps le permet » : M3 a l'acceptation la plus dure et son périmètre ne s'élargit pas sans décision).
- **Interface WebView** (carte HTML existante dans `WebBrowserComponent`, 12 langues).
- **CLAP** via `clap-juce-extensions` (compatibilité JUCE 9 non vérifiée ; rester en JUCE 8 si CLAP est voulu).
- **Application autonome + port MIDI virtuel** (loopMIDI ou Windows MIDI Services) : **sans ASIO** (WASAPI exclusif, binaire MIT) ou avec ASIO et binaire GPLv3 annoncé.
- **macOS / AU**, **Linux**, **signature Windows** (SignPath pour l'open source).

## 8. Risques et parades (rappel)

1. Live ne liste pas l'effet dans *MIDI From* → M0 le mesure avec les deux cibles ; repli : cible instrument + side-chain, ou port MIDI virtuel (section 7).
2. Pire cas CPU POLY à tampon 64 → règle 3.3-6, mesuré en M3 ; repli : décomposeur sur fil de travail (+1 hop).
3. Dérive numérique → double + float32 aux mêmes endroits, règle 3.3-9 (FMA, tris stables, `Math.round`), barres de 4.4 ; UB → sanitizers.
4. Mises à jour de Live / JUCE qui cassent → une session par casse, issue GitHub comme canal.
5. Pitch bend VST3 dans Live 12.4 jamais revérifié → M0 (rampe de bend **pendant** la note) ; repli : BEND off (redéclenchement chromatique).
6. Build qui ne copie pas (hôte ouvert, DLL verrouillée) ou plugin absent du navigateur de Live → procédure « Revenir en arrière » du README (M2 tâche 3, forme courte dès M0).
