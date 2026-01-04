# Journal de développement – Contrôle Pompe Chalet

## Objectif du projet
Automatiser la gestion d’une pompe à eau de chalet avec Arduino : affichage LCD, clavier, capteurs (pression, débit, courant), électrovannes, sécurité, et supervision évolutive (communication radio à terme).

## Matériel utilisé
- Arduino UNO R4 Minima (x2)
- Afficheur LCD 16x2
- Clavier matriciel 4x4
- Capteur de pression analogique (3 fils)
- Capteur de débit (3 fils, impulsions)
- Capteur de courant ACS712
- 2 électrovannes 12V (1 NO, 1 NC)
- Pompe 12V (5-7A, max 12.5A au démarrage)
- Relais automobile 12V 30A (pour la pompe)
- Modules relais 12V (pour électrovannes)
- Modules MOSFET 15A (option pour pompe/électrovannes)
- NRF24L01+ (communication radio, à venir)
- Diodes 1N4007 (roue libre)
- Condensateurs 10-100uF (pour NRF24L01+)
- Breakers 10A 12V
- Breadboard, fils, bornier à vis, etc.

## Plan d’attaque
1. **Créer un dépôt Git propre**
   - Dossier : `controle_pompe_chalet/`
   - Fichiers : `src/main.ino`, `README.md`, `NOTES.md`, `hardware/`, `docs/`
2. **Premiers tests**
   - Lire et afficher la pression, le débit, le courant sur LCD et Serial
   - Tester individuellement chaque capteur
   - Piloter manuellement chaque électrovanne et la pompe
3. **Logique de supervision**
   - Machine à états (remplissage, attente, purge, alarme...)
   - Utilisation de millis() pour toutes les temporisations
   - Sécurité : arrêt pompe si défaut, purge automatique, alarme
4. **Interface utilisateur**
   - Menus simples sur LCD + clavier (test, reset, affichage valeurs)
5. **Versionnage régulier sur GitHub**
   - Commits à chaque étape clé

## Conseils et points techniques
- Utiliser des diodes de roue libre sur chaque relais/MOSFET
- Condensateur sur chaque module NRF24L01+
- Relais automobile pour la pompe (30A), relais module ou MOSFET pour électrovannes
- Section de fil adaptée (12 AWG pour la pompe)
- Alimenter Arduino et relais séparément si possible
- Documenter chaque branchement dans `hardware/`

## Prochaines étapes
- Brancher chaque capteur, noter les broches utilisées
- Faire un sketch de test pour lire et afficher chaque capteur
- Valider le fonctionnement de chaque actionneur
- Documenter les résultats et les plages de valeurs

---

**Historique et décisions**
- Test du clavier, LCD, logique sans blocage avec millis() validés
- Choix relais/mosfet selon charge et robustesse
- Préférence pour machine à états et temporisations non bloquantes
- Préparation d’un schéma de câblage et d’un README détaillé à venir

---

*Copie/colle ce fichier dans ton nouveau projet et complète-le au fil de l’avancement !*
