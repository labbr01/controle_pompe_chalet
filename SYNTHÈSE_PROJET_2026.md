peyx t# Synthèse projet Contrôleur Pompe Chalet

## État actuel (15 février 2026)

### Ce qui fonctionne
- **Communication RF** robuste entre le contrôleur (ctl_pompe_chalet_v2) et l’afficheur (ctl_affiche_chalet_v2), protocole compact SSSAPDCMM.
- **Affichage LCD** côté pompe : toutes les valeurs utiles (statut air, pompe, débit, minuterie, etc.)
- **Affichage LCD** côté chalet : réception et décodage du message, affichage des valeurs principales (statut, débit, courant, séquence).
- **Logique de détection d’air** et gestion de l’anomalie d’air (statut visible sur LCD, émission RF).
- **Gestion du courant pompe** (lecture ACS712, affichage, transmission).
- **Protection thermique** (minuterie, pause, redémarrage manuel).
- **Protocole RF** : émission max 5 fois par valeur, séquence incrémentée uniquement si changement, pas de handshake requis.
- **Archivage Git** : toutes les versions validées sont sauvegardées.

### Ce qu’il reste à faire (par mini-bouchées)

1. **Fiabiliser la détection d’air et la séquence de purge**
   - Investiguer : parfois la purge ne s’ouvre pas alors que l’air est détecté.
   - Vérifier la logique d’activation de la valve de purge et la gestion du courant pompe.
   - S’assurer que la détection d’air déclenche toujours la séquence attendue.

2. **Harmoniser l’affichage chalet/pompe**
   - Rendre l’affichage côté chalet aussi lisible et complet que côté pompe.
   - Ajouter/adapter les informations affichées (statut, ampérage, etc.).

3. **Exploiter l’ampérage pour la détection d’état pompe**
   - Utiliser la mesure de courant pour confirmer l’état réel de la pompe (ON/OFF).
   - Afficher l’ampérage côté pompe aussi si pertinent.

4. **Tests matériels avec nouveaux composants**
   - Ajouter les condensateurs un à un, vérifier la stabilité RF et capteurs à chaque étape.
   - Tester la nouvelle sonde de pression (avec et sans condensateur).

5. **Finalisation et robustesse**
   - Refaire des tests de bout en bout (simulation fuite d’air, coupure pompe, etc.).
   - Documenter les cas limites et les comportements attendus.
   - Préparer la version finale pour début avril (objectif : kit prêt pour la fin du gel).

## Règle d’or
- **Procéder par mini-bouchées** : chaque modification doit être testée isolément, sans casser ce qui fonctionne déjà.
- **Archiver chaque étape stable** dans Git avant tout changement matériel ou logiciel.

---

**Prochaines étapes**
- Solidifier le câblage avec les nouveaux composants.
- Tester la stabilité après chaque ajout (condensateur, sonde).
- Reprendre les ajustements logiciels (purge, affichage, ampérage) une fois la base matérielle stabilisée.

---

*Synthèse générée le 15 février 2026 par GitHub Copilot.*
