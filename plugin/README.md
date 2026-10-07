# Plugin VST3 « JAMRACK Guitar → MIDI » (Windows)

Plan complet : [`docs/plugin-plan.md`](../docs/plugin-plan.md). État : **jalon M0** —
deux prototypes jetables qui mesurent ce qu'Ableton Live 12 accepte, avant d'écrire
le moindre traitement du signal. Ils ne reconnaissent **aucune note de guitare** :
ils détectent juste une attaque et jouent un do (note 60) avec un bend.

## Les deux prototypes M0

| Plugin (navigateur de Live) | Code | Type | Ce qu'on teste |
|---|---|---|---|
| **JAMRACK GTM Fx** | `Gtm0` | effet audio, sur la piste guitare | Live liste-t-il un **effet** dans *MIDI From* ? |
| **JAMRACK GTM Inst** | `Gtm9` | instrument, avec une entrée *side-chain* | Live propose-t-il un sélecteur ***Audio From*** sur l'instrument ? |

Ce qu'ils émettent, identique pour les deux : à chaque attaque franche (niveau
au-dessus de −30 dBFS, réarmement sous −40 dBFS), **note 60 (do), vélocité 100,
tenue 600 ms** ; pendant la note, le bend **monte d'un ton** (de 100 à 250 ms) puis
**redescend** (de 250 à 400 ms) ; la note s'arrête à 600 ms. À l'oreille : « do qui
monte d'un ton et redescend ». Le Fx laisse passer l'audio de la guitare tel quel ;
l'Inst est muet (il ne fait que du MIDI).

L'éditeur de chaque plugin affiche **son nom, sa version et le hash git du build**
(par exemple `JAMRACK GTM Fx  0.0.1  (a1b2c3d)`) et le nombre de notes envoyées : recopie
cette ligne dans ton compte rendu.

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

1. Fermer Live et REAPER avant chaque build (DLL verrouillée).
2. Si Live plante ou se bloque à l'ouverture : fermer Live, supprimer les deux dossiers
   `D:\VST3\JAMRACK GTM Fx.vst3` et `D:\VST3\JAMRACK GTM Inst.vst3`, relancer Live :
   il démarre comme avant. Envoie-moi le fichier `Log.txt` du dossier Preferences de Live.
3. Si un plugin n'apparaît pas dans le navigateur : vérifier que
   `D:\VST3\JAMRACK GTM Fx.vst3\Contents\x86_64-win\JAMRACK GTM Fx.vst3` existe,
   **Alt + Rescan**, puis capture d'écran de Settings → Plug-Ins.
4. **Avant d'installer le vrai plugin (M2), supprimer les deux `.vst3` de M0** de `D:\VST3`.

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
Options → Preferences → Plug-ins → VST → ajouter `;D:\VST3` → **Re-scan**. Piste 1 :
Input = canal GUITAR du GCS-5 (mono), monitoring activé ; FX : **JAMRACK GTM Fx** puis
**ReaSynth** derrière lui ; clic droit sur le bouton d'armement → *Record: output* →
*Record: output (MIDI)* ; armer, enregistrer 10 notes.
- [ ] On entend ReaSynth pendant le jeu ? (oui/non)
- [ ] Après l'arrêt, un item MIDI **contenant des notes** apparaît sur la piste ? (oui/non)

**2. Live — l'effet (a).** Piste audio « Guitare » : *Audio From* = canal GUITAR, *Monitor*
= **In**, **JAMRACK GTM Fx** en premier. Piste MIDI « Synthé » : *MIDI From* = Guitare,
puis dans le menu du dessous **JAMRACK GTM Fx** ; *Monitor* = **In** ; un instrument
réglé sur **plage de bend 2** (Wavetable / Operator : *Pitch Bend Range* = 2).
- [ ] *MIDI From* → Guitare liste-t-il **JAMRACK GTM Fx** ? (oui/non)
- [ ] Notes reçues (le synthé joue un do à chaque attaque) ? (oui/non)
- [ ] **Bend entendu pendant la note** : le do monte d'**un ton** puis redescend ? (oui/non ;
  s'il monte de six tons, l'instrument est resté en plage 12)

**3. Live — l'instrument (b).** Piste MIDI : **JAMRACK GTM Inst** ; sur cette piste,
le panneau du plugin a-t-il un sélecteur ***Audio From*** (side-chain) ? Si oui :
side-chain = piste Guitare ; une deuxième piste MIDI avec un instrument, *MIDI From* =
cette piste.
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
Build : (ligne lue dans l'éditeur du plugin, ex. « JAMRACK GTM Fx 0.0.1 (a1b2c3d) »)

1. REAPER : ReaSynth entendu oui/non ; item MIDI avec notes oui/non
2. Live, Fx : listé dans MIDI From oui/non ; notes oui/non ; bend d'un ton entendu oui/non
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

*À remplir après la soirée (date, Live, tampon, fréquence, pilote, build).*

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
