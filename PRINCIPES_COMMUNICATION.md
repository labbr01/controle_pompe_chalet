# Principes de Communication RF – Contrôle Pompe Chalet

## Objectifs
- Assurer une communication fiable entre deux Arduino (maître et client) via modules nRF24L01.
- Minimiser la consommation d'énergie et la saturation du bus série.
- Permettre l'extension future du protocole (ajout de capteurs, commandes, etc.).


## Structure des Messages

- Format général :

  `SEQ|IDX_POMPE|IDX_AIR|IDX_POMPAGE|IDX_VALVE|DEBIT|CHK`

  - `SEQ` : Numéro de séquence (4 chiffres, incrémenté à chaque changement d'état)
  - `IDX_POMPE` : État pompe (4=On, 5=Off, 5=Pause thermique)
  - `IDX_AIR` : Présence d'air (0=Oui, 1=Non)
  - `IDX_POMPAGE` : Pompage actif (0=Oui, 1=Non)
  - `IDX_VALVE` : Position de la vanne (6=Chalet, 7=Purge)
  - `DEBIT` : Débit détecté (1=Oui, 0=Non)
  - `CHK` : **Checksum** (voir ci-dessous)

- Exemple :

  `0023|4|1|0|6|1|A7`

## Checksum (somme de contrôle)

Le checksum est un octet hexadécimal (1 ou 2 caractères) ajouté à la fin du message pour garantir l'intégrité des données transmises.

**Principe :**
- On calcule la somme de tous les caractères du message (hors checksum), puis on prend le résultat modulo 256.
- On convertit ce résultat en hexadécimal (ex : 0xA7 → "A7").
- Le checksum est ajouté après le dernier champ, séparé par un `|`.

**Exemple de calcul :**
1. Message sans checksum : `0023|4|1|0|6|1`
2. Somme des codes ASCII de chaque caractère : `0+0+2+3+|+4+|+1+|+0+|+6+|+1` (additionner les valeurs ASCII)
3. Résultat modulo 256, puis conversion hexadécimale : `A7`
4. Message final : `0023|4|1|0|6|1|A7`

**Utilité :**
- Permet au récepteur de vérifier que le message n’a pas été altéré pendant la transmission.
- Si le checksum ne correspond pas, le message est ignoré ou une demande de retransmission est envoyée.

**Implémentation :**
- Le calcul du checksum est simple et rapide, adapté aux microcontrôleurs.
- Peut être étendu à d’autres algorithmes si besoin (CRC, etc.).

## Logique d'Émission
- Un message n'est émis que si l'état change (hors numéro de séquence).
- Un ping périodique peut être envoyé pour signaler la présence (watchdog).
- Le numéro de séquence s’incrémente uniquement sur changement d’état.
- Le checksum permet de détecter les erreurs de transmission.

## Logique de Réception
- Le récepteur vérifie le checksum et l’unicité du numéro de séquence.
- Un accusé de réception (ACK) peut être envoyé en retour.
- Les messages dupliqués (même SEQ) sont ignorés.

## Extensibilité
- De nouveaux champs peuvent être ajoutés en fin de message.
- Les versions du protocole peuvent être gérées par un champ optionnel.

## Consommation et Robustesse
- La communication est optimisée pour minimiser la consommation sur batterie/solaire.
- Les messages sont courts et peu fréquents (seulement sur changement d’état ou ping).
- Le système tolère les pertes de messages grâce à la redondance et aux ACKs.

## Exemple de Séquence
1. Changement d’état détecté → émission d’un message avec nouveau SEQ.
2. Récepteur valide le message, envoie un ACK.
3. Si pas d’ACK, l’émetteur peut réessayer (nombre limité de tentatives).
4. Si pas de changement, émission d’un ping périodique (SEQ inchangé).

---

*Document à compléter au fil des évolutions du protocole et des besoins matériels.*
