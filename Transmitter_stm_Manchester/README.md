# Émetteur Manchester — Nucleo L476RG

Cette application remplace le TX Nordic pour une liaison entre deux Nucleo
L476RG. Les dossiers `T_test` et `Reciever` ne sont pas modifiés.

## Signal transmis

- Sortie : **PA9**, comme dans l'ancien émetteur STM32.
- Codage : `0 → LOW/HIGH`, `1 → HIGH/LOW`, bits de poids fort en premier.
- Demi-bit : **3 µs** ; débit logique nominal : **166 667 bit/s**.
- Trame répétée : GAP HIGH de 4 demi-bits, `55 D3`, puis `FF 16 C1` répété 4 fois.
- Payload : **12 octets**, correspondant au RX actuel.
- Longueur : **228 demi-bits**, soit **684 µs** par trame.

Pour tester, remplacer la connexion du TX Nordic par **PA9 de la Nucleo TX →
PA9 de la Nucleo RX**, avec les masses communes. Le RX conserve son fonctionnement :
réception pendant 60 secondes, puis statistiques UART.

## Horloge référencée au quartz

La Nucleo possède un quartz basse fréquence **LSE à 32,768 kHz**. L'overlay active
l'auto-calibration du MSI par cette référence, puis la PLL :

```text
Quartz LSE → calibration MSI 4 MHz → PLL ×40 /2 → système et TIM2 à 80 MHz
```

Cette solution n'utilise pas le signal HSE/MCO du ST-Link. Ce n'est pas un quartz
haute fréquence directement connecté au système : le quartz LSE corrige
l'oscillateur MSI interne. Le cristal LSE doit être présent et fonctionnel ; il
est également activé dans la configuration standard de cette carte.

L'horloge est configurée par Zephyr avant `main()`. Le programme vérifie que LSE,
MSI et son mode d'auto-calibration sont actifs avant de lancer l'émission.

## Génération matérielle : TIM2 + DMA circulaire

Le STM32 n'utilise pas l'EasyDMA Nordic. Son DMA général remplit le même rôle :

```text
TIM2 : événement toutes les 3 µs
                 ↓ demande DMA TIM2_UP
DMA1, canal 2, sélection 4 : lit le prochain mot en RAM
                 ↓
GPIOA BSRR : impose LOW ou HIGH sur PA9
```

Chaque mot de `waveform` est une commande GPIO de 32 bits :

- bit 9 : met PA9 à HIGH ;
- bit 25 : met PA9 à LOW.

Le DMA incrémente l'adresse en RAM, conserve la même destination BSRR et revient
au début du tableau après les 228 mots. Le tableau statique occupe **912 octets**.
Aucune interruption n'est demandée par demi-bit ou par trame. Le processeur peut
terminer `main()` ; TIM2 et le DMA poursuivent la transmission.

TIM2 compte de 0 à ARR inclus : pour 240 ticks, ARR vaut **239**. L'événement de
mise à jour logiciel charge les registres avant l'activation des demandes DMA.
La broche reste GPIO : aucune fonction alternative PWM n'est nécessaire.
USART1 est désactivé pour libérer PA9 ; la console reste sur USART2.

**TIM2 et DMA1 canal 2 sont réservés à cet émetteur.** Les données sont fixes et le
buffer n'est plus modifié après le démarrage du DMA.

## Compilation et utilisation

Depuis la racine du projet, avec l'environnement Zephyr existant :

```bash
CCACHE_DISABLE=1 cmake --build Transmitter/build -j2
```

Le firmware est `Transmitter/build/zephyr/zephyr.elf`. Pour une première
configuration, sélectionner la cible `nucleo_l476rg/stm32l476xx` avec les outils
Zephyr habituels. Sélectionner explicitement la sonde de la **carte TX** au flash.

Un message UART annonce le démarrage, puis l'émission reste continue. Aucun
fichier de mesures ni CSV n'est créé. Cette version a été compilée et son tableau
Manchester vérifié sur l'hôte ; la validation sur deux Nucleo reste à effectuer.

Références ST : [RM0351, mapping DMA et timers](https://www.st.com/resource/en/reference_manual/rm0351-stm32l47xxx-stm32l48xxx-stm32l49xxx-and-stm32l4axxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
et [AN4736, calibration MSI par LSE](https://www.st.com/resource/en/application_note/an4736-how-to-calibrate-stm32l4-series-microcontrollers-internal-rc-oscillator-stmicroelectronics.pdf).
