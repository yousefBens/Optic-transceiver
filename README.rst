# Projet de Contrôle GPIO & SPI via UART en Python

## Description

Ce projet a pour objectif de **contrôler et configurer les interfaces GPIO et SPI** à l’aide d’un **script Python** communiquant via **UART (Universal Asynchronous Receiver Transmitter)**.  
Il permet d’activer, de configurer et d’utiliser les broches GPIO et les périphériques SPI pour réaliser diverses tâches spécifiques, comme le pilotage de capteurs, d’actionneurs ou de modules externes.

---

## Fonctionnalités principales

- **Communication UART** : Échange de données entre Python et un microcontrôleur ou un périphérique distant.  
- **Contrôle des GPIO** :  
  - Configuration des broches en **entrée** ou **sortie**  
  - Lecture et écriture d’états logiques  
  - Activation de fonctions spéciales selon les besoins de l’application  
- **Communication SPI** :  
  - Configuration des paramètres SPI (fréquence, mode, polarité, phase)  
  - Envoi et réception de données avec des périphériques SPI  
- **Interface Python simple et extensible** : Permet de définir des commandes UART pour interagir dynamiquement avec les interfaces matérielles.  

---

## Prérequis

- **Python 3.8+**
- Bibliothèques Python :
  - `pyserial` (pour la communication UART)
  - `spidev` (si exécution sur une plateforme compatible SPI comme Raspberry Pi)
  - `RPi.GPIO` ou équivalent selon le matériel utilisé
- Un périphérique matériel disposant de :
  - UART actif
  - GPIO et SPI disponibles
