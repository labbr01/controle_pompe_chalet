# Protocole de communication NRF24L01 – Contrôleur Pompe Chalet

## Vue d'ensemble
Ce protocole permet une communication fiable entre deux Arduino via NRF24L01 : un maître (contrôleur pompe) et un client (récepteur/afficheur). Il est conçu pour être robuste, synchrone, et pour fournir un retour d’état clair sur écran LCD 20x4.

## Matériel
- 2 × Arduino (UNO R4 Minima recommandé)
- 2 × NRF24L01 (CE=D4, CSN=D5)
- 2 × LCD I2C 20x4 (adresse 0x27)
- Fils, alimentation, etc.

## Protocole
### Message du maître → client
Format : `SEQ|L1|L2|L3|L4`
- `SEQ` : numéro de séquence (entier, s’incrémente à chaque envoi)
- `L1` à `L4` : chaînes de 20 caractères max, à afficher sur le LCD du client

### Réponse du client → maître
Format : `SEQ|CMDx` ou `-1|SYNC`
- `SEQ` : numéro de séquence reçu
- `CMDx` : commande détectée sur les entrées D6/D7/D8 (CMD1, CMD2, CMD3, ou NONE)
- `-1|SYNC` : demande de resynchronisation si 10 messages identiques reçus sans changement

### Logique handshake
- Le maître envoie le message, attend la réponse correcte (SEQ identique)
- Si pas de réponse ou mauvaise séquence, il réémet jusqu’à 10 fois
- Si le client demande SYNC, le maître repart à SEQ=0
- Les deux appareils peuvent être allumés dans n’importe quel ordre

## Affichage LCD
### Maître
- Ligne 1 : Statut communication (ENV:OK/ECHEC) + commande reçue (CMDx)
- Lignes 2-4 : Lignes envoyées au client (L1, L2, L3, L4)

### Client
- Ligne 1 : Statut réception + commande envoyée
- Lignes 2-4 : Lignes reçues du maître (L1, L2, L3, L4)
- Au démarrage, affiche « En attente comm... » tant qu’aucun message reçu

## Pins utilisées
- NRF24L01 : CE=D4, CSN=D5 (SPI)
- LCD I2C : SDA=A4, SCL=A5
- Commandes client : D6 (CMD1), D7 (CMD2), D8 (CMD3)

## Robustesse
- Communication synchrone, gestion des pertes, resynchronisation automatique
- Affichage LCD toujours cohérent
- Aucun effet de bord si un appareil démarre avant l’autre

## Fichiers concernés
- `test_comm_maitre.ino` : code maître (contrôleur)
- `test_comm_client.ino` : code client (afficheur)

## Historique
- Protocole évolutif, testé et validé en conditions réelles
- Dernière version : Janvier 2026

---

## Archivage GitHub
Pour archiver l’état actuel du code et de la documentation :

```sh
git add test_comm_maitre.ino test_comm_client.ino COMMUNICATION.md
git commit -m "Doc: protocole communication NRF24L01 maître/client, version stable"
git push
```

