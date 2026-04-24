// =============================
// ctl_pompe_chalet_v3.ino
// Version modulaire avec RESET handshake robuste et affichage séparé
// =============================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <nRF24L01.h>
#include <RF24.h>

// LCD 20x4 I2C
LiquidCrystal_I2C lcd(0x27, 20, 4);

// RF24 : CE = 4, CSN = 5
#define CE_PIN 4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const uint8_t adresse[6] = "00001";

// Boutons
const int PIN_BOUTON_1 = A2;
const int PIN_BOUTON_2 = 10;
const int PIN_BOUTON_3 = 3;
const int PIN_BOUTON_4 = 9; // RESET

// --- Prototypes ---
void updateAffichage();
void handleCommunication();
void handleResetHandshake();
void doReset();
void gestionAnomalieAir(int idxAir);
void resetBuffers();
void calculer_stats();

// --- Variables et constantes pour la logique capteurs/relais/affichage ---
const int PIN_RELAIS_POMPE = 7;   // D7
const int PIN_RELAIS_PURGE = 8;   // D8
const int PIN_SONDE_IR = 2;       // D2
const int PIN_DEBIT = 6;          // D6
const int PIN_COURANT = A1;       // A1

// Buffers pour acquisition rapide
const int NBUF = 10;
int bufDebit[NBUF];
float bufCourant[NBUF];
int bufSondeIR[NBUF];
int idxBuf = 0;

// Buffers pour statut Air
const int NBUF_AIR = 100;
int bufAir[NBUF_AIR];
int idxBufAir = 0;

// Moyennes/min/max sur 1s
float courantMoy = 0, courantMin = 0, courantMax = 0;
float debitMoy = 0, debitMin = 0, debitMax = 0;
float sondeIRMoy = 0;

// Timing
unsigned long lastSample = 0;
unsigned long lastAffichage = 0;
const unsigned long INTERVAL_SAMPLE = 20;   // 20ms
const unsigned long INTERVAL_AFFICHAGE = 1000; // 1s

// Anomalie air
enum EtatAnomalieAir { NORMAL, ANOMALIE, SECURITE, ATTENTE_REDEMARRAGE };
EtatAnomalieAir etatAnomalieAir = NORMAL;
unsigned long tDebutAnomalie = 0;
unsigned long tDernierEtatAir = 0;
char dernierStatutAir[6] = "";

// Protection thermique
unsigned int minutesPompageConsecutives = 0;
unsigned long lastPompageStateChange = 0;
bool etatPompagePrecedent = false;
bool pauseThermiqueActive = false;
unsigned long debutPauseThermique = 0;
unsigned int minutesPauseRestantes = 0;

// --- Squelettes pour éviter les erreurs de linkage ---
void calculer_stats() {
  // À compléter : calcul des moyennes/min/max sur 1s
  courantMoy = 0; debitMoy = 0; sondeIRMoy = 0;
  for (int i = 0; i < NBUF; i++) {
    courantMoy += bufCourant[i];
    debitMoy += bufDebit[i];
    sondeIRMoy += bufSondeIR[i];
  }
  courantMoy /= NBUF;
  debitMoy /= NBUF;
  sondeIRMoy /= NBUF;
}

void resetBuffers() {
  for (int i = 0; i < NBUF; i++) {
    bufDebit[i] = 0;
    bufCourant[i] = 0;
    bufSondeIR[i] = 0;
  }
  for (int i = 0; i < NBUF_AIR; i++) bufAir[i] = 0;
  idxBuf = 0;
  idxBufAir = 0;
}

void gestionAnomalieAir(int idxAir) {
  // À compléter : logique d'anomalie d'air
}


void setup() {
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
  pinMode(PIN_BOUTON_1, INPUT_PULLUP);
  pinMode(PIN_BOUTON_2, INPUT_PULLUP);
  pinMode(PIN_BOUTON_3, INPUT_PULLUP);
  pinMode(PIN_BOUTON_4, INPUT_PULLUP);
  pinMode(PIN_SONDE_IR, INPUT);
  pinMode(PIN_DEBIT, INPUT);
  digitalWrite(PIN_RELAIS_POMPE, LOW); // Pompe ON au démarrage
  digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF au démarrage
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  delay(100);
  lcd.setCursor(0, 0);
  lcd.print("POMPE V3 READY");
  radio.begin();
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.openReadingPipe(0, adresse);
  radio.openWritingPipe(adresse);
  radio.startListening();
  lcd.setCursor(0,1);
  lcd.print("RF OK");
}

void loop() {
  handleResetHandshake(); // Prioritaire
  updateAffichage();     // Affichage normal
  handleCommunication(); // Communication normale
}

// --- À compléter : gestion du RESET handshake robuste ---
void handleResetHandshake() {
  // --- RESET handshake robuste avec priorité affichage ---
  static bool resetEnAttenteAck = false;
  static unsigned long resetAckTimeout = 0;
  const unsigned long RESET_ACK_TIMEOUT_MS = 1000;

  static bool resetCountdownActive = false;
  static unsigned long countdownStart = 0;
  static int countdownValue = 3;

  // Déclenchement du décompte RESET (bouton D9)
  if (!resetEnAttenteAck) {
    if (!resetCountdownActive && digitalRead(PIN_BOUTON_4) == LOW) {
      resetCountdownActive = true;
      countdownStart = millis();
      countdownValue = 3;
      lcd.setCursor(0,0);
      lcd.print("RESET dans 3...   ");
    }
    if (resetCountdownActive) {
      // Si bouton relâché avant la fin, annule
      if (digitalRead(PIN_BOUTON_4) == HIGH) {
        resetCountdownActive = false;
        lcd.setCursor(0,0);
        lcd.print("RESET ANNULE      ");
        delay(500);
        lcd.setCursor(0,0);
        lcd.print("POMPE V3 READY    ");
      } else {
        // Gère le décompte
        unsigned long elapsed = millis() - countdownStart;
        int newCountdown = 3 - (elapsed / 1000); // 1s par étape
        if (newCountdown != countdownValue && newCountdown > 0) {
          countdownValue = newCountdown;
          lcd.setCursor(0,0);
          lcd.print("RESET dans ");
          lcd.print(countdownValue);
          lcd.print("...   ");
        }
        if (elapsed >= 3000) { // 3,2,1 terminé (3s)
          // Lance le RESET handshake
          resetCountdownActive = false;
          lcd.setCursor(0,0);
          lcd.print("RESET EN COURS    ");
          // Envoi RESET!
          radio.stopListening();
          radio.write("RESET!", 7);
          radio.startListening();
          lcd.setCursor(0,1);
          lcd.print("ENVOYE:RESET!     ");
          resetEnAttenteAck = true;
          resetAckTimeout = millis();
          delay(300);
        }
      }
    }
  }

  // Réception de message
  if (radio.available()) {
    char rfMsg[32] = "";
    radio.read(&rfMsg, sizeof(rfMsg));
    lcd.setCursor(0,0);
    lcd.print("RECU:              ");
    lcd.setCursor(0,0);
    lcd.print("RECU:");
    lcd.print(rfMsg);
    if (strcmp(rfMsg, "RESET!") == 0) {
      // On reçoit une demande de reset : on ACK puis on reboot
      radio.stopListening();
      radio.write("RESET_ACK!", 10);
      radio.startListening();
      lcd.setCursor(0,1);
      lcd.print("REBOOT...         ");
      delay(200);
      doReset();
    } else if (strcmp(rfMsg, "RESET_ACK!") == 0 && resetEnAttenteAck) {
      // On reçoit l'ACK attendu : on reboot
      lcd.setCursor(0,1);
      lcd.print("ACK RECU, REBOOT  ");
      delay(200);
      doReset();
    }
  }

  // Timeout si pas d'ACK reçu
  if (resetEnAttenteAck && (millis() - resetAckTimeout > RESET_ACK_TIMEOUT_MS)) {
    lcd.setCursor(0,1);
    lcd.print("ACK RATE, REBOOT  ");
    delay(200);
    doReset();
  }
}

// --- À compléter : affichage normal (hors décompte/reset) ---
void updateAffichage() {
  // Logique complète v2 : acquisition, sécurité, relais, affichage, Serial.print
  unsigned long now = millis();
  // Acquisition rapide (tous les 20ms)
  if (now - lastSample >= INTERVAL_SAMPLE) {
    lastSample = now;
    bufDebit[idxBuf] = digitalRead(PIN_DEBIT);
    bufCourant[idxBuf] = analogRead(PIN_COURANT) * (5.0 / 1023.0);
    bufSondeIR[idxBuf] = digitalRead(PIN_SONDE_IR);
    idxBuf = (idxBuf + 1) % NBUF;
  }

  // Affichage, sécurité, relais toutes les secondes
  if (now - lastAffichage >= INTERVAL_AFFICHAGE) {
    lastAffichage = now;
    calculer_stats();
    // --- Affichage LCD (exemple v2 simplifié) ---
    lcd.setCursor(0,0);
    lcd.print("Pompe:");
    lcd.print(pauseThermiqueActive ? "Off " : "On  ");
    lcd.print(" Air:N/A   ");
    lcd.setCursor(0,1);
    lcd.print("Courant:");
    lcd.print(courantMoy,2);
    lcd.print("A   ");
    lcd.setCursor(0,2);
    lcd.print("Debit:");
    lcd.print(debitMoy,2);
    lcd.print("   ");
    lcd.setCursor(0,3);
    lcd.print("IR:");
    lcd.print(sondeIRMoy,2);
    lcd.print("   ");
    // --- Serial.print pour debug ---
    Serial.print("Pompe:"); Serial.print(pauseThermiqueActive ? "Off" : "On");
    Serial.print(" | Courant:"); Serial.print(courantMoy,2);
    Serial.print(" | Debit:"); Serial.print(debitMoy,2);
    Serial.print(" | IR:"); Serial.print(sondeIRMoy,2);
    Serial.println();
    // --- Logique sécurité, relais, etc. à compléter ici (voir v2 pour détails complets) ---
  }
}

// --- À compléter : communication régulière (hors RESET) ---
void handleCommunication() {
  // (À compléter plus tard) Transmission régulière des trames de données RF
  // Pour l’instant, rien ici
}

// Reset universel compatible UNO R4 (ARM) et UNO classique (AVR)
void doReset() {
#if defined(__arm__) || defined(ARDUINO_ARCH_RENESAS_UNO)
  NVIC_SystemReset();
#else
  asm volatile ("jmp 0");
#endif
}