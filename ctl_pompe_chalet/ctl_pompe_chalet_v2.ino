// ====================
// ARCHIVE 2026-02-14 :
// Version validée (affichage LCD, console, logique débit, courant, minuteur, encodage condencé)
// Affichage et encodage OK, prêt pour intégration communication RF


// === CONSTANTES IMMUABLES (hardware, textes, indices) ===
// Broches matérielles
const int PIN_RELAIS_POMPE = 7;   // D7
const int PIN_RELAIS_PURGE = 8;   // D8
const int PIN_BOUTON_RESET = 9;   // D9 (à adapter selon dispo)

// Tableau unique pour tous les statuts (pour encodage et affichage)
// Indices : 0=Oui, 1=Non, 2=Oui*, 3=Non*, 4=On, 5=Off, 6=Chalet, 7=Purge
const char* statusText[] = {"Oui", "Non", "Oui*", "Non*", "On", "Off", "Chalet", "Purge"};

// === CONSTANTES AJUSTABLES (délais, temps, protections) ===
const int POMPE_MAX_CONSEC_MIN = 6;      // 2 min ON consécutives max (test)
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

// --- Variables d'état pour l'anomalie d'air ---
enum EtatAnomalieAir { NORMAL, ANOMALIE, SECURITE, ATTENTE_REDEMARRAGE };
EtatAnomalieAir etatAnomalieAir = NORMAL;
unsigned long tDebutAnomalie = 0;
unsigned long tDebutRetourNormal = 0;
unsigned long tDebutSecurite = 0;
unsigned long tDernierEtatAir = 0;
char dernierStatutAir[6] = "";

// --- Prototypes ---

void gestionAnomalieAir(int statutAir);
void resetBuffers();
void calculer_stats();
void maj_affichage(int statutAirCourant);
void ajuster_relais_pompe();
void ajuster_relais_purge();
void communiquer_chalet();
int evaluerStatutAir(bool pompeEnMarche);
void traiter_capteur_debimetre();
void traiter_capteur_courant();
void traiter_sonde_ir();
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

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// Déclaration de l'objet radio RF24 (filage identique à test_comm_maitre : CE = 4, CSN = 5)
RF24 radio(4, 5);

LiquidCrystal_I2C lcd(0x27, 20, 4);

// Broches capteurs (adapter si besoin)
const int PIN_SONDE_IR = 2;   // D2
const int PIN_DEBIT = 6;      // D6
const int PIN_COURANT = A1;   // A1
const int PIN_PRESSION = A0;  // A0 (capteur de pression)

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
int pressionActuelle = 0;

// Timing
unsigned long lastSample = 0;
unsigned long lastAffichage = 0;
const unsigned long INTERVAL_SAMPLE = 100;   // 100ms
const unsigned long INTERVAL_AFFICHAGE = 1000; // 1s

volatile unsigned int pulseCount = 0;
int debitImpulsions = 0; // Nombre d'impulsions par seconde
int lastDebitState = 0;

void setup() {
  lastDebitState = digitalRead(PIN_DEBIT);
  pinMode(PIN_RELAIS_POMPE, OUTPUT);
  pinMode(PIN_RELAIS_PURGE, OUTPUT);
  pinMode(PIN_BOUTON_RESET, INPUT_PULLUP);
  digitalWrite(PIN_RELAIS_POMPE, LOW); // Pompe ON au démarrage
  digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF au démarrage
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  delay(100);

  // Initialisation du module radio RF24
  if (!radio.begin()) {
    Serial.println("[RF24] Erreur d'initialisation du module radio!");
  } else {
    radio.setPALevel(RF24_PA_LOW); // Puissance basse pour tests
    radio.setDataRate(RF24_1MBPS); // Débit standard
    // Adresse du pipe d'émission identique à test_comm_maitre
    const byte adresse[6] = "00001";
    radio.openWritingPipe(adresse);
    radio.stopListening();
    Serial.println("[RF24] Module radio initialisé");
  }

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

// Début du cycle de pompage continu (pour affichage mm'ss)
unsigned long debutPompage = 0;

void loop() {
  unsigned long now = millis();

  // --- Gestion inconditionnelle du bouton RESET (3s d'appui) ---
    static unsigned long boutonResetStart = 0;
    static int lastCountdown = -1;
    static bool etaitEnReset = false;
    bool boutonAppuye = (digitalRead(PIN_BOUTON_RESET) == LOW);
    if (boutonAppuye) {
      if (boutonResetStart == 0) boutonResetStart = millis();
      unsigned long elapsed = millis() - boutonResetStart;
      int countdown = 3 - (int)(elapsed / 1000);
      if (countdown < 0) countdown = 0;
      // Affichage du compte à rebours uniquement si changement
      if (countdown != lastCountdown) {
    lcd.setCursor(0,0);
    char ligne[21];
    snprintf(ligne, sizeof(ligne), "RESET dans %d", countdown+1);
    lcd.print("                    ");
    lcd.setCursor(0,0);
    lcd.print(ligne);
    lastCountdown = countdown;
      }
      etaitEnReset = true;
      if (elapsed >= 3000) {
        NVIC_SystemReset();
      }
      return;
    } else {
      if (etaitEnReset) {
    // Efface la ligne 0 et force le réaffichage normal
    lcd.setCursor(0,0);
    lcd.print("                    ");
    // On force le rafraîchissement complet à la prochaine maj_affichage
    lastAffichage = 0;
    etaitEnReset = false;
      }
      boutonResetStart = 0;
      lastCountdown = -1;
    }

  // Si en attente de redémarrage manuel, on ne fait rien sauf surveiller le bouton
  if (etatAnomalieAir == ATTENTE_REDEMARRAGE) {
    static unsigned long boutonResetStart2 = 0;
    static int lastCountdown2 = -1;
    bool boutonAppuye2 = (digitalRead(PIN_BOUTON_RESET) == LOW);
    if (boutonAppuye2) {
      if (boutonResetStart2 == 0) boutonResetStart2 = millis();
      unsigned long elapsed = millis() - boutonResetStart2;
      int countdown = 3 - (int)(elapsed / 1000);
      if (countdown < 0) countdown = 0;
      // Affichage du compte à rebours uniquement si changement
      if (countdown != lastCountdown2) {
        lcd.setCursor(0,1);
        char ligne[21];
        snprintf(ligne, sizeof(ligne), "RESET: %d   ", countdown);
        lcd.print("                    ");
        lcd.setCursor(0,1);
        lcd.print(ligne);
        lastCountdown2 = countdown;
      }
      if (elapsed >= 3000) {
        // Relance la pompe, reset buffers, retour mode normal
        digitalWrite(PIN_RELAIS_POMPE, LOW); // Pompe ON
        etatAnomalieAir = NORMAL;
        resetBuffers();
        lcd.clear();
        lcd.setCursor(0,0); lcd.print("Redemarrage...");
        delay(1000);
        boutonResetStart2 = 0;
        lastCountdown2 = -1;
      }
    } else {
      boutonResetStart2 = 0;
      lastCountdown2 = -1;
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
    int currentDebitState = digitalRead(PIN_DEBIT);
    if (lastDebitState == 0 && currentDebitState == 1) {
      pulseCount++;
    }
    lastDebitState = currentDebitState;
  }

  // Traitement et affichage toutes les secondes
  if (now - lastAffichage >= INTERVAL_AFFICHAGE) {
    lastAffichage = now;
    // Si la pompe est coupée (SECURITE ou ATTENTE_REDEMARRAGE), on bloque tout affichage autre que le message d'erreur
    if (etatAnomalieAir == SECURITE || etatAnomalieAir == ATTENTE_REDEMARRAGE) {
      // Plus de reset logiciel automatique ici. Seul le bouton humain dans loop() fait un reset.
      lcd.clear();
      lcd.setCursor(0,0); lcd.print("ANOMALIE AIR");
      lcd.setCursor(0,1); lcd.print("Pompe COUPEE");
      lcd.setCursor(0,2); lcd.print("Redemarrage");
      lcd.setCursor(0,3); lcd.print("manuel requis");
      return;
    }

    // Si pause thermique active, on n'interprète rien, on affiche juste l'état de pause
    static bool pauseThermiqueLogEntree = false;
    if (pauseThermiqueActive) {
      unsigned long tempsEcoule = (now - debutPauseThermique) / 1000UL;
      unsigned int minutesEcoulees = tempsEcoule / 60;
      minutesPauseRestantes = (POMPE_PAUSE_MIN > minutesEcoulees) ? (POMPE_PAUSE_MIN - minutesEcoulees) : 0;
      if (!pauseThermiqueLogEntree) {
        Serial.print("[THERMIQUE] Début pause thermique à t=");
        Serial.print(now / 1000UL);
        Serial.print("s pour ");
        Serial.print(POMPE_PAUSE_MIN);
        Serial.println(" min");
        pauseThermiqueLogEntree = true;
      }
      if (tempsEcoule >= (unsigned long)POMPE_PAUSE_MIN * 60UL) {
        pauseThermiqueActive = false;
        minutesPompageConsecutives = 0;
        minutesPauseRestantes = 0;
        debutPompage = 0;
        // On relance la pompe
        digitalWrite(PIN_RELAIS_POMPE, LOW);
        Serial.print("[THERMIQUE] Fin de pause thermique à t=");
        Serial.print(now / 1000UL);
        Serial.println("s, pompe relancée");
        pauseThermiqueLogEntree = false;
      } else {
        digitalWrite(PIN_RELAIS_POMPE, HIGH);
      }
      // Affichage uniquement
      maj_affichage(0); // Statut Air = Oui (ou autre valeur neutre)
      return;
    } else {
      pauseThermiqueLogEntree = false;
    }

    // --- Lecture du capteur de pression (A0) ---
    pressionActuelle = analogRead(PIN_PRESSION);
    Serial.print("[PRESSION] ");
    Serial.println(pressionActuelle);

    debitImpulsions = pulseCount;
    pulseCount = 0;
    calculer_stats();
    // Calcul du statut Air une seule fois par seconde (retourne un indice)
    int statutAirCourant = evaluerStatutAir(true); // PompeEnMarche = true (à adapter)
    gestionAnomalieAir(statutAirCourant);

    // --- Nouvelle logique robuste pour le compteur de minutes consécutives ---
    // Critère : débit > 0 ET courant > seuil (pompe vraiment active)
    const float COURANT_POMPE_SEUIL = 0.5; // Ampères, à ajuster selon ton installation
    int nbDebitOn = 0;
    for (int i = 0; i < NBUF; i++) nbDebitOn += bufDebit[i];
    float courantMoyenne = 0;
    for (int i = 0; i < NBUF; i++) courantMoyenne += bufCourant[i];
    courantMoyenne /= NBUF;
    //bool pompeVraimentActive = (nbDebitOn > 0) && (courantMoyenne > COURANT_POMPE_SEUIL);
    bool pompeVraimentActive = (debitImpulsions > 0) && (courantMoyenne > COURANT_POMPE_SEUIL);
    static unsigned long tempsPause = 0;
    static bool enPause = false;
    static bool sortieDePause = false;
    static unsigned long tempsDernierPompageOff = 0;

    if (sortieDePause) {
      if (pompeVraimentActive) {
        debutPompage = now;
        minutesPompageConsecutives = 0;
        sortieDePause = false;
      }
    } else {
      if (pompeVraimentActive) {
        if (debutPompage == 0) {
          debutPompage = now;
          minutesPompageConsecutives = 0;
        }
        // Si la pompe était OFF récemment (<5s), on met juste en pause le compteur
        if (tempsDernierPompageOff && (now - tempsDernierPompageOff < DUREE_NON_POMPAGE_RESET_MS)) {
          // On ne fait rien, compteur en pause
        } else {
          unsigned long elapsed = now - debutPompage;
          unsigned int newMinutes = elapsed / 60000UL;
          if (newMinutes != minutesPompageConsecutives) {
            minutesPompageConsecutives = newMinutes;
          }
        }
        tempsDernierPompageOff = 0;
      } else {
        // Pompe OFF : on ne reset pas tout de suite, on attend 5s
        if (!tempsDernierPompageOff) tempsDernierPompageOff = now;
        if (now - tempsDernierPompageOff >= DUREE_NON_POMPAGE_RESET_MS) {
          debutPompage = 0;
          minutesPompageConsecutives = 0;
        }
      }
      // Déclenche la pause thermique si limite atteinte
      if (minutesPompageConsecutives >= POMPE_MAX_CONSEC_MIN) {
        pauseThermiqueActive = true;
        debutPauseThermique = now;
        minutesPauseRestantes = POMPE_PAUSE_MIN;
        digitalWrite(PIN_RELAIS_POMPE, HIGH);
      }
    }
    etatPompagePrecedent = pompeVraimentActive;
    // Log état global
    Serial.print("[ETAT GLOBAL] pauseThermiqueActive: "); Serial.print(pauseThermiqueActive);
    Serial.print(" | etatAnomalieAir: ");
    switch(etatAnomalieAir) {
      case NORMAL: Serial.print("NORMAL"); break;
      case ANOMALIE: Serial.print("ANOMALIE"); break;
      case SECURITE: Serial.print("SECURITE"); break;
      case ATTENTE_REDEMARRAGE: Serial.print("ATTENTE_REDEMARRAGE"); break;
    }
    Serial.print(" | Pompe: "); Serial.print(digitalRead(PIN_RELAIS_POMPE)==LOW ? "ON" : "OFF");
    Serial.print(" | Air: "); Serial.print(statusText[statutAirCourant]);
    Serial.print(" | P: "); Serial.print(pressionActuelle);
    Serial.print(" | I: "); Serial.println(courantMoy);

    // Log du buffer débit et de debitMoy juste avant affichage LCD
    Serial.print("[DEBUG DEBIT] bufDebit[]: ");
    for (int i = 0; i < NBUF; i++) {
      Serial.print(bufDebit[i]);
      Serial.print(" ");
    }
    Serial.print("| debitMoy: ");
    Serial.println(debitMoy, 3);
    maj_affichage(statutAirCourant);
    ajuster_relais_pompe();
    ajuster_relais_purge();

    // Log de debitMoy juste avant l'envoi du message RF24
    Serial.print("[DEBUG RF24] debitMoy: ");
    Serial.println(debitMoy, 3);
    communiquer_chalet();
  }
}

// --- Fonction centrale de gestion de l'anomalie d'air ---
void gestionAnomalieAir(int statutAir) {
  unsigned long now = millis();

  // Suivi du statut Air courant
  static int dernierStatutAirIdx = -1;
  if (statutAir != dernierStatutAirIdx) {
    dernierStatutAirIdx = statutAir;
    tDernierEtatAir = now;
    Serial.print("[ANOMALIE AIR] Changement statut Air: ");
    Serial.println(statusText[statutAir]);
  }

  // Affiche chaque seconde l'état et le temps passé dans ce statut
  static unsigned long lastLog = 0;
  if (now - lastLog >= 1000) {
    lastLog = now;
    Serial.print("[ANOMALIE AIR] Statut Air: ");
    Serial.print(statusText[statutAir]);
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
  int idxStatutAir = statutAir;
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
        //strcpy(dernierStatutAir, statutAir); // plus utile
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
  // Rien d'autre ici : resetBuffers() ne doit contenir que la remise à zéro des buffers !
}

// --- Fonction d'évaluation du statut Air ---
int evaluerStatutAir(bool pompeEnMarche) {
  // Si redémarrage pompe, forcer Non pendant 10s sauf si état précédent était Oui
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

  // Compte le nombre de détections Air dans le buffer
  int airCount = 0;
  for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];

  // Si la pression chute sous 125, on force Air à Oui (air détecté)
  if (pressionActuelle < 125) {
    return 0; // Oui
  }

  if (airCount == 0) return 1; // Non
  if (airCount >= 1 && airCount <= 6) return 3; // Non*
  if (airCount >= 7 && airCount <= 9) return 2; // Oui*
  if (airCount == 10) return 0; // Oui
  // Pour 10s, airCount varie de 0 à 100, donc adapte la logique
  if (airCount <= 60) return 3; // Non*
  if (airCount <= 90) return 2; // Oui*
  return 0; // Oui
}

// --- Fonctions capteurs (stubs à compléter selon besoin) ---
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

//
// === UTILISATION DES 4 LIGNES LCD 20x4 ===
// Ligne 0 : Pompe:On/Off Air:Oui/Non/.. + compteur air (2 chiffres)
// Ligne 1 : Pompage:Oui/Non + compteur minutes (ex: 02/06)
// Ligne 2 : Valve:Chalet/Purge + pression (ex: Valve:Chalet   P:123)
// Ligne 3 : Débit (ex: Debit:  7/10)
//
void maj_affichage(int statutAirCourant) {
  // ...affichage LCD restauré strictement identique à l'original...
    unsigned long now = millis();
    int nbDebitOn = 0;
    for (int i = 0; i < NBUF; i++) nbDebitOn += bufDebit[i];
    float courantMoyenne = 0;
    for (int i = 0; i < NBUF; i++) courantMoyenne += bufCourant[i];
    courantMoyenne /= NBUF;
    const float COURANT_POMPE_SEUIL = 0.5; // même seuil que dans loop()
    bool pompeVraimentActive = (nbDebitOn > 0) && (courantMoyenne > COURANT_POMPE_SEUIL);
    int airCount = 0;
    for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];
    char airCountStr[3] = "  ";
    if (afficherAirCountLCD) {
      snprintf(airCountStr, 3, "%02d", airCount > 99 ? 99 : airCount);
    }
    int idxPompe = pauseThermiqueActive ? 5 : 4;
    const char* pompeStr = statusText[idxPompe];
    // Utiliser l'indice statutAirCourant pour l'affichage
    const char* airStr = statusText[statutAirCourant];
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
    char compteurFinal[10] = "        ";
    char ligneBase[16] = "";
    bool pompageOui = false;
    static unsigned long lastAffichageSec = 0;
    static unsigned long lastPompageDebut = 0;
    static unsigned long lastPompageElapsed = 0;
    static char dernierAffichageSec[10] = "";
    unsigned long elapsedPompage = 0;
    if (pauseThermiqueActive) {
      // Affiche le temps de pause écoulé au format mm'ss/03, mais avec le label Pompage:Non*
      unsigned long now_pause = millis();
      unsigned long tempsEcoule = (now_pause - debutPauseThermique) / 1000UL;
      unsigned int min = tempsEcoule / 60;
      unsigned int sec = tempsEcoule % 60;
      char compteurPause[10];
      snprintf(compteurPause, sizeof(compteurPause), "%02u'%02u/%02u", min, sec, POMPE_PAUSE_MIN);
      snprintf(ligne1, 21, "Pompage:Non*%s", compteurPause);
      ligne1[20] = '\0';
      lcd.setCursor(0, 1); lcd.print(ligne1);
      // On sort immédiatement pour ne pas écraser l'affichage pause par le bloc compteurLCDaZero
      return;
    } else if (pompeVraimentActive) {
      pompageTousUn = false;
      pompageTousUnStart = 0;
      if (!pompageActif) {
        pompageActif = true;
        pompageNonStart = 0;
      }
      // Calcul du temps écoulé depuis le début du pompage continu
      if (lastPompageDebut == 0 || debutPompage != lastPompageDebut) {
        lastPompageDebut = debutPompage;
        lastPompageElapsed = 0;
      }
      elapsedPompage = now - debutPompage;
      // Rafraîchir les secondes toutes les 5 secondes seulement
      unsigned long elapsedSec = elapsedPompage / 1000UL;
      static unsigned long lastDisplayedSec = 0;
      if (elapsedSec / 5 != lastDisplayedSec / 5) {
        lastDisplayedSec = elapsedSec;
        unsigned int min = elapsedSec / 60;
        unsigned int sec = elapsedSec % 60;
        snprintf(dernierAffichageSec, sizeof(dernierAffichageSec), "%02u'%02u/%02u", min, sec, POMPE_MAX_CONSEC_MIN);
      }
      snprintf(ligne1, 21, "Pompage:Oui %s", dernierAffichageSec);
      pompageOui = true;
    } else if (!pompeVraimentActive && nbDebitOn == NBUF) {
      if (!pompageTousUn) {
        pompageTousUn = true;
        pompageTousUnStart = now;
      }
      if (now - pompageTousUnStart < DUREE_NON_POMPAGE_RESET_MS) {
        if (!pompageActif) {
          pompageActif = true;
          pompageNonStart = 0;
        }
        if (lastPompageDebut == 0 || debutPompage != lastPompageDebut) {
          lastPompageDebut = debutPompage;
          lastPompageElapsed = 0;
        }
        elapsedPompage = now - debutPompage;
        unsigned long elapsedSec = elapsedPompage / 1000UL;
        static unsigned long lastDisplayedSec = 0;
        if (elapsedSec / 5 != lastDisplayedSec / 5) {
          lastDisplayedSec = elapsedSec;
          unsigned int min = elapsedSec / 60;
          unsigned int sec = elapsedSec % 60;
          snprintf(dernierAffichageSec, sizeof(dernierAffichageSec), "%02u'%02u/%02u", min, sec, POMPE_MAX_CONSEC_MIN);
        }
        snprintf(ligne1, 21, "Pompage:Oui %s", dernierAffichageSec);
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
      snprintf(compteurFinal, sizeof(compteurFinal), "00'00/%02u", POMPE_MAX_CONSEC_MIN);
      if (pauseThermiqueActive) {
        snprintf(ligne1, 21, "Pompage:Non* %s", compteurFinal);
      } else {
        snprintf(ligne1, 21, "Pompage:Non %s", compteurFinal);
      }
    }
    ligne1[20] = '\0';
    lcd.setCursor(0, 1); lcd.print(ligne1);

    // Affichage de la ligne 2 : Valve + pression
    const char* valve = (digitalRead(PIN_RELAIS_PURGE) == HIGH) ? "Purge " : statusText[6];
    char ligne2[21];
    // 7 caractères pour Valve:..., 7 pour Chalet/Purge, 6 pour pression
    // Format: Valve:Chalet   P:123
    snprintf(ligne2, 21, "Valve:%-7sP:%4d", valve, pressionActuelle);
    ligne2[20] = '\0';
    lcd.setCursor(0, 2); lcd.print(ligne2);

    if (afficherDebitSimple) {
      char ligne3[21];
      int nbZero = 0;
      for (int i = 0; i < NBUF; i++) if (bufDebit[i] == 0) nbZero++;
      int debitAffiche = (nbZero == 0) ? 0 : (int)(debitMoy * 10 + 0.5);
      // Affichage courant format I:5.23A, aligné à droite
      char courantStr[8];
      snprintf(courantStr, sizeof(courantStr), "I:%1.2fA", courantMoy);
      // Compose la ligne: "Debit:  7/10 I:5.23A"
      //snprintf(ligne3, 21, "Debit: %2d/10 %-7s", debitAffiche, courantStr);
      snprintf(ligne3, 21, "Debit: %2d/s %-7s", debitImpulsions, courantStr);
      ligne3[20] = '\0';
      lcd.setCursor(0, 3); lcd.print(ligne3);
    } else {
      char bufStr[11];
      for (int i = 0; i < NBUF; i++) bufStr[i] = bufDebit[(idxBuf + i) % NBUF] ? '1' : '0';
      bufStr[NBUF] = '\0';
      char ligne3[21];
      int nbZero = 0;
      for (int i = 0; i < NBUF; i++) if (bufDebit[i] == 0) nbZero++;
      int debitAffiche = (nbZero == 0) ? 0 : (int)(debitMoy * 10 + 0.5);
      char courantStr[8];
      snprintf(courantStr, sizeof(courantStr), "I:%1.2fA", courantMoy);
      snprintf(ligne3, 21, "Debit:%s %2d/10%-7s", bufStr, debitAffiche, courantStr);
      ligne3[20] = '\0';
      lcd.setCursor(0, 3); lcd.print(ligne3);
    }
}

// Variables pour l'encodage et la détection de changement
char lastEncodedMsg[32] = "";
unsigned int msgSeq = 0; // Séquentiel 0-999
unsigned long lastMsgSent = 0;
unsigned int repeatCount = 0; // Nombre de répétitions du message courant (max 5)

// --- Fonction d'envoi RF simple (non intégrée à la logique métier) ---
bool sendRFMessage(const char* msg) {
  // Envoie le message via RF24, retourne true si succès
  bool ok = radio.write(msg, strlen(msg) + 1); // +1 pour le '\0'
  if (ok) {
    Serial.print("[RF24] Message envoyé: ");
    Serial.println(msg);
  } else {
    Serial.print("[RF24] Echec envoi: ");
    Serial.println(msg);
  }
  return ok;
}

// ...le reste du code continue sans accolade fermante ici...

void ajuster_relais_pompe() {
  // À implémenter : logique de contrôle de la pompe
}

void ajuster_relais_purge() {
  // À implémenter : logique de contrôle de la purge
}

void communiquer_chalet() {
  // Encodage du message compact positionnel :
  // Format : SSSAPDCMM\n
  // SSS = séquentiel (3 chiffres, 000 à 999)
  // A = statut Air (index statusText[])
  // P = statut Pompe (ON/OFF, index statusText[])
  // D = débit (0-9)
  // C = courant (0-9, optionnel ou à fixer)
  // MM = minutes pompage consécutives (2 chiffres)

  int idxAir = evaluerStatutAir(true);
  int pompeEtat = digitalRead(PIN_RELAIS_POMPE) == LOW ? 4 : 5;
  //int debit = (int)(debitMoy * 10 + 0.5);
  //if (debit > 9) debit = 9;
  int debit = debitImpulsions;
  if (debit > 9) debit = 9;
  int courant = (int)(courantMoy + 0.5);
  if (courant > 9) courant = 9;
  unsigned int minPompe = minutesPompageConsecutives;

  // Construction du message sans incrémenter la séquence
  char msg[16];
  snprintf(msg, sizeof(msg), "%03u%d%d%d%d%02u", msgSeq, idxAir, pompeEtat, debit, courant, minPompe);

  // Construction du message sans la séquence pour détection de changement
  char msgValues[16];
  snprintf(msgValues, sizeof(msgValues), "%d%d%d%d%02u", idxAir, pompeEtat, debit, courant, minPompe);

  static char lastMsgValues[16] = "";
  unsigned long now = millis();
  if (strcmp(msgValues, lastMsgValues) != 0) {
    // Valeurs changées, incrémente la séquence
    msgSeq = (msgSeq + 1) % 1000;
    snprintf(msg, sizeof(msg), "%03u%d%d%d%d%02u", msgSeq, idxAir, pompeEtat, debit, courant, minPompe);
    strncpy(lastEncodedMsg, msg, sizeof(lastEncodedMsg));
    strncpy(lastMsgValues, msgValues, sizeof(lastMsgValues));
    repeatCount = 1;
    lastMsgSent = now;
    Serial.print("MSG:");
    Serial.println(msg);
    sendRFMessage(msg);
  } else if (repeatCount > 0 && repeatCount < 5 && (now - lastMsgSent >= 1000)) {
    repeatCount++;
    lastMsgSent = now;
    Serial.print("MSG:");
    Serial.println(msg);
    sendRFMessage(msg);
  } else if (repeatCount >= 5) {
    // Après 5 répétitions, ne rien faire tant que la valeur ne change pas
    // Serial.println("[RF24] Message identique, emission bloquée");
  }
}