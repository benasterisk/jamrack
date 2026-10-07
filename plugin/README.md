# Plugin VST3 « JAMRACK Guitar → MIDI » (Windows)

Plan complet : [`docs/plugin-plan.md`](../docs/plugin-plan.md). État : **jalon M0** —
deux prototypes jetables qui mesurent ce qu'Ableton Live 12 accepte, avant d'écrire
le moindre traitement du signal. Ils ne reconnaissent **aucune note de guitare** :
ils détectent juste une attaque et jouent un do (note 60) avec un bend d'un demi-ton.

## Les deux prototypes M0

| Plugin (navigateur de Live) | Code | Type | Ce qu'on teste |
|---|---|---|---|
| **JAMRACK GTM Fx** | `Gtm0` | effet audio, sur la piste guitare | Live liste-t-il un **effet** dans *MIDI From* ? |
| **JAMRACK GTM Inst** | `Gtm9` | instrument, avec une entrée *side-chain* | Live propose-t-il un sélecteur ***Audio From*** sur l'instrument ? |

Ce qu'ils émettent, identique pour les deux : à chaque attaque franche (niveau
au-dessus de −30 dBFS, réarmement sous −40 dBFS), **note 60 (do), vélocité 100,
tenue 600 ms** ; pendant la note, le bend **monte d'un demi-ton** (de 100 à 250 ms,
avec un instrument réglé sur une plage de bend de 2) puis **redescend** (de 250 à
400 ms) ; la note s'arrête à 600 ms. À l'oreille : « do qui glisse vers do# et revient ».
**Laisse plus de 600 ms entre deux attaques** : une attaque pendant la note tenue est
ignorée. Le Fx laisse passer l'audio de la guitare tel quel ; l'Inst est muet (il ne
fait que du MIDI).

L'éditeur de chaque plugin affiche **son nom, sa version et le hash git du build**
(par exemple `JAMRACK GTM Fx  0.0.1  (a1b2c3d)`) et le nombre de notes envoyées : recopie
cette ligne dans ton compte rendu. Un **`+`** après le hash (`a1b2c3d+`) signifie « build
fait avec des modifications non commitées » : signale-le.

## Construire et installer (la session le fait pour toi)

**Fermer Live et REAPER avant tout build** : tant qu'un hôte a chargé le plugin, la
DLL est verrouillée et la copie échoue (« Permission denied » / LNK1104).

```powershell
cmake -S plugin -B plugin/build -G "Visual Studio 17 2022" -A x64
cmake --build plugin/build --config Release
```

Le build copie `JAMRACK GTM Fx.vst3` et `JAMRACK GTM Inst.vst3` dans `D:\VST3`.
Validation : `D:\tools\pluginval\pluginval.exe --strictness-level 5 --validate "D:\VST3\JAMRACK GTM Fx.vst3"` (idem Inst).

Dans Live 12 (une seule fois) : Options → **Settings** (Ctrl+,) → onglet **Plug-Ins** →
**Use VST3 Plug-In Custom Folder = On** → Browse → `D:\VST3` → **Rescan** (si rien
n'apparaît : **Alt** + clic sur Rescan).

## Revenir en arrière (forme courte M0)

1. Fermer Live et REAPER avant chaque build (DLL verrouillée). Le hash affiché dans
   l'éditeur est recalculé à chaque build : commiter d'abord, construire ensuite.
2. Si Live plante ou se bloque à l'ouverture : fermer Live, supprimer les deux dossiers
   `D:\VST3\JAMRACK GTM Fx.vst3` et `D:\VST3\JAMRACK GTM Inst.vst3`, relancer Live :
   il démarre comme avant. Envoie-moi le fichier `Log.txt` du dossier Preferences de Live.
3. Si un plugin n'apparaît pas dans le navigateur : vérifier que
   `D:\VST3\JAMRACK GTM Fx.vst3\Contents\x86_64-win\JAMRACK GTM Fx.vst3` existe,
   **Alt + Rescan**, puis capture d'écran de Settings → Plug-Ins.
4. **Avant d'installer le vrai plugin (M2), supprimer les deux `.vst3` de M0** de `D:\VST3`.

## Test Live sans guitare (5 minutes)

Pour répondre aux questions de routage sans brancher la guitare, un fichier de
**10 attaques** (une par seconde, 48 kHz) a été déposé dans ta bibliothèque Live :
navigateur de Live → **User Library → Samples → JAMRACK → `m0-plucks-48k.wav`**.

1. Glisse le fichier dans la piste audio « Guitare » (case de clip vide). Dans la vue du
   clip (en bas), **désactive Warp** (sinon Live étire le fichier au tempo du Set).
2. Pose **JAMRACK GTM Fx** sur cette piste, puis la piste MIDI « Synthé » comme à l'étape 2
   de la check-list (*MIDI From* = Guitare → JAMRACK GTM Fx, *Monitor* In, instrument en
   plage de bend 2).
3. Lance le clip : le synthé doit jouer 10 do, chacun avec le glissement d'un demi-ton.
4. Même chose avec **JAMRACK GTM Inst** sur une piste MIDI, *Audio From* (side-chain) =
   Guitare.

Ce test répond aux questions 2 et 3 de la check-list (routage, bend, side-chain) ; la
latence et le jeu réel demandent toujours la guitare.

## Check-list de la soirée M0

Préparation (10 min) :
- Gigcaster : mode **MTK-STREAM**, **MIX MINUS = ON**, canal **GUITAR brut**, effets du
  canal éteints (réglage du 5 octobre).
- Live : Settings → Audio → Driver Type **ASIO**, Audio Device **GCS-5**, *Hardware Setup*
  → tampon **128**. **Note la fréquence d'échantillonnage affichée** (44,1 ou 48 kHz).
  Puis **Input Config** : active l'entrée mono du canal GUITAR.
- Live : dossier VST3 = `D:\VST3` et Rescan (ci-dessus) ; les deux plugins JAMRACK GTM
  doivent apparaître.

**1. REAPER (à installer ce soir-là, évaluation gratuite 60 jours) — contre-épreuve.**
**Ferme Live d'abord** (il tient le pilote ASIO du Gigcaster). REAPER : Options →
Preferences → **Audio → Device** : Audio system **ASIO**, driver **GCS-5**, même tampon
que dans Live ; puis **Plug-ins → VST** → ajouter `;D:\VST3` à la fin du champ
« VST plug-in paths » → **Re-scan**. Piste 1 :
Input = canal GUITAR du GCS-5 (mono), monitoring activé ; FX : **JAMRACK GTM Fx** puis
**ReaSynth** derrière lui ; clic droit sur le bouton d'armement → *Record: output* →
*Record: output (MIDI)* ; armer, enregistrer 10 notes.
- [ ] On entend ReaSynth pendant le jeu ? (oui/non)
- [ ] Après l'arrêt, un item MIDI **contenant des notes** apparaît sur la piste ? (oui/non)

**2. Live — l'effet (a).** **Ferme REAPER, rouvre Live.** Piste audio « Guitare » : *Audio From* = canal GUITAR, *Monitor*
= **In**, **JAMRACK GTM Fx** en premier. Piste MIDI « Synthé » : *MIDI From* = Guitare,
puis dans le menu du dessous **JAMRACK GTM Fx** ; *Monitor* = **In** ; un instrument
réglé sur **plage de bend 2** (Wavetable / Operator : *Pitch Bend Range* = 2).
- [ ] *MIDI From* → Guitare liste-t-il **JAMRACK GTM Fx** ? (oui/non)
- [ ] Notes reçues (le synthé joue un do à chaque attaque) ? (oui/non)
- [ ] **Bend entendu pendant la note** : le do monte d'**un demi-ton** puis redescend ?
  (oui/non ; s'il monte de **trois tons** (un triton), l'instrument est resté en plage 12)

**3. Live — l'instrument (b).** Piste MIDI « Inst » : y poser **JAMRACK GTM Inst**. Dans
la barre de titre du plugin (en haut du panneau du périphérique), Live doit afficher un
sélecteur ***Audio From*** (side-chain) : le régler sur la piste **Guitare**. Puis une
deuxième piste MIDI « Synthé 2 » avec le même instrument (plage de bend 2) : *MIDI From*
= piste « Inst », puis dans le menu du dessous **JAMRACK GTM Inst** ; *Monitor* = **In**.
- [ ] Sélecteur *Audio From* présent sur l'Inst ? (oui/non)
- [ ] Notes reçues par l'instrument ? (oui/non)
- [ ] Lequel des deux Live liste-t-il dans *MIDI From* : Fx, Inst, les deux, aucun ?

**4. Armement.** Avec « Guitare » en *Monitor Auto*, armer « Synthé » coupe-t-il
l'entrée de la guitare (le synthé ne joue plus) ?
- [ ] Oui / non (attendu : oui → garder *Monitor In* sur Guitare, ou **Ctrl+clic** pour
  armer les deux)

**5. Latence pick → son (méthode téléphone).** Téléphone (dictaphone, WAV ou meilleur
format disponible) posé entre les cordes et l'enceinte ; dix notes détachées sur le mi
aigu, dix sur le mi grave ; ouvrir le fichier dans **Audacity** et mesurer pour chaque
note l'écart entre le bruit du médiator et le début du son du synthé ; noter la
**médiane**. Son du synthé : percussif, attaque nette (le même type de son des deux
côtés).
- [ ] Plugin (Fx) dans Live, tampon GCS-5 **64** : médiane mi aigu ___ ms / mi grave ___ ms
- [ ] idem tampon **128** : ___ / ___ ms
- [ ] idem tampon **256** : ___ / ___ ms
- [ ] **Page web** (`jamrack.openmindlab.fr`, TAMPON minimum, même téléphone, même
  position ; fermer Live d'abord, il tient le pilote ASIO) : ___ / ___ ms
- [ ] (Secondaire) écart interne Live : enregistrer la piste guitare et une piste audio en
  *Resampling*, mesurer l'écart dans l'arrangement : ___ ms (ne pas comparer à la page)

**6. Décalage d'enregistrement.** Métronome de Live allumé, enregistrer un clip MIDI sur
« Synthé » en jouant sur les temps.
- [ ] Décalage du clip par rapport au clic : ___ ms (en avance / en retard)

## Modèle de compte rendu (à recopier et remplir)

```
Compte rendu M0 — date : ____
Live : 12.__.__ (Help → About Live)
Pilote : GCS-5, tampon ___, fréquence d'échantillonnage ___ kHz
Build : (ligne lue dans l'éditeur du plugin, ex. « JAMRACK GTM Fx 0.0.1 (a1b2c3d) », avec le « + » s'il y en a un)

1. REAPER : ReaSynth entendu oui/non ; item MIDI avec notes oui/non
2. Live, Fx : listé dans MIDI From oui/non ; notes oui/non ; bend d'un demi-ton entendu oui/non
3. Live, Inst : Audio From présent oui/non ; notes oui/non ; Live liste : Fx / Inst / les deux / aucun
4. Armement : armer Synthé coupe la guitare oui/non
5. Latence (médianes, ms) : plugin 64 ___/___ ; 128 ___/___ ; 256 ___/___ ; page web ___/___ ;
   écart interne Live ___
6. Décalage du clip MIDI : ___ ms (avance/retard)

Ce qui a bloqué, dans l'ordre :
-

Fichiers joints : (enregistrement téléphone, capture de Settings → Plug-Ins si un plugin
n'apparaît pas, Log.txt du dossier Preferences de Live si Live plante)
```

## Mesures M0

*À compléter après la soirée Live + Gigcaster (date, Live, tampon, fréquence, pilote, build).*

**Essai partiel, 7 octobre 2026 — REAPER seul, sans Live ni Gigcaster.** PC portable,
micro et enceintes intégrés, REAPER en WASAPI, 48 kHz, tampon 256 ; build
`JAMRACK GTM Fx 0.0.1 (aafa807)` ; chaîne FX : JAMRACK GTM Fx → ReaSynth.
- Do entendu à chaque attaque : **oui**.
- Bend d'un demi-ton entendu pendant la note : **oui** → le pitch bend VST3 sort du plugin.
- Enregistrement en *Record: output (MIDI)* (essayé aussi en MIDI + audio, overdub) :
  objet MIDI avec les notes : **oui** → contre-épreuve REAPER réussie, le plugin émet bien.
- Latence : « très acceptable » à l'oreille (non mesurée ; mesure au téléphone à faire
  avec le Gigcaster).
- Reste à faire : tout Live 12 (Fx et Inst dans *MIDI From*, side-chain *Audio From*,
  armement, décalage d'enregistrement), mesures de latence à 64/128/256.

## Tests sans DAW (hôte VST3 en ligne de commande)

`plugin/tools/vst3_probe.cpp` (cible `jamrack_vst3_probe`) est un petit hôte VST3 : il
charge un `.vst3` **installé**, lui envoie un fichier WAV bloc par bloc comme un DAW, et
écrit chaque message MIDI émis, à l'échantillon près, en JSON. `plugin/m0/check_probe.py`
fabrique les WAV de test et vérifie les deux prototypes :

```powershell
python plugin/m0/check_probe.py --probe D:\JamRack\plugin\build\jamrack_vst3_probe_artefacts\Release\jamrack_vst3_probe.exe --outdir $env:TEMP\m0-probe
```

**Résultat du 8 octobre 2026** (builds installés dans `D:\VST3`) : **40 essais sur 40
conformes** — les deux plugins × 44,1 / 48 / 96 kHz × tampons 32 à 1024, plus canal MIDI 5
et bypass de l'hôte en pleine note. Pour chaque attaque : note 60 vélocité 100 émise
**0,28 à 0,32 ms** après l'attaque, molette remise à 8192 au même échantillon, rampe 8192 →
~12 270 → 8192 entre 100 et 400 ms (un message tous les 64 échantillons), note-off à
600 ms pile ; positions identiques quel que soit le tampon ; l'effet laisse passer l'audio
**à l'identique**, l'instrument reçoit bien le signal par son entrée side-chain et reste
muet ; latence déclarée 0. Ce que ce test **ne dit pas** : comment Live route ce MIDI
(*MIDI From*, *Audio From*, armement) et la latence réelle — c'est la soirée.

## Avis de licence

- Sources du plugin : **MIT** (licence du dépôt `jamrack`).
- **JUCE 8.0.15**, sous licence *Starter* (gratuite sous 20 000 USD de revenus annuels,
  dons compris) ; JUCE est téléchargé par CMake au moment du build et n'est pas copié dans
  le dépôt ; toute personne qui recompile accepte le contrat JUCE pour sa copie ; les
  notices de copyright de JUCE sont conservées.
- **SDK VST3** tel qu'embarqué par JUCE 8.0.15
  (`modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt`) : MIT,
  « Copyright (c) 2025, Steinberg Media Technologies GmbH ».
- VST is a trademark of Steinberg Media Technologies GmbH.
