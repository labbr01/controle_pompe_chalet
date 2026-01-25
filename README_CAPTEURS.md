# Contrôle Pompe Puits/Chalet – Documentation et Historique

## Table des matières
- [Tests individuels](#tests-individuels)
  - [test_base](#test_base)
  - [test_debimetre](#test_debimetre)
  - [test_olad](#test_olad)
  - [test_pression](#test_pression)
  - [test_sonde_ir](#test_sonde_ir)
  - [test_courant](#test_courant)
- [Obsolète](#obsolete)
  - [test_capteurs](#test_capteurs)
  - [ctl_pompe_v1 à v5](#ctl_pompe_v1-à-v5)
- [Architecture future](#architecture-future)
  - [ctl_pompe_puit](#ctl_pompe_puit)
  - [ctl_pompe_chalet](#ctl_pompe_chalet)

---

## Tests individuels

### test_base
Test de base pour valider l’Arduino, l’afficheur et l’environnement. Sert de point de départ pour tout nouveau montage.

### test_debimetre
Test du débitmètre (D6). Affiche les impulsions et le débit mesuré. Permet de calibrer le capteur et de vérifier la détection de flux.

### test_olad
Test de l’écran OLED (SDA=A4, SCL=A5). Obsolète si tu utilises maintenant un LCD I2C.

### test_pression
Test du capteur de pression analogique (A0). Affiche la valeur brute, la tension et min/max. Capteur instable ou incompatible, mais code conservé pour référence/calibration.

### test_sonde_ir
Test de la sonde infra-rouge CQRobot (D2). Affiche “VOIT AIR” ou “VOIT EAU” selon la détection. Très fiable pour détecter la présence d’eau ou d’air dans le tuyau.

### test_courant
Test du capteur de courant (A1, ex: ACS712). Affiche la valeur brute, la tension (U) et le courant estimé (I). Voir en-tête du fichier pour les valeurs typiques selon l’état de la pompe.

---

## Obsolete

### test_capteurs
Obsolète – mélangeait plusieurs capteurs et utilisait l’OLED. À ne plus utiliser.

### ctl_pompe_v1 à v5
Anciennes versions du contrôleur. À archiver pour référence historique. Ajouter la mention `// OBSOLÈTE` en première ligne de chaque fichier.

---

## Architecture future

### ctl_pompe_puit
- Projet principal pour le contrôle local du puits.
- Structure modulaire : une fonction par capteur, noms explicites, commentaires détaillés.
- Affichage LCD 20x4 : présence d’eau (sonde IR), position des valves (purge/eau chalet), débit, courant, etc.
- Prévoir fonctions pour : collecte des données, mise à jour de l’affichage, contrôle des relais (purge/pompe), communication (RF 2400Hz, à venir).
- Les relais : 1 pour la pompe (D7), 1 pour la purge/vanne (D8). Statut normal : relais non activé = vanne “eau chalet” ouverte, purge fermée.
- La pression n’est pas implantée pour l’instant (capteur instable).

### ctl_pompe_chalet
- Projet futur pour la partie “remote” (réception des infos du puits via RF 2400Hz).
- Affichage des infos reçues, alarmes, etc.

---

## Notes
- Toujours documenter les broches utilisées, le type de capteur, et les valeurs de référence dans chaque fichier de test.
- Les fichiers obsolètes doivent être clairement marqués pour éviter toute confusion.
- Cette documentation sert de mémoire technique pour l’évolution du projet.
