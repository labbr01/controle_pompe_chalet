# Contrôle Pompe Chalet

Automatisation de la gestion d’une pompe à eau de chalet avec Arduino R4 Minima.

## Objectif
- Supervision intelligente de la pompe, électrovannes, et capteurs (pression, débit, courant)
- Interface LCD + clavier
- Sécurité et évolutivité (radio à venir)

## Matériel principal
- Arduino UNO R4 Minima (x2)
- LCD 16x2, clavier 4x4, capteurs pression/débit/courant, relais, MOSFET, NRF24L01+

## Structure recommandée
- `src/` : code Arduino (.ino)
- `hardware/` : schémas, photos, docs matériel
- `docs/` : documentation technique
- `notes.md` : journal de développement

## Premiers pas
1. Vérifier la détection de l’Arduino R4 Minima sur le poste
2. Uploader un sketch de test (Blink)
3. Versionner chaque étape sur GitHub

Voir `notes.md` pour le suivi détaillé.


git remote add origin https://github.com/labbr01/controle_pompe_chalet.git
git branch -M main
git push -u origin main