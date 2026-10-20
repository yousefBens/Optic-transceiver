# Émetteur OOK — Nucleo L476RG

Cette version utilise TIM2 + DMA circulaire vers **PA9**. Le MSI est calibré par
le quartz LSE, puis la PLL fournit 80 MHz. Chaque bit OOK dure **3 µs** :
`1 = HIGH`, `0 = LOW`. Il n'y a plus de paire Manchester.

Le payload, le préambule et les paramètres sont directement dans `src/main.c`.
Ils doivent correspondre à ceux du récepteur.

Voir le [README OOK commun](../README.md) pour le principe, le câblage,
la compilation et les limites du protocole. Le DMA réserve **DMA1 canal 2** et
le timer **TIM2**. Le programme affiche le démarrage, puis transmet en continu avec un compteur
de séquence protégé. Deux buffers DMA alternent pour préparer la trame suivante.
