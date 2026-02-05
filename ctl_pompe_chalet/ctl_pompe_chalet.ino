// === Dépendances et déclaration LCD ===
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <string.h>


LiquidCrystal_I2C lcd(0x27, 20, 4);

// --- Gestion de l'astérisque de communication RF ---
unsigned long lastAsterisk = 0;
const unsigned long ASTERISK_DURATION = 90000UL; // 1 min 30 s
bool showAsterisk = false;

void onMessageRecu() {
  lastAsterisk = millis();
  showAsterisk = true;
  lcd.setCursor(19, 3);
  lcd.print("*");
  Serial.println("[RF] Réception confirmée !");
}
// === Gabarits de lignes LCD ===
const char* LABEL_POMPE = "Pompe:";
const char* LABEL_AIR = "Air:";
const char* LABEL_POMPAGE = "Pompage:";
const char* LABEL_VALVE = "Valve:";
const char* LABEL_DEBIT = "Debit:";
// === Textes variables pour affichage et protocole compact ===
const char* statusText[] = {"Oui", "Non", "Oui*", "Non*", "On", "Off", "Chalet", "Purge"}; // 0=Oui, 1=Non, 2=Oui*, 3=Non*, 4=On, 5=Off, 6=Chalet, 7=Purge
// ====================
// PARAMÈTRES DE TEST (protection thermique accélérée)
// Pour test rapide : 6 min ON max consécutives, 9 min ON max sur 18 min glissantes, pause forcée 3 min
// Remettre les valeurs originales (20/60 min) pour l'archivage !
const int POMPE_MAX_CONSEC_MIN = 6;      // 6 min ON consécutives max (test)
const int POMPE_MAX_HR_MIN = 9;          // 9 min ON max sur 18 min glissantes (test)
const int POMPE_HR_WINDOW_MIN = 18;      // Fenêtre glissante de 18 min (test)
const int POMPE_PAUSE_MIN = 3;           // Pause forcée 3 min (test)
// ====================
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

// Option d'affichage temporaire du airCount sur LCD
bool afficherAirCountLCD = true;
// --- Affichage simple/détaillé du débit ---
bool afficherDebitSimple = true; // Mettre à false pour l'ancien affichage

// --- Prototypes ---
void gestionAnomalieAir(int idxAir);
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







// NRF24L01
#define CE_PIN 4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const byte adresse[6] = "00001";
const byte adresse_reponse[6] = "00002";
unsigned long tPurgeDebug = 0;
bool purgeDebugActive = false;

// Broches capteurs (adapter si besoin)
const int PIN_SONDE_IR = 2;   // D2
const int PIN_DEBIT = 6;      // D6
const int PIN_COURANT = A1;   // A1

// Buffers pour 1 seconde (100 échantillons à 10ms)
const int NBUF = 100;
int bufDebit[NBUF];
volatile unsigned int debitImpulsions = 0;

// Interrupt pour le débitmètre (pulse sur D6)

// Interrupt pour le débitmètre (pulse sur D6)
void debitInterrupt() {
  debitImpulsions++;
}
float bufCourant[NBUF];
int bufSondeIR[NBUF];
int idxBuf = 0;

// Index et compteurs pour protocole RF (doivent être globaux)
int idxPompe = 0;
int idxAir = 0;
int idxPompage = 0;
int idxValve = 0;
int debitActif = 0;

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
const unsigned long INTERVAL_SAMPLE = 10;   // 10ms
const unsigned long INTERVAL_AFFICHAGE = 1000; // 1s

void setup() {
      // Initialisation explicite des buffers à zéro pour éviter tout résidu après reset
      for (int i = 0; i < NBUF; i++) {
        bufDebit[i] = 0;
        bufCourant[i] = 0;
        bufSondeIR[i] = 0;
      }
      for (int i = 0; i < NBUF_AIR; i++) bufAir[i] = 0;
      idxBuf = 0;
      idxBufAir = 0;
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
  attachInterrupt(digitalPinToInterrupt(PIN_DEBIT), debitInterrupt, RISING);
  // PIN_COURANT = A1 (analogique)

  lcd.setCursor(0, 0);
  lcd.print("CTL CHALET (RF)");

  // Init NRF24L01
  radio.begin();
  if (!radio.begin()) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("NRF24L01 FAIL");
    while (1);
  }
  radio.openWritingPipe(adresse);
  radio.openReadingPipe(1, adresse_reponse);
  radio.setPALevel(RF24_PA_LOW);
  radio.stopListening();
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
    int idxAir = evaluerStatutAir(true); // PompeEnMarche = true (à adapter)
    gestionAnomalieAir(idxAir);

    // --- LOGIQUE DE POMPAGE EXACTEMENT COMME L'ANCIEN CODE ---
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
      unsigned long tempsEcoule = (now - debutPauseThermique) / 1000UL;
      unsigned int minutesEcoulees = tempsEcoule / 60;
      minutesPauseRestantes = (POMPE_PAUSE_MIN > minutesEcoulees) ? (POMPE_PAUSE_MIN - minutesEcoulees) : 0;
      if (tempsEcoule >= (unsigned long)POMPE_PAUSE_MIN * 60UL) {
        pauseThermiqueActive = false;
        minutesPompageConsecutives = 0;
        minutesPauseRestantes = 0;
        debutPompage = 0;
        lastMinuteTick = 0;
        sortieDePause = true;
        digitalWrite(PIN_RELAIS_POMPE, LOW);
      } else {
        digitalWrite(PIN_RELAIS_POMPE, HIGH);
      }
    } else {
      if (sortieDePause) {
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
        if (minutesPompageConsecutives >= POMPE_MAX_CONSEC_MIN) {
          pauseThermiqueActive = true;
          debutPauseThermique = now;
          minutesPauseRestantes = POMPE_PAUSE_MIN;
          digitalWrite(PIN_RELAIS_POMPE, HIGH);
        }
      }
    }
    etatPompagePrecedent = etatPompage;

    // --- Affichage LCD à partir des index et construction du message compact ---
    // Détermination des index pour chaque statut
    int idxPompe = pauseThermiqueActive ? 5 : 4; // 4=On, 5=Off
    // int idxAir = 0; // Oui
    // const int idxAir = evaluerStatutAir(!pauseThermiqueActive);

    // Correction logique Pompage
    int idxPompage = 1; // Non par défaut
    if (pauseThermiqueActive) {
      idxPompage = 3; // Non*
    } else if (etatPompage) {
      idxPompage = 0; // Oui
    } else {
      idxPompage = 1; // Non
    }
    int idxValve = 6; // Chalet
    // Débit actif uniquement si alternance 0/1 dans le buffer
    unsigned int debitActif = 0;
    if (hasZero && hasOne) {
      for (int i = 0; i < NBUF; i++) {
        if (bufDebit[i] == 1) debitActif++;
      }
    } else {
      debitActif = 0;
    }
    // Affichage airCount dans les 2 dernières positions de la ligne 0
    char airCountStr[3] = "  ";
    if (afficherAirCountLCD) {
      int airCount = getAirCount();
      snprintf(airCountStr, 3, "%02d", airCount > 99 ? 99 : airCount);
    }
    char ligne0[21];
    snprintf(ligne0, 19, "%s%s %s%s", LABEL_POMPE, statusText[idxPompe], LABEL_AIR, statusText[idxAir]);
    int len0 = strlen(ligne0);
    for (int i = len0; i < 18; i++) ligne0[i] = ' ';
    ligne0[18] = airCountStr[0];
    ligne0[19] = airCountStr[1];
    ligne0[20] = '\0';
    char ligne1[21], ligne2[21], ligne3[21];
    // Pompage: Oui/Non + minutes consécutives OU pause thermique (aligné à droite)
    char compteurFinal[6] = "     ";
    char ligneBase[16] = "";
    if (pauseThermiqueActive) {
      snprintf(compteurFinal, 6, "%02u/%02u", minutesPauseRestantes, POMPE_PAUSE_MIN);
      snprintf(ligneBase, 16, "%s%s", LABEL_POMPAGE, statusText[3]); // Non*
      while (strlen(ligneBase) < 15) strcat(ligneBase, " ");
      snprintf(ligne1, 21, "%s%s", ligneBase, compteurFinal);
    } else if (hasZero && hasOne) {
      snprintf(compteurFinal, 6, "%02u/%02u", minutesPompageConsecutives, POMPE_MAX_CONSEC_MIN);
      snprintf(ligneBase, 16, "%s%s", LABEL_POMPAGE, statusText[0]); // Oui
      while (strlen(ligneBase) < 15) strcat(ligneBase, " ");
      snprintf(ligne1, 21, "%s%s", ligneBase, compteurFinal);
    } else {
      snprintf(ligne1, 21, "%s%s", LABEL_POMPAGE, statusText[1]); // Non
    }
    ligne1[20] = '\0';
    snprintf(ligne2, 21, "%s%s", LABEL_VALVE, statusText[idxValve]);
    // Débit aligné à droite (5 dernières positions, format 09/10)
    if (afficherDebitSimple) {
      snprintf(ligne3, 21, "%s %2d/%d%10s", LABEL_DEBIT, debitActif, NBUF, "");
      ligne3[20] = '\0';
    } else {
      char bufStr[11];
      for (int i = 0; i < NBUF; i++) bufStr[i] = bufDebit[(idxBuf + i) % NBUF] ? '1' : '0';
      bufStr[NBUF] = '\0';
      snprintf(ligne3, 21, "%s:%s %2d/%d%2s", LABEL_DEBIT, bufStr, debitActif, NBUF, "");
      ligne3[20] = '\0';
    }
    lcd.setCursor(0, 0); lcd.print(ligne0);
    lcd.setCursor(0, 1); lcd.print(ligne1);
    lcd.setCursor(0, 2); lcd.print(ligne2);
    lcd.setCursor(0, 3); lcd.print(ligne3);
    // Affichage/effacement de l'astérisque
    if (showAsterisk && (now - lastAsterisk > ASTERISK_DURATION)) {
      lcd.setCursor(19, 3);
      lcd.print(" ");
      showAsterisk = false;
    }

    // Construction du message compact à transmettre (ex: 4 chiffres pour chaque index, puis compteur)
    char msg[32];
    snprintf(msg, sizeof(msg), "%d%d%d%d%02d", idxPompe, idxAir, idxPompage, idxValve, debitActif);

    // Log console: 4 lignes LCD + message compact
    Serial.println(ligne0);
    Serial.println(ligne1);
    Serial.println(ligne2);
    Serial.println(ligne3);
    Serial.print("MSG: "); Serial.println(msg);

    ajuster_relais_pompe();
    ajuster_relais_purge();

    // Communication RF et debug purge
    char cmdClient[16] = "";
    envoyer_affichage_client(ligne0, ligne1, ligne2, ligne3, cmdClient);
    if (strcmp(cmdClient, "CMD1") == 0) {
      Serial.println("[DEBUG] CMD1 reçu du client : activation purge 10s");
      digitalWrite(PIN_RELAIS_PURGE, HIGH);
      tPurgeDebug = millis();
      purgeDebugActive = true;
    }
    if (purgeDebugActive && (millis() - tPurgeDebug > 10000)) {
      digitalWrite(PIN_RELAIS_PURGE, LOW);
      purgeDebugActive = false;
      Serial.println("[DEBUG] Fin purge debug (10s)");
    }
  }
}

// --- Fonction centrale de gestion de l'anomalie d'air ---
void gestionAnomalieAir(int idxAir) {
  unsigned long now = millis();

  if (idxAir != dernierStatutAir[0]) {
    dernierStatutAir[0] = idxAir;
    tDernierEtatAir = now;
    Serial.print("[ANOMALIE AIR] Changement statut Air: ");
    Serial.println(statusText[idxAir]);
  }

  static unsigned long lastLog = 0;
  if (now - lastLog >= 1000) {
    lastLog = now;
    Serial.print("[ANOMALIE AIR] Statut Air: ");
    Serial.print(statusText[idxAir]);
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

  switch (etatAnomalieAir) {
    case NORMAL:
      if ((idxAir == 0 && now - tDernierEtatAir >= DELAI_AIR_OUI) ||
          (idxAir == 2 && now - tDernierEtatAir >= DELAI_AIR_OUIE)) {
        etatAnomalieAir = ANOMALIE;
        tDebutAnomalie = now;
        digitalWrite(PIN_RELAIS_PURGE, HIGH); // Purge ON
      }
      if (idxAir == 1) {
        digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF
      }
      break;
    case ANOMALIE:
      if ((idxAir == 1 && now - tDernierEtatAir >= DELAI_AIR_NON) ||
          (idxAir == 3 && now - tDernierEtatAir >= DELAI_AIR_NONE)) {
        etatAnomalieAir = NORMAL;
        digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF
        tDebutAnomalie = 0;
        tDernierEtatAir = now;
        dernierStatutAir[0] = idxAir;
      }
      if (idxAir == 1) {
        digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF
      }
      else if (now - tDebutAnomalie >= DELAI_ANOMALIE_MAX) {
        etatAnomalieAir = SECURITE;
        tDebutSecurite = now;
        digitalWrite(PIN_RELAIS_POMPE, HIGH); // Pompe OFF
        digitalWrite(PIN_RELAIS_PURGE, LOW);  // Purge OFF
      }
      break;
    case SECURITE:
      if (now - tDebutSecurite >= 0) {
        etatAnomalieAir = ATTENTE_REDEMARRAGE;
      }
      break;
    case ATTENTE_REDEMARRAGE:
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
  // Retourne un index: 0=Oui, 1=Non, 2=Oui*, 3=Non*
int evaluerStatutAir(bool pompeEnMarche) {
  static unsigned long pompeRestartTime = 0;
  static bool forceNon = false;
  if (pompeRedemarrage) {
    pompeRestartTime = millis();
    forceNon = !etatAirPrecedentOui;
    pompeRedemarrage = false;
  }
  if (forceNon && (millis() - pompeRestartTime < 10000)) {
    return 1; // Non
  } else {
    forceNon = false;
  }
  int airCount = 0;
  for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];
  if (airCount == 0) return 1;         // Non
  if (airCount >= 1 && airCount <= 6) return 3;   // Non*
  if (airCount >= 7 && airCount <= 9) return 2;   // Oui*
  if (airCount == 10) return 0;        // Oui
  if (airCount <= 60) return 3;        // Non*
  if (airCount <= 90) return 2;        // Oui*
  return 0;                            // Oui
}

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



void ajuster_relais_pompe() {
  // À implémenter : logique de contrôle de la pompe
}

void ajuster_relais_purge() {
  // À implémenter : logique de contrôle de la purge
}


// Envoie les 4 lignes au client, handshake, récupère la commande
void envoyer_affichage_client(const char* l0, const char* l1, const char* l2, const char* l3, char* cmdClient) {
  static int seq = 1;
  char msg[32];
  // Message compact: seq|idxPompe|idxAir|idxPompage|idxValve|debitActif
  snprintf(msg, sizeof(msg), "%d|%d|%d|%d|%d|%02d", seq, idxPompe, idxAir, idxPompage, idxValve, debitActif);
  Serial.print("[RF SEND] ");
  Serial.println(msg);
  bool handshake_ok = false;
  char bufRecu[32] = "";
  int tentatives = 0;
  while (tentatives < 10) {
    radio.stopListening();
    delay(2);
    radio.write(&msg, sizeof(msg));
    radio.startListening();
    unsigned long startWait = millis();
    bool recu = false;
    while (millis() - startWait < 500 && !recu) {
      if (radio.available()) {
        radio.read(&bufRecu, sizeof(bufRecu));
        recu = true;
      }
    }
    radio.stopListening();
    if (recu) {
      onMessageRecu(); // Affiche l'astérisque à chaque réception RF
      int rseq = 0;
      char rcmd[16] = "";
      char *token = strtok(bufRecu, "|");
      if (token) rseq = atoi(token);
      token = strtok(NULL, "|");
      if (token) strncpy(rcmd, token, 15);
      rcmd[15] = '\0';
      // Gestion des commandes reçues
      if (rseq == 0) {
        Serial.println("[RF] RESET demandé par le client");
        // Reset buffers et état
        resetBuffers();
        etatAnomalieAir = NORMAL;
        digitalWrite(PIN_RELAIS_POMPE, LOW);
        digitalWrite(PIN_RELAIS_PURGE, LOW);
        seq = 1; // Toujours repartir à 1
        return;
      } else if (rseq == seq) {
        // Confirmation normale
        strncpy(cmdClient, rcmd, 15); cmdClient[15] = '\0';
        handshake_ok = true;
        // Gestion des commandes
        if (strcmp(rcmd, "CMD1") == 0) {
          Serial.println("[DEBUG] CMD1 reçu du client : activation purge 10s");
          digitalWrite(PIN_RELAIS_PURGE, HIGH);
          tPurgeDebug = millis();
          purgeDebugActive = true;
        } else if (strcmp(rcmd, "CMD2") == 0) {
          Serial.println("[DEBUG] CMD2 reçu du client : commande spéciale 2");
          // Ajouter logique CMD2 ici
        }
        break;
      } else if (rseq == -1) {
        snprintf(bufRecu, sizeof(bufRecu), "-1|SYNC");
        radio.stopListening(); delay(2); radio.write(&bufRecu, sizeof(bufRecu));
        seq = 0;
        return;
      }
    }
    tentatives++;
    delay(100);
  }
  seq++;
  if (seq > 9999) seq = 1;
}
