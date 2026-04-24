// =============================
// ctl_affiche_chalet_v3.ino
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

void setup() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("MAJ OK - Copilot");
  delay(1200);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("AFFICHEUR V3 READY");

  pinMode(PIN_BOUTON_1, INPUT_PULLUP);
  pinMode(PIN_BOUTON_2, INPUT_PULLUP);
  pinMode(PIN_BOUTON_3, INPUT_PULLUP);
  pinMode(PIN_BOUTON_4, INPUT_PULLUP);

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
        lcd.print("AFFICHEUR V3 READY");
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
  // TODO: Affichage normal de l'afficheur (hors décompte/reset)
}

// --- À compléter : communication régulière (hors RESET) ---
void handleCommunication() {
  // TODO: Réception et affichage des trames de données
}

// Reset universel compatible UNO R4 (ARM) et UNO classique (AVR)
void doReset() {
#if defined(__arm__) || defined(ARDUINO_ARCH_RENESAS_UNO)
  NVIC_SystemReset();
#else
  asm volatile ("jmp 0");
#endif
}
