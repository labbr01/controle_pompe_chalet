// ====================
// ARCHIVE 2026-02-14 :
// Version validée (affichage LCD, console, logique débit, courant, minuteur, encodage condencé)
// Affichage et encodage OK, prêt pour intégration communication RF

// === PARAMÈTRES AJUSTABLES (délais, temps, protections) ===
const int POMPE_MAX_CONSEC_MIN = 6;      // 6 min ON consécutives max (test)
const int POMPE_MAX_HR_MIN = 9;          // 9 min ON max sur 18 min glissantes (test)
const int POMPE_HR_WINDOW_MIN = 18;      // Fenêtre glissante de 18 min (test)
const int POMPE_PAUSE_MIN = 3;           // Pause forcée 3 min (test)
// Pour archivage : remettre 20/60 min

// Délais (en ms) pour la gestion d'anomalie d'air
const unsigned long DELAI_AIR_OUI = 15000;      // 15s (au lieu de 30s)
const unsigned long DELAI_AIR_OUIE = 45000;     // 45s (au lieu de 90s)
const unsigned long DELAI_AIR_NON = 15000;      // 15s (au lieu de 30s)
const unsigned long DELAI_AIR_NONE = 45000;     // 45s (au lieu de 90s)
const unsigned long DELAI_ANOMALIE_MAX = 60000; // 60s (1min)

// Durée NON consécutif pour reset du compteur pompage
const unsigned long DUREE_NON_POMPAGE_RESET_MS = 5000; // 5 secondes

// === CONSTANTES FIXES (labels, encodage, etc.) ===
// Tableau unique pour tous les statuts (pour encodage et affichage)
// Indices : 0=Oui, 1=Non, 2=Oui*, 3=Non*, 4=On, 5=Off, 6=Chalet, 7=Purge
const char* statusText[] = {"Oui", "Non", "Oui*", "Non*", "On", "Off", "Chalet", "Purge"};

// Utilitaire pour retrouver l'indice dans statusText[]
int getStatusIndex(const char* str) {
  for (int i = 0; i < 8; i++) {
    if (strcmp(str, statusText[i]) == 0) return i;
  }
  return -1;
}

// --- Déclaration des broches relais et bouton ---
const int PIN_RELAIS_POMPE = 7;   // D7
const int PIN_RELAIS_PURGE = 8;   // D8
const int PIN_BOUTON_RESET = 9;   // D9 (à adapter selon dispo)

// --- Variables d'état pour l'anomalie d'air ---
enum EtatAnomalieAir { NORMAL, ANOMALIE, SECURITE, ATTENTE_REDEMARRAGE };
EtatAnomalieAir etatAnomalieAir = NORMAL;
unsigned long tDebutAnomalie = 0;
unsigned long tDebutRetourNormal = 0;
unsigned long tDebutSecurite = 0;
unsigned long tDernierEtatAir = 0;
char dernierStatutAir[6] = "";

// --- Prototypes ---
void gestionAnomalieAir(const char* statutAir);
void resetBuffers();
void calculer_stats();
void maj_affichage();
void ajuster_relais_pompe();
void ajuster_relais_purge();
void communiquer_chalet();
/*
 * ============================================================================
 * Contrôleur Pompe Chalet - Structure modulaire, acquisition rapide, affichage capteurs
 * ============================================================================
 *
 * Cette version lit les capteurs (sonde IR, débitmètre, courant) toutes les 100ms,
 * calcule la moyenne/min/max sur 1s, puis affiche les valeurs sur le LCD 20x4.
 * Les fonctions de relais et communication sont présentes mais inactives.
 *
 *
 * Brochage Arduino (sorties) :
 * - Sonde IR (présence air/eau) : D2
 * - Débitmètre : D6
 * - Capteur de courant (ACS712) : A1
 * - Relais Pompe : D7 (prévu)
 * - Relais Purge : D8 (prévu)
 *
 * LCD I2C 20x4 :
 * - SDA → A4 (I2C)
 * - SCL → A5 (I2C)
 * - VCC → 5V
 * - GND → GND
 *
 * ============================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 20, 4);

// Broches capteurs (adapter si besoin)
const int PIN_SONDE_IR = 2;   // D2
const int PIN_DEBIT = 6;      // D6
const int PIN_COURANT = A1;   // A1

// Buffers pour 1 seconde (10 échantillons)
const int NBUF = 10;
int bufDebit[NBUF];
float bufCourant[NBUF];
int bufSondeIR[NBUF];
int idxBuf = 0;

// --- Affichage simple/détaillé du débit ---
bool afficherDebitSimple = true; // Mettre à false pour l'ancien affichage


// Buffer circulaire pour le statut Air (10s d'historique)
const int NBUF_AIR = 100;
int bufAir[NBUF_AIR];
int idxBufAir = 0;
unsigned long lastPompeOn = 0;
bool pompeRedemarrage = false;
bool etatAirPrecedentOui = false;

// Moyennes/min/max sur 1s
float courantMoy = 0, courantMin = 0, courantMax = 0;
float debitMoy = 0, debitMin = 0, debitMax = 0;
float sondeIRMoy = 0;

// Timing
unsigned long lastSample = 0;
unsigned long lastAffichage = 0;
const unsigned long INTERVAL_SAMPLE = 100;   // 100ms
const unsigned long INTERVAL_AFFICHAGE = 1000; // 1s

void setup() {
    pinMode(PIN_RELAIS_POMPE, OUTPUT);
    pinMode(PIN_RELAIS_PURGE, OUTPUT);
    pinMode(PIN_BOUTON_RESET, INPUT_PULLUP);
    digitalWrite(PIN_RELAIS_POMPE, LOW); // Pompe ON au démarrage
    digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF au démarrage
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  delay(100);

  pinMode(PIN_SONDE_IR, INPUT);
  pinMode(PIN_DEBIT, INPUT);
  // PIN_COURANT = A1 (analogique)

  lcd.setCursor(0, 0);
  lcd.print("CTL CHALET (RF)");
}

// --- Variables pour la protection thermique ---
unsigned int minutesPompageConsecutives = 0;
unsigned long lastPompageStateChange = 0;
bool etatPompagePrecedent = false;
bool pauseThermiqueActive = false;
unsigned long debutPauseThermique = 0;
unsigned int minutesPauseRestantes = 0;

void loop() {
  unsigned long now = millis();

  // Si en attente de redémarrage manuel, on ne fait rien sauf surveiller le bouton
  if (etatAnomalieAir == ATTENTE_REDEMARRAGE) {
    if (digitalRead(PIN_BOUTON_RESET) == LOW) { // Bouton appuyé (INPUT_PULLUP)
      // Relance la pompe, reset buffers, retour mode normal
      digitalWrite(PIN_RELAIS_POMPE, LOW); // Pompe ON
      etatAnomalieAir = NORMAL;
      resetBuffers();
      lcd.clear();
      lcd.setCursor(0,0); lcd.print("Redemarrage...");
      delay(1000);
    }
    return;
  }

  // Acquisition rapide (tous les 100ms)
  if (now - lastSample >= INTERVAL_SAMPLE) {
    lastSample = now;
    traiter_capteur_debimetre();
    traiter_capteur_courant();
    traiter_sonde_ir();
    idxBuf = (idxBuf + 1) % NBUF;
  }

  // Traitement et affichage toutes les secondes
  if (now - lastAffichage >= INTERVAL_AFFICHAGE) {
    lastAffichage = now;
    // Si la pompe est coupée (SECURITE ou ATTENTE_REDEMARRAGE), on bloque tout affichage autre que le message d'erreur
    if (etatAnomalieAir == SECURITE || etatAnomalieAir == ATTENTE_REDEMARRAGE) {
      lcd.clear();
      lcd.setCursor(0,0); lcd.print("ANOMALIE AIR");
      lcd.setCursor(0,1); lcd.print("Pompe COUPEE");
      lcd.setCursor(0,2); lcd.print("Redemarrage");
      lcd.setCursor(0,3); lcd.print("manuel requis");
      return;
    }
    calculer_stats();
    // Gestion de l'anomalie d'air (statut filtré sur 10s)
    const char* airStr = evaluerStatutAir(true); // PompeEnMarche = true (à adapter)
    gestionAnomalieAir(airStr);

    // --- Mise à jour du compteur de minutes consécutives de pompage ---
    int hasZero = 0, hasOne = 0;
    for (int i = 0; i < NBUF; i++) {
      if (bufDebit[i] == 0) hasZero = 1;
      if (bufDebit[i] == 1) hasOne = 1;
    }
    bool etatPompage = (hasZero && hasOne);
    static unsigned long debutPompage = 0;
    static unsigned long lastMinuteTick = 0;
    static bool sortieDePause = false;
    if (pauseThermiqueActive) {
      // Gestion de la pause thermique
      unsigned long tempsEcoule = (now - debutPauseThermique) / 1000UL;
      unsigned int minutesEcoulees = tempsEcoule / 60;
      minutesPauseRestantes = (POMPE_PAUSE_MIN > minutesEcoulees) ? (POMPE_PAUSE_MIN - minutesEcoulees) : 0;
      if (tempsEcoule >= (unsigned long)POMPE_PAUSE_MIN * 60UL) {
        // Fin de pause, on réactive la pompe
        pauseThermiqueActive = false;
        minutesPompageConsecutives = 0;
        minutesPauseRestantes = 0;
        debutPompage = 0;
        lastMinuteTick = 0;
        sortieDePause = true; // Indique qu'on sort de pause
        // Pompe ON (relais)
        digitalWrite(PIN_RELAIS_POMPE, LOW);
      } else {
        // Pompe OFF (relais)
        digitalWrite(PIN_RELAIS_POMPE, HIGH);
      }
    } else {
      // Gestion du compteur de minutes consécutives
      if (sortieDePause) {
        // On vient de sortir de pause, on attend que la pompe tourne vraiment avant de compter
        if (etatPompage) {
          debutPompage = now;
          minutesPompageConsecutives = 0;
          lastMinuteTick = 0;
          sortieDePause = false;
        }
      } else {
        if (etatPompage != etatPompagePrecedent) {
          lastPompageStateChange = now;
          if (!etatPompage) {
            minutesPompageConsecutives = 0;
            debutPompage = 0;
            lastMinuteTick = 0;
          }
        }
        if (etatPompage) {
          if (debutPompage == 0) {
            debutPompage = now;
            minutesPompageConsecutives = 0;
          }
          unsigned long elapsed = now - debutPompage;
          unsigned int newMinutes = elapsed / 60000UL;
          if (newMinutes != minutesPompageConsecutives) {
            minutesPompageConsecutives = newMinutes;
          }
        } else {
          debutPompage = 0;
          lastMinuteTick = 0;
          minutesPompageConsecutives = 0;
        }
        // Déclenche la pause thermique si limite atteinte
        if (minutesPompageConsecutives >= POMPE_MAX_CONSEC_MIN) {
          pauseThermiqueActive = true;
          debutPauseThermique = now;
          minutesPauseRestantes = POMPE_PAUSE_MIN;
          // Pompe OFF (relais)
          digitalWrite(PIN_RELAIS_POMPE, HIGH);
        }
      }
    }
    etatPompagePrecedent = etatPompage;

    maj_affichage();
    ajuster_relais_pompe();
    ajuster_relais_purge();
    communiquer_chalet();
  }
}

// --- Fonction centrale de gestion de l'anomalie d'air ---
void gestionAnomalieAir(const char* statutAir) {
  unsigned long now = millis();

  // Suivi du statut Air courant
  if (strcmp(statutAir, dernierStatutAir) != 0) {
    strcpy(dernierStatutAir, statutAir);
    tDernierEtatAir = now;
    Serial.print("[ANOMALIE AIR] Changement statut Air: ");
    Serial.println(statutAir);
  }

  // Affiche chaque seconde l'état et le temps passé dans ce statut
  static unsigned long lastLog = 0;
  if (now - lastLog >= 1000) {
    lastLog = now;
    Serial.print("[ANOMALIE AIR] Statut Air: ");
    Serial.print(statutAir);
    Serial.print(" | Etat global: ");
    switch(etatAnomalieAir) {
      case NORMAL: Serial.print("NORMAL"); break;
      case ANOMALIE: Serial.print("ANOMALIE"); break;
      case SECURITE: Serial.print("SECURITE"); break;
      case ATTENTE_REDEMARRAGE: Serial.print("ATTENTE_REDEMARRAGE"); break;
    }
    Serial.print(" | Temps dans ce statut: ");
    Serial.print((now - tDernierEtatAir)/1000);
    Serial.print("s");
    if (etatAnomalieAir == ANOMALIE) {
      Serial.print(" | Duree anomalie: ");
      Serial.print((now - tDebutAnomalie)/1000);
      Serial.print("s");
    }
    Serial.println();
  }

  // Gestion des transitions d'état
  int idxStatutAir = getStatusIndex(statutAir);
  switch (etatAnomalieAir) {
    case NORMAL:
      if ((idxStatutAir == 0 && now - tDernierEtatAir >= DELAI_AIR_OUI) ||
          (idxStatutAir == 2 && now - tDernierEtatAir >= DELAI_AIR_OUIE)) {
        etatAnomalieAir = ANOMALIE;
        tDebutAnomalie = now;
        digitalWrite(PIN_RELAIS_PURGE, HIGH); // Purge ON
      }
      // Désactive la purge dès qu'on revient à Non (eau franche)
      if (idxStatutAir == 1) {
        digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF
      }
      break;
    case ANOMALIE:
      // Si retour à Non ou Non* assez longtemps, on lève l'anomalie
      if ((idxStatutAir == 1 && now - tDernierEtatAir >= DELAI_AIR_NON) ||
          (idxStatutAir == 3 && now - tDernierEtatAir >= DELAI_AIR_NONE)) {
        etatAnomalieAir = NORMAL;
        digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF
        tDebutAnomalie = 0;
        // Purge les compteurs/délais d'anomalie pour éviter une rechute immédiate
        tDernierEtatAir = now;
        strcpy(dernierStatutAir, statutAir);
      }
      // Désactive la purge dès qu'on revient à Non (eau franche)
      if (idxStatutAir == 1) {
        digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF
      }
      // Si anomalie > durée max, passe en sécurité
      else if (now - tDebutAnomalie >= DELAI_ANOMALIE_MAX) {
        etatAnomalieAir = SECURITE;
        tDebutSecurite = now;
        digitalWrite(PIN_RELAIS_POMPE, HIGH); // Pompe OFF
        digitalWrite(PIN_RELAIS_PURGE, LOW);  // Purge OFF
      }
      break;
    case SECURITE:
      // Passe en attente de redémarrage manuel
      if (now - tDebutSecurite >= 0) {
        etatAnomalieAir = ATTENTE_REDEMARRAGE;
      }
      break;
    case ATTENTE_REDEMARRAGE:
      // Géré dans loop()
      break;
  }
}

// --- Fonction pour reset tous les buffers (après redémarrage manuel) ---
void resetBuffers() {
  for (int i = 0; i < NBUF; i++) {
    bufDebit[i] = 0;
    bufCourant[i] = 0;
    bufSondeIR[i] = 0;
  }
  for (int i = 0; i < NBUF_AIR; i++) bufAir[i] = 0;
  idxBuf = 0;
  idxBufAir = 0;
  lastSample = millis();
  lastAffichage = millis();
// ...fin de resetBuffers...
}

void traiter_capteur_debimetre() {
  int val = digitalRead(PIN_DEBIT);
  bufDebit[idxBuf] = val;
}

void traiter_capteur_courant() {
  int raw = analogRead(PIN_COURANT);
  float tension = raw * 5.0 / 1023.0;
  float offset = 2.5; // V (pour ACS712)
  float sensibilite = 0.185; // V/A (pour ACS712-5A)
  float courant = (tension - offset) / sensibilite;
  bufCourant[idxBuf] = courant;
}

void traiter_sonde_ir() {
  int etat = digitalRead(PIN_SONDE_IR);
  bufSondeIR[idxBuf] = etat;
  // Met à jour le buffer Air (0 = Non, 1 = Oui)
  bufAir[idxBufAir] = (etat < 1) ? 1 : 0; // IR bas = Air
  idxBufAir = (idxBufAir + 1) % NBUF_AIR;
}

// Fonction d'évaluation du statut Air sur 10s
const char* evaluerStatutAir(bool pompeEnMarche) {
  // Si redémarrage pompe, forcer Non pendant 10s sauf si état précédent était Oui
  static unsigned long pompeRestartTime = 0;
  static bool forceNon = false;
  if (pompeRedemarrage) {
    pompeRestartTime = millis();
    forceNon = !etatAirPrecedentOui;
    pompeRedemarrage = false;
  }
  if (forceNon && (millis() - pompeRestartTime < 10000)) {
    return statusText[1]; // retourne Non
  } else {
    forceNon = false;
  }

  // Compte le nombre de détections Air dans le buffer
  int airCount = 0;
  for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];
  if (airCount == 0) return statusText[1]; // retourne Non
  if (airCount >= 1 && airCount <= 6) return statusText[3]; // retourne Non*
  if (airCount >= 7 && airCount <= 9) return statusText[2]; // retourne Oui*
  if (airCount == 10) return statusText[0]; // retourne Oui
  // Pour 10s, airCount varie de 0 à 100, donc adapte la logique
  if (airCount <= 60) return statusText[3]; // retourne Non*
  if (airCount <= 90) return statusText[2]; // retourne Oui*
  return statusText[0]; // retourne Oui
// ...fin de evaluerStatutAir...
}

// Option d'affichage temporaire du airCount sur LCD
bool afficherAirCountLCD = true;

// Fonction utilitaire pour obtenir le airCount
int getAirCount() {
  int airCount = 0;
  for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];
  return airCount;
}

void calculer_stats() {
  // Débit
  int sumD = 0, minD = 1, maxD = 0;
  for (int i = 0; i < NBUF; i++) {
    sumD += bufDebit[i];
    if (bufDebit[i] < minD) minD = bufDebit[i];
    if (bufDebit[i] > maxD) maxD = bufDebit[i];
  }
  debitMoy = sumD / (float)NBUF;
  debitMin = minD;
  debitMax = maxD;

  // Courant
  float sumC = 0, minC = bufCourant[0], maxC = bufCourant[0];
  for (int i = 0; i < NBUF; i++) {
    sumC += bufCourant[i];
    if (bufCourant[i] < minC) minC = bufCourant[i];
    if (bufCourant[i] > maxC) maxC = bufCourant[i];
  }
  courantMoy = sumC / NBUF;
  courantMin = minC;
  courantMax = maxC;

  // Sonde IR
  int sumIR = 0;
  for (int i = 0; i < NBUF; i++) sumIR += bufSondeIR[i];
  sondeIRMoy = sumIR / (float)NBUF;
}

// Ajout d'une définition vide pour maj_affichage()
void maj_affichage() {
  // ...affichage LCD restauré strictement identique à l'original...
    unsigned long now = millis();
    int nbZero = 0, nbOne = 0;
    for (int i = 0; i < NBUF; i++) {
      if (bufDebit[i] == 0) nbZero++;
      if (bufDebit[i] == 1) nbOne++;
    }
    int airCount = 0;
    for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];
    char airCountStr[3] = "  ";
    if (afficherAirCountLCD) {
      snprintf(airCountStr, 3, "%02d", airCount > 99 ? 99 : airCount);
    }
    int idxPompe = pauseThermiqueActive ? 5 : 4;
    const char* pompeStr = statusText[idxPompe];
    int airIdx = 1;
    if (airCount == 0) airIdx = 1;
    else if (airCount <= 6) airIdx = 3;
    else if (airCount <= 9) airIdx = 2;
    else airIdx = 0;
    const char* airStr = statusText[airIdx];
    char ligne0[21];
    snprintf(ligne0, 19, "Pompe:%s Air:%s", pompeStr, airStr);
    int len0 = strlen(ligne0);
    for (int i = len0; i < 18; i++) ligne0[i] = ' ';
    ligne0[18] = airCountStr[0];
    ligne0[19] = airCountStr[1];
    ligne0[20] = '\0';
    lcd.setCursor(0, 0); lcd.print(ligne0);

    // Ligne 1 : Pompage: Oui/Non + compteur à droite
    static bool pompageActif = false;
    static unsigned long pompageNonStart = 0;
    static bool pompageTousUn = false;
    static unsigned long pompageTousUnStart = 0;
    static const int NBUF_MINUTAGE = 63;
    static int bufMinutage[NBUF_MINUTAGE] = {0};
    static int idxBufMinutage = 0;
    static int sommeMinutage = 0;
    char ligne1[21];
    char compteurFinal[6] = "     ";
    char ligneBase[16] = "";
    bool pompageOui = false;
    if (pauseThermiqueActive) {
      snprintf(compteurFinal, 6, "%02u/%02u", minutesPauseRestantes, POMPE_PAUSE_MIN);
      snprintf(ligneBase, 16, "Pompage:Non*");
      while (strlen(ligneBase) < 15) strcat(ligneBase, " ");
      snprintf(ligne1, 21, "%s%s", ligneBase, compteurFinal);
      pompageOui = false;
    } else if (nbZero && nbOne) {
      pompageTousUn = false;
      pompageTousUnStart = 0;
      if (!pompageActif) {
        pompageActif = true;
        pompageNonStart = 0;
      }
      snprintf(compteurFinal, 6, "%02u/%02u", minutesPompageConsecutives, POMPE_MAX_CONSEC_MIN);
      snprintf(ligneBase, 16, "Pompage:Oui");
      while (strlen(ligneBase) < 15) strcat(ligneBase, " ");
      snprintf(ligne1, 21, "%s%s", ligneBase, compteurFinal);
      pompageOui = true;
    } else if (nbOne == NBUF) {
      if (!pompageTousUn) {
        pompageTousUn = true;
        pompageTousUnStart = now;
      }
      if (now - pompageTousUnStart < DUREE_NON_POMPAGE_RESET_MS) {
        if (!pompageActif) {
          pompageActif = true;
          pompageNonStart = 0;
        }
        snprintf(compteurFinal, 6, "%02u/%02u", minutesPompageConsecutives, POMPE_MAX_CONSEC_MIN);
        snprintf(ligneBase, 16, "Pompage:Oui");
        while (strlen(ligneBase) < 15) strcat(ligneBase, " ");
        snprintf(ligne1, 21, "%s%s", ligneBase, compteurFinal);
        pompageOui = true;
      } else {
        if (pompageActif) {
          pompageActif = false;
          minutesPompageConsecutives = 0;
        }
        snprintf(ligne1, 21, "Pompage:Non");
        pompageOui = false;
      }
    } else {
      pompageTousUn = false;
      pompageTousUnStart = 0;
      if (pompageActif) {
        if (pompageNonStart == 0) pompageNonStart = now;
        if (now - pompageNonStart > DUREE_NON_POMPAGE_RESET_MS) {
          pompageActif = false;
          minutesPompageConsecutives = 0;
        }
      }
      snprintf(ligne1, 21, "Pompage:Non");
      pompageOui = false;
    }
    static bool attente5Non = false;
    static bool compteurLCDaZero = true;
    if (pompageOui) {
      sommeMinutage -= bufMinutage[idxBufMinutage];
      bufMinutage[idxBufMinutage] = 1;
      sommeMinutage += 1;
      idxBufMinutage = (idxBufMinutage + 1) % NBUF_MINUTAGE;
      if (idxBufMinutage == 0) {
        if (sommeMinutage >= 59) {
          minutesPompageConsecutives++;
        }
        for (int i = 0; i < NBUF_MINUTAGE; i++) bufMinutage[i] = 0;
        sommeMinutage = 0;
      }
      attente5Non = false;
      compteurLCDaZero = false;
    } else {
      int nbNon = 0;
      for (int i = 0; i < 5; i++) {
        int idx = (idxBufMinutage + i) % NBUF_MINUTAGE;
        if (bufMinutage[idx] == 0) nbNon++;
      }
      if (nbNon == 5) {
        for (int i = 0; i < NBUF_MINUTAGE; i++) bufMinutage[i] = 0;
        sommeMinutage = 0;
        idxBufMinutage = 0;
        minutesPompageConsecutives = 0;
        attente5Non = false;
        compteurLCDaZero = true;
      } else {
        attente5Non = true;
      }
      if (attente5Non == false) {
        minutesPompageConsecutives = 0;
        compteurLCDaZero = true;
      }
    }
    if (compteurLCDaZero) {
      snprintf(compteurFinal, 6, "00/%02u", POMPE_MAX_CONSEC_MIN);
      if (pauseThermiqueActive) snprintf(ligneBase, 16, "Pompage:Non*");
      else snprintf(ligneBase, 16, "Pompage:Non");
      while (strlen(ligneBase) < 15) strcat(ligneBase, " ");
      snprintf(ligne1, 21, "%s%s", ligneBase, compteurFinal);
    }
    ligne1[20] = '\0';
    lcd.setCursor(0, 1); lcd.print(ligne1);

    const char* valve = statusText[6];
    char ligne2[21];
    snprintf(ligne2, 21, "Valve:%s%13s", valve, "");
    ligne2[20] = '\0';
    lcd.setCursor(0, 2); lcd.print(ligne2);

    if (afficherDebitSimple) {
        char ligne3[21];
        int debitAffiche = (nbZero == 0) ? 0 : (int)(debitMoy * 10 + 0.5);
        snprintf(ligne3, 21, "Debit: %2d/10%13s", debitAffiche, "");
        ligne3[20] = '\0';
        lcd.setCursor(0, 3); lcd.print(ligne3);
    } else {
        char bufStr[11];
        for (int i = 0; i < NBUF; i++) bufStr[i] = bufDebit[(idxBuf + i) % NBUF] ? '1' : '0';
        bufStr[NBUF] = '\0';
        char ligne3[21];
        int debitAffiche = (nbZero == 0) ? 0 : (int)(debitMoy * 10 + 0.5);
        snprintf(ligne3, 21, "Debit:%s %2d/10%5s", bufStr, debitAffiche, "");
        ligne3[20] = '\0';
        lcd.setCursor(0, 3); lcd.print(ligne3);
    }
}

// Variables pour l'encodage et la détection de changement
char lastEncodedMsg[32] = "";
unsigned int msgSeq = 0; // Séquentiel 0-999
unsigned long lastMsgSent = 0;
unsigned int repeatCount = 0; // Nombre de répétitions du message courant (max 5)

// ...le reste du code continue sans accolade fermante ici...

void ajuster_relais_pompe() {
  // À implémenter : logique de contrôle de la pompe
}

void ajuster_relais_purge() {
  // À implémenter : logique de contrôle de la purge
}

void communiquer_chalet() {
  // Encodage du message compact positionnel :
  // Format : SSSAPDMM\n
  // SSS = séquentiel (3 chiffres, 000 à 999)
  // A = statut Air (index statusText[])
  // P = statut Pompe (ON/OFF, index statusText[])
  // D = débit (0-9)
  // MM = minutes pompage consécutives (2 chiffres)

  const char* airStr = evaluerStatutAir(true);
  int idxAir = getStatusIndex(airStr);
  int pompeEtat = digitalRead(PIN_RELAIS_POMPE) == LOW ? 4 : 5;
  int debit = (int)(debitMoy * 10 + 0.5);
  if (debit > 9) debit = 9;
  unsigned int minPompe = minutesPompageConsecutives;

  char msg[12];
  snprintf(msg, sizeof(msg), "%03u%d%d%d%02u", msgSeq, idxAir, pompeEtat, debit, minPompe);

  // Compare uniquement les positions 3 et plus (APDMM)
  char msgData[9];
  snprintf(msgData, sizeof(msgData), "%d%d%d%02u", idxAir, pompeEtat, debit, minPompe);

  unsigned long now = millis();
  // Compare msgData avec lastEncodedMsg[3..]
  bool msgChanged = strncmp(msgData, lastEncodedMsg + 3, 8) != 0;
  if (msgChanged) {
    msgSeq = (msgSeq + 1) % 1000;
    strncpy(lastEncodedMsg, msg, sizeof(lastEncodedMsg));
    repeatCount = 1;
    lastMsgSent = now;
    Serial.print("MSG:");
    Serial.println(msg);
  } else if (repeatCount > 0 && repeatCount < 5 && (now - lastMsgSent >= 1000)) {
    repeatCount++;
    lastMsgSent = now;
    Serial.print("MSG:");
    Serial.println(lastEncodedMsg);
  }
}