============================
Liaison Manchester TX / RX
============================

La carte Nordic transmet en continu. La carte STM32 reçoit pendant 60 secondes,
puis affiche une seule fois ses statistiques sur UART et arrête la réception.
Il n'y a aucun affichage pendant la réception, ni export CSV, ni enregistrement
de mesures. Un reset du STM32 démarre une nouvelle acquisition de 60 secondes.
Le TX reste silencieux, sauf en cas d'erreur d'initialisation.

Cartes et connexions
===================

* TX : ``T_test``, Nordic nRF52833 DK, sortie **P0.04**.
* RX : ``Reciever``, STM32 Nucleo L476RG, entrée **PA9**.
* Pour une liaison électrique directe : P0.04 vers PA9 et masses communes.
* Console RX : ``/dev/ttyACM0``, **115200 bauds, 8N1**.
* Console Nordic : ``/dev/ttyACM1`` ; aucun affichage périodique côté TX.

Le dossier ``Transmitter`` correspond à une autre application ; il n'est pas
utilisé dans cette version Nordic vers STM32.

Codage et format de trame
========================

Les octets sont transmis du bit de poids fort au bit de poids faible.
La convention Manchester est::

    Bit 0 : LOW  puis HIGH  (01)
    Bit 1 : HIGH puis LOW   (10)

Un demi-bit dure **3 us**, donc un bit logique dure **6 us**.
Le débit logique est d'environ **166 667 bit/s**.

La même trame est répétée en boucle::

    GAP HIGH | PREAMBULE | SFD | PAYLOAD
    4 demis  |    55     | D3  | FF 16 C1

Le GAP dure 12 us. Les cinq octets représentent 80 demi-bits ; la trame complète
occupe donc **84 demi-bits**, soit **252 us**. Avec trois octets utiles par trame,
le débit utile maximal théorique est d'environ **95 238 bit/s**.

Les constantes ``HALF_BIT_US`` des deux fichiers ``src/main.c`` doivent rester
identiques. Le contenu de ``frame`` côté TX et de ``expected_payload`` côté RX
doit également correspondre.

Principe du TX
==============

Le TX utilise **PWM0 et EasyDMA**, sans interruption par demi-bit :

1. Le programme demande le quartz haute fréquence Nordic et attend son démarrage.
2. Il construit une séquence de 84 valeurs en RAM : le GAP HIGH, puis les octets
   encodés en Manchester.
3. PWM0 fonctionne avec une horloge de 1 MHz et une période de 3 ticks.
   Une valeur de comparaison nulle produit LOW ; une comparaison égale à la
   période produit HIGH, avec la polarité sélectionnée dans la séquence.
4. EasyDMA fournit successivement ces valeurs au PWM. Le matériel rejoue
   indéfiniment la séquence sur P0.04.

La séquence occupe **168 octets de RAM**. Elle reste valide après la fin de
``main()`` car elle est statique. Le périphérique continue à transmettre sans
traitement logiciel périodique. Le quartz et les fronts matériels réduisent
la dérive et la gigue du signal.

Principe du RX
==============

Le RX utilise **les interruptions sur fronts GPIO**, sans suréchantillonnage :

1. TIM2 tourne librement à 80 MHz. Un demi-bit de 3 us correspond à 240 ticks.
2. À chaque front montant ou descendant de PA9, l'interruption lit TIM2 et
   le niveau GPIO directement dans les registres.
3. La différence entre deux timestamps donne la durée du niveau précédent.
   Cette durée est arrondie à un ou deux demi-bits Manchester.
4. Le décodeur reconstitue les paires ``01`` ou ``10``, puis les octets.
   Une paire ``00`` ou ``11`` est rejetée.
5. Le RX vérifie successivement ``55``, ``D3``, puis les trois octets utiles.
   Chaque octet utile est comparé au payload attendu.

Pour se synchroniser, le RX recherche un niveau HIGH d'au moins **9 us**, suivi
d'un front descendant. Ce seuil est supérieur au niveau continu maximal d'une
trame Manchester normale, qui dure deux demi-bits, soit 6 us.

Chaque front rétablit la référence temporelle ; l'erreur de phase ne s'accumule
pas sur toute la trame. Une erreur provoque un retour à la recherche du GAP.
Le cas où le dernier demi-bit HIGH se prolonge dans le GAP HIGH est aussi traité.

La soustraction non signée des timestamps reste correcte au débordement de TIM2,
qui survient vers 53,7 secondes. L'interruption est prioritaire
(``IRQ_ZERO_LATENCY``) et ne fait aucun appel au noyau Zephyr.
**EXTI5..9 est réservé à PA9 dans cette application** : ajouter une autre entrée
sur ce groupe nécessite d'adapter le gestionnaire d'interruption.

Affichage UART après 60 secondes
===============================

Le RX désactive l'interruption, arrête TIM2, puis affiche :

* la durée, le demi-bit et le débit logique ;
* le nombre théorique de trames attendues et les GAP détectés ;
* les trames complètes, correctes et les payloads incorrects ;
* les erreurs Manchester, de préambule et de SFD ;
* les bits erronés dans les payloads complets ;
* le débit utile, calculé uniquement à partir des trames correctes.

Le nombre de trames attendues est une **estimation** fondée sur la durée nominale.
L'horloge interne STM32 et les trames partielles aux limites de l'acquisition
peuvent créer un écart. Le protocole n'ayant ni numéro de séquence ni CRC,
cet écart ne permet pas de compter exactement les trames perdues. Le compteur
de bits erronés concerne uniquement les trames reçues complètement.

Compilation
===========

Avec l'environnement Zephyr du projet déjà configuré::

    CCACHE_DISABLE=1 cmake --build T_test/build -j2
    CCACHE_DISABLE=1 cmake --build Reciever/build -j2

Les caches Zephyr restent dans les dossiers de construction des applications.
Les options expérimentales ont été retirées des sources et de CMake.
Les anciens fichiers de ``measurements/`` sont des archives ; ils ne sont pas
utilisés par les firmwares finaux.
