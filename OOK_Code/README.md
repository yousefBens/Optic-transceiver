# OOK NRZ — Nordic / Nucleo L476RG

Chaque bit dure 3 µs : `1 = GPIO HIGH`, `0 = GPIO LOW`, MSB en premier.
Chaque projet est autonome, avec ses paramètres dans `src/main.c`.
Les projets hors de `OOK_Code` restent inchangés.

| Projet | Carte | Données | Matériel |
|---|---|---|---|
| Transmitter_stm_OOK | Nucleo L476RG | PA9 sortie | TIM2 + DMA1 canal 2 |
| T_test_nordic_OOK | nRF52833 DK | P0.04 sortie | PWM0 + EasyDMA |
| Reciever_stm_OOK | Nucleo L476RG | PA9 entrée | EXTI + horodatage TIM2 |

Relier la sortie TX à PA9 du RX et les masses ensemble pour un test électrique
3,3 V, ou utiliser la liaison optique active HIGH. Choisir un seul émetteur.
La console est à 115200 bauds. Les commandes propres à chaque carte sont dans
son fichier `Commands_OPV4COM.txt`.

## Trame de validation

```text
GAP HIGH 16 bits | 55 55 55 55 D3 | séquence protégée 8 octets | payload 12 octets
```

Le payload de test envoyé par les TX est `FF 16 C1 FF 16 C1 FF 16 C1 FF 16 C1`.
La séquence augmente à chaque trame, de 0 à 65535, puis revient à 0.
Le TX concatène la séquence 16 bits et son complément bit à bit, puis envoie
les huit quartets MSB en premier. Chaque quartet `xxxx` devient l'octet
`101xxxx0`. Ces niveaux restent du OOK direct : chaque bit envoyé impose son
niveau GPIO. Ce format évite qu'une valeur du compteur ressemble au GAP.
Le RX vérifie les bits fixes et le complément avant d'utiliser la séquence.
Il connaît seulement la taille du payload et stocke ses octets dans
`received_payload[]`, sans les comparer à un motif attendu. Le buffer est
réutilisé à la trame suivante ; aucun historique de payload n'est conservé.
Cette protection n'est pas un CRC et ne détecte pas toutes les corruptions possibles.

La trame contient 25 octets, plus 16 bits de GAP : 216 périodes de 3 µs,
soit 648 µs par trame. Débit logique : 333333 bit/s ; débit utile maximal
théorique du payload : environ 148148 bit/s. Les informations de synchronisation
et de séquence expliquent la baisse par rapport à la version précédente.
Les deux cartes doivent utiliser cette nouvelle version ensemble.

## Émission et horloges

Le STM32 utilise le MSI calibré par le quartz LSE, puis une PLL à 80 MHz.
Le Nordic demande l'horloge HF à quartz avant d'activer PWM0.
Le timer/DMA ou PWM/EasyDMA applique chaque niveau sans interruption par bit.
Deux buffers alternent : une interruption par fin de buffer prépare la trame
suivante pendant que le matériel transmet l'autre buffer. Chaque buffer contient
une trame ; le délai disponible pour le remplir est de 648 µs.
Il n'y a pas de journal UART pendant l'émission, seulement le message de démarrage.

## Synchronisation du récepteur

Le RX attend un HIGH d'au moins 12 bits suivi d'un front descendant.
L'interruption enregistre uniquement l'horodatage et le niveau dans un buffer
de 1024 fronts. La boucle principale décode les durées entre fronts, arrondies
en nombre de bits, pendant que les interruptions continuent à remplir le buffer.
Le seuil supplémentaire à 35 % a été retiré : les contrôles de longueur, de
préambule, de SFD, et de séquence restent actifs.
Le préambule de quatre `55` et le SFD `D3` doivent correspondre exactement.
Après une erreur, le RX attend le prochain GAP. Les fronts produits pendant
l'initialisation sont écartés avant de commencer la mesure.

Le dernier bit HIGH du payload peut rejoindre le GAP HIGH : la longueur connue
de la trame permet de terminer le payload avant de commencer la trame suivante.
Le motif actuel contient au maximum 9 HIGH consécutifs, sous le seuil de 12.
Si le payload change, cette contrainte doit rester vraie, y compris aux jonctions
entre octets. Ce protocole de test n'accepte pas tout payload arbitraire.
Le préambule plus long et les contrôles renforcent la validation ; ils ne prouvent
pas que les rejets observés auparavant provenaient d'un défaut de synchronisation.

## Bilan UART après 60 secondes

Le RX affiche son démarrage, reçoit pendant 60000 ms puis affiche un seul bilan.
Aucun fichier de mesure n'est enregistré.

- **Trames complètes** : format et séquence valides, payload décodé sans comparaison.
  Sans CRC, cela ne garantit pas l’intégrité du payload.
- **Buffer débordements** : fronts perdus faute de place ; la synchronisation
  est réinitialisée. Ce compteur doit rester à zéro pour valider la capture.
- **Séquences invalides** : format ou complément incorrect ; compteur non utilisé.
- **Trames uniques** : première séquence valide et nouvelles séquences en avant.
- **Trames manquantes** : somme des sauts de séquence moins un.
- **Doublons** : répétitions consécutives de la dernière séquence valide.
- **Discontinuités** : saut ambigu vers l'arrière, redémarrage possible du TX ou
  interruption trop longue ; le RX reprend une référence sans inventer de pertes.
- **Trames attendues estimées** : calcul à partir du temps et de 648 µs, distinct
  des pertes mesurées par les séquences.

Les pertes sont mesurées entre séquences valides, pas avant
la première ni après la dernière. Le calcul modulo 65536 suppose un saut inférieur
à 32768 trames (environ 21,2 s). Des interruptions plus longues et des redémarrages
peuvent rendre le résultat ambigu ; refaire un essai continu dans ce cas.
Un doublon plus ancien peut être classé comme une discontinuité.

## Validation

Compiler puis flasher le TX choisi et le RX, ouvrir leur console et laisser le RX
terminer les 60 secondes. Pour un bon essai continu : pertes, doublons,
discontinuités, erreurs de séquence et débordements du buffer doivent rester à zéro.
Les compteurs de synchronisation permettent de voir les réacquisitions nécessaires.

Les trois projets ont été compilés. Les tests sur ordinateur exécutent les
fonctions réelles de génération/décodage : réception, retour du compteur à zéro,
pertes, doublons, acceptation de payloads différents et vérification de toutes les valeurs de
séquence contre les faux GAP. Ils ne remplacent pas un essai matériel : cette
nouvelle version doit encore être validée sur les cartes.
