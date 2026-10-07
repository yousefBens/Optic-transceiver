==================
Manchester TX / RX
==================

Introduction
============

Ce projet implémente une communication numérique simple entre un émetteur
et un récepteur.

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

La convention utilisée est::

    Bit logique 0  ->  0 1
    Bit logique 1  ->  1 0

Le préambule utilisé est::

    0x55 = 01010101

Après codage Manchester::

    0    1    0    1    0    1    0    1
    01   10   01   10   01   10   01   10

La séquence physique transmise est donc::

    0110011001100110

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
    Rbit = --------
            80 us

soit::

    Rbit = 12.5 kbit/s

Le signal physique peut changer toutes les 40 us, tandis qu'un bit logique
nécessite 80 us.


Format de la trame
==================

La trame est constituée de::

    +-------------+-------------+------------------+
    | PREAMBLE    | SFD         | PAYLOAD          |
    +-------------+-------------+------------------+
    | 0x55        | 0xD3        | N octets         |
    +-------------+-------------+------------------+

Un intervalle HIGH est placé entre deux trames successives::

    GAP | PREAMBLE | SFD | PAYLOAD | GAP | PREAMBLE | SFD | PAYLOAD ...

Le format complet est donc::

         GAP          PREAMBLE        SFD           PAYLOAD

    +-----------+---------------+-------------+----------------+
    | HIGH      |     0x55      |    0xD3     |    N octets    |
    +-----------+---------------+-------------+----------------+
                        |               |               |
                        +---------------+---------------+
                                Manchester

Le préambule, le SFD et le payload sont tous encodés en Manchester.


GAP
===

Un intervalle HIGH est inséré entre deux trames.

Dans le TX::

    #define INTER_FRAME_HALFS 4U

Un demi-bit dure 40 us.

La durée minimale du GAP généré est donc::

    4 * 40 us = 160 us

Le GAP permet au récepteur de distinguer deux trames successives.

Le niveau HIGH est donc l'état utilisé entre deux trames.


PREAMBLE
========

Le préambule utilisé est::

    PREAMBLE = 0x55

En binaire::

    01010101

Le premier bit logique est donc 0.

Avec la convention Manchester utilisée::

    0 -> 01

Le début du préambule commence donc par un niveau LOW.

Comme le GAP est HIGH, le passage du GAP au préambule produit un front
descendant::

    GAP HIGH                PREAMBLE

    -------------------+
                       |
                       +---------- LOW
                       ^
                       |
                 début de trame

Après décodage Manchester, le premier octet reçu doit être égal à 0x55.


SFD
===

SFD signifie::

    Start Frame Delimiter

La valeur choisie est::

    SFD = 0xD3

En binaire::

    11010011

Le SFD permet au récepteur de confirmer que le prochain champ correspond
au payload.

Le récepteur attend donc successivement::

    0x55 -> 0xD3 -> PAYLOAD

Si le préambule ou le SFD n'est pas correct, la trame n'est pas considérée
comme valide.


PAYLOAD
=======

Le payload contient les données utiles.

Côté TX, il est défini sous forme d'un buffer.

Exemple::

    static const uint8_t payload[] = {
        0xFF,
        0x16,
        0xC1
    };

La taille du payload est calculée automatiquement côté TX::

    #define PAYLOAD_SIZE sizeof(payload)

La trame devient alors::

    GAP HIGH | 55 | D3 | FF | 16 | C1 | GAP HIGH

Comme aucun champ LENGTH n'est transmis, le récepteur doit connaître à
l'avance la taille du payload.

Pour cet exemple::

    #define PAYLOAD_SIZE 3U


Architecture du TX
==================

Le TX est basé sur les éléments suivants::

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
==================

La transmission utilise la machine d'états suivante::

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
======

Pendant cet état, la sortie est maintenue à HIGH::

    gpio_pin_set_dt(&data, 1);

Le nombre de périodes est compté avec::

    gap_index++;

Lorsque le nombre demandé de demi-bits est atteint::

    if (gap_index >= INTER_FRAME_HALFS)

le préambule est chargé et la transmission commence.


TX_PREAMBLE
===========

Le premier octet transmis est::

    0x55

Il est chargé avec::

    load_byte(PREAMBLE);

L'octet est ensuite transmis bit par bit et demi-bit par demi-bit en
Manchester.


TX_SFD
======

Lorsque le préambule est complètement transmis, le TX charge::

    0xD3

avec::

    load_byte(SFD);

Le SFD est transmis de la même manière que le préambule.


TX_PAYLOAD
==========

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

    bit = (current_byte >> bit_index) & 0x01U;

Pour la première moitié du bit::

    level = bit;

Pour la seconde moitié::

    level = !bit;

Cela produit directement::

    bit 0 -> 01
    bit 1 -> 10

La sortie est appliquée au GPIO::

    gpio_pin_set_dt(&data, level);


Timer hardware du TX
====================

Le TX utilise un timer hardware périodique.

La durée d'un demi-bit est::

    40 us

La conversion est réalisée avec::

    half_bit_ticks = counter_us_to_ticks(timer, HALF_BIT_US);

La configuration périodique est réalisée avec::

    top_cfg.ticks = half_bit_ticks;
    top_cfg.callback = timer_callback;
    top_cfg.user_data = NULL;
    top_cfg.flags = 0;

puis::

    counter_set_top_value(timer, &top_cfg);

Le timer est finalement démarré avec::

    counter_start(timer);

Cette méthode permet de générer périodiquement un nouveau demi-bit sans
ajouter le temps d'exécution du callback à la période demandée.


Architecture du RX
==================

Le RX utilise une stratégie de suréchantillonnage::

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
    Détection GAP HIGH
       |
       v
    Synchronisation
       |
       v
    Décodage Manchester
       |
       v
    PREAMBLE 0x55
       |
       v
    SFD 0xD3
       |
       v
    PAYLOAD

Le RX ne réalise pas le décodage directement dans l'interruption timer.

Le callback du timer effectue uniquement::

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

Cette méthode permet d'utiliser plusieurs échantillons pour déterminer
le niveau d'un demi-bit.


Timer hardware du RX
====================

Le RX utilise également ``counter_set_top_value()``.

Pour::

    SAMPLE_US = 10 us

le nombre de ticks est calculé avec::

    sample_ticks = counter_us_to_ticks(timer, SAMPLE_US);

La configuration est::

    top_cfg.ticks = sample_ticks;
    top_cfg.callback = timer_callback;
    top_cfg.user_data = NULL;
    top_cfg.flags = 0;

Le timer génère ainsi un événement périodique toutes les 10 us.


Callback timer RX
=================

Le callback RX lit le GPIO::

    level = gpio_pin_get_dt(&data);

L'échantillon est ensuite placé dans un ring buffer.

Le callback ne réalise pas :

* le décodage Manchester ;
* la recherche du préambule ;
* la recherche du SFD ;
* l'affichage avec ``printk``.

Cette séparation permet de garder une interruption courte.


Ring buffer
===========

Le ring buffer permet de séparer l'acquisition temps réel du traitement
du protocole.

Sa taille est::

    #define RING_SIZE 1024U

Deux index sont utilisés::

    write_index
    read_index

``write_index`` indique la prochaine position utilisée par le callback timer.

``read_index`` indique la prochaine position traitée par la boucle principale.

Le prochain index est calculé par::

    next = (write_index + 1U) & RING_MASK;

Avec::

    RING_SIZE = 1024
    RING_MASK = 1023


Détection du GAP
================

Lorsque le RX n'est pas synchronisé, il se trouve dans l'état::

    RX_SEARCH_GAP

Il compte le nombre d'échantillons HIGH consécutifs.

Le seuil utilisé est::

    GAP_MIN_SAMPLES = 12

Avec une période d'échantillonnage de 10 us::

    12 * 10 us = 120 us

Le TX produit un GAP d'au moins 160 us.

Le seuil de 120 us permet donc de reconnaître cet intervalle avec une marge.

Lorsqu'un niveau LOW apparaît après suffisamment de HIGH::

    HIGH HIGH HIGH HIGH ... HIGH LOW
                              |
                              +--> début de trame

le RX considère ce LOW comme le début du premier demi-bit du préambule.

Cela permet de récupérer la phase du signal.


Décision d'un demi-bit
======================

Après synchronisation, les échantillons sont regroupés par quatre.

Pour chaque groupe, le RX compte le nombre de niveaux HIGH.

Si au moins deux échantillons sont HIGH::

    high_count >= 2

le demi-bit est considéré comme HIGH.

Sinon, il est considéré comme LOW.

Exemples::

    Samples          Décision

    0 0 0 0    ->       0
    0 0 0 1    ->       0
    0 0 1 1    ->       1
    0 1 1 1    ->       1
    1 1 1 1    ->       1


Décodage Manchester
===================

Deux demi-bits sont nécessaires pour reconstruire un bit logique.

Les combinaisons Manchester valides sont::

    01 -> bit 0
    10 -> bit 1

Les combinaisons suivantes sont invalides::

    00
    11

Une erreur Manchester provoque une perte de synchronisation et le RX
retourne à la recherche d'une nouvelle trame.


Reconstruction d'un octet
=========================

Chaque bit décodé est ajouté dans ``current_byte`` avec::

    current_byte = (current_byte << 1) | bit;

Après huit bits, un octet complet est disponible.

Par exemple, pour le préambule::

    0 1 0 1 0 1 0 1

le résultat obtenu est::

    0x55


Machine d'états RX
==================

Le RX utilise la machine d'états suivante::

    RX_SEARCH_GAP
          |
          | GAP HIGH détecté
          v
    RX_PREAMBLE
          |
          | 0x55 valide
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

    0x55

Si::

    value != PREAMBLE

le RX considère que la synchronisation n'est pas correcte et retourne dans
l'état::

    RX_SEARCH_GAP


Validation du SFD
=================

Après un préambule correct, le deuxième octet doit être::

    0xD3

Si la valeur reçue est correcte, le RX passe dans::

    RX_PAYLOAD

Sinon, la trame est rejetée.


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
        0xFF,
        0x16,
        0xC1
    };

le RX doit utiliser::

    #define PAYLOAD_SIZE 3U

La sortie attendue est par exemple::

    Frame 1 : FF 16 C1
    Frame 2 : FF 16 C1
    Frame 3 : FF 16 C1


Taille du payload
=================

Dans la version actuelle, la taille du payload n'est pas transmise.

Le TX et le RX doivent donc utiliser la même taille.

Cette approche minimise le header de la trame.

Le header contient uniquement::

    PREAMBLE = 1 octet
    SFD      = 1 octet

L'overhead fixe est donc de deux octets, sans compter le GAP.


Validation expérimentale
========================

La première validation est réalisée avec une connexion électrique directe
entre le TX et le RX.

La connexion est::

    TX GPIO ---------------- RX GPIO
    TX GND ----------------- RX GND

Cette étape permet de valider indépendamment de la chaîne optique :

* la construction de la trame ;
* le codage Manchester ;
* le timing TX ;
* l'échantillonnage RX ;
* la synchronisation ;
* le décodage Manchester ;
* la détection du préambule ;
* la détection du SFD ;
* la récupération du payload.


Architecture actuelle
=====================

La chaîne numérique est::

    TX
    ==

    Payload buffer
         |
         v
    0x55 + 0xD3 + PAYLOAD
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
    ==

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
    GAP HIGH detection
         |
         v
    Manchester decoder
         |
         v
    0x55 detection
         |
         v
    0xD3 detection
         |
         v
    Payload reconstruction


Résumé
======

La structure de la trame est::

    GAP HIGH | 0x55 | 0xD3 | PAYLOAD

Le codage Manchester utilisé est::

    0 -> 01
    1 -> 10

Les paramètres temporels sont::

    Demi-bit              : 40 us
    Bit Manchester        : 80 us
    Débit logique         : 12.5 kbit/s
    Echantillonnage RX    : 10 us
    Oversampling RX       : x4

Le TX utilise un timer hardware périodique pour générer les demi-bits.

Le RX utilise un timer hardware périodique et un ring buffer pour séparer
l'acquisition du traitement du protocole.

Le GAP est maintenu à HIGH et le préambule utilisé est 0x55.