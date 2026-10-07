**==============================**

Manchester TX / RX

**==============================**



Introduction

**============**



Ce projet implémente une communication numérique simple entre un émetteur

et un récepteur pour cette liaison numérique.



L'objectif final est de transmettre des données à travers une liaison optique.

Dans un premier temps, la chaîne numérique est développée et validée avec une

connexion directe entre la sortie GPIO de l'émetteur et l'entrée GPIO du

récepteur.



La transmission utilise un codage Manchester.



L'architecture générale est la suivante::



&#x20;   +-------------+                          +-------------+

&#x20;   \|             |                          |             |

&#x20;   \|     TX      |                          |     RX      |

&#x20;   \|             |                          |             |

&#x20;   +------+------+                          +------+------+

&#x20;          \|                                        |

&#x20;          \| Payload                                | Payload reçu

&#x20;          v                                        ^

&#x20;   +-------------+                          +-------------+

&#x20;   \| Construction|                          | Décodage    |

&#x20;   \| de la trame |                          | de la trame |

&#x20;   +------+------+                          +------+------+

&#x20;          \|                                        ^

&#x20;          v                                        |

&#x20;   +-------------+                          +-------------+

&#x20;   \| Manchester  |                          | Manchester  |

&#x20;   \| Encoder     |                          | Decoder     |

&#x20;   +------+------+                          +------+------+

&#x20;          \|                                        ^

&#x20;          v                                        |

&#x20;   +-------------+                          +-------------+

&#x20;   \| GPIO TX     |------------------------->| GPIO RX     |

&#x20;   +-------------+                          +-------------+





Dans une version ultérieure du système, la connexion GPIO directe sera remplacée

par la chaîne optique::



&#x20;   MCU TX

&#x20;     |

&#x20;     v

&#x20;   Driver LED

&#x20;     |

&#x20;     v

&#x20;   LED / Lampe

&#x20;     |

&#x20;     v

&#x20;   Canal optique

&#x20;     |

&#x20;     v

&#x20;   Photodétecteur

&#x20;     |

&#x20;     v

&#x20;   Front-end analogique

&#x20;     |

&#x20;     v

&#x20;   MCU RX





Principe du codage Manchester

**=============================**



Le codage Manchester représente chaque bit logique par deux niveaux

successifs.



La convention utilisée dans ce projet est::



&#x20;   Bit logique 0  ->  0 1



&#x20;   Bit logique 1  ->  1 0





Par exemple, l'octet::



&#x20;   0x55 = 01010101



devient après codage Manchester::



&#x20;   0    1    0    1    0    1    0    1



&#x20;   01   10   01   10   01   10   01   10





La séquence physique transmise est donc::



&#x20;   0110011001100110





Le principal intérêt du Manchester est qu'une transition est toujours

présente au milieu d'un bit.



Cela facilite la récupération temporelle du signal au niveau du récepteur.





Paramètres temporels

**====================**



La durée d'un demi-bit est fixée à::



&#x20;   HALF_BIT_US = 40 us





Un bit Manchester contient deux demi-bits.



La durée d'un bit est donc::



&#x20;   Tbit = 2 \* 40 us



&#x20;   Tbit = 80 us





Le débit logique est donc::



&#x20;                1

&#x20;   Rbit = ---------------

&#x20;             80 us





soit::



&#x20;   Rbit = 12.5 kbit/s





Il est important de distinguer le débit des niveaux physiques Manchester

et le débit des données utiles.



Le signal physique change potentiellement toutes les 40 us, tandis qu'un

bit logique nécessite 80 us.





Format de la trame

**==================**



Afin de conserver un maximum de débit pour les données utiles, le protocole

utilisé reste volontairement simple.



La trame est constituée de::



&#x20;   +-------------+-------------+------------------+

&#x20;   \| PREAMBLE    | SFD         | PAYLOAD          |

&#x20;   +-------------+-------------+------------------+

&#x20;   \| 0x55        | 0xD3        | N octets         |

&#x20;   +-------------+-------------+------------------+





Un court intervalle HIGH est également placé entre deux trames successives::



&#x20;   GAP | PREAMBLE | SFD | PAYLOAD | GAP | PREAMBLE | SFD | PAYLOAD ...





Le format complet est donc::



&#x20;        GAP          PREAMBLE        SFD           PAYLOAD



&#x20;   +-----------+---------------+-------------+----------------+

&#x20;   \| HIGH      |     0x55      |    0xD3     |    N octets    |

&#x20;   +-----------+---------------+-------------+----------------+

&#x20;                     \|               |               |

&#x20;                     +---------------+---------------+

&#x20;                             Manchester





Le préambule, le SFD et le payload sont tous encodés en Manchester.





GAP

**---**



Un court intervalle HIGH est inséré entre deux trames.



Dans le TX::



&#x20;   \#define INTER_FRAME_HALFS 4U





Un demi-bit dure 40 us.



La durée minimale du GAP généré est donc::



&#x20;   4 \* 40 us = 160 us





Le GAP facilite la détection du début d'une nouvelle trame par le récepteur.



Selon le dernier niveau Manchester du payload et le premier niveau de la

trame suivante, l'intervalle entre deux fronts mesuré sur le signal peut

être supérieur à 160 us.





PREAMBLE

**--------**



Le préambule utilisé est::



&#x20;   PREAMBLE = 0x55





En binaire::



&#x20;   01010101





Il permet au récepteur de vérifier qu'il est correctement synchronisé avec

le signal reçu.



Après décodage Manchester, le premier octet reçu doit être égal à 0x55.





SFD

**---**



SFD signifie::



&#x20;   Start Frame Delimiter





La valeur choisie est::



&#x20;   SFD = 0xD3





En binaire::



&#x20;   11010011





Le SFD permet au récepteur de confirmer que le prochain champ correspond

au payload.



Le récepteur attend donc successivement::



&#x20;   0x55 -> 0xD3 -> PAYLOAD





Si le préambule ou le SFD n'est pas correct, la trame n'est pas considérée

comme valide.





PAYLOAD

**-------**



Le payload contient les données utiles.



Côté TX, il est défini sous forme d'un buffer.



Exemple::



&#x20;   static const uint8_t payload[] = {

&#x20;       0x1F

&#x20;   };





Pour envoyer plusieurs octets::



&#x20;   static const uint8_t payload[] = {

&#x20;       0x1F,

&#x20;       0x25,

&#x20;       0xA7,

&#x20;       0x52

&#x20;   };





La taille du payload est calculée automatiquement côté TX::



&#x20;   \#define PAYLOAD_SIZE sizeof(payload)





La trame devient alors::



&#x20;   AA | D3 | 1F | 25 | A7 | 52





Comme aucun champ LENGTH n'est transmis, le récepteur doit connaître à

l'avance la taille du payload.



Par exemple, pour quatre octets::



&#x20;   \#define PAYLOAD_SIZE 4U





Cette solution permet de réduire l'overhead du protocole.



Si une taille de payload variable doit être supportée ultérieurement, un

champ LENGTH pourra être ajouté dans la trame.





Architecture du TX

**==================**



Le TX est basé sur quatre éléments principaux::



&#x20;   Payload

&#x20;      |

&#x20;      v

&#x20;   Machine d'états

&#x20;      |

&#x20;      v

&#x20;   Encodeur Manchester

&#x20;      |

&#x20;      v

&#x20;   Timer hardware 40 us

&#x20;      |

&#x20;      v

&#x20;   GPIO





Machine d'états TX

**------------------**



La transmission utilise une machine d'états simple::



&#x20;   TX_GAP

&#x20;      |

&#x20;      v

&#x20;   TX_PREAMBLE

&#x20;      |

&#x20;      v

&#x20;   TX_SFD

&#x20;      |

&#x20;      v

&#x20;   TX_PAYLOAD

&#x20;      |

&#x20;      v

&#x20;   TX_GAP

&#x20;      |

&#x20;      +----> nouvelle trame





Les états sont définis par::



&#x20;   enum tx_state {

&#x20;       TX_GAP,

&#x20;       TX_PREAMBLE,

&#x20;       TX_SFD,

&#x20;       TX_PAYLOAD

&#x20;   };





TX_GAP

**------**



Pendant cet état, la sortie est maintenue à HIGH::



&#x20;   gpio_pin_set_dt(

&#x20;       &data,

&#x20;       0

&#x20;   );





Le nombre de périodes est compté avec::



&#x20;   gap_index++;





Lorsque le nombre demandé de demi-bits est atteint::



&#x20;   if (gap_index >= INTER_FRAME_HALFS)





le préambule est chargé et la transmission commence.





TX_PREAMBLE

**-----------**



Le premier octet transmis est::



&#x20;   0x55





Il est chargé avec la fonction::



&#x20;   load_byte(PREAMBLE);





L'octet est ensuite transmis bit par bit et demi-bit par demi-bit en

Manchester.





TX_SFD

**------**



Lorsque le préambule est complètement transmis, le TX charge::



&#x20;   0xD3





avec::



&#x20;   load_byte(SFD);





Le SFD est ensuite transmis exactement de la même manière que le préambule.





TX_PAYLOAD

**----------**



Après le SFD, les octets du payload sont envoyés successivement.



L'index::



&#x20;   payload_index





permet de sélectionner l'octet courant::



&#x20;   payload[payload_index]





Lorsque tous les octets ont été transmis, le TX retourne dans l'état

TX_GAP.





Fonction load_byte()

**====================**



La fonction \`\`load_byte()\`\` prépare la transmission d'un nouvel octet::



&#x20;   static void load_byte(uint8_t value)

&#x20;   {

&#x20;       current_byte = value;



&#x20;       bit_index = 7;



&#x20;       half = 0;

&#x20;   }





\`\`current_byte\`\` contient l'octet actuellement transmis.



\`\`bit_index\`\` commence à 7 car la transmission commence par le bit de poids

fort.



L'ordre de transmission est donc::



&#x20;   bit7 bit6 bit5 bit4 bit3 bit2 bit1 bit0





\`\`half\`\` indique quelle moitié du bit Manchester doit être générée.





Génération Manchester

**=====================**



Pour récupérer le bit actuellement transmis::



&#x20;   bit =

&#x20;       (current_byte >> bit_index) &

&#x20;       0x01U;





Pour la première moitié du bit::



&#x20;   level = bit;





Pour la seconde moitié::



&#x20;   level = !bit;





Cela produit directement la convention choisie::



&#x20;   bit = 0



&#x20;   première moitié = 0

&#x20;   deuxième moitié = 1



&#x20;   résultat = 01





et::



&#x20;   bit = 1



&#x20;   première moitié = 1

&#x20;   deuxième moitié = 0



&#x20;   résultat = 10





La sortie est ensuite appliquée au GPIO::



&#x20;   gpio_pin_set_dt(

&#x20;       &data,

&#x20;       level

&#x20;   );





Timer hardware du TX

**====================**



Le timing est un point critique du système.



Une première implémentation utilisait une alarme relative reprogrammée

depuis le callback du timer.



Le principe était::



&#x20;   callback

&#x20;      |

&#x20;      v

&#x20;   traitement

&#x20;      |

&#x20;      v

&#x20;   programmation de l'alarme

&#x20;      |

&#x20;      v

&#x20;   attente 40 us

&#x20;      |

&#x20;      v

&#x20;   callback suivant





Cette méthode produisait un demi-bit d'environ 55 us alors que 40 us

étaient demandées.



La durée réelle était approximativement::



&#x20;   Treal = Tcallback + 40 us





Les mesures montraient principalement::



&#x20;   55 us

&#x20;   110 us





au lieu de::



&#x20;   40 us

&#x20;   80 us





La solution retenue consiste à utiliser le timer hardware en mode

périodique avec::



&#x20;   counter_set_top_value()





Le compteur est configuré pour revenir périodiquement à zéro.



Avec une fréquence timer de::



&#x20;   80 MHz





et une période demandée de::



&#x20;   40 us





le nombre de ticks est::



&#x20;   80 000 000 \* 40e-6 = 3200 ticks





Le timer fonctionne donc selon le principe::



&#x20;   0 -------- 3200

&#x20;        40 us

&#x20;                |

&#x20;                +--> callback



&#x20;   0 -------- 3200

&#x20;        40 us

&#x20;                |

&#x20;                +--> callback





Le temps d'exécution du callback ne vient donc plus s'ajouter à la période

demandée.



La configuration est réalisée avec::



&#x20;   half_bit_ticks =

&#x20;       counter_us_to_ticks(

&#x20;           timer,

&#x20;           HALF_BIT_US

&#x20;       );





puis::



&#x20;   top_cfg.ticks =

&#x20;       half_bit_ticks;



&#x20;   top_cfg.callback =

&#x20;       timer_callback;



&#x20;   top_cfg.user_data =

&#x20;       NULL;



&#x20;   top_cfg.flags =

&#x20;       0;





et::



&#x20;   counter_set_top_value(

&#x20;       timer,

&#x20;       &top_cfg

&#x20;   );





Le timer est finalement démarré avec::



&#x20;   counter_start(timer);





Validation du TX

**================**



Le signal TX a été contrôlé expérimentalement.



Après correction de la gestion du timer, les intervalles principaux

observés sont::



&#x20;   40 us

&#x20;   80 us





Ces valeurs sont cohérentes avec le codage Manchester.



Une durée de 40 us correspond à un demi-bit.



Une durée de 80 us entre deux fronts peut apparaître lorsque deux demi-bits

adjacents possèdent le même niveau logique.



Un intervalle plus long, proche de 200 us, peut être observé entre deux

trames à cause du GAP.





Architecture du RX

**==================**



Le RX est basé sur une stratégie de suréchantillonnage.



L'architecture est::



&#x20;   GPIO RX

&#x20;      |

&#x20;      v

&#x20;   Timer hardware 10 us

&#x20;      |

&#x20;      v

&#x20;   Echantillonnage

&#x20;      |

&#x20;      v

&#x20;   Ring buffer

&#x20;      |

&#x20;      v

&#x20;   Détection GAP

&#x20;      |

&#x20;      v

&#x20;   Synchronisation

&#x20;      |

&#x20;      v

&#x20;   Décodage Manchester

&#x20;      |

&#x20;      v

&#x20;   PREAMBLE

&#x20;      |

&#x20;      v

&#x20;   SFD

&#x20;      |

&#x20;      v

&#x20;   PAYLOAD





Le RX ne réalise pas le décodage directement dans l'interruption timer.



Le callback du timer reste volontairement très léger.



Il effectue uniquement::



&#x20;   GPIO -> lecture -> stockage dans le ring buffer





Le traitement du protocole est réalisé dans \`\`main()\`\`.





Suréchantillonnage

**==================**



Le demi-bit TX dure::



&#x20;   40 us





Le RX échantillonne le signal toutes les::



&#x20;   SAMPLE_US = 10 us





On obtient donc::



&#x20;   SAMPLES_PER_HALF = 4





Représentation::



&#x20;   TX demi-bit



&#x20;   |<------------- 40 us ------------->|



&#x20;        x        x        x        x

&#x20;       10       20       30       40 us



&#x20;             4 échantillons





Cette méthode apporte plus de robustesse qu'une seule lecture du GPIO par

demi-bit.





Timer hardware du RX

**====================**



Le RX utilise également \`\`counter_set_top_value()\`\`.



Pour::



&#x20;   SAMPLE_US = 10 us





et une fréquence de timer de::



&#x20;   80 MHz





le nombre de ticks est::



&#x20;   80 000 000 \* 10e-6 = 800 ticks





La configuration est donc::



&#x20;   sample_ticks =

&#x20;       counter_us_to_ticks(

&#x20;           timer,

&#x20;           SAMPLE_US

&#x20;       );





puis::



&#x20;   top_cfg.ticks =

&#x20;       sample_ticks;



&#x20;   top_cfg.callback =

&#x20;       timer_callback;



&#x20;   top_cfg.user_data =

&#x20;       NULL;



&#x20;   top_cfg.flags =

&#x20;       0;





Le timer génère ainsi un événement périodique toutes les 10 us.





Callback timer RX

**=================**



Le callback RX lit uniquement le GPIO::



&#x20;   level =

&#x20;       gpio_pin_get_dt(

&#x20;           &data

&#x20;       );





L'échantillon est ensuite placé dans un ring buffer.



Le callback ne réalise pas::



&#x20;   \- le décodage Manchester

&#x20;   \- la recherche du préambule

&#x20;   \- la recherche du SFD

&#x20;   \- l'affichage avec printk





Cette séparation est importante pour garder une interruption courte et

prévisible.





Ring buffer

**===========**



Le ring buffer permet de séparer l'acquisition temps réel du traitement

du protocole.



Sa taille est::



&#x20;   \#define RING_SIZE 1024U





Le timer écrit les échantillons dans le buffer::



&#x20;   Timer ISR

&#x20;      |

&#x20;      v

&#x20;   sample_buffer[]

&#x20;      |

&#x20;      v

&#x20;   main()





Deux index sont utilisés::



&#x20;   write_index

&#x20;   read_index





\`\`write_index\`\` indique la prochaine position utilisée par le producteur,

c'est-à-dire le callback timer.



\`\`read_index\`\` indique la prochaine position à traiter par le consommateur,

c'est-à-dire la boucle principale.





Le prochain index est calculé par::



&#x20;   next =

&#x20;       (write_index + 1U) &

&#x20;       RING_MASK;





Comme la taille du buffer est une puissance de deux::



&#x20;   RING_SIZE = 1024





on peut utiliser::



&#x20;   RING_MASK = 1023





Cette opération évite d'utiliser un modulo classique.





Détection du GAP

**================**



Lorsque le RX n'est pas synchronisé, il se trouve dans l'état::



&#x20;   RX_SEARCH_GAP





Il compte le nombre d'échantillons HIGH consécutifs.



Le seuil utilisé est::



&#x20;   GAP_MIN_SAMPLES = 12





Avec une période d'échantillonnage de 10 us::



&#x20;   12 \* 10 us = 120 us





Le TX produit un GAP HIGH d'au moins 160 us.



Le seuil de 120 us permet donc de reconnaître cet intervalle tout en

conservant une marge.





Lorsqu'un niveau LOW apparaît après suffisamment de HIGH::



&#x20;   HIGH HIGH HIGH HIGH ... HIGH LOW

&#x20;                        |

&#x20;                        +--> début de trame





le RX considère ce LOW comme le début du premier demi-bit du préambule.



Cela permet de récupérer la phase du signal.





Décision d'un demi-bit

**======================**



Après synchronisation, les échantillons sont regroupés par quatre.



Pour chaque groupe, le RX compte le nombre de niveaux HIGH.



Si au moins deux échantillons sont HIGH::



&#x20;   high_count >= 2





le demi-bit est considéré comme HIGH.



Sinon, il est considéré comme LOW.



Le principe est donc::



&#x20;   Samples          Décision



&#x20;   0 0 0 0    ->       0

&#x20;   0 0 0 1    ->       0

&#x20;   0 0 1 1    ->       1

&#x20;   0 1 1 1    ->       1

&#x20;   1 1 1 1    ->       1





Cette décision majoritaire améliore la tolérance à un échantillon perturbé

ou placé près d'une transition.





Décodage Manchester

**===================**



Deux demi-bits sont nécessaires pour reconstruire un bit logique.



Le RX conserve donc::



&#x20;   first_half





puis attend le deuxième demi-bit.





Les seules combinaisons Manchester valides sont::



&#x20;   01 -> bit 0



&#x20;   10 -> bit 1





La fonction de décodage applique::



&#x20;   if ((first == 0U) &&

&#x20;       (second == 1U)) {



&#x20;       \*bit = 0U;



&#x20;       return true;

&#x20;   }





et::



&#x20;   if ((first == 1U) &&

&#x20;       (second == 0U)) {



&#x20;       \*bit = 1U;



&#x20;       return true;

&#x20;   }





Les combinaisons suivantes sont invalides::



&#x20;   00

&#x20;   11





Une erreur Manchester provoque une perte de synchronisation et le RX

retourne à la recherche d'une nouvelle trame.





Reconstruction d'un octet

**==========================**



Chaque bit décodé est ajouté dans \`\`current_byte\`\`.



L'opération utilisée est::



&#x20;   current_byte =

&#x20;       (current_byte << 1) |

&#x20;       bit;





Après huit bits::



&#x20;   bit_count == 8





un octet complet est disponible.





Par exemple, si les bits décodés sont::



&#x20;   1 0 1 0 1 0 1 0





le résultat obtenu est::



&#x20;   0x55





Machine d'états RX

**==================**



Le RX utilise la machine d'états suivante::



&#x20;   RX_SEARCH_GAP

&#x20;         |

&#x20;         \| GAP détecté

&#x20;         v

&#x20;   RX_PREAMBLE

&#x20;         |

&#x20;         \| 0x55 valide

&#x20;         v

&#x20;      RX_SFD

&#x20;         |

&#x20;         \| 0xD3 valide

&#x20;         v

&#x20;    RX_PAYLOAD

&#x20;         |

&#x20;         \| N octets reçus

&#x20;         v

&#x20;   RX_SEARCH_GAP





Les états sont définis par::



&#x20;   enum rx_state {

&#x20;       RX_SEARCH_GAP,

&#x20;       RX_PREAMBLE,

&#x20;       RX_SFD,

&#x20;       RX_PAYLOAD

&#x20;   };





Validation du préambule

**=======================**



Le premier octet reconstruit doit être::



&#x20;   0x55





Si::



&#x20;   value != PREAMBLE





le RX considère que la synchronisation n'est pas correcte et revient dans

l'état::



&#x20;   RX_SEARCH_GAP





Validation du SFD

**=================**



Après un préambule correct, le deuxième octet doit être::



&#x20;   0xD3





Si la valeur reçue est correcte, le RX passe dans::



&#x20;   RX_PAYLOAD





Sinon la trame est rejetée.





Réception du payload

**====================**



Une fois le préambule et le SFD validés, chaque octet suivant est placé dans::



&#x20;   payload[]





L'index est incrémenté avec::



&#x20;   payload_index++;





Lorsque::



&#x20;   payload_index >= PAYLOAD_SIZE





le payload complet est disponible.





Pour un TX contenant::



&#x20;   static const uint8_t payload[] = {

&#x20;       0x1F

&#x20;   };





et un RX configuré avec::



&#x20;   \#define PAYLOAD_SIZE 1U





la sortie attendue est par exemple::



&#x20;   Frame 1 : 1F

&#x20;   Frame 2 : 1F

&#x20;   Frame 3 : 1F

&#x20;   Frame 4 : 1F





Taille du payload

**=================**



Dans la version actuelle du protocole, la taille du payload n'est pas

transmise.



Le TX et le RX doivent donc utiliser la même taille.



Exemple TX::



&#x20;   static const uint8_t payload[] = {

&#x20;       0x11,

&#x20;       0x22,

&#x20;       0x33,

&#x20;       0x44

&#x20;   };





Le RX doit alors être configuré avec::



&#x20;   \#define PAYLOAD_SIZE 4U





Cette approche minimise le header de la trame.



Le header contient uniquement::



&#x20;   PREAMBLE = 1 octet

&#x20;   SFD      = 1 octet





L'overhead fixe est donc de deux octets, sans compter le GAP.





Efficacité de la trame

**======================**



Pour un payload de N octets, sans CRC, l'efficacité liée au header est::



&#x20;                  N

&#x20;   efficiency = -------

&#x20;                N + 2





Par exemple, pour 1 octet de payload::



&#x20;                  1

&#x20;   efficiency = -------

&#x20;                  3



&#x20;   efficiency = 33.3 %





Pour 8 octets::



&#x20;                  8

&#x20;   efficiency = -------

&#x20;                 10



&#x20;   efficiency = 80 %





Pour 32 octets::



&#x20;                  32

&#x20;   efficiency = --------

&#x20;                  34



&#x20;   efficiency \~= 94.1 %





L'impact du préambule et du SFD devient donc faible lorsque la taille du

payload augmente.





Configuration Device Tree TX

**============================**



Sur le nRF52833 DK, la sortie DATA est configurée dans l'overlay.



Exemple::



&#x20;   \#include \<zephyr/dt-bindings/gpio/gpio.h>



&#x20;   / {

&#x20;       chosen {

&#x20;           zephyr,console = &uart0;

&#x20;       };



&#x20;       zephyr,user {

&#x20;           data-gpios = <&gpio0 4 GPIO_ACTIVE_HIGH>;

&#x20;       };

&#x20;   };





&#x20;   &uart0 {

&#x20;       compatible = "nordic,nrf-uarte";

&#x20;       status = "okay";

&#x20;       current-speed = <115200>;

&#x20;       pinctrl-0 = <&uart0_default>;

&#x20;       pinctrl-1 = <&uart0_sleep>;

&#x20;       pinctrl-names = "default", "sleep";

&#x20;   };





&#x20;   &timer1 {

&#x20;       status = "okay";

&#x20;   };





Le GPIO utilisé pour DATA est donc::



&#x20;   GPIO0 pin 4





Le timer utilisé est::



&#x20;   TIMER1





Dans le programme C, ces ressources sont récupérées avec::



&#x20;   \#define DATA_NODE DT_PATH(zephyr_user)



&#x20;   static const struct gpio_dt_spec data =

&#x20;       GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);





et::



&#x20;   \#define TIMER_NODE DT_NODELABEL(timer1)



&#x20;   static const struct device \*timer =

&#x20;       DEVICE_DT_GET(TIMER_NODE);





Validation expérimentale

**========================**



La première validation est réalisée avec une connexion électrique directe

entre le TX et le RX.



La connexion est::



&#x20;   TX GPIO ---------------- RX GPIO



&#x20;   TX GND ----------------- RX GND





Cette étape permet de valider indépendamment de la chaîne optique::



&#x20;   \- la construction de la trame

&#x20;   \- le codage Manchester

&#x20;   \- le timing TX

&#x20;   \- l'échantillonnage RX

&#x20;   \- la synchronisation

&#x20;   \- le décodage Manchester

&#x20;   \- la détection du préambule

&#x20;   \- la détection du SFD

&#x20;   \- la récupération du payload





Les mesures réalisées ont montré des intervalles principalement égaux à::



&#x20;   40 us

&#x20;   80 us





ce qui correspond au timing Manchester attendu.





Architecture finale validée

**============================**



La chaîne numérique actuellement validée est::



&#x20;   TX

&#x20;   \===



&#x20;   Payload buffer

&#x20;        |

&#x20;        v

&#x20;   PREAMBLE + SFD + PAYLOAD

&#x20;        |

&#x20;        v

&#x20;   Manchester encoder

&#x20;        |

&#x20;        v

&#x20;   Timer hardware

&#x20;        |

&#x20;        \| 40 us

&#x20;        v

&#x20;   GPIO TX





&#x20;   RX

&#x20;   \===



&#x20;   GPIO RX

&#x20;        |

&#x20;        v

&#x20;   Timer hardware

&#x20;        |

&#x20;        \| 10 us

&#x20;        v

&#x20;   4x oversampling

&#x20;        |

&#x20;        v

&#x20;   Ring buffer

&#x20;        |

&#x20;        v

&#x20;   GAP detection

&#x20;        |

&#x20;        v

&#x20;   Manchester decoder

&#x20;        |

&#x20;        v

&#x20;   0x55 detection

&#x20;        |

&#x20;        v

&#x20;   0xD3 detection

&#x20;        |

&#x20;        v

&#x20;   Payload reconstruction





Résultat actuel

**===============**



La communication numérique directe TX/RX est fonctionnelle.



Le TX génère correctement une trame Manchester avec un demi-bit de 40 us.



Le RX suréchantillonne le signal toutes les 10 us et utilise quatre

échantillons par demi-bit.



Le récepteur est capable de::



&#x20;   \- détecter le début d'une trame

&#x20;   \- se synchroniser sur le signal

&#x20;   \- décoder le Manchester

&#x20;   \- vérifier le préambule 0x55

&#x20;   \- vérifier le SFD 0xD3

&#x20;   \- reconstruire le payload





Pour un payload TX égal à::



&#x20;   0x1F





le RX récupère::



&#x20;   0x1F





Améliorations futures

**=====================**



La version actuelle constitue une première couche de communication

fonctionnelle.



Les prochaines améliorations possibles sont::



&#x20;   1\. Ajouter un CRC-8 après le payload.



&#x20;   2\. Ajouter éventuellement un champ LENGTH si la taille du payload doit

&#x20;      varier dynamiquement d'une trame à l'autre.



&#x20;   3\. Tester différentes tailles de payload.



&#x20;   4\. Mesurer le taux d'erreur de transmission.



&#x20;   5\. Remplacer la connexion GPIO directe par la chaîne optique.



&#x20;   6\. Tester la réception avec différents niveaux de lumière.



&#x20;   7\. Tester la robustesse face au bruit et à la lumière ambiante.



&#x20;   8\. Optimiser le débit en réduisant éventuellement le GAP ou la durée

&#x20;      du demi-bit.





Une évolution particulièrement importante sera l'ajout d'un CRC.



La trame pourra alors devenir::



&#x20;   +----------+----------+----------------+---------+

&#x20;   \| PREAMBLE | SFD      | PAYLOAD        | CRC-8   |

&#x20;   +----------+----------+----------------+---------+

&#x20;   \| 0x55     | 0xD3     | N octets       | 1 octet |

&#x20;   +----------+----------+----------------+---------+





Le CRC permettra au récepteur de vérifier que le payload décodé est

effectivement valide avant de le transmettre à l'application.





Résumé

**======**



Le protocole actuellement développé utilise une trame volontairement

courte afin de maximiser le débit utile.



La structure retenue est::



&#x20;   GAP HIGH | 0x55 | 0xD3 | PAYLOAD





Le codage Manchester utilisé est::



&#x20;   0 -> 01

&#x20;   1 -> 10





Les paramètres temporels sont::



&#x20;   Demi-bit              : 40 us

&#x20;   Bit Manchester        : 80 us

&#x20;   Débit logique         : 12.5 kbit/s

&#x20;   Echantillonnage RX    : 10 us

&#x20;   Oversampling RX       : x4





Le TX utilise un timer hardware périodique afin de garantir précisément

la durée des demi-bits.



Le RX utilise également un timer hardware périodique et un ring buffer

afin de séparer l'acquisition temps réel du traitement du protocole.



Cette architecture fournit une base simple, efficace et robuste pour

l'intégration future de la communication optique.