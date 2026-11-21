# Onirique MeterBox

Le compteur à poser en fin de master : le loudness aux normes, vu dans le temps
et pas seulement en chiffres ; le spectre, tenu contre la forme qu'a un titre
fini ; et la phase, sur un goniomètre et fréquence par fréquence.

VST3 (Windows, macOS) et Audio Unit (macOS). Entrée et sortie mono ou stéréo.
Le son n'est pas touché : ce qui sort est ce qui est entré, au bit près, sans
latence.

---

## État du projet

| Partie | État |
|---|---|
| Mesures (loudness, true peak, plage, spectre, corrélation) | faites, testées hors ligne sur les signaux des normes |
| Plugin VST3, réglages automatisables, état sauvegardé | compile sous Windows, autotest passé |
| Interface | écrite, vérifiée sur capture (`meterbox_snapshot`) |
| Build macOS (VST3 + AU) | workflow prêt, **jamais lancé** : pas encore de dépôt GitHub |
| Essai dans un DAW | **pas encore fait** |
| Fiche dans le catalogue ONIRIQUE CENTRAL | **pas encore créée** : sans elle, le plugin est en mode démo |

Les cibles de balance tonale d'origine ont été posées par raisonnement, à
partir de ce qu'on sait du spectre moyen des disques du commerce, et non
mesurées sur une bibliothèque de titres : elles restent à ajuster sur des
références. La cible **Reference**, capturée sur un titre, vaut mieux qu'elles.
« MeterBox » est un nom provisoire.

---

## Construire

JUCE 9.0.3 est cherché dans `JUCE/`, puis dans `../PitchBox/JUCE` : les plugins
de la gamme partagent la même copie.

```bash
cmake -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Le plugin sort dans
`build/OniriqueMeterBox_artefacts/Release/VST3/MeterBox.vst3` ; à copier dans
`C:\Program Files\Common Files\VST3`.

Outil de développement, sans hôte :

```bash
cmake --build build --config Release --target meterbox_snapshot
build/Release/meterbox_snapshot.exe snapshot.png             # capture de l'interface, sur un signal qui a la forme d'un titre
build/Release/meterbox_snapshot.exe phase.png --phase        # le même, les graves hors phase : à quoi ressemble un défaut
build/Release/meterbox_snapshot.exe licence.png --licence    # avec la pastille de licence et sa page
build/Release/meterbox_snapshot.exe snapshot.png --bench     # et le temps que prend le dessin de la fenêtre
build/Release/meterbox_snapshot.exe ref.png --reference      # avec une référence chargée (--listen : écoutée, sur B)
build/Release/meterbox_snapshot.exe find.png --find "titre"  # une vraie recherche, ses résultats, et le premier chargé
build/Release/meterbox_snapshot.exe shop.png --rest          # au repos, comme la boutique le montre
build/Release/meterbox_snapshot.exe focus.png --focus 2      # la vue simplifiée, sur un outil (0 loudness .. 4 phase)
build/Release/meterbox_snapshot.exe login.png --login        # avec la connexion au CLOUD ouverte
build/Release/meterbox_snapshot.exe --selftest               # le plugin comme un hôte le pilote
```

---

## Utilisation

**Partout** : le pointeur laissé immobile deux secondes sur une partie de la
fenêtre en fait apparaître la description, en français.

### En-tête — quand il mesure

| Contrôle | Rôle |
|---|---|
| **Sync** | La mesure suit le transport : elle repart de zéro quand la lecture démarre et se fige à l'arrêt. Éteint, elle tourne en continu. Un hôte qui ne dit rien de son transport est pris pour en lecture. |
| **Pause** | Fige l'intégré, la plage, les maxima et l'historique. |
| **Reset** | Remet à zéro tout ce qui se mesure depuis le début, moyenne du spectre comprise. |

À côté, ce que fait le compteur (`MEASURING`, `PAUSED`, `WAITING FOR PLAYBACK`)
et depuis combien de temps il mesure.

En pause ou à l'arrêt, les niveaux instantanés (momentané, court terme, true
peak), le spectre et la phase continuent : seul ce qui s'accumule est figé.

### Pied — contre quoi il mesure

| Contrôle | Rôle |
|---|---|
| **Norme** | Règle la cible et le plafond d'un coup : Streaming −14 (Spotify, YouTube, Tidal, Amazon Music), Apple Music −16, Loud master −9, EBU R 128 −23, ATSC A/85 −24. `Custom` dès qu'un des deux chiffres est déplacé à la main. |
| **Target** | Le loudness intégré visé, de −36 à −6 LUFS. |
| **TP Max** | Le plafond de true peak, de −9 à 0 dBTP. |

### Référence (A/B)

Le bandeau au-dessus du pied compare le mix à un titre de référence.

- **La référence** : un fichier glissé sur la fenêtre (n'importe où), ou choisi
  en cliquant sur le bandeau ou sur **Load**. WAV, AIFF, FLAC, MP3, OGG ; M4A et
  AAC sur Mac. Quinze minutes au plus. Elle est lue et mesurée à part, sans
  toucher au thread audio, puis gardée avec le projet (le chemin du fichier).
  Choisie à la main, sa balance tonale devient la cible **Reference** du
  spectre.
- **A / B** : A, le mix ; B, la référence, jouée à sa place. Le passage est
  fondu sur 20 ms. La référence avance avec le DAW, que l'on écoute A ou B
  (voir Lock). Un export sort toujours le mix, même sur B. Le projet rouvre
  toujours sur A.
- **Match** : la référence est jouée au loudness intégré du mix (le court terme
  tant que l'intégré n'est pas mesuré). À côté, le gain qui lui est appliqué.
- **Search** : la référence cherchée par son titre, son artiste ou un lien
  Spotify, comme sur la page « Comparer à une référence » du site
  (`https://onirique.store/api/reference/search`, le catalogue de Deezer). Les
  résultats s'ouvrent au-dessus du bandeau, avec pochette, titre, artiste,
  album et durée ; un clic prend l'extrait officiel de 30 s
  (`/api/reference/preview/<id>`, en MP3 : l'air au-dessus de 16 kHz y est
  coupé, sa balance tonale ne devient donc pas la cible). Un lien YouTube ou
  TikTok tapé ou collé là donne le titre entier, récupéré en WAV par le
  Downloader du site (`/api/downloader/youtube`, un lien par minute sans le
  rôle ONIRIQUE, 20 minutes au plus). Un lien glissé depuis le navigateur sur
  la fenêtre marche aussi. Ce qui est récupéré est gardé dans
  `~/Library/Onirique/MeterBox References` (Mac) ou
  `AppData\Roaming\Onirique\MeterBox References` (Windows).
- **Lock** (allumé au départ) : la référence joue au temps du projet. La
  seconde 72 du son est la seconde 72 de la référence, où que soit le DAW :
  deux versions du même son restent calées à l'échantillon. Un clic sur sa
  forme d'onde la décale par rapport au projet (le décalage est écrit à côté de
  ses chiffres), un double-clic la remet à zéro. Avant son début et après sa
  fin, elle se tait. Éteint, elle avance toute seule quand le DAW joue, en
  boucle, et un clic choisit où elle joue. Lock et décalage sont gardés avec le
  projet.
- **Cloud** : la référence prise dans l'ONIRIQUE CLOUD, en entier, comme
  « Référence depuis mon CLOUD » sur la page du site. La première fois, une
  connexion au compte ONIRIQUE (e-mail, mot de passe, et le code de
  l'application d'authentification si le compte en a une) :
  `POST /api/auth/login`. La session est gardée dans
  `~/Library/Onirique/meterbox-cloud-session` (Mac, lisible par l'utilisateur
  seul) ou `AppData\Roaming\Onirique\meterbox-cloud-session` (Windows) ; le
  mot de passe n'est jamais gardé. Ensuite la liste des sons
  (`GET /api/me/sounds`), projet par projet, à leur version en avant, avec la
  pochette du projet ; un clic télécharge le son entier par son adresse signée
  et le charge. « Se déconnecter » en haut de la liste. Une session finie
  ramène à la connexion.
- Ce qui est chargé (fichier, recherche, lien, CLOUD) **remplace** la référence en
  place dès qu'il est prêt : il n'y a pas de bouton pour l'enlever.

Les mesures restent celles du mix, même sur B. Les chiffres de la référence,
mesurés une fois sur tout le titre, sont écrits à côté, en bleu, dans chaque
partie : son loudness intégré et l'écart du mix, ses maxima sur les barres, sa
plage, son true peak et son PLR ; son intégré et sa répartition dans
l'historique ; sa moyenne dans le spectre, posée sur celle du mix ; sa crête,
son RMS et son facteur de crête sous la forme d'onde ; sa corrélation, sa
balance, sa largeur, sa perte en mono et sa corrélation par fréquence dans la
phase.

### Full / Focus

En haut à droite des mesures (dans l'en-tête de Phase en Full) : **Full**
montre tous les outils à la fois, **Focus** un seul, en grand. En Focus, une
rangée de cinq tuiles en haut, comme les modules de VOCALBOX : Loudness,
History, Spectrum, Waveform, Phase, chacune avec son pictogramme et son chiffre
principal en direct (intégré, court terme, cible du spectre, true peak,
corrélation). Un clic sur une tuile affiche cet outil seul ; le bandeau de la
référence et le pied restent. En grand, la phase met le goniomètre à gauche et
le reste à droite, la forme d'onde ses chiffres à droite. La vue et l'outil
choisis sont gardés avec le projet.

### Loudness

Mesuré selon ITU-R BS.1770-4 et EBU R 128.

- **Integrated**, en grand : le loudness de tout ce qui a été joué depuis la
  remise à zéro, avec ses deux seuils (absolu à −70 LUFS, relatif à −10 LU), et
  son écart à la cible.
- **Les barres**, sur une seule échelle : le true peak de chaque côté (sa valeur
  en direct est écrite au-dessus), puis le
  loudness momentané (0,4 s), court terme (3 s) et intégré. Les traits blancs
  sont le plafond et la cible ; ce qui les dépasse est chauffé à blanc. Les
  petits traits sont les maxima.
- **Range** : la plage de loudness (LRA, EBU Tech 3342).
- **True Peak** : la crête la plus haute, entre les échantillons compris (signal
  reconstruit à quatre fois sa fréquence ; deux fois à partir de 88,2 kHz).
  `OVER` quand elle a passé le plafond.
- **PLR** : la crête la plus haute moins le loudness intégré. **PSR** : la crête
  des trois dernières secondes moins le loudness court terme. Plus ils sont
  petits, plus le master est écrasé.

### History

Le loudness dans le temps : le momentané en fond, le court terme en rouge,
l'intégré en blanc, la cible en pointillés. Une marque blanche en haut là où le
true peak a dépassé le plafond. L'échelle suit la cible (de 9 LU au-dessus à
27 en dessous).

Le menu choisit la durée affichée : 30 s, 2 min, 10 min, ou tout depuis la
remise à zéro. L'historique garde une heure, dix points par seconde.

À droite, la répartition : le temps que le loudness court terme a passé à
chaque niveau.

### Spectrum

Le spectre par tiers d'octave, de 20 Hz à 20 kHz, un sinus pleine échelle à
0 dB : un bruit rose y est une ligne plate.

- **Live**, rempli : le spectre à l'instant.
- **Average**, en blanc : sa moyenne, sur 3 s, 10 s, ou depuis la remise à zéro.
  Les silences n'y comptent pas : elle reste en place à l'arrêt.
- **Target**, la bande claire : la forme attendue. Elle ne dit rien du niveau :
  elle est posée sur la moyenne avant d'être comparée. La moyenne passe au rouge
  là où elle sort de la bande.

Cibles : **Modern** (pop, électro), **Urban** (rap, trap, bass music), **Rock**,
**Acoustic** (jazz, classique, folk), et **Reference**.

**Capture** fait de la moyenne affichée la cible Reference : jouer un titre de
référence, laisser la moyenne se poser (réglée sur `All`, elle couvre tout le
titre), puis capturer. Elle est enregistrée avec le projet.

En bas, le même écart résumé en quatre zones (Low jusqu'à 250 Hz, Low-Mid
jusqu'à 2 kHz, High-Mid jusqu'à 8 kHz, High au-delà), en dB par rapport à la
cible ; la partie claire de chaque jauge est ce que la cible admet.

### Waveform

La forme d'onde de tout ce qui a été mesuré depuis la remise à zéro, en
colonne : le début en haut, maintenant en bas, la gauche à gauche du milieu et
la droite à droite. En rouge, les crêtes ; en clair au milieu, le niveau RMS.
Les lignes sont −12 dB, −6 dB et la pleine échelle ; une marque blanche au bord
signale un true peak au-dessus du plafond. En dessous, les chiffres : la crête
la plus haute de chaque côté depuis la remise à zéro, le niveau RMS de chaque
côté et le facteur de crête sur la dernière seconde, et la durée mesurée. Elle montre toujours tout (une heure au
plus), quelle que soit la durée choisie pour l'historique.

### Phase

- **Le goniomètre** : chaque échantillon est un point. Ce qui est identique des
  deux côtés monte au milieu ; ce qui n'est que d'un côté penche de ce côté ; ce
  qui est en opposition de phase s'étale à l'horizontale. Il est dessiné à la
  même taille quel que soit le niveau.
- **Correlation** : de −1 (les deux côtés s'annulent en mono) à +1 (mono), sur le
  dernier tiers de seconde, avec le minimum des dernières secondes.
- **Balance** : le côté le plus fort, en dB.
- **Width** : la part du son qui est sur les côtés. 0 % : mono ; 50 % : deux côtés
  sans rapport.
- **Mono** : ce que le mix perd une fois ses deux côtés additionnés.
- **Correlation by frequency** : la même corrélation, fréquence par fréquence.
  Là où la courbe passe sous le milieu, cette zone du spectre disparaît en
  mono : c'est là qu'on voit un grave élargi de travers.

---

## Ce que ça coûte

Mesuré sur un cœur, à 48 kHz : 0,1 % sur le thread audio (les filtres du
loudness et le true peak), 0,3 % pour les transformées du spectre, faites sur
un fil à part, et environ 5 ms pour dessiner la fenêtre, trente fois par
seconde tant qu'elle est ouverte.

Pendant un export, les transformées sont faites dans le rendu lui-même : la
moyenne du spectre couvre tout le fichier, comme le loudness.

---

## Sources

| Fichier | Contenu |
|---|---|
| `Source/dsp/Loudness.*` | Pondération K, loudness momentané, court terme et intégré, plage, true peak. Sans dépendance. |
| `Source/dsp/Spectrum.*` | Les deux transformées (longue sous 400 Hz, courte au-dessus), les bandes, la moyenne, la corrélation par bande. |
| `Source/dsp/Stereo.h` | Corrélation, balance, largeur, perte en mono. |
| `Source/dsp/Tonal.*` | Les cibles de balance tonale et leur comparaison. |
| `Source/dsp/Meter.*` | Le tout, et son passage du thread audio à l'écran : historique, goniomètre. |
| `Source/PluginProcessor.*` | Réglages, transport, normes, référence capturée, état. |
| `Source/Cloud.*` | Le compte ONIRIQUE : connexion, session, sons du CLOUD. |
| `Source/Reference.*` | La référence A/B : lecture du fichier, mesure sur tout le titre, lecture à la place du mix, match de loudness. |
| `Source/PluginEditor.*`, `Source/ui/` | Interface : thème de la gamme, et un fichier par partie de la fenêtre. |
| `License/` | La licence commune à la gamme, copiée telle quelle de `plugin-license/` du dépôt ONIRIQUE CENTRAL. |
| `tests/test_meter.cpp` | Toutes les mesures, hors ligne, sur les signaux d'EBU Tech 3341 et 3342. |
| `tools/snapshot.cpp` | Capture de l'interface et autotest du plugin. |
