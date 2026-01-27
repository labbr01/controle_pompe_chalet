# Logique d'anomalie d'air et gestion de la sécurité (ctl_pompe_chalet)

## Description littérale du fonctionnement

- **Détection d’anomalie d’air** :
  - Si le statut Air est "Oui" pendant 1 minute (60 secondes consécutives), on considère qu’il y a une anomalie d’air.
  - Si le statut Air est "Oui*" pendant 3 minutes (180 secondes consécutives), on considère aussi qu’il y a une anomalie d’air.

- **Action lors d’anomalie d’air** :
  - On active la valve de purge (relais purge ON).
  - On garde la valve de purge activée tant que le statut Air n’est pas revenu à "Non" ou "Non*".

- **Sortie d’anomalie d’air** :
  - Si le statut Air redevient "Non" pendant 1 minute OU "Non*" pendant 3 minutes, on lève la condition d’anomalie d’air (purge OFF).

- **Sécurité prolongée** :
  - Si l’anomalie d’air dure plus de 5 minutes, on coupe le courant de la pompe (relais pompe OFF), on désactive la purge, et on affiche "Redémarrage manuel nécessaire" sur le LCD (utilisation des 4 lignes si besoin). On arrête toute lecture des capteurs.

- **Redémarrage manuel** :
  - Un bouton sur une pin libre de l’Arduino permet d’effectuer le redémarrage manuel.
  - Tant que le bouton n’est pas pressé, la pompe reste coupée et il n’y a pas d’acquisition capteur.
  - Quand le bouton est pressé :
    - On relance la pompe (relais pompe ON).
    - On réinitialise tous les buffers (air, débit, courant).
    - On reprend le fonctionnement normal (comme après un reset Arduino).

- **Lisibilité et modularité** :
  - Toute la logique de détection d’anomalie d’air et de gestion des temporisations/statuts est regroupée dans une fonction dédiée, bien documentée, pour garder le code principal clair et maintenable.

---

Ce fonctionnement assure une gestion robuste des situations d’air dans le circuit, une sécurité automatique, et une reprise simple par l’utilisateur.
