# Gestion thermique et limitation d'usage de la pompe

## Objectif

Assurer la protection de la pompe contre la surchauffe et l'usure prématurée en limitant son temps de fonctionnement, tout en affichant clairement l'état sur le LCD.

## Règles de fonctionnement

1. **Limite horaire glissante**
   - La pompe ne doit pas fonctionner plus de 30 minutes sur toute période glissante d'une heure.
   - Un buffer d'une heure (60 cases, 1 case = 1 minute) mémorise l'état ON/OFF de la pompe chaque minute.
   - Si le cumul ON dépasse 30 minutes sur la dernière heure, la pompe est forcée à l'arrêt jusqu'à ce que le cumul repasse sous 30 minutes.

2. **Limite de fonctionnement consécutif**
   - La pompe ne doit jamais fonctionner plus de 20 minutes consécutives.
   - Après 20 minutes ON sans interruption, une pause obligatoire de 10 minutes est imposée.
   - Si la pompe a tourné 19 min, s'arrête 2 min, puis repart pour 12 min, on force une pause de 10 min (pas de "triche" par micro-pauses).

3. **Affichage LCD**
   - Ligne Pompage :
     - Les 2 derniers caractères affichent le nombre de minutes ON sur la dernière heure.
     - Si la pompe est en pause forcée, afficher "Pompage:Non*" et les 2 derniers caractères indiquent le temps restant de la pause (en minutes).

4. **Détection réelle de fonctionnement**
   - Utiliser la mesure de courant/tension pour déterminer si la pompe est effectivement ON (et non seulement commandée ON).

5. **Principe général**
   - Toute la logique de limitation d'usage et de protection thermique doit être regroupée dans une fonction dédiée, bien documentée, pour garder le code principal clair et maintenable.

## Exemple de scénario

- Pompe ON 18 min → arrêt 2 min → ON 12 min :
  - Après 20 min consécutives ON, pause forcée de 10 min, même si la pompe a "triché" avec une courte pause.
- Pompe ON 30 min (cumul sur 1h) :
  - Pause forcée jusqu'à ce que le cumul repasse sous 30 min sur la dernière heure.

---

Ce principe garantit la sécurité de la pompe, la lisibilité de l'état pour l'utilisateur, et la robustesse du système face aux cycles répétés.
