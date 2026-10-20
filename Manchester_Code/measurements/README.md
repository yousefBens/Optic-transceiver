# Banc réel Nordic → STM32

Les essais ont été effectués sur les cartes connectées, avec les logs, CSV et
sources des expériences conservés dans ce dossier. Voir `RESULTS.md` pour le
réglage retenu et les résultats de validation longue.

- TX : `T_test`, nRF52833 DK, P0.04, `/dev/ttyACM1`, J-Link **685278459**.
- RX : `Reciever`, Nucleo L476RG, PA9, `/dev/ttyACM0`, ST-Link **0666FF485753667187162860**.
- Trame inchangée : GAP HIGH ×4 demi-bits, 55 D3 FF 16 C1, Manchester 0→01 / 1→10.
- Le dossier `Transmitter` n'est pas utilisé par ce banc.

## Profil optimisé

Le TX utilise le quartz HF et rejoue une séquence de 84 demi-bits par PWM0/EasyDMA
sur la même broche. Les fronts sont produits matériellement, sans interruption
par demi-bit. Les deux tableaux PWM internes rejouent la même trame en boucle.

Le RX utilise les fronts montants et descendants de PA9, horodatés par TIM2 libre
à 80 MHz. La durée d'un niveau est arrondie à un ou deux demi-bits Manchester ;
chaque front rétablit donc la synchronisation temporelle. La réception du dernier
demi-bit HIGH fusionné avec le GAP est prise en compte. Les affichages sont faits
après désactivation de l'acquisition.

L'interruption EXTI9_5 est réservée à PA9 dans cette application. Le gestionnaire
prioritaire ne fait aucun appel au noyau Zephyr. Ne pas ajouter une autre source
EXTI5..9 sans adapter le gestionnaire. La soustraction non signée des timestamps
supporte le débordement de TIM2, qui arrive vers 53,7 s.

`sample_us=0` et `rx_mode=1` dans un CSV signifient **réception sur fronts** : le
paramètre SAMPLE_US n'est pas utilisé pour acquérir le signal. La couverture
d'échantillonnage n'a donc pas de sens pour ces lignes.

## Reproduire les essais

Depuis la racine du projet, sur la machine ayant accès aux USB :

```bash
/home/yousef/zephyrproject/.venv/bin/python measurements/bench.py \
  --half 3 --sample 1 --duration 60000 --repeat 3 \
  --edge-irq --zero-latency --hw-tx --label reproduction_3us
```

Cette commande compile et flashe les deux cartes, vérifie leur identité USB,
réinitialise RX entre acquisitions et écrit un CSV par essai. Le TX tourne en
continu. RX fait une acquisition de la durée configurée après chaque reset, puis
s'arrête. Le banc sélectionne chaque sonde par son numéro de série, vérifie le
flash STM32 et conserve les messages J-Link. Le port série ne suffit pas à
sélectionner la sonde de programmation.

Les caches Zephyr sont forcés dans le dossier de construction de chaque application.
Le banc désactive ccache pour éviter un cache compilateur externe. Les outils et
bibliothèques existants sont utilisés sans installation ni modification du SDK.

Pour lire seulement un RX déjà programmé, lancer la capture puis réinitialiser RX :

```bash
/home/yousef/zephyrproject/.venv/bin/python measurements/collect.py --timeout 90
```

## Visualisations et vérification

`analysis.ipynb` contient les données physiques et les graphiques Plotly. La page
`comparison.html` fonctionne hors ligne. Pour les actualiser sans installer Jupyter :

```bash
/home/yousef/zephyrproject/.venv/bin/python measurements/render_analysis.py
python3 measurements/test_decoder.py
```

Les tests hôtes compilent le décodeur C réellement utilisé : 1 000 trames idéales,
erreurs de payload/préambule/SFD/Manchester, reprise, dérive de ±1,5 % pour le mode
échantillonné sur fronts, et fronts horodatés avec gigue et dernier bit finissant
HIGH. Ils complètent les mesures physiques, sans les remplacer.

Les anciens modes sont conservés pour comparaison : RX par échantillonnage
périodique, RX avec suivi des fronts échantillonnés, TX par timer logiciel. Les
options explicites de la commande ci-dessus activent le profil optimisé. Les
variables CMake restent dans le cache : préciser chaque réglage pour reproduire
un essai. Les fichiers `raw/*metadata.json` contiennent les paramètres et empreintes
des sources et firmwares ; les essais exploratoires les plus anciens ont moins de
métadonnées que la validation finale.

## Limites des mesures

Le débit utile est calculé à partir des payloads **corrects** et du temps local RX.
L'horloge interne STM32 présente un décalage relatif d'environ 0,3 % par rapport au
quartz TX sur ces cartes. Le nombre `expected_frames` et le pourcentage de succès
théorique sont donc des estimations, avec également des trames partielles aux
limites de la fenêtre. Ils ne constituent pas un comptage exact des pertes.

Le protocole ne contient ni numéro de séquence ni CRC. Le payload fixe FF 16 C1
permet de vérifier les données reçues, mais pas de compter exactement toutes les
trames perdues. Le compteur de bits erronés et le BER ne concernent que les trames
complètes, pas celles rejetées par le décodeur.

La stabilité observée vaut pour le câblage, le payload et les conditions de ces
essais ; elle ne caractérise pas à elle seule un autre canal optique.

`stm32_hse.overlay` est un **essai non retenu** : l'horloge HSE/MCO n'a pas démarré
sur cette carte. L'horloge STM32 interne a été rétablie. Ne pas utiliser `--hse`
pour reproduire le profil validé. Les premiers essais `dma_irq_16` utilisaient une
polarité PWM inversée ; leur échec est conservé dans les données exploratoires.
