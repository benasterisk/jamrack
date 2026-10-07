# Faisabilité : un plugin « guitare → MIDI » pour les DAW à partir de JAMRACK

Étude du 7 octobre 2026 pour le propriétaire du projet. Question posée : « peut-on
prendre uniquement la partie GUITARE → MIDI de JAMRACK et en faire un plugin pour
Ableton Live et les autres DAW, qui ressorte du MIDI vers les pistes ou vers une
sortie MIDI ? ». Aucune ligne de code n'a été écrite ; tout est recherche sourcée.
Chaque chiffre renvoie à une source `[n]` (section 9). Ce qui n'a pas pu être
vérifié est dit tel quel.

Vocabulaire : un **DAW** est un logiciel de production (Ableton Live, Logic, Cubase…) ;
un **plugin** est un module chargé dans une piste ; un **format** (VST3, AU, AAX,
CLAP) est le contrat technique entre le DAW et le module ; un **port MIDI virtuel**
est un câble MIDI logiciel entre deux applications ; un **tampon audio** (buffer) est
le paquet d'échantillons que le pilote livre d'un coup, source principale de latence.

---

## 0. Réponse courte

1. **Faisable** : le moteur (1 634 lignes de JavaScript pur sur tableaux typés, sans
   API web) se porte presque ligne à ligne en C++ [86].
2. **Ça existe déjà depuis 2014** : Jam Origin *MIDI Guitar* (version 3 en bêta depuis
   février 2024, toujours bêta le 7 octobre 2026), plugin sur la piste audio de la
   guitare, MIDI routé par le DAW ou port virtuel, polyphonique, 149,95 USD, code
   fermé, aucune précision publiée [16][17][18][19]. Aucun équivalent libre, maintenu,
   polyphonique et mesuré n'existe [26][28][29].
3. **Recommandation** : un plugin **VST3** « effet audio avec sortie MIDI », construit
   avec **JUCE** (licence gratuite *Starter*, sources du plugin en MIT), d'abord pour
   **Windows + Ableton Live 12**, CLAP en sous-produit, AU (macOS) et application
   autonome ensuite. Pas d'AAX (Pro Tools) en v1.
4. **Comment ça sort.** Ce n'est pas un « VST instrument » : dans Live, un instrument
   n'a pas d'entrée audio, et un traqueur en a besoin. Le plugin se pose donc comme
   **effet** sur la piste audio de la guitare ; une piste MIDI choisit *MIDI From* =
   piste Guitare → JAMRACK et porte l'instrument, l'*Instrument Rack* ou le *Drum Rack*
   (ou *MIDI To* = port externe pour un synthé matériel) [1][2][16]. Hors DAW, ou pour
   Pro Tools et GarageBand : application autonome + port MIDI virtuel [8][16].
5. **Ce que le plugin saura faire** : exactement ce que fait la page aujourd'hui, ni
   plus ni moins. **MONO** est la partie fiable et mesurée. **POLY** reste la bêta du
   navigateur : meilleure que MONO sur les notes seules, « doubles et triades au
   mieux » (la porte du plan § 13 a **échoué** : doubles 55,8 % contre 70 visés, triades
   53,5 % contre 60) [85] ; le réglage livré aujourd'hui mesure F1 solo 80,7 %, doubles
   57,0 %, triades 43,6 %, précision 77,8 % [87] ; POLY ne décide que sur une **attaque
   pincée** (une note frottée ou un swell ne déclenche rien, par construction) et n'a
   **pas de bend par note** [87]. Le passage en plugin ne change ni la précision ni ces
   limites.
6. **Effort** : 10 à 14 sessions d'agents et 6 à 8 soirées d'essai du propriétaire
   (compiler via la CI, installer dans Live, tester au Gigcaster), soit 1 à 3 mois
   calendaires en borne haute. Pour comparaison, tout le port POLY (prototype, port JS,
   intégration, calibration) a tenu en ~50 heures et 46 commits du 3 au 5 octobre 2026
   [106] ; le plugin ajoute un cycle compilation → installation → essai par jalon.
7. **Argent** : 0 EUR pour une bêta Windows (JUCE Starter, VST3, CLAP et CI GitHub
   gratuits) ; 99 USD/an pour signer une version macOS ; 150 à 300 USD/an pour un
   certificat Windows si l'on refuse de livrer non signé [52][63][71][72][97].
8. **Gain réel** : le plugin retire les tampons du navigateur, **de l'ordre de 8 à
   15 ms sur votre PC** (estimation du dépôt pour un Chrome déjà réglé au minimum,
   comme JAMRACK l'est : `latencyHint 0`, EC/NS/AGC coupés) [84] ; le chiffre de 20 à
   40 ms ne vaut que par rapport à un Chrome **non réglé** [80][88]. Aucune mesure
   publiée de Chrome réglé sur Windows ; le gain dépend du tampon que le pilote GCS-5
   du Gigcaster supporte sans craquer (le 6e cran craquait déjà) [84] : **à mesurer en
   M0** avec RTL Utility. Ce que le plugin ne retire **pas** : le délai de détection,
   **mesuré 9 à 36 ms selon la corde** (31 à 36 ms sur le mi grave : 1 à 2 périodes de
   la note plus le saut du transitoire ; le plancher physique seul serait 12 à 24 ms)
   [84]. La précision mesurée sur GuitarSet ne change pas.

---

## 1. Ce qui existe déjà

| Produit | Type | Poly | Formats | Prix | Latence annoncée / précision publiée |
|---|---|---|---|---|---|
| **Jam Origin MIDI Guitar 2** (2.2.1, stable) | plugin + application | poly (+ mono) | Win : app, VST2/3 ; mac : app, AU, VST2/3 ; iOS : app, AUv3 [17] | 149,95 USD (99,95 en 2017) [18][20] | app +3 à 6 ms, tampon ≤ 128 ; un utilisateur : ~40 ms [20][23] / aucune |
| **Jam Origin MIDI Guitar 3** (3.0.68 Win, 3.0.74 mac, **bêta**) | idem, MPE / MIDI 2.0 | poly | Win : app, VST2/3 ; mac : app, AU, VST2/3 ; iOS : AUv3 ; **ni AAX ni CLAP** [17] | 149,95 USD, gratuit pour MG2 [18][23] | « blazing fast », sans chiffre [19] / aucune |
| Vochlea Dubler 2 | voix → MIDI | 1 voix | app, VST3, AU | 99 USD [24] | « 10-12 ms » (résumé, non relu) / aucune |
| imitone | voix → MIDI | 1 voix | app, VST Windows | 29 / 59 / 99 USD [25] | « < 30 ms » / aucune |
| Dodo MIDI 2.1 (2025) | audio → MIDI gratuit, fermé | non précisé | VST3, AU | gratuit [26] | « imperceptible » / aucune |
| Melda MTuner | accordeur avec sortie MIDI | mono | VST/VST3/AU/AAX | gratuit [27] | — / aucune |
| Guit2Mid, Warf (GitHub) | plugins JUCE expérimentaux, 0 étoile | poly / mono | VST3 | libres, licence floue [28][29] | 27-46 ms / synthétique seulement |
| Fishman TriplePlay (hexaphonique) | matériel | poly | USB/MIDI | Express 199,95 USD [30] | ~10 ms (2013) [31] / aucune |
| BOSS GM-800 + GK-5 | matériel | poly | USB/MIDI | 749,99 + 249,99 USD [32] | sans chiffre / aucune |
| Sonuus G2M V3 | pédale | mono | MIDI 5 broches | 99,99 USD [33] | « near zero » / aucune |
| **JAMRACK** | page web (AudioWorklet) | mono + POLY bêta (attaques pincées seulement, pas de bend par note) [87] | aucun | gratuit, **MIT** | détection 9 à 36 ms selon la corde + tampons navigateur [84] / **oui** |

Ce que disent les utilisateurs : les notes seules passent bien, les accords restent le
point faible même pour MG2 (« far from flawless », doubles déclenchements) [21] ; le
matériel hexaphonique suit mieux les accords mais impose un micro [31][32] ; le
plancher de latence est physique, « plus de 10 ms » sur l'octave grave [34].

**Où se placerait un plugin JAMRACK.** L'équivalent libre de MIDI Guitar, même
déploiement (plugin sur la piste audio ; application + port virtuel en secours), même
promesse « sans micro spécial » [16]. Il serait le seul de la catégorie à publier des
mesures : GuitarSet, MONO solo, F1 71,8 %, précision ~72 % / rappel ~84 % avec la
fenêtre asymétrique [84] ; POLY, **réglage livré** (`DECOMPOSER` : parcimonieux
τ 1e-3, 8 itérations), sur les six jeux du banc (13 624 notes) : F1 solo 80,7 %,
doubles 57,0 %, triades 43,6 %, précision 77,8 % ; mode ÉCO automatique sous charge :
79,7 / 54,2 / 38,3 / 74,3 ; fantômes ≤ 1 % [87]. Les chiffres du prototype Python
(notes isolées F1 81 %, doubles 55,8 %, triades 53,5 %, précision 81,8 % [85]) ne
valent que pour le mode dense à 15 itérations, trop coûteux sur le PC du propriétaire
[87] : ce sont une borne haute, pas ce que le plugin embarquerait. Dans tous les cas la
porte du plan § 13 (doubles ≥ 70 %, triades ≥ 60 %) a **échoué** [85][87]. Jam Origin
ne publie rien : nous n'avons **aucune mesure** de leur produit, donc aucune
comparaison chiffrée possible. Réserves à écrire à côté : GuitarSet est de la guitare
acoustique enregistrée en hexaphonique [35] ; les systèmes académiques hors ligne
atteignent ~88 % de F-mesure dessus [36]. Message honnête : « mesuré et temps réel »,
et pour POLY « notes seules mieux que MONO, doubles et triades au mieux, attaques
pincées seulement », sans parité revendiquée avec le matériel hexaphonique sur les
accords.

---

## 2. Comment un plugin envoie du MIDI dans un DAW

### 2.1 Les deux routes

**Route A : le DAW route la sortie MIDI du plugin.** Le plugin est inséré sur la piste
audio de la guitare et déclare une sortie d'événements ; une seconde piste
(instrument) le choisit comme entrée MIDI. C'est ce que fait Jam Origin en VST [16] et
ce qu'Ableton documente [1]. Les quatre formats savent émettre du MIDI (VST3 : bus
d'événements de sortie [43] ; AU : propriété `MIDIOutputCallback`, « requires host
support » [45] ; CLAP : port de notes de sortie [47] ; AAX : plugins MIDI depuis Pro
Tools 2024.3 [40]). **La vraie question est ce que chaque DAW en fait**, et cela dépend
du DAW et du type de plugin (effet ou instrument).

**Route B : application autonome + port MIDI virtuel.** L'application ouvre elle-même
l'interface et publie un port que tout DAW voit comme un clavier. Gratuit et sans
pilote sur macOS (CoreMIDI, `MIDISourceCreate`) [73] ; sur Windows il faut loopMIDI
(pilote tiers gratuit) [8][74] ou les nouveaux *Windows MIDI Services* (Windows 11
24H2+, déployés depuis février 2026 ; outil de bouclage « released to consumers in
November 2026 », déjà disponible pour les développeurs ; « Both types of loopbacks
create MIDI 1.0 API ports », donc visibles par tout DAW) [76][77]. Inconvénients : un processus de plus et, sur Windows, un
pilote ASIO souvent mono-client, donc l'application et le DAW peuvent se disputer
l'interface (Vochlea le documente pour Dubler) [79]. Le pilote du BOSS Gigcaster 5
est-il multi-client ? Non vérifié.

**Effet ou instrument ?** Un traqueur a besoin d'une entrée audio, d'une sortie audio
(Live refuse de charger un VST3 sans bus audio [13]) et d'une sortie MIDI. En VST3 la
catégorie « Fx » avec bus d'événements convient ; « Analyzer » est à éviter (« not
selectable as insert ») [44]. CLAP a même une catégorie dédiée, `note-detector` : « Add
this feature if your plugin converts audio to notes » [48]. Seul Logic impose le type
*instrument* (matrice ci-dessous).

### 2.2 Ableton Live 12, pas à pas

Table d'Ableton (6 octobre 2026) : sortie MIDI directe **AU non / VST2 oui / VST3
oui** ; « To route MIDI from a plug-in, use the VST version » [2]. Historique : VST3
chargé depuis Live 10.1 [3] ; le routage des **notes** d'un VST vers une autre piste
est documenté par Ableton sans numéro de version (« Live Versions: All ») [1][2] ;
**10.1.25** : « Live now receives and routes MIDI CC, Pitch Bend, and Aftertouch events
sent from a VST3 plug-in device to a MIDI-out bus » [3] ; **11.3.25** (8 mai 2024) :
tous les CC 0-127 [4] ; les notes de Live 12.0 à 12.4.6 (15 septembre 2026), lues en
entier, ne changent rien [5]. Donc notes et **bend** sortent d'un VST3 ; aucune
régression trouvée en 12.x, mais **à mesurer** en 12.4 (non vérifié).

Procédure (article Ableton du 18 août 2026 [1], placement sur piste audio d'après Jam
Origin [16], réglage du Gigcaster d'après l'essai du 5 octobre [84]) :

1. Piste **audio** « Guitare », *Audio From* = *Ext. In*, **canal GUITAR brut** du
   Gigcaster en mode **MTK-STREAM** (pas USB MAIN ni USB MONITOR), **MIX MINUS = ON**,
   EFFECTS du canal guitare éteints : sinon la sortie de Live revient par USB, le
   traqueur redétecte le synthé et « les notes tournent toutes seules », comme constaté
   avec la page web [84]. *Monitor* de cette piste sur **In** (pas *Auto* : voir le
   piège de l'armement exclusif ci-dessous). Plugin JAMRACK en premier dans la chaîne
   (avant tout simulateur d'ampli : il lui faut le signal sec).
2. Créer une piste **MIDI** « Synthé ».
3. Sur « Synthé », *MIDI From* = la piste « Guitare » ; dans le sélecteur du dessous,
   choisir **le plugin JAMRACK** (listé par son nom).
4. *Monitor* de « Synthé » sur **In** (ou *Auto* et armer la piste) [6].
5. Poser l'instrument : instrument Live, *Instrument Rack* (toutes les chaînes
   reçoivent le MIDI) ou *Drum Rack* (chaque pad a un sélecteur *Receive* de note).
6. Enregistrer : armer « Synthé » ; *Capture MIDI* marche sur toute piste armée ou
   monitorée.
7. Synthé matériel : *External Instrument* sur la piste MIDI, curseur *Hardware
   Latency* [6].

Pièges propres à Live :

- **Armement exclusif** : un effet sur une piste audio ne reçoit l'entrée que si la
  piste est armée ou en *Monitor In/Auto* [6]. Or « Clicking one track's Arm button
  unarms all other tracks unless the Ctrl (Win) / Cmd (Mac) modifier is held » [6b] :
  avec « Guitare » en *Auto*, armer « Synthé » pour enregistrer coupe l'entrée de
  « Guitare » et le plugin devient muet. D'où *Monitor In* sur « Guitare », ou
  Ctrl/Cmd-clic pour armer les deux pistes.
- **Boucle USB du Gigcaster** : en mode USB « 2 MIX », le PC reçoit son propre mixage ;
  réglage qui marche : MTK-STREAM + MIX MINUS ON + canal GUITAR brut, et monter d'un
  cran le tampon du pilote GCS-5 si ça craque [84].
- **Un seul canal MIDI** : « Live merges all MIDI channels to one channel when being
  routed internally from track to track » [1]. Sans effet pour un traqueur sur un
  canal ; fatal pour une sortie par corde ou MPE (ce que propose MG3). MPE n'est
  documenté que pour Max for Live [4] : un futur bend par note POLY devra être testé.
- **Horodatage de l'enregistrement** : avec *Monitor Auto/In*, Live décale le début de
  l'enregistrement de la latence globale ; Live 12 ajoute *Keep Monitoring Latency in
  Recording* ; Ableton interdit *Track Delay* pour corriger une désynchronisation [7].
  Le décalage d'un clip MIDI généré depuis une autre piste n'est documenté nulle part
  pour Live 12 (seul un rapport de forum de 2016) : à mesurer sur métronome. Doctrine
  de Live : « latency is preferable to jitter », MIDI horodaté par le pilote [104].
- **Reduced Latency When Monitoring** ne contourne que la latence des *autres* pistes
  [81].
- **AU inutile dans Live**, même sur macOS : livrer le VST3 [2].

### 2.3 Matrice par DAW

(A) = plugin dans le DAW, MIDI routé par l'hôte ; (B) = application + port virtuel.

| DAW | (A) | Comment / réserve | (B) |
|---|---|---|---|
| **Ableton Live 12** | **Oui** VST2/VST3, **non** AU | *MIDI From* → piste → plugin, Monitor In ; canaux fusionnés [1][2] | Oui (IAC, loopMIDI documentés par Ableton) [8] |
| **Logic Pro 11/12** | **Oui avec réserve** | Sortie MIDI des AU prise en charge depuis 10.8 (« Audio Units that output MIDI », notes 10.8/10.8.1) [15c] ; routage vers une autre piste depuis 11.0 : *Internal MIDI In → Instrument Output* liste les pistes d'instrument logiciel « capable of sending out MIDI events » [14][15]. **Les pistes audio ne sont pas listées** (guide Apple [15] non relu par l'outil, seule sa table des matières est servie ; confirmé par le fil Waves OVox : « Logic does not permit/support MIDI out from an audio track ») [17b]. Un AU effet sur la piste guitare ne sert donc à rien par le routage de Logic : soit une variante AU *instrument* à entrée side-chain (jugée « clumsy » par des utilisateurs de Waves OVox) [17b], soit le modèle Jam Origin : l'AU sur la piste audio crée **son propre port CoreMIDI**, et Jam Origin note que l'application autonome « will add a bit of latency compared to » l'AU [16][22]. Logic 12.0–12.4 (29 sept. 2026) n'ajoutent rien [15b]. | Oui [16][73] |
| **Cubase / Nuendo 14** | **Oui** VST3 | *Input Routing* de la piste MIDI = « NN.Plugin – MIDI Out » (guide NI pour Cubase 8, peut-être daté) [37] ; VST2 désactivé par défaut [38] ; bug de timing signalé à 4096 échantillons (août 2026) [39] ; page officielle pour un insert audio non retrouvée | Oui |
| **REAPER 7.82** | **Oui**, le plus permissif | Sortie MIDI des VST depuis 2006, *Record: output (MIDI)* sur la même piste, CLAP et AUv3 (7.55, nov. 2025) [9][10] | Oui |
| **Bitwig 5** | **Oui avec réserve** | Notes passées dans la chaîne « like a bucket brigade » ; *Note Receiver* ou entrée de piste = piste source ; l'enregistrement en clip passe par l'entrée de piste (rapports 2015–2023, à revérifier en 5.x) [11][12][12b] | Oui |
| **FL Studio 2024+** | **Partiel** | Wrapper *Output port = Input port* [41] ; enregistrement des notes routées signalé défaillant (2017), routage VST3 signalé cassé (2018), sans démenti officiel (non vérifié 2025/2026) [42][42b] | Oui |
| **Studio One 7** | **Oui** VST3 | Piste instrument, *Instrument Input* = le plugin ; l'AU n'expose pas de sortie MIDI (mars 2025) [49][49b] | Oui |
| **Pro Tools 2024.3+** | **Non** pour un effet audio | Seuls les AAX **MIDI** sur insert de piste instrument sont documentés [40] ; AAX = accord Avid, iLok, signature PACE [50] ; Jam Origin renvoie vers l'application + loopMIDI [16][17] | Oui, seule voie |
| **Reason 12.5+** | **Non** | « Reason does not support VSTs that output MIDI » (18 juillet 2026) [51] | Oui |
| **GarageBand** | **Non** | Seuls AU effet et instrument documentés [46] | Oui |

### 2.4 Max for Live

Depuis Live 11.0, un effet audio Max for Live émet du MIDI visible dans *MIDI From/To*,
MPE compris [4][55]. Il faudrait réécrire le DSP en Max/gen~/RNBO ou l'embarquer comme
external C ; Max for Live est inclus dans Live Suite (749 USD) ou vendu 199 USD, et
éditer du gen~ dans Live exige une licence Max [56][57]. Écarté : Ableton seulement,
DSP à réécrire, et l'external C serait de toute façon le même port C++ que le VST3.

---

## 3. Les technologies à jour en 2026

### 3.1 Formats

| Format | Licence du SDK (oct. 2026) | État | Pour nous |
|---|---|---|---|
| **VST3** | **MIT depuis VST 3.8.0 (20 oct. 2025)** ; GPLv3 et licence propriétaire « no longer available » [52][53] | Standard partout sauf chez Apple ; CC/bend de sortie via `LegacyMIDICCOutEvent` (SDK ≥ 3.6.12) [43] | **Obligatoire** (seul format routé par Live hors VST2) |
| **VST2** | SDK fermé depuis octobre 2018 [58] | Encore chargé par Live 12 ; désactivé par défaut dans Cubase 14 [38] | Pas de nouveau code |
| **AU v2/v3** | Xcode, gratuit | Obligatoire pour Logic/GarageBand ; Live ignore sa sortie MIDI [2] | Plus tard, avec port CoreMIDI intégré |
| **AAX** | Accord Avid, iLok, signature PACE [50] | Pro Tools seulement | **Hors périmètre v1** |
| **CLAP** | MIT [47] | Bitwig, REAPER, FL Studio 2024, Studio One 7 ; pas Live, Logic, Cubase, Pro Tools [59][60][61] | Sous-produit gratuit |

### 3.2 Frameworks (bibliothèques produisant plusieurs formats depuis un seul code)

| Framework | Licence | Formats | UI web réutilisable ? | Remarques |
|---|---|---|---|---|
| **JUCE 8/9** (C++) | AGPLv3 **ou** commercial ; **Starter gratuit** jusqu'à 20 000 USD de revenus (dons inclus), Indie 40 USD/mois ou 800 USD, Pro 175 USD/mois ou 3 500 USD ; plus d'écran de démarrage ; JUCE 9 (21 juillet 2026) garde ces conditions [62][63][64][65] | VST3, AU, AUv3, AAX, LV2, autonome ; **pas de CLAP officiel, même en JUCE 9.0** (annonce du 21 juillet 2026 : « currently working on AudioProcessor v2, the foundation for … unique CLAP features ») [64][66] ; CLAP via l'extension MIT `clap-juce-extensions`, qui annonce JUCE 6/7/8 et jamais JUCE 9 : à tester, sinon rester en JUCE 8.x pour la cible CLAP [67][68] | Oui, `WebBrowserComponent` depuis JUCE 8 (WebView2 / WebKit) [69] | Le plus documenté ; son README demande aux outils IA de prévenir qu'une licence commerciale peut être requise [70] |
| **iPlug2** (C++) | zlib-like, permissive [71b] | CLAP, VST2, VST3, AUv2/3, AAX, autonome | Oui | Communauté plus petite ; Linux non listé |
| **DPF** (C++) | ISC/MIT, sans SDK Steinberg [72b] | LADSPA, LV2, VST2, VST3, CLAP | Non (C++/OpenGL) | Orienté Linux ; AU incertain |
| **nih-plug → nice-plug** (Rust) | ISC, mais liaisons VST3 GPLv3 dans nih-plug (« en maintenance ») ; fork nice-plug actif (14 sept. 2026) [73b][74b] | VST3, CLAP, autonome ; pas d'AU | Non | Port DSP plus sûr ; statut GPL du VST3 non vérifié |
| **clap-wrapper** | MIT [75b] | CLAP → VST3, AUv2/3, AAX, autonome | — | Complément d'un code « CLAP d'abord » |
| **Cmajor** | GPLv3 ou commercial [76b] | JUCE ou CLAP | Oui | Troisième langage, pas MIT |
| JS embarqué (QuickJS/Duktape) | MIT [77b][78b] | — | — | **Non** : interpréteurs sans JIT, ramasse-miettes sur le fil audio ; prototype jetable au mieux |

### 3.3 Décision

**Formats : VST3 (Windows), puis CLAP, puis AU + VST3 macOS, puis autonome. Pas
d'AAX.** VST3 est le seul format que Live route [2] et il est désormais MIT [52] ; CLAP
coûte quelques lignes ; AU ne sert que Logic/GarageBand et doit embarquer un port
CoreMIDI pour être utile [15][16].

**Framework : JUCE sous licence Starter, sources du plugin en MIT.** Parce que (1) les
agents écrivent tout et JUCE a le plus de documentation et d'exemples, dont un
`WebViewPluginDemo` ; (2) la carte HTML/CSS/JS existante se réutilise dans la WebView
[69] ; (3) Starter est gratuit et le projet n'a aucun revenu [63] ; (4) l'application
autonome et l'AU viennent sans second outillage. Prix à payer : JUCE reste sous son
contrat (chaque personne qui recompile l'accepte pour sa copie), avis à reproduire
dans notre README [70]. Repli si c'est jugé incompatible avec l'esprit MIT : **iPlug2**.
La combinaison JUCE-AGPL rendrait le binaire AGPL ; ce n'est pas un avis juridique.

**Plateformes et support (périmètre v1).** Pris en charge : **Windows + Live 12 +
REAPER**, testés par le propriétaire. Tout autre hôte (Cubase, Bitwig, FL, Studio One)
est « compile, non testé ». **macOS** : construit par la CI mais non testé sans testeur
Mac (M4). **Linux** : VST3 + CLAP produits par la même CI (REAPER et Bitwig les
chargent nativement, pas de signature [60][61]), livrés « non testés ». **iOS / AUv3**
(que Jam Origin livre [17]) : **hors périmètre**, parce qu'il faut le programme Apple
(99 USD/an) [99], une distribution App Store ou TestFlight et un testeur iPhone ; la
page gh-pages reste la voie iOS. Maintenance : le propriétaire ne code pas, donc chaque
casse due à une mise à jour de DAW, d'OS ou de JUCE devient une session d'agent
déclenchée depuis une issue GitHub, et les sources de ce rapport montrent qu'il y en a
chaque année (déploiement Windows MIDI Services de 2026 cassant loopMIDI et le port
virtuel de Dubler [76][79] ; sortie MIDI AU de Logic 10.8 « buggy » [15d] ; timing
VST3 cassé dans Cubase à 4096 [39]). Compter **une session par casse majeure**, une à
trois par an, en plus des jalons.

---

## 4. Ce que cela demande à notre code

### 4.1 Ce qui est porté (lecture du dépôt au commit `bcbb30d` [86])

| Fichiers | Lignes | Destination |
|---|---:|---|
| `js/audio/guitar/tracker.js` (MONO : DC, biquads, décimation, YIN, états, bend) | 550 | bibliothèque DSP C++ |
| `poly/engine.js` 301, `nmf.js` 212, `notes.js` 207, `resample.js` 138, `bank.js` 121, `fft.js` 66, `profile.js` 39 (POLY) | 1 084 | idem ; banque (122 × 1025, 500 Ko) rendue hors fil audio en 53-56 ms |
| `poly/calibrate.js` (FFT 65 536 points, hors ligne) | 291 | bibliothèque, fil de fond |
| **Total DSP** (1 634 en temps réel) | **1 925** | |

Reste au navigateur ou est refait dans la coque : `worklet.js` (98), `js/input/guitar.js`
(299, dont le jeu de sécurité `sounding`), `profiles.js` (75, IndexedDB → fichiers
JSON), `js/ui/calibration.js` (243), la carte de `rack.js`, le routage de `main.js`
(le DAW s'en charge). Points relevés : arithmétique en double partout avec quelques
tableaux float32 (banque, historique YIN), donc un port **double + banque float32** est
le fidèle, un port tout-float32 dérive via le warm start de la NMF et devrait être
mesuré ; trois `sort()` par hop et des allocations sur le chemin audio à remplacer par
des tableaux préalloués ; lectures hors bornes silencieuses en JS, indéfinies en C++
(une passe ASan/UBSan sur le corpus) ; dénormaux à couper (FTZ/DAZ) [86]. Les moteurs
travaillent échantillon par échantillon : la taille du tampon hôte n'influence pas les
décisions, seulement l'horodatage et le budget CPU.

### 4.2 L'oracle d'équivalence

Même standard que le port Python → JS : **note pour note**. Le JS POLY reproduit le
prototype Python 13 624 notes sur 13 624 en **mode dense** (arithmétique exacte) [87].
Attention au réglage : le compte note pour note publié (13 591 identiques, 31 à ± 1
hop, 2 manquantes, 1 en trop) a été mesuré pour le **premier** réglage parcimonieux
(τ 1e-4, 15 itérations), pas pour celui **livré** aujourd'hui (`engine.js` :
τ 1e-3, 8 itérations), dont on ne connaît que les écarts de score (−0,1 point de F1 en
solo, −1,6 sur les triades) [87]. La référence du port est donc le **moteur JS aux
mêmes réglages**, le mode dense servant de vérification exacte. Concrètement : (1)
régénérer les dumps GuitarSet (`test/dump-events.mjs`, `test/poly-dump-events.mjs` ;
4,3 Go non versionnés) ; (2) écrire le script de diff d'environ 50 lignes qui manque
(même prise, même note, onset et offset à ± 1 hop, vélocité à 1e-3) ; (3) **établir
d'abord** avec lui l'écart JS dense ↔ JS livré, qui devient ensuite la barre
d'acceptation du port C++ (attendu : quelques dizaines de notes sur 13 624, à
confirmer). Où ça tourne : la CI GitHub ne téléchargera pas 4,3 Go à chaque commit ;
elle ne compare que le corpus synthétique de `test/plucks.mjs` (17 tests MONO, 9 POLY,
15,7 s), rendu une fois en WAV et nourri aux deux implémentations ; le diff GuitarSet
(36 prises solo + comp, puis les six jeux) se lance à la main sur le PC du propriétaire
avant chaque jalon, ou sur un sous-ensemble de prises mis en cache (quelques centaines
de Mo). Coût mesuré ici (Node 22, 4 cœurs) : décomposeur 0,689 ms/hop, hop complet
0,971 ms en moyenne, 2,73 ms au pire ; MONO 13,6 % d'un cœur ; sur le PC du
propriétaire, réglage livré : 1,10 ms/hop en Node, 1,62 ms (61 % du budget) dans le
worklet avec le synthé qui joue [86][87] ; la première compilation C++ doit imprimer
les mêmes chiffres sur la même machine.

### 4.3 Paramètres et MIDI

| Paramètre | Plage / défaut | Devenir |
|---|---|---|
| GAIN | 0,1..10 / 1 | multiplie le bloc d'entrée |
| SENS | 0..1 / 0,5 | MONO : porte −36..−60 dBFS ; grisé en POLY |
| DECAY | 0..1 / 0,5 | MONO : note-off 15..45 dB sous la crête ; POLY fixe (20 dB) |
| DYN | 0..1 / 0,7 | loi de vélocité MONO |
| BEND | bool / on | MONO : bend ou redéclenchement chromatique |
| **RANGE** (nouveau) | 2 / 12 / 24 / 48 demi-tons, défaut 2 | convertit le bend en demi-tons en molette 14 bits ; l'instrument récepteur doit avoir la même plage |
| OCTAVE, TRANSPOSE | −2..2, −12..12 | décalage |
| MODE | mono / poly | bascule avec flush ; POLY « bêta » dans le libellé : décide seulement sur une attaque pincée, pas de bend par note [87] |
| PROFILE | générique ou profil nommé | re-rendu de la banque hors fil audio |
| BUFFER, INPUT | — | **disparaissent** : l'hôte possède tampon et entrée |

Événements : note-on (midi borné 0..127, vélocité 1..127), note-off, bend global en
MONO (pas de MPE : MONO a une voix, POLY n'a pas de bend), reset du bend à chaque
note-on/off, note-off de tout ce qui sonne au stop, au bypass, au changement de mode
ou de profil ; les mesures (niveau, accordeur, CPU, ECO, latence) vont à l'interface
par une file sans verrou, jamais en MIDI. Latence déclarée : **0 échantillon** (la
détection n'est pas un retard compensable, section 5). Un canal MIDI par défaut,
sélectionnable pour Cubase/REAPER.

### 4.4 Calibration, interface, langage

**Profils** : la banque générique (`profile.js`) est embarquée ; le format JSON existe
déjà (`{ format: 'jamrack-guitar-profile', version: 1, bLaw[6][2], prof[6][40], … }`) ;
le plugin lit et écrit les mêmes fichiers, un profil fait dans le navigateur se
transfère tel quel. `calibrate.js` (tampon de capture 31 s, ~3 Mo) tourne sur un fil de
fond ; l'assistant (une corde à vide à la fois, 12e case optionnelle, refus nommés) est
la partie la plus longue à refaire ; précision sur cordes synthétiques : B à 12 %, f0 à
3 cents [87].

**Interface** : (a) l'éditeur générique de JUCE (gratuit, laid, suffisant pour la bêta)
pour les jalons 1 à 3 ; (b) la carte HTML existante servie depuis le binaire par
`WebBrowserComponent`, WebView2 étant présent sur Windows 11 et « la grande majorité »
des Windows 10 [69][82], à partir du jalon 5, car l'assistant de calibration existe
déjà en HTML et 12 langues.

**C++ ou Rust** : le port DSP serait moins risqué en Rust (bornes vérifiées, pas de
comportement indéfini), la coque moins risquée en JUCE (hôtes, autonome, éditeur) [86].
Décision : **tout C++ avec JUCE**, le DSP dans une bibliothèque statique séparée, sans
dépendance au framework, avec son harnais de dump et une passe de sanitizers ; une
seule chaîne d'outils, AU, autonome et WebView inclus. Si le framework change un jour,
la bibliothèque DSP n'y touche pas.

**WebAssembly** : faisable (module `wasm32` chargé dans l'AudioWorklet comme la banque
aujourd'hui) mais il casserait la règle « aucun build, source lisible » et MONO n'y
gagnerait rien [84]. À revoir seulement si les deux implémentations divergent ou si
POLY manque de marge dans le navigateur.

---

## 5. Latence

« RTL » = aller-retour complet entrée → sortie mesuré en boucle. C'est bien
l'aller-retour qui compte dès que le synthé est dans le DAW : le son ressort par le
tampon de sortie du même pilote, comme dans le navigateur.

| Chemin | Mesure | Source |
|---|---|---|
| Chrome, Windows 10, défaut | RTL **62,8 ms** (Edge 60,8 ; Firefox 104,7) | WAC 2025, 100 passes [80] |
| Chrome, macOS, défaut | RTL 52,3 ms | [80] |
| Chrome, macOS, `latencyHint: 0`, sans EC/NS/AGC (notre réglage) | RTL **~19 ms** ; 19-23 ms avec Focusrite à 128 | [83][83b] |
| Chrome, Windows, réglé | **aucune mesure publiée** ; Chromium : « total typical delay of 35 ms » en sortie seule ; périodes Windows de 10 ms par défaut | [88][89] |
| DAW, ASIO natif (RME ADI-2 Pro FS), 64 @ 44,1 kHz | RTL **« a little less than 5.3 ms (232 samples) »** ; 256 : ~14,3 ms | [91] |
| DAW, ASIO natif (RME UCX II), 48 kHz | « RTL is less than 5 ms » à petits tampons (chiffres détaillés seulement dans une image hébergée sur un forum tiers, non reproduits en texte) [90][90b] ; ~3 ms à 32 (MusicTech) | [90][90b][90c] |
| DAW, Focusrite Forte, 64 @ 44,1 kHz | RTL 11,8 ms ; ASIO4ALL 15,8 ; FlexASIO 9,6 ; WDM < 15 ; DirectSound 122 | [91] |
| BOSS Gigcaster 5 (pilote GCS-5) | **non mesuré** ; le 6e cran du pilote craquait déjà avec la page web, un tampon de 64 n'est pas acquis | [84] |
| Détection par le traqueur (identique partout) | 1-2 périodes : 12-24 ms de physique sur le mi grave ; mesuré avec saut du transitoire : ~9 ms aigus, 17-20 ms sol/si, 31-36 ms mi grave | [84] |
| Bloc hôte 64 / 128 / 256 @ 48 kHz | 1,33 / 2,67 / 5,33 ms, au plus un bloc d'ordonnancement en plus | [92][93] |

Gain attendu, dans l'ordre utile : le dépôt estime déjà le gain d'un chemin natif à
**~8-15 ms sur Windows, ~3-5 ms sur macOS** pour JAMRACK tel que réglé (`latencyHint 0`,
EC/NS/AGC coupés) [84] ; les mesures tierces vont dans le même sens (Chrome réglé
~19 ms sur macOS contre ~5 ms en ASIO/CoreAudio) [83][91] ; le chiffre de **20 à 40 ms**
ne vaut que par rapport à un Chrome **par défaut** sur Windows [80][88], ce qui n'est
pas la configuration du propriétaire. Aucune mesure publiée de Chrome réglé sur
Windows ; le gain réel dépend du tampon que le pilote GCS-5 tient sans craquer [84].
Estimations dérivées des sources, pas une mesure de JAMRACK sur le PC du propriétaire.
**Le délai de détection reste** (9 à 36 ms selon la corde [84]). Pour situer : un
utilisateur de MG2 rapporte ~40 ms de bout en bout [23] et SOS ne chiffre que le
surcoût de l'application autonome (3 à 6 ms) [20] ; notre propre estimation pour le
plugin est une bande de 15 à 45 ms selon la corde et le tampon, dans celle du matériel
hexaphonique (~10 ms [31]) seulement sur les cordes aiguës.

Ce que Live fait de la latence d'un plugin qui émet du MIDI : rien d'utile pour nous.
Déclarer une latence ne fait pas **avancer** le MIDI : les hôtes la compensent en
**retardant** la sortie MIDI, pour que plugin et instruments restent alignés (« the DAW
delays MIDI output by the same amount so that the listener experiences hardware and
software instruments playing in sync », JUCE, 12 août 2025) ; le développeur du fil
mesurait 11 à 15 ms de retard dans REAPER sans rien déclarer, « 11 ms WORSE » en
déclarant 11 ms, aucun effet des valeurs négatives, et ~150 ms dans Digital Performer
[95]. La question « MIDI de sortie corrigé de la latence du plugin ? » posée sur le
forum Steinberg en 2019 n'a reçu aucune réponse de Steinberg [94]. D'où la règle :
déclarer **0**, documenter le délai mesuré par taille de tampon, laisser l'utilisateur
poser un *Track Delay* négatif (Live) ou *Recording Delay* (Logic) pour des
enregistrements calés [95][96] ; comportement exact de Live à mesurer en M0. Seule
façon de passer de l'estimation au chiffre : mesurer navigateur puis plugin sur le PC
du propriétaire avec RTL Utility en boucle sur le Gigcaster [91].

---

## 6. Distribution

| Sujet | Fait | Coût |
|---|---|---|
| Windows non signé | Un `.vst3` est une DLL chargée par le DAW ; SmartScreen bloque fortement les **installateurs** `.exe` non signés ; sa réaction à un `.vst3` extrait d'un zip est **non vérifiée** [97] | 0 |
| Windows signé | SignPath Foundation signe gratuitement l'open source ; certificat OV 150-300 USD/an ; Azure Artifact Signing ~9,99 USD/mois mais particuliers USA/Canada seulement [97][98] | 0 à 300 USD/an |
| macOS | Signature + notarisation = Apple Developer Program payant, sinon autorisation manuelle par l'utilisateur [72][99] | 99 USD/an |
| AAX | Compte Avid, build Pro Tools Developer, signature PACE gratuite, **iLok physique** ; Pro Tools refuse un AAX non signé [50][100] | iLok ; hors périmètre |
| Validation | `pluginval` (VST3/AU, niveau 5 = minimum de compatibilité, codes de sortie CI) [101] ; `clap-validator` [102] | 0 |
| CI | GitHub Actions gratuit sur dépôt public, exécuteur macOS M1 compris [71][103] ; Visual Studio Community gratuit [105] | 0 |
| Publication | GitHub Releases : zip par OS (VST3 + CLAP), checksums, notes avec la procédure Live et la table de latence | 0 |
| Autonome Windows et ASIO | Le SDK ASIO est GPLv3 ou accord propriétaire signé avec Steinberg [53][54]. **Décision proposée** : autonome v1 **sans ASIO** (WASAPI exclusif / `IAudioClient3`, latence à mesurer) pour garder un binaire distribuable sous MIT ; ou avec ASIO et binaire annoncé **GPLv3** (les sources restent MIT). L'accord propriétaire n'est pas recommandé. Sur le Gigcaster, dont le pilote GCS-5 est un pilote ASIO dédié [84], l'absence d'ASIO dans l'autonome annule une partie du gain de latence ; le plugin dans Live, lui, profite de l'ASIO du DAW. | 0 |
| Support | Chaque DAW a ses réglages ; la page DAW de Jam Origin, écrite pour MG2 et des DAW vieux de dix ans, montre ce qu'il faut maintenir [16]. Un `docs/plugin-daw.md` par hôte, issues GitHub comme seul canal ; périmètre supporté et coût de maintenance en section 3.3. | une session par casse majeure |

---

## 7. Plan proposé

« Session » = une session d'agent du type de celles qui ont produit le port POLY ;
« soirée » = une séance d'essai du propriétaire (récupérer le binaire compilé par la
CI, l'installer dans Live, jouer au Gigcaster, rapporter). Estimations, pas mesures.
Point de comparaison mesuré : le port POLY complet (prototype Python, port JS,
intégration, assistant de calibration, essais au Gigcaster compris) a pris environ
**50 heures calendaires et 46 commits du 3 au 5 octobre 2026** [106][87] ; le plugin
est plus lent parce que chaque jalon passe par compilation → installation → essai au
lieu d'un rechargement de gh-pages, d'où les soirées ci-dessous. « 1 à 3 mois » est
une borne haute, pas une prévision.

| Jalon | Contenu | Acceptation | Sessions | Soirées | Argent |
|---|---|---|---|---|---|
| **M0** Prototype jetable | VST3 JUCE « audio entrant → note fixe sortante » | Live 12 route la note par *MIDI From* ; type de piste confirmé ; latence entrée → MIDI mesurée à 64/128/256 | 1 | 1 | 0 |
| **M1** Port MONO | `tracker.js` → C++ (double + float32 aux mêmes endroits), horodatage par échantillon, harnais CLI au format de `dump-events.mjs`, script de diff | CI : plucks WAV identiques note pour note ; à la main sur le PC du propriétaire : GuitarSet 24 solo + 12 comp, même note, onset ± 1 hop sauf une poignée ; ASan/UBSan propre | 1-2 | 0 | 0 |
| **M2** Coque VST3 + CLAP | processeur audio-in/MIDI-out, paramètres de 4.3, état, `sounding`, flush, file de mesures, éditeur générique, CI Windows/Linux, pluginval ≥ 5, clap-validator | REAPER et Live 12 : notes, note-off, bend reçus et enregistrés ; latence par tampon documentée | 2-3 | 2 | 0 |
| **M3** Port POLY | sept fichiers → C++ ; sorts et allocations retirés ; banque sur fil de fond ; dense/parcimonieux ; budget ECO = min(hop, tampon hôte) ou décomposeur sur un fil avec +1 hop | six jeux (13 624 notes) : écart C++ ↔ JS livré égal à l'écart JS dense ↔ JS livré établi en 4.2 ; **≤ 1,1 ms/hop en Node et ≤ 1,6 ms dans l'hôte sur le PC du propriétaire** (chiffres du réglage livré [87]) ; MODE poly dans Live | 2-3 | 1-2 | 0 |
| **M4** AU + macOS | AU avec port CoreMIDI intégré, VST3 macOS, build sur l'exécuteur M1, signature, notarisation | pluginval AU ≥ 5 ; un testeur Mac vérifie Live (VST3) et Logic (AU + port) | 1-2 | 0 (testeur Mac) | 99 USD/an |
| **M5** Calibration + UI WebView | port de `calibrate.js`, fichiers de profil, carte HTML dans la WebView, 12 langues | profil navigateur = profil plugin ; tests de `poly-calibrate.test.mjs` reproduits | 2 | 1 | 0 |
| **M6** Autonome + port virtuel | cible Standalone JUCE ; audio **sans ASIO en v1** (WASAPI exclusif) ou ASIO avec binaire GPLv3 annoncé (section 6) ; sortie MIDI (matériel, loopMIDI, Windows MIDI Services, CoreMIDI) | Pro Tools/GarageBand reçoivent le MIDI ; latence WASAPI exclusif mesurée ; conflit mono-client documenté | 1 | 1 | 0 |
| **Total** | | | **10-14** | **6-8** | 0 à 99 USD/an (+ 150-300 si certificat Windows payant) |

Calendrier : borne haute 1 à 3 mois, dictée par les soirées d'essai, pas par les
sessions. M0 à M2 donnent déjà une bêta MONO utilisable dans Live.

**Décisions du propriétaire** (prises le 7 octobre 2026 : VST3 seul, Windows seul, tout gratuit, pas de Mac, dossier `plugin/` ; plan d'exécution dans `docs/plugin-plan.md`) :

1. **Formats** : VST3 + CLAP d'abord, AU ensuite, pas d'AAX (recommandé tel quel).
2. **Framework et licence** : JUCE Starter, sources MIT et avis JUCE dans le README ;
   ou iPlug2 pour une pile 100 % permissive au prix d'une documentation moindre
   (recommandé : JUCE).
3. **Application autonome** : oui, en dernier (M6), parce que le plugin couvre Live et
   que l'autonome ajoute le problème ASIO mono-client sur Windows ; et **sans ASIO en
   v1** (binaire MIT) ou avec ASIO et binaire GPLv3 annoncé (section 6).
4. **Où vit le code** : un dossier **`plugin/`** dans le dépôt `jamrack` (CMake séparé,
   JUCE récupéré par `FetchContent`, pas copié dans le dépôt), parce que l'oracle
   (`plucks.mjs`, `score.py`, `takes.json`, les scripts de dump) et la carte HTML sont
   dans `test/` et `js/`, et que la CI compare les deux implémentations sur le corpus
   synthétique à chaque commit (le diff GuitarSet reste manuel, section 4.2). Un
   dépôt séparé ne se justifierait que si les binaires de release gênaient le site
   statique (recommandé : `plugin/`).
5. **Mac** : trouver un testeur Mac et payer 99 USD/an, ou livrer macOS « compilé, non
   testé, non signé » (M4 reporté).

---

## 8. Risques et inconnues

Par ordre de probabilité :

1. **Sécurité temps réel** : rendu de banque (50 ms) et calibration (FFT 65 536) jamais
   dans le rappel audio ; échange atomique + flush [86].
2. **Budget CPU POLY à petit tampon** : un hop se termine tous les 128 échantillons à
   48 kHz ; avec un tampon hôte de 64 (1,33 ms), la décomposition doit tenir en 1,33 ms,
   pas 2,67. Mesures du réglage livré : 0,97 ms en moyenne et 2,73 ms au pire ici
   (Node 22, conteneur) ; 1,10 ms en Node et 1,62 ms en moyenne dans le worklet sur le
   PC du propriétaire, synthé en marche ; le **réglage précédent** (τ 1e-4, 15 it.)
   montait à 4,05 ms sur ce même PC chargé, ce qui a motivé le mode ÉCO [86][87]. La
   moyenne tient, le pire cas non : à trancher avant M2 (ÉCO = min(hop, tampon) ou
   décomposeur sur un fil avec +1 hop).
3. **Dérive numérique** en float32 pur, **dénormaux**, **comportement indéfini** sur les
   index : trois risques connus, trois remèdes connus [86].
4. **Pitch bend VST3 dans Live 12.4** : routé depuis 10.1.25 [3], jamais revérifié en
   12.x ; M0 le mesure.
5. **Timing des clips MIDI enregistrés** depuis une piste plugin dans Live 12 : non
   documenté ; M0/M2 le mesurent.
6. **Maintenance sans développeur** : chaque casse DAW/OS/JUCE est une session d'agent
   sur issue GitHub, une à trois par an d'après les précédents cités en 3.3 [76][79]
   [15d][39] ; le périmètre supporté v1 (Windows + Live 12 + REAPER) limite l'exposition.

Non vérifié (à tester ou à demander) : compatibilité de `clap-juce-extensions` avec
JUCE 9 (son README s'arrête à JUCE 8 ; JUCE 9.0 n'a pas de CLAP officiel, vérifié
[64][67]) ; routage MIDI d'un **effet** AAX dans Pro Tools ; side-chain Logic vers un AU
instrument tiers ; VST3 dans FL Studio 2025/2026 ; méthode d'enregistrement actuelle
dans Bitwig 5 ; statut GPL du VST3 dans nice-plug ; réaction de SmartScreen à un
`.vst3` extrait d'un zip ; pilote ASIO du Gigcaster 5 (multi-client ? latence ? tampon
minimal sans craquement ?) ; latence de WASAPI exclusif via JUCE sur le Gigcaster (pour
l'autonome sans ASIO) ; mesure de Chrome optimisé sur Windows ; écart note pour note
JS dense ↔ réglage livré (à établir en 4.2) ; prix exact de MG3 (149,95 USD sur la
boutique, « 10-149,95 » sur KVR) ; « MIDI Guitar 3 for Logic » (page 404) ; le corps
du guide Apple [15] (seule la table des matières est servie à l'outil) ; les articles
Ableton [1][2] ont été lus par l'API Zendesk, la page HTML refusant les lecteurs
automatiques.

**À mesurer en premier** (M0, un soir au Gigcaster) : latence pick → note dans Chrome
tel que réglé aujourd'hui, puis dans le prototype VST3 à 64/128/256 ; routage et bend
dans Live 12.4 ; décalage d'un clip MIDI enregistré. Ces quatre nombres valent plus que
toute la section 5.

---

## 9. Sources

Officiel sauf mention (communauté = forum, blog tiers). Consultées le 7 octobre 2026.

- [1] Ableton Help, « Accessing the MIDI output of a VST plug-in » (mis à jour 18 août 2026, lu via l'API Zendesk) — https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in
- [2] Ableton Help, « Using AU and VST plug-ins on macOS » (table AU/VST2/VST3, mis à jour 6 oct. 2026) — https://help.ableton.com/hc/en-us/articles/209068929
- [3] Ableton, notes de version Live 10 (10.1 VST3 ; 10.1.25 CC/Pitch Bend/Aftertouch depuis VST3) — https://www.ableton.com/en/release-notes/live-10/
- [4] Ableton, notes de version Live 11 (11.0 Max for Live MIDI/MPE ; 11.3.25 tous les CC) — https://www.ableton.com/en/release-notes/live-11/
- [5] Ableton, notes de version Live 12 (12.0–12.4.6, 15 sept. 2026) — https://www.ableton.com/en/release-notes/live-12/
- [6] Manuel Live 12, « Routing and I/O » (Monitor, Keep Monitoring Latency, External Instrument) — https://www.ableton.com/en/live-manual/12/routing-and-i-o/
- [6b] Manuel Live 12, « Recording New Clips » (armement exclusif : « Clicking one track's Arm button unarms all other tracks unless the Ctrl (Win) / Cmd (Mac) modifier is held » ; Capture MIDI sur pistes armées ou monitorées) — https://www.ableton.com/en/live-manual/12/recording-new-clips/
- [7] Ableton Help, « Recordings are out of sync » (mis à jour 5 août 2026) — https://help.ableton.com/hc/en-us/articles/19450890686876-Recordings-are-out-of-sync
- [8] Ableton Help, « Setting up a virtual MIDI bus » (IAC, loopMIDI ; mis à jour 6 oct. 2026) — https://help.ableton.com/hc/en-us/articles/209774225-Setting-up-a-virtual-MIDI-bus
- [9] Cockos, changelog REAPER `whatsnew.txt` (v0.967 2006 ; v6.80 2023 CLAP ; v7.55 nov. 2025 AUv3 MIDI out ; v7.82 4 oct. 2026) — https://www.reaper.fm/whatsnew.txt
- [10] KVR, « How to bounce midi to midi in Reaper? » (communauté, 2016) — https://www.kvraudio.com/forum/viewtopic.php?t=458435
- [11] Bitwig User Guide, « Introduction to Devices » (chaîne « bucket brigade ») — https://www.bitwig.com/userguide/latest/introduction_to_devices/
- [12] Bitwig User Guide, « Routing » (Note Receiver) — https://www.bitwig.com/userguide/latest/routing/
- [12b] KVR Bitwig, « routing midi from one track to plugin on another track » (communauté, 2023) — https://www.kvraudio.com/forum/viewtopic.php?t=591491
- [13] JUCE forum, « VST3 MIDI Plugins Won't Load in Ableton Live » (communauté) — https://forum.juce.com/t/vst3-midi-plugins-wont-load-in-ableton-live/36323
- [14] Apple, Logic Pro 11 release notes (Internal MIDI In) — https://support.apple.com/en-kz/126835
- [15] Apple, guide Logic Pro, « Route MIDI internally to software instrument tracks » — https://support.apple.com/guide/logicpro/route-midi-internally-software-instrument-lgcp1efa7c4d/mac
- [15b] Apple, Logic Pro release notes 12.0–12.4 (12.4 publié le 29 sept. 2026) — https://support.apple.com/en-us/109503
- [15c] Apple, Logic Pro 10.8 / 10.8.1 release notes (page datée 13 mai 2024 : « Improved stability when MIDI 2.0 is enabled and using Audio Units that output MIDI » ; « Recorded MIDI notes are no longer duplicated with certain AUv3 instrument plug-ins ») — https://support.apple.com/120134
- [15d] KVR, « Logic and AU instrument midi out » (communauté : sortie MIDI AU de Logic 10.8 jugée « buggy », contournement IAC + External Instrument) — https://www.kvraudio.com/forum/viewtopic.php?t=591011
- [16] Jam Origin, documentation « DAW support » (par DAW ; écrite pour MIDI Guitar 2, peut-être datée ; Logic : AU dans le slot audio FX d'une piste audio, la voie « Virtual MIDI Out » « will add a bit of latency compared to recommended approach ») — https://www.jamorigin.com/docs/daw/
- [17] Jam Origin, page de téléchargement (MG2 2.2.1 : Win « App, VST2/3 », mac « App, AU, VST2/3 », iOS « App, AUv3 » ; MG3 3.0.68 Win / 3.0.74 mac / 3.0.75 iOS, bêta ; ni AAX ni CLAP) — https://jam.live/downloads/
- [17b] Waves forum, « OVox MIDI out with Logic Pro 11 » (communauté, 2024) — https://forum.waves.com/t/ovox-midi-out-with-logic-pro-11/9281
- [18] Jam Origin, boutique (149,95 USD, mise à jour à vie) — https://jam.live/shop/
- [19] Jam Origin, page MIDI Guitar 3 (bêta, MPE/MIDI 2.0) — https://jam.live/products/MG3/
- [20] Sound On Sound, test MIDI Guitar 2 (sept. 2017 ; 99,95 USD ; +3-6 ms en autonome) — https://www.soundonsound.com/reviews/jam-origin-midi-guitar-2
- [21] KVR, fil MG2 (communauté, sept. 2020 : accords « far from flawless ») — https://www.kvraudio.com/forum/viewtopic.php?t=552408
- [22] Jam Origin, modules MG3, « MIDI Output » (AU : port virtuel automatique ; VST : Direct MIDI Output ; MPE canaux 2-9) — https://jam.live/modules/modules/
- [23] jamosapien, « MIDI Guitar 3 introduction » (communauté, févr. 2024 ; ~40 ms ; upgrade gratuit) — https://jamosapien.com/t/midi-guitar-3-introduction-a-first-look-inside/5960
- [24] Vochlea, Dubler 2 (99 USD) — https://vochlea.com/ ; test MusicTech 2021 — https://musictech.com/reviews/software-instruments/vochlea-dubler-2-review/
- [25] imitone (29/59/99 USD, « < 30 ms ») — https://imitone.com/
- [26] Dodo MIDI 2.1 (gratuit, VST3/AU) — https://dodobirdmusic.com/dodo-midi/
- [27] KVR, MTuner (MeldaProduction) — https://www.kvraudio.com/product/mtuner-by-meldaproduction
- [28] GitHub, Guit2Mid — https://github.com/CGFrog/Guit2Mid
- [29] GitHub, Warf — https://github.com/Str8b33fcak3/warf
- [30] KVR news, Fishman TriplePlay Express (199,95 USD, sept. 2024) — https://www.kvraudio.com/news-print.php?id=61752
- [31] NZ Musician, Fishman TriplePlay (2013, ~10 ms) — https://nzmusician.co.nz/features/fishman-tripleplay/
- [32] Sound On Sound, test BOSS GM-800 (nov. 2023) — https://www.soundonsound.com/reviews/boss-gm-800 ; prix Adorama — https://www.adorama.com/bsgm800.html
- [33] Sonuus G2M V3 — https://www.sonuus.com/products_g2m.html
- [34] KVR, « over 10 ms » pour l'octave grave (communauté, 2016) — https://www.kvraudio.com/forum/viewtopic.php?t=462790
- [35] GuitarSet, ISMIR 2018 — https://archives.ismir.net/ismir2018/paper/000188.pdf
- [36] GAPS, ISMIR 2024 (~88 % F sur GuitarSet, hors ligne) — https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2024/RileyEtAl-ISMIR-2024.pdf
- [37] Native Instruments, « Sending MIDI from the Komplete Kontrol plug-in in Cubase » (Cubase 8) — https://support.native-instruments.com/support/solutions/articles/69000879829-sending-midi-from-the-komplete-kontrol-plug-in-in-cubase
- [38] Steinberg Help, « Using VST 2 plug-ins in Cubase 14 / Nuendo 14 » — https://helpcenter.steinberg.de/hc/en-us/articles/22554401894162
- [39] Steinberg forum, « VST3 MIDI-out timing breaks at large ASIO buffer » (communauté, août 2026) — https://forums.steinberg.net/t/vst3-midi-out-timing-breaks-at-large-asio-buffer/1039971
- [40] Sound On Sound, « Pro Tools has upped its MIDI game » (mai 2024) — https://www.soundonsound.com/techniques/pro-tools-midi-plug-ins
- [41] Image-Line, manuel FL Studio, « Wrapper » (Input/Output port) — https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/wrapper.htm
- [42] Image-Line forum, notes routées non enregistrées (communauté, 2017) — https://forum.image-line.com/viewtopic.php?t=168249
- [42b] Scaler forum, « MIDI Routing – FL Studio – VST3 » (communauté, 2018) — https://forum.scalermusic.com/t/midi-routing-fl-studio-vst3/60
- [43] Steinberg, VST 3 Developer Portal, « About MIDI » (bus d'événements, LegacyMIDICCOutEvent, SDK 3.6.12) — https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/About+MIDI/Index.html
- [44] Steinberg, `ivstaudioprocessor.h` (catégories Fx / Instrument / Analyzer) — https://raw.githubusercontent.com/steinbergmedia/vst3_pluginterfaces/master/vst/ivstaudioprocessor.h
- [45] Apple, exemple AudioUnitInstrumentExample (« Use of these properties requires host support ») — https://developer.apple.com/library/archive/samplecode/sc2195/Listings/AudioUnitInstrumentExample_ReadMe_md.html
- [46] Apple, guide GarageBand, « Use Audio Units plug-ins » — https://support.apple.com/guide/garageband/gbnde06a4e4d/mac
- [47] GitHub, free-audio/clap (MIT) — https://github.com/free-audio/clap
- [48] CLAP, `plugin-features.h` (« note-detector ») — https://raw.githubusercontent.com/free-audio/clap/main/include/clap/plugin-features.h
- [49] KVR, Studio One « Instrument Input » (communauté) — https://www.kvraudio.com/forum/viewtopic.php?t=528060
- [49b] Scaler forum, Studio One 6.6.4/7, VST3 vs AU (communauté, mars 2025) — https://forum.scalermusic.com/t/cannot-send-individual-scaler-3-tracks-to-instrument-tracks-in-studio-one/20079
- [50] Avid Developer, AAX (licence, iLok, signature) — https://developer.avid.com/aax/
- [51] Reason Studios Help, « Using VST plugins in Reason 12.5 » (mis à jour 18 juil. 2026) — https://help.reasonstudios.com/hc/en-us/articles/9088187420050-Using-VST-plugins-in-Reason-12-5
- [52] GitHub, steinbergmedia/vst3sdk (README : MIT, GPLv3/propriétaire « no longer available ») — https://github.com/steinbergmedia/vst3sdk
- [53] KVR news, « Steinberg Moves VST 3 SDK to MIT… ASIO Now GPLv3 » (29 oct. 2025) — https://www.kvraudio.com/news/steinberg-moves-vst-3-sdk-to-mit-open-source-license-asio-now-gplv3-65179
- [54] Steinberg forum, accord ASIO propriétaire (réponse staff, juin 2026) — https://forums.steinberg.net/t/where-do-i-submit-the-proprietary-asio-sdk-license-agreement/1035889
- [55] Cycling '74, « What's New in Live 11, Part 1 » — https://cycling74.com/articles/what's-new-in-live-11-part-1
- [56] Ableton, boutique Live (Intro 99 / Standard 349 / Suite 749 USD) — https://www.ableton.com/en/shop/live/ ; Max for Live 199 USD — https://www.ableton.com/en/shop/
- [57] Ableton Help, « Editing gen patchers » (mis à jour 30 sept. 2026) — https://help.ableton.com/hc/en-us/articles/360003262599-Editing-gen-patchers
- [58] Steinberg forum, « VST 2 SDK discontinued » — https://forums.steinberg.net/t/vst-2-sdk-discontinued/201774
- [59] KVR news, REAPER 6.71 CLAP (30 nov. 2022) — https://www.kvraudio.com/news/cockos-updates-reaper-to-v6-71---clap-plugin-support-56611
- [60] Image-Line, FL Studio 2024 (CLAP, 1 juil. 2024) — https://www.image-line.com/fl-studio-news/fl-studio-2024-whats-new
- [61] Sound On Sound, Studio One Pro 7 (CLAP) — https://www.soundonsound.com/reviews/presonus-studio-one-pro-7 ; Manuel Live 12, « Using Plug-Ins » (VST2/VST3/AU seulement) — https://www.ableton.com/en/live-manual/12/using-plug-ins/
- [62] JUCE, LICENSE.md (AGPLv3 ou commercial) — https://raw.githubusercontent.com/juce-framework/JUCE/master/LICENSE.md
- [63] JUCE forum, « Amendments to the JUCE End User Licence Agreement for JUCE 8 » (7 mai 2024 ; paliers) — https://forum.juce.com/t/amendments-to-the-juce-end-user-licence-agreement-for-juce-8/61265
- [64] JUCE forum, « JUCE 9 is available now » (21 juil. 2026 ; mêmes conditions) — https://forum.juce.com/t/juce-9-is-available-now/69175
- [65] JUCE forum, licence JUCE 8 et projets open source (dons comptés) — https://forum.juce.com/t/juce8-license-and-open-source-projects/60987
- [66] JUCE, CMake API (formats, `NEEDS_MIDI_OUTPUT`, pas de CLAP) — https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/CMake%20API.md
- [67] GitHub, clap-juce-extensions (MIT, JUCE 6/7/8) — https://github.com/free-audio/clap-juce-extensions
- [68] JUCE forum, feuille de route (CLAP annoncé pour JUCE 9 en juil. 2024, non tenu en 9.0) — https://forum.juce.com/t/juce-roadmap-updates/62275 ; KVR, JUCE 9 (« CLAP » n'y figure que comme étiquette de catégorie, pas dans le texte) — https://www.kvraudio.com/news/juce-9-now-available-67802
- [69] JUCE, `WebBrowserComponent` — https://docs.juce.com/master/classWebBrowserComponent.html
- [70] JUCE, README (avis aux outils IA) — https://raw.githubusercontent.com/juce-framework/JUCE/master/README.md
- [71] GitHub Docs, facturation GitHub Actions (gratuit sur dépôt public) — https://docs.github.com/en/actions/reference/usage-limits-billing-and-administration
- [71b] GitHub, iPlug2 (licence zlib-like, formats) — https://github.com/iPlug2/iPlug2
- [72] Apple, comparer les adhésions (notarisation = programme payant) — https://developer.apple.com/support/compare-memberships/
- [72b] DPF, README et LICENSING — https://raw.githubusercontent.com/DISTRHO/DPF/main/README.md ; https://github.com/DISTRHO/DPF/blob/main/LICENSING.md
- [73] Apple, `MIDISourceCreate` — https://developer.apple.com/documentation/coremidi/midisourcecreate(_:_:_:)
- [73b] nih-plug, README (maintenance, VST3 GPLv3) — https://raw.githubusercontent.com/robbert-vdh/nih-plug/master/README.md
- [74] Tobias Erichsen, loopMIDI — https://www.tobias-erichsen.de/?p=45 ; SDK virtualMIDI (« NOT freeware », clearance requise) — https://www.tobias-erichsen.de/?p=343
- [74b] Codeberg, RustAudio/nice-plug — https://codeberg.org/RustAudio/nice-plug
- [75b] GitHub, clap-wrapper — https://github.com/free-audio/clap-wrapper
- [76] Microsoft, « Windows MIDI Services 2026 release – known issues » (déploiement févr. 2026, ports tiers cassés, correctif 30 avr. 2026) — https://devblogs.microsoft.com/windows-music-dev/windows-midi-services-rollout-known-issues-and-workarounds/
- [76b] Cmajor, licence — https://cmajor.dev/docs/Licence
- [77] Microsoft, MIDI Loopback Setup (« will be released to consumers in November 2026. It's currently available for developers » ; « Both types of loopbacks create MIDI 1.0 API ports ») — https://microsoft.github.io/MIDI/tools/midiloopbacksetup/ ; Overview — https://microsoft.github.io/MIDI/overview/
- [77b] QuickJS — https://bellard.org/quickjs/
- [78b] Duktape, timing-sensitive.rst — https://github.com/svaarala/duktape/blob/master/doc/timing-sensitive.rst
- [79] Vochlea, « Setting up with Studio One » (ASIO multi-client) — https://vochlea.com/learn/setting-up-with-studio-one ; « Live MIDI not working on Windows 11 » — https://vochlea.com/learn/live-midi-not-working-on-windows-11
- [80] gilpanal/weblatencytest (WAC 2025 : Chrome 62,8 ms Win10, 52,3 ms macOS) — https://github.com/gilpanal/weblatencytest
- [81] Ableton Help, « Reduced Latency When Monitoring » — https://help.ableton.com/hc/en-us/articles/209072249
- [82] Microsoft Learn, distribution WebView2 — https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution
- [83] Jeff Kaufman, « Browser Audio Latency » (2021 : ~67 ms par défaut, ~19 ms réglé) — https://www.jefftk.com/p/browser-audio-latency
- [83b] micbuffa/WAlatencyCompensation (19-23 ms, Focusrite, 128) — https://github.com/micbuffa/WAlatencyCompensation
- [84] JAMRACK, `docs/guitar-to-midi.md` (table de latence, mesures GuitarSet MONO) — https://github.com/benasterisk/jamrack/blob/bcbb30d/docs/guitar-to-midi.md
- [85] JAMRACK, `docs/polyphonic-plan.md` (§13 verdict POLY, porte) — https://github.com/benasterisk/jamrack/blob/bcbb30d/docs/polyphonic-plan.md
- [86] Note de portabilité de cette étude (lecture du dépôt à `bcbb30d`, mesures Node 22) — `docs/daw-plugin-research/portability.md`, à partir de https://github.com/benasterisk/jamrack/tree/bcbb30d/js/audio/guitar
- [87] JAMRACK, `docs/poly-implementation.md` (équivalence 13 624/13 624, coûts) — https://github.com/benasterisk/jamrack/blob/bcbb30d/docs/poly-implementation.md
- [88] Chromium, `audio_low_latency_output_win.h` (« total typical delay of 35 ms ») — https://chromium.googlesource.com/chromium/src/media/+/main/audio/win/audio_low_latency_output_win.h
- [89] Microsoft Learn, « Low Latency Audio » (périodes 10 ms par défaut) — https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio
- [90] RME forum, « Fireface UCX II Latencies » (communauté, févr. 2023 ; le texte ne dit que « RTL is less than 5 ms » et renvoie à une image) — https://forum.rme-audio.de/viewtopic.php?id=36957
- [90b] Image jointe sur Audio Science Review citée par [90] (tableau UCX II par taille de tampon ; valeurs lues sur une image, non reproduites en texte dans ce rapport) — https://www.audiosciencereview.com/forum/index.php?attachments/1674306624330-png.258743/
- [90c] MusicTech, test RME Fireface UCX II (~3 ms RTL à 32 échantillons) — https://musictech.com/reviews/rme-fireface-ucx-ii-review/
- [91] Archimago, « RTL Utility » (RME ADI-2 Pro FS « a little less than 5.3 ms (232 samples) » à 64 @ 44,1 kHz, ~14,3 ms à 256 ; Forte 11,8 ms ; ASIO4ALL 15,8 ; FlexASIO 9,6 ; WDM < 15 ; DirectSound 122) — https://archimago.blogspot.com/2021/11/rtl-utility-look-at-audio-interface.html
- [92] Steinberg, `Vst::Event` (`sampleOffset`) — https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1Event.html
- [93] CLAP, `events.h` (`time` = offset dans le tampon) — https://github.com/free-audio/clap/blob/main/include/clap/events.h
- [94] Steinberg forum, « Delay/latency compensation for MIDI output from an effect plug-in » (**question d'utilisateur** clarke1, 7 mars 2019, sans réponse de Steinberg ; redirection de sdk.steinberg.net) — https://forums.steinberg.net/t/delay-latency-compensation-for-midi-output-from-an-effect-plug-in/201864
- [95] JUCE forum, « How do you compensate for latency in a MIDI-generating plugin? » (communauté, 11-12 août 2025 : « the DAW delays MIDI output by the same amount… » ; « 11 ms WORSE » ; « negative numbers don't seem to have an effect » ; REAPER 11-15 ms, Digital Performer 150 ms) — https://forum.juce.com/t/how-do-you-compensate-for-latency-in-a-midi-generating-plugin/66839
- [96] Apple, « Manage input monitoring latency in Logic Pro » — https://support.apple.com/105040
- [97] Microsoft Learn, options de signature de code (29 août 2026) — https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options
- [98] SignPath Foundation — https://signpath.org/
- [99] Apple, inscription au Developer Program (99 USD/an) — https://developer.apple.com/programs/enroll/
- [100] truce.audio, AAX (signature, iLok, erreur -7054) — https://truce.audio/docs/formats/aax/
- [101] Tracktion, pluginval README — https://raw.githubusercontent.com/Tracktion/pluginval/develop/README.md
- [102] GitHub, clap-validator — https://github.com/free-audio/clap-validator
- [103] GitHub changelog, exécuteur macOS M1 gratuit pour l'open source (30 janv. 2024) — https://github.blog/changelog/2024-01-30-github-actions-introducing-the-new-m1-macos-runner-available-to-open-source/
- [104] Manuel Live 12, « MIDI Fact Sheet » (horodatage, « latency is preferable to jitter ») — https://www.ableton.com/en/live-manual/12/midi-fact-sheet/
- [105] Microsoft, Visual Studio Community — https://visualstudio.microsoft.com/vs/community/
- [106] JAMRACK, historique git (46 commits du 3 octobre 11 h au 5 octobre 13 h 2026 pour guitare → MIDI, looper, POLY et calibration) — https://github.com/benasterisk/jamrack/commits/bcbb30d

---

## 10. Relecture

Après deux relecteurs adversariaux (faits et point de vue du propriétaire) :
- POLY : chiffres du réglage livré (80,7 / 57,0 / 43,6 / 77,8 [87]) à la place du prototype ; porte § 13 dite échouée ; limite « attaques pincées, pas de bend par note » ajoutée (§ 0, 1, 4.3) ; oracle et cible CPU M3 réalignés sur `engine.js` (1,10 / 1,62 ms).
- Latence : détection mesurée 9-36 ms au lieu du plancher 12-24 ; gain ramené à 8-15 ms (estimation du dépôt) ; règle « déclarer 0 » rejustifiée par le fil JUCE (les hôtes retardent le MIDI) et [94] requalifié en question d'utilisateur ; chiffres RME tirés d'une image remplacés.
- Live et effort : armement exclusif et réglage Gigcaster (MTK-STREAM, MIX MINUS) dans la procédure ; effort en sessions + soirées d'essai, comparé aux 50 h / 46 commits du port POLY ; iOS, Linux, périmètre supporté, maintenance et ASIO de l'autonome tranchés ; JUCE 9 sans CLAP officiel, MG2 en VST3, Loopback « novembre 2026 », Logic 10.8, label FL corrigés.
- Reste non vérifié : la liste de la section 8, en tête les quatre mesures de M0 sur le PC du propriétaire, `clap-juce-extensions` avec JUCE 9 et l'écart note pour note du réglage POLY livré.
