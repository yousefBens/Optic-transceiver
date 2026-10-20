# Résultats des essais physiques — 19 octobre 2026

Nordic sur `/dev/ttyACM1` → STM32 sur `/dev/ttyACM0`, liaison telle qu’elle est branchée.

## Profil retenu

- Demi-bit TX/RX : **3 µs** ; débit logique : **166 666,7 bit/s**.
- Payload utile : **3 octets** ; trame : **84 demi-bits**, soit **252 µs**.
- TX : quartz HF + PWM0/EasyDMA ; RX : fronts PA9 horodatés par TIM2 + interruption prioritaire.
- `sample_us=0` indique que le RX ne fait plus d’échantillonnage périodique.

## Validation longue

**3 acquisitions de 60 s**, total 180.000 s :
**712,281 trames correctes**, débit utile moyen **94,970.80 bit/s**.

Erreurs : Manchester **0**, préambule **0**, SFD **0**, GPIO **0**, payload incorrect **0** et bits de payload erronés **0**.

Firmware initial : **1574.77 bit/s utiles**, 10846 erreurs Manchester en 60 s. Gain utile observé : **×60.3**.

Les deux cartes sont flashées avec le profil retenu. RX termine son acquisition puis s’arrête ; un reset lance une nouvelle acquisition de 60 s. Les acquisitions longues traversent le débordement de TIM2.

## Balayage et comparaisons

Les essais exploratoires utilisent des durées différentes ; le débit utile est normalisé par leur durée. Les logs bruts et les CSV individuels permettent de retrouver chaque répétition.

| Expérience | Demi-bit (µs) | Essais | Durée totale (s) | Trames correctes | Débit utile (bit/s) | Erreurs Manchester |
|---|---:|---:|---:|---:|---:|---:|
| initial_firmware | 48 | 1 | 60.001 | 3937 | 1574.77 | 10846 |
| corrected_timer_48_12 | 48 | 3 | 30.003 | 54 | 43.20 | 7334 |
| diagnose_clock | 48 | 1 | 2.001 | 0 | 0.00 | 491 |
| hfxo_48_12 | 48 | 3 | 30.003 | 7419 | 5934.61 | 0 |
| hfxo_sweep_40_10 | 40 | 1 | 5.001 | 1482 | 7112.18 | 1 |
| hfxo_sweep_32_8 | 32 | 1 | 5.000 | 1853 | 8894.40 | 1 |
| hfxo_sweep_24_6 | 24 | 1 | 5.000 | 2471 | 11860.80 | 1 |
| hfxo_sweep_16_4 | 16 | 1 | 5.000 | 0 | 0.00 | 3710 |
| hfxo_sweep_12_3 | 12 | 1 | 5.000 | 0 | 0.00 | 4945 |
| hfxo_sweep_8_2 | 8 | 1 | 5.001 | 0 | 0.00 | 0 |
| fast_gpio_16_4 | 16 | 1 | 5.000 | 0 | 0.00 | 3708 |
| fast_both_16_4 | 16 | 2 | 10.000 | 7290 | 17496.00 | 128 |
| center_vote_16_4 | 16 | 2 | 20.002 | 14764 | 17715.03 | 73 |
| speed_center_16_4 | 16 | 1 | 5.001 | 3486 | 16729.45 | 223 |
| speed_center_12_4 | 12 | 1 | 5.000 | 0 | 0.00 | 4945 |
| speed_center_8_4 | 8 | 1 | 5.000 | 0 | 0.00 | 37160 |
| speed_center_12_3 | 12 | 1 | 5.003 | 0 | 0.00 | 4946 |
| edge_16_4 | 16 | 2 | 20.002 | 14834 | 17799.02 | 3 |
| gpio_irq_16 | 16 | 2 | 20.002 | 14831 | 17795.42 | 5 |
| dma_irq_16 | 16 | 2 | 20.002 | 0 | 0.00 | 0 |
| dma_corrected_irq_16 | 16 | 1 | 5.000 | 3709 | 17803.20 | 1 |
| dma_corrected_irq_12 | 12 | 1 | 5.000 | 4941 | 23716.80 | 7 |
| dma_corrected_irq_8 | 8 | 1 | 5.000 | 7405 | 35544.00 | 13 |
| dma_corrected_irq_6 | 6 | 1 | 5.000 | 0 | 0.00 | 9892 |
| dma_corrected_irq_4 | 4 | 1 | 5.000 | 0 | 0.00 | 4 |
| zli_dma_8 | 8 | 1 | 5.000 | 7417 | 35601.60 | 0 |
| zli_dma_6 | 6 | 1 | 5.000 | 9889 | 47467.20 | 0 |
| zli_dma_4 | 4 | 1 | 5.000 | 14835 | 71208.00 | 0 |
| zli_dma_3 | 3 | 1 | 5.000 | 19779 | 94939.20 | 0 |
| zli_dma_2 | 2 | 1 | 5.000 | 0 | 0.00 | 29671 |
| final_validation_3us | 3 | 3 | 180.000 | 712281 | 94970.80 | 0 |

## Limites et fichiers

À **2 µs**, aucune trame complète ne passe : ce réglage dépasse la limite de cette implémentation. 3 µs est le meilleur réglage validé dans les conditions de ces essais ; il ne garantit pas le même résultat sur un autre canal.

**Aucune perte exacte ne peut être annoncée sans numéro de séquence TX.** Le pourcentage théorique de succès inclut le décalage de l’horloge interne STM32 (~0,3 %) et les trames aux limites de la fenêtre. Zéro erreur détectée ne prouve pas zéro trame perdue. Le payload fixe FF 16 C1 permet de vérifier les trames complètes ; il n’y a pas de CRC.

- [Notebook exécuté](analysis.ipynb)
- [Graphiques de comparaison](comparison.html)
- [Comparaison avant/après](validation.html)
- [Synthèse CSV](summary.csv)
- [Instructions de reproduction](README.md)
- Données originales : `*.csv` et `raw/`.
