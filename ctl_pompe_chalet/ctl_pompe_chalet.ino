// --- Délais (en ms) pour la gestion d'anomalie d'air ---
const unsigned long DELAI_AIR_OUI = 15000;      // 15s (au lieu de 30s)
const unsigned long DELAI_AIR_OUIE = 45000;     // 45s (au lieu de 90s)
const unsigned long DELAI_AIR_NON = 15000;      // 15s (au lieu de 30s)
const unsigned long DELAI_AIR_NONE = 45000;     // 45s (au lieu de 90s)
const unsigned long DELAI_ANOMALIE_MAX = 60000; // 60s (1min)
/*
 * ============================================================================
 * LOGIQUE D'ANOMALIE D'AIR ET GESTION DE LA PURGE/SECURITE
 *
 * - Si Air = "Oui" pendant 1 min OU "Oui*" pendant 3 min → anomalie d'air.
 * - En anomalie d'air : purge activée (relais purge ON), tant que Air ≠ "Non"/"Non*".
 * - Si Air = "Non" 1 min OU "Non*" 3 min → on lève l'anomalie (purge OFF).
 * - Si anomalie > 5 min : pompe coupée (relais pompe OFF), purge OFF, LCD = "Redémarrage manuel nécessaire" (4 lignes), plus d'acquisition capteurs.
 * - Un bouton sur une pin libre permet le redémarrage manuel :
 *     - Pompe ON, buffers remis à zéro, fonctionnement normal.
 * Toute la logique d'anomalie est dans une fonction dédiée pour garder le code lisible.
 * ============================================================================
 */

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
  switch (etatAnomalieAir) {
    case NORMAL:
      if ((strcmp(statutAir, "Oui") == 0 && now - tDernierEtatAir >= DELAI_AIR_OUI) ||
          (strcmp(statutAir, "Oui*") == 0 && now - tDernierEtatAir >= DELAI_AIR_OUIE)) {
        etatAnomalieAir = ANOMALIE;
        tDebutAnomalie = now;
        digitalWrite(PIN_RELAIS_PURGE, HIGH); // Purge ON
      }
      // Désactive la purge dès qu'on revient à Non (eau franche)
      if (strcmp(statutAir, "Non") == 0) {
        digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF
      }
      break;
    case ANOMALIE:
      // Si retour à Non ou Non* assez longtemps, on lève l'anomalie
      if ((strcmp(statutAir, "Non") == 0 && now - tDernierEtatAir >= DELAI_AIR_NON) ||
          (strcmp(statutAir, "Non*") == 0 && now - tDernierEtatAir >= DELAI_AIR_NONE)) {
        etatAnomalieAir = NORMAL;
        digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF
        tDebutAnomalie = 0;
        // Purge les compteurs/délais d'anomalie pour éviter une rechute immédiate
        tDernierEtatAir = now;
        strcpy(dernierStatutAir, statutAir);
      }
      // Désactive la purge dès qu'on revient à Non (eau franche)
      if (strcmp(statutAir, "Non") == 0) {
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
    return "Non";
  } else {
    forceNon = false;
  }

  // Compte le nombre de détections Air dans le buffer
  int airCount = 0;
  for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];
  if (airCount == 0) return "Non";
  if (airCount >= 1 && airCount <= 6) return "Non*";
  if (airCount >= 7 && airCount <= 9) return "Oui*";
  if (airCount == 10) return "Oui";
  // Pour 10s, airCount varie de 0 à 100, donc adapte la logique
  if (airCount <= 60) return "Non*";
  if (airCount <= 90) return "Oui*";
  return "Oui";
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

void maj_affichage() {


  // Pompe: On/Off (toujours On pour l’instant)
  const char* pompeStr = "On";
  bool pompeEnMarche = true; // À ajuster plus tard selon la logique de relais
  // Air: statut filtré sur 10s
  const char* airStr = evaluerStatutAir(pompeEnMarche);

  // Ligne 0 : Pompe:On Air:Oui/Non + airCount (optionnel, positions 18-19)
  char airCountStr[3] = "  ";
  if (afficherAirCountLCD) {
    int airCount = getAirCount();
    snprintf(airCountStr, 3, "%02d", airCount > 99 ? 99 : airCount);
  }
  char ligne0[21];
  // Compose la ligne sans le chiffre, puis ajoute le chiffre aux deux dernières positions
  snprintf(ligne0, 19, "Pompe:%s Air:%s", pompeStr, airStr); // 18 caractères max
  // Complète avec des espaces si besoin
  int len = strlen(ligne0);
  for (int i = len; i < 18; i++) ligne0[i] = ' ';
  // Ajoute le chiffre aux deux dernières positions
  ligne0[18] = airCountStr[0];
  ligne0[19] = airCountStr[1];
  ligne0[20] = '\0';
  lcd.setCursor(0, 0); lcd.print(ligne0);


  // Pompage: Oui si le buffer contient au moins un 1 ET au moins un 0 (alternance)
  int sommeDebit = 0;
  int hasZero = 0, hasOne = 0;
  for (int i = 0; i < NBUF; i++) {
    if (bufDebit[i] == 0) hasZero = 1;
    if (bufDebit[i] == 1) hasOne = 1;
    sommeDebit += bufDebit[i];
  }
  char ligne1[21];
  snprintf(ligne1, 21, "Pompage:%s%12s", (hasZero && hasOne) ? "Oui" : "Non", "");
  ligne1[20] = '\0';
  lcd.setCursor(0, 1); lcd.print(ligne1);

  // Valve: Chalet/Purge (fixe Chalet)
  const char* valve = "Chalet";
  char ligne2[21];
  snprintf(ligne2, 21, "Valve:%s%13s", valve, "");
  ligne2[20] = '\0';
  lcd.setCursor(0, 2); lcd.print(ligne2);

  // Débit: séquence binaire et total
  char bufStr[11];
  for (int i = 0; i < NBUF; i++) bufStr[i] = bufDebit[(idxBuf + i) % NBUF] ? '1' : '0';
  bufStr[NBUF] = '\0';
  char ligne3[21];
  snprintf(ligne3, 21, "Debit:%s %2d/10%5s", bufStr, sommeDebit, "");
  ligne3[20] = '\0';
  lcd.setCursor(0, 3); lcd.print(ligne3);


  // Trace console (pour debug)
  Serial.print("Pompe: On");
  Serial.print(" | Pompage: ");
  Serial.print((hasZero && hasOne) ? "Oui" : "Non");
  Serial.print(" | Air: ");
  Serial.print((sondeIRMoy < 0.5) ? "Oui" : "Non");
  Serial.print(" | Valve: Chalet");
  Serial.print(" | I: "); Serial.print(courantMoy, 2);
  Serial.print("A | D: "); Serial.print(debitMoy, 2);
  Serial.print(" | IR: "); Serial.print(sondeIRMoy, 2);
  Serial.println();
}

void ajuster_relais_pompe() {
  // À implémenter : logique de contrôle de la pompe
}

void ajuster_relais_purge() {
  // À implémenter : logique de contrôle de la purge
}

void communiquer_chalet() {
  // À implémenter : communication RF ou autre
}
