==============================
OPV4COM - Manchester TX / RX
==============================

Introduction
============

Ce projet implémente une communication numérique simple entre un émetteur
et un récepteur dans le cadre du projet OPV4COM.

L'objectif final est de transmettre des données à travers une liaison optique.
Dans un premier temps, la chaîne numérique est développée et validée avec une
connexion directe entre la sortie GPIO de l'émetteur et l'entrée GPIO du
récepteur.

La transmission utilise un codage Manchester.

L'architecture générale est la suivante::

    +-------------+                          +-------------+
    |             |                          |             |
    |     TX      |                          |     RX      |
    |             |                          |             |
    +------+------+                          +------+------+
           |                                        |
           | Payload                                | Payload reçu
           v                                        ^
    +-------------+                          +-------------+
    | Construction|                          | Décodage    |
    | de la trame |                          | de la trame |
    +------+------+                          +------+------+
           |                                        ^
           v                                        |
    +-------------+                          +-------------+
    | Manchester  |                          | Manchester  |
    | Encoder     |                          | Decoder     |
    +------+------+                          +------+------+
           |                                        ^
           v                                        |
    +-------------+                          +-------------+
    | GPIO TX     |------------------------->| GPIO RX     |
    +-------------+                          +-------------+


Dans la version finale du système, la connexion GPIO directe sera remplacée
par la chaîne optique::

    MCU TX
      |
      v
    Driver LED
      |
      v
    LED / Lampe
      |
      v
    Canal optique
      |
      v
    Photodétecteur
      |
      v
    Front-end analogique
      |
      v
    MCU RX


Principe du codage Manchester
=============================

Le codage Manchester représente chaque bit logique par deux niveaux
successifs.

La convention utilisée dans ce projet est::

    Bit logique 0  ->  0 1

    Bit logique 1  ->  1 0


Par exemple, l'octet::

    0xAA = 10101010

devient après codage Manchester::

    1    0    1    0    1    0    1    0

    10   01   10   01   10   01   10   01


La séquence physique transmise est donc::

    1001100110011001


Le principal intérêt du Manchester est qu'une transition est toujours
présente au milieu d'un bit.

Cela facilite la récupération temporelle du signal au niveau du récepteur.


Paramètres temporels
====================

La durée d'un demi-bit est fixée à::

    HALF_BIT_US = 40 us


Un bit Manchester contient deux demi-bits.

La durée d'un bit est donc::

    Tbit = 2 * 40 us

    Tbit = 80 us


Le débit logique est donc::

                 1
    Rbit = ---------------
              80 us


soit::

    Rbit = 12.5 kbit/s


Il est important de distinguer le débit des niveaux physiques Manchester
et le débit des données utiles.

Le signal physique change potentiellement toutes les 40 us, tandis qu'un
bit logique nécessite 80 us.


Format de la trame
==================

Afin de conserver un maximum de débit pour les données utiles, le protocole
utilisé reste volontairement simple.

La trame est constituée de::

    +-------------+-------------+------------------+
    | PREAMBLE    | SFD         | PAYLOAD          |
    +-------------+-------------+------------------+
    | 0xAA        | 0xD3        | N octets         |
    +-------------+-------------+------------------+


Un court intervalle LOW est également placé entre deux trames successives::

    GAP | PREAMBLE | SFD | PAYLOAD | GAP | PREAMBLE | SFD | PAYLOAD ...


Le format complet est donc::

         GAP          PREAMBLE        SFD           PAYLOAD

    +-----------+---------------+-------------+----------------+
    | LOW       |     0xAA      |    0xD3     |    N octets    |
    +-----------+---------------+-------------+----------------+
                      |               |               |
                      +---------------+---------------+
                              Manchester


Le préambule, le SFD et le payload sont tous encodés en Manchester.


GAP
---

Un court intervalle LOW est inséré entre deux trames.

Dans le TX::

    #define INTER_FRAME_HALFS 4U


Un demi-bit dure 40 us.

La durée minimale du GAP généré est donc::

    4 * 40 us = 160 us


Le GAP facilite la détection du début d'une nouvelle trame par le récepteur.

Selon le dernier niveau Manchester du payload et le premier niveau de la
trame suivante, l'intervalle entre deux fronts mesuré sur le signal peut
être supérieur à 160 us.


PREAMBLE
--------

Le préambule utilisé est::

    PREAMBLE = 0xAA


En binaire::

    10101010


Il permet au récepteur de vérifier qu'il est correctement synchronisé avec
le signal reçu.

Après décodage Manchester, le premier octet reçu doit être égal à 0xAA.


SFD
---

SFD signifie::

    Start Frame Delimiter


La valeur choisie est::

    SFD = 0xD3


En binaire::

    11010011


Le SFD permet au récepteur de confirmer que le prochain champ correspond
au payload.

Le récepteur attend donc successivement::

    0xAA -> 0xD3 -> PAYLOAD


Si le préambule ou le SFD n'est pas correct, la trame n'est pas considérée
comme valide.


PAYLOAD
-------

Le payload contient les données utiles.

Côté TX, il est défini sous forme d'un buffer.

Exemple::

    static const uint8_t payload[] = {
        0x1F
    };


Pour envoyer plusieurs octets::

    static const uint8_t payload[] = {
        0x1F,
        0x25,
        0xA7,
        0x52
    };


La taille du payload est calculée automatiquement côté TX::

    #define PAYLOAD_SIZE sizeof(payload)


La trame devient alors::

    AA | D3 | 1F | 25 | A7 | 52


Comme aucun champ LENGTH n'est transmis, le récepteur doit connaître à
l'avance la taille du payload.

Par exemple, pour quatre octets::

    #define PAYLOAD_SIZE 4U


Cette solution permet de réduire l'overhead du protocole.

Si une taille de payload variable doit être supportée ultérieurement, un
champ LENGTH pourra être ajouté dans la trame.


Architecture du TX
==================

Le TX est basé sur quatre éléments principaux::

    Payload
       |
       v
    Machine d'états
       |
       v
    Encodeur Manchester
       |
       v
    Timer hardware 40 us
       |
       v
    GPIO


Machine d'états TX
------------------

La transmission utilise une machine d'états simple::

    TX_GAP
       |
       v
    TX_PREAMBLE
       |
       v
    TX_SFD
       |
       v
    TX_PAYLOAD
       |
       v
    TX_GAP
       |
       +----> nouvelle trame


Les états sont définis par::

    enum tx_state {
        TX_GAP,
        TX_PREAMBLE,
        TX_SFD,
        TX_PAYLOAD
    };


TX_GAP
------

Pendant cet état, la sortie est maintenue à LOW::

    gpio_pin_set_dt(
        &data,
        0
    );


Le nombre de périodes est compté avec::

    gap_index++;


Lorsque le nombre demandé de demi-bits est atteint::

    if (gap_index >= INTER_FRAME_HALFS)


le préambule est chargé et la transmission commence.


TX_PREAMBLE
-----------

Le premier octet transmis est::

    0xAA


Il est chargé avec la fonction::

    load_byte(PREAMBLE);


L'octet est ensuite transmis bit par bit et demi-bit par demi-bit en
Manchester.


TX_SFD
------

Lorsque le préambule est complètement transmis, le TX charge::

    0xD3


avec::

    load_byte(SFD);


Le SFD est ensuite transmis exactement de la même manière que le préambule.


TX_PAYLOAD
----------

Après le SFD, les octets du payload sont envoyés successivement.

L'index::

    payload_index


permet de sélectionner l'octet courant::

    payload[payload_index]


Lorsque tous les octets ont été transmis, le TX retourne dans l'état
TX_GAP.


Fonction load_byte()
====================

La fonction ``load_byte()`` prépare la transmission d'un nouvel octet::

    static void load_byte(uint8_t value)
    {
        current_byte = value;

        bit_index = 7;

        half = 0;
    }


``current_byte`` contient l'octet actuellement transmis.

``bit_index`` commence à 7 car la transmission commence par le bit de poids
fort.

L'ordre de transmission est donc::

    bit7 bit6 bit5 bit4 bit3 bit2 bit1 bit0


``half`` indique quelle moitié du bit Manchester doit être générée.


Génération Manchester
=====================

Pour récupérer le bit actuellement transmis::

    bit =
        (current_byte >> bit_index) &
        0x01U;


Pour la première moitié du bit::

    level = bit;


Pour la seconde moitié::

    level = !bit;


Cela produit directement la convention choisie::

    bit = 0

    première moitié = 0
    deuxième moitié = 1

    résultat = 01


et::

    bit = 1

    première moitié = 1
    deuxième moitié = 0

    résultat = 10


La sortie est ensuite appliquée au GPIO::

    gpio_pin_set_dt(
        &data,
        level
    );


Timer hardware du TX
====================

Le timing est un point critique du système.

Une première implémentation utilisait une alarme relative reprogrammée
depuis le callback du timer.

Le principe était::

    callback
       |
       v
    traitement
       |
       v
    programmation de l'alarme
       |
       v
    attente 40 us
       |
       v
    callback suivant


Cette méthode produisait un demi-bit d'environ 55 us alors que 40 us
étaient demandées.

La durée réelle était approximativement::

    Treal = Tcallback + 40 us


Les mesures montraient principalement::

    55 us
    110 us


au lieu de::

    40 us
    80 us


La solution retenue consiste à utiliser le timer hardware en mode
périodique avec::

    counter_set_top_value()


Le compteur est configuré pour revenir périodiquement à zéro.

Avec une fréquence timer de::

    80 MHz


et une période demandée de::

    40 us


le nombre de ticks est::

    80 000 000 * 40e-6 = 3200 ticks


Le timer fonctionne donc selon le principe::

    0 -------- 3200
         40 us
                 |
                 +--> callback

    0 -------- 3200
         40 us
                 |
                 +--> callback


Le temps d'exécution du callback ne vient donc plus s'ajouter à la période
demandée.

La configuration est réalisée avec::

    half_bit_ticks =
        counter_us_to_ticks(
            timer,
            HALF_BIT_US
        );


puis::

    top_cfg.ticks =
        half_bit_ticks;

    top_cfg.callback =
        timer_callback;

    top_cfg.user_data =
        NULL;

    top_cfg.flags =
        0;


et::

    counter_set_top_value(
        timer,
        &top_cfg
    );


Le timer est finalement démarré avec::

    counter_start(timer);


Validation du TX
================

Le signal TX a été contrôlé expérimentalement.

Après correction de la gestion du timer, les intervalles principaux
observés sont::

    40 us
    80 us


Ces valeurs sont cohérentes avec le codage Manchester.

Une durée de 40 us correspond à un demi-bit.

Une durée de 80 us entre deux fronts peut apparaître lorsque deux demi-bits
adjacents possèdent le même niveau logique.

Un intervalle plus long, proche de 200 us, peut être observé entre deux
trames à cause du GAP.


Architecture du RX
==================

Le RX est basé sur une stratégie de suréchantillonnage.

L'architecture est::

    GPIO RX
       |
       v
    Timer hardware 10 us
       |
       v
    Echantillonnage
       |
       v
    Ring buffer
       |
       v
    Détection GAP
       |
       v
    Synchronisation
       |
       v
    Décodage Manchester
       |
       v
    PREAMBLE
       |
       v
    SFD
       |
       v
    PAYLOAD


Le RX ne réalise pas le décodage directement dans l'interruption timer.

Le callback du timer reste volontairement très léger.

Il effectue uniquement::

    GPIO -> lecture -> stockage dans le ring buffer


Le traitement du protocole est réalisé dans ``main()``.


Suréchantillonnage
==================

Le demi-bit TX dure::

    40 us


Le RX échantillonne le signal toutes les::

    SAMPLE_US = 10 us


On obtient donc::

    SAMPLES_PER_HALF = 4


Représentation::

    TX demi-bit

    |<------------- 40 us ------------->|

         x        x        x        x
        10       20       30       40 us

              4 échantillons


Cette méthode apporte plus de robustesse qu'une seule lecture du GPIO par
demi-bit.


Timer hardware du RX
====================

Le RX utilise également ``counter_set_top_value()``.

Pour::

    SAMPLE_US = 10 us


et une fréquence de timer de::

    80 MHz


le nombre de ticks est::

    80 000 000 * 10e-6 = 800 ticks


La configuration est donc::

    sample_ticks =
        counter_us_to_ticks(
            timer,
            SAMPLE_US
        );


puis::

    top_cfg.ticks =
        sample_ticks;

    top_cfg.callback =
        timer_callback;

    top_cfg.user_data =
        NULL;

    top_cfg.flags =
        0;


Le timer génère ainsi un événement périodique toutes les 10 us.


Callback timer RX
=================

Le callback RX lit uniquement le GPIO::

    level =
        gpio_pin_get_dt(
            &data
        );


L'échantillon est ensuite placé dans un ring buffer.

Le callback ne réalise pas::

    - le décodage Manchester
    - la recherche du préambule
    - la recherche du SFD
    - l'affichage avec printk


Cette séparation est importante pour garder une interruption courte et
prévisible.


Ring buffer
===========

Le ring buffer permet de séparer l'acquisition temps réel du traitement
du protocole.

Sa taille est::

    #define RING_SIZE 1024U


Le timer écrit les échantillons dans le buffer::

    Timer ISR
       |
       v
    sample_buffer[]
       |
       v
    main()


Deux index sont utilisés::

    write_index
    read_index


``write_index`` indique la prochaine position utilisée par le producteur,
c'est-à-dire le callback timer.

``read_index`` indique la prochaine position à traiter par le consommateur,
c'est-à-dire la boucle principale.


Le prochain index est calculé par::

    next =
        (write_index + 1U) &
        RING_MASK;


Comme la taille du buffer est une puissance de deux::

    RING_SIZE = 1024


on peut utiliser::

    RING_MASK = 1023


Cette opération évite d'utiliser un modulo classique.


Détection du GAP
================

Lorsque le RX n'est pas synchronisé, il se trouve dans l'état::

    RX_SEARCH_GAP


Il compte le nombre d'échantillons LOW consécutifs.

Le seuil utilisé est::

    GAP_MIN_SAMPLES = 12


Avec une période d'échantillonnage de 10 us::

    12 * 10 us = 120 us


Le TX produit un GAP d'au moins 160 us.

Le seuil de 120 us permet donc de reconnaître cet intervalle tout en
conservant une marge.


Lorsqu'un niveau HIGH apparaît après suffisamment de LOW::

    LOW LOW LOW LOW ... LOW HIGH
                         |
                         +--> début de trame


le RX considère ce HIGH comme le début du premier demi-bit du préambule.

Cela permet de récupérer la phase du signal.


Décision d'un demi-bit
======================

Après synchronisation, les échantillons sont regroupés par quatre.

Pour chaque groupe, le RX compte le nombre de niveaux HIGH.

Si au moins deux échantillons sont HIGH::

    high_count >= 2


le demi-bit est considéré comme HIGH.

Sinon, il est considéré comme LOW.

Le principe est donc::

    Samples          Décision

    0 0 0 0    ->       0
    0 0 0 1    ->       0
    0 0 1 1    ->       1
    0 1 1 1    ->       1
    1 1 1 1    ->       1


Cette décision majoritaire améliore la tolérance à un échantillon perturbé
ou placé près d'une transition.


Décodage Manchester
===================

Deux demi-bits sont nécessaires pour reconstruire un bit logique.

Le RX conserve donc::

    first_half


puis attend le deuxième demi-bit.


Les seules combinaisons Manchester valides sont::

    01 -> bit 0

    10 -> bit 1


La fonction de décodage applique::

    if ((first == 0U) &&
        (second == 1U)) {

        *bit = 0U;

        return true;
    }


et::

    if ((first == 1U) &&
        (second == 0U)) {

        *bit = 1U;

        return true;
    }


Les combinaisons suivantes sont invalides::

    00
    11


Une erreur Manchester provoque une perte de synchronisation et le RX
retourne à la recherche d'une nouvelle trame.


Reconstruction d'un octet
==========================

Chaque bit décodé est ajouté dans ``current_byte``.

L'opération utilisée est::

    current_byte =
        (current_byte << 1) |
        bit;


Après huit bits::

    bit_count == 8


un octet complet est disponible.


Par exemple, si les bits décodés sont::

    1 0 1 0 1 0 1 0


le résultat obtenu est::

    0xAA


Machine d'états RX
==================

Le RX utilise la machine d'états suivante::

    RX_SEARCH_GAP
          |
          | GAP détecté
          v
    RX_PREAMBLE
          |
          | 0xAA valide
          v
       RX_SFD
          |
          | 0xD3 valide
          v
     RX_PAYLOAD
          |
          | N octets reçus
          v
    RX_SEARCH_GAP


Les états sont définis par::

    enum rx_state {
        RX_SEARCH_GAP,
        RX_PREAMBLE,
        RX_SFD,
        RX_PAYLOAD
    };


Validation du préambule
=======================

Le premier octet reconstruit doit être::

    0xAA


Si::

    value != PREAMBLE


le RX considère que la synchronisation n'est pas correcte et revient dans
l'état::

    RX_SEARCH_GAP


Validation du SFD
=================

Après un préambule correct, le deuxième octet doit être::

    0xD3


Si la valeur reçue est correcte, le RX passe dans::

    RX_PAYLOAD


Sinon la trame est rejetée.


Réception du payload
====================

Une fois le préambule et le SFD validés, chaque octet suivant est placé dans::

    payload[]


L'index est incrémenté avec::

    payload_index++;


Lorsque::

    payload_index >= PAYLOAD_SIZE


le payload complet est disponible.


Pour un TX contenant::

    static const uint8_t payload[] = {
        0x1F
    };


et un RX configuré avec::

    #define PAYLOAD_SIZE 1U


la sortie attendue est par exemple::

    Frame 1 : 1F
    Frame 2 : 1F
    Frame 3 : 1F
    Frame 4 : 1F


Taille du payload
=================

Dans la version actuelle du protocole, la taille du payload n'est pas
transmise.

Le TX et le RX doivent donc utiliser la même taille.

Exemple TX::

    static const uint8_t payload[] = {
        0x11,
        0x22,
        0x33,
        0x44
    };


Le RX doit alors être configuré avec::

    #define PAYLOAD_SIZE 4U


Cette approche minimise le header de la trame.

Le header contient uniquement::

    PREAMBLE = 1 octet
    SFD      = 1 octet


L'overhead fixe est donc de deux octets, sans compter le GAP.


Efficacité de la trame
======================

Pour un payload de N octets, sans CRC, l'efficacité liée au header est::

                   N
    efficiency = -------
                 N + 2


Par exemple, pour 1 octet de payload::

                   1
    efficiency = -------
                   3

    efficiency = 33.3 %


Pour 8 octets::

                   8
    efficiency = -------
                  10

    efficiency = 80 %


Pour 32 octets::

                   32
    efficiency = --------
                   34

    efficiency ~= 94.1 %


L'impact du préambule et du SFD devient donc faible lorsque la taille du
payload augmente.


Configuration Device Tree TX
============================

Sur le nRF52833 DK, la sortie DATA est configurée dans l'overlay.

Exemple::

    #include <zephyr/dt-bindings/gpio/gpio.h>

    / {
        chosen {
            zephyr,console = &uart0;
        };

        zephyr,user {
            data-gpios = <&gpio0 4 GPIO_ACTIVE_HIGH>;
        };
    };


    &uart0 {
        compatible = "nordic,nrf-uarte";
        status = "okay";
        current-speed = <115200>;
        pinctrl-0 = <&uart0_default>;
        pinctrl-1 = <&uart0_sleep>;
        pinctrl-names = "default", "sleep";
    };


    &timer1 {
        status = "okay";
    };


Le GPIO utilisé pour DATA est donc::

    GPIO0 pin 4


Le timer utilisé est::

    TIMER1


Dans le programme C, ces ressources sont récupérées avec::

    #define DATA_NODE DT_PATH(zephyr_user)

    static const struct gpio_dt_spec data =
        GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);


et::

    #define TIMER_NODE DT_NODELABEL(timer1)

    static const struct device *timer =
        DEVICE_DT_GET(TIMER_NODE);


Validation expérimentale
========================

La première validation est réalisée avec une connexion électrique directe
entre le TX et le RX.

La connexion est::

    TX GPIO ---------------- RX GPIO

    TX GND ----------------- RX GND


Cette étape permet de valider indépendamment de la chaîne optique::

    - la construction de la trame
    - le codage Manchester
    - le timing TX
    - l'échantillonnage RX
    - la synchronisation
    - le décodage Manchester
    - la détection du préambule
    - la détection du SFD
    - la récupération du payload


Les mesures réalisées ont montré des intervalles principalement égaux à::

    40 us
    80 us


ce qui correspond au timing Manchester attendu.


Architecture finale validée
============================

La chaîne numérique actuellement validée est::

    TX
    ===

    Payload buffer
         |
         v
    PREAMBLE + SFD + PAYLOAD
         |
         v
    Manchester encoder
         |
         v
    Timer hardware
         |
         | 40 us
         v
    GPIO TX


    RX
    ===

    GPIO RX
         |
         v
    Timer hardware
         |
         | 10 us
         v
    4x oversampling
         |
         v
    Ring buffer
         |
         v
    GAP detection
         |
         v
    Manchester decoder
         |
         v
    0xAA detection
         |
         v
    0xD3 detection
         |
         v
    Payload reconstruction


Résultat actuel
===============

La communication numérique directe TX/RX est fonctionnelle.

Le TX génère correctement une trame Manchester avec un demi-bit de 40 us.

Le RX suréchantillonne le signal toutes les 10 us et utilise quatre
échantillons par demi-bit.

Le récepteur est capable de::

    - détecter le début d'une trame
    - se synchroniser sur le signal
    - décoder le Manchester
    - vérifier le préambule 0xAA
    - vérifier le SFD 0xD3
    - reconstruire le payload


Pour un payload TX égal à::

    0x1F


le RX récupère::

    0x1F


Améliorations futures
=====================

La version actuelle constitue une première couche de communication
fonctionnelle.

Les prochaines améliorations possibles sont::

    1. Ajouter un CRC-8 après le payload.

    2. Ajouter éventuellement un champ LENGTH si la taille du payload doit
       varier dynamiquement d'une trame à l'autre.

    3. Tester différentes tailles de payload.

    4. Mesurer le taux d'erreur de transmission.

    5. Remplacer la connexion GPIO directe par la chaîne optique.

    6. Tester la réception avec différents niveaux de lumière.

    7. Tester la robustesse face au bruit et à la lumière ambiante.

    8. Optimiser le débit en réduisant éventuellement le GAP ou la durée
       du demi-bit.


Une évolution particulièrement importante sera l'ajout d'un CRC.

La trame pourra alors devenir::

    +----------+----------+----------------+---------+
    | PREAMBLE | SFD      | PAYLOAD        | CRC-8   |
    +----------+----------+----------------+---------+
    | 0xAA     | 0xD3     | N octets       | 1 octet |
    +----------+----------+----------------+---------+


Le CRC permettra au récepteur de vérifier que le payload décodé est
effectivement valide avant de le transmettre à l'application.


Résumé
======

Le protocole OPV4COM actuellement développé utilise une trame volontairement
courte afin de maximiser le débit utile.

La structure retenue est::

    GAP | 0xAA | 0xD3 | PAYLOAD


Le codage Manchester utilisé est::

    0 -> 01
    1 -> 10


Les paramètres temporels sont::

    Demi-bit              : 40 us
    Bit Manchester        : 80 us
    Débit logique         : 12.5 kbit/s
    Echantillonnage RX    : 10 us
    Oversampling RX       : x4


Le TX utilise un timer hardware périodique afin de garantir précisément
la durée des demi-bits.

Le RX utilise également un timer hardware périodique et un ring buffer
afin de séparer l'acquisition temps réel du traitement du protocole.

Cette architecture fournit une base simple, efficace et robuste pour
l'intégration future de la communication optique du projet OPV4COM.