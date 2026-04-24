// =============================
// ctl_affiche_chalet_v2.ino
// Afficheur LCD pour protocole compact SSSAPDCMM (compatible ctl_pompe_chalet_v2)
// =============================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <nRF24L01.h>
#include <RF24.h>

// =============================
// FILAGE HARMONISÉ AFFICHEUR (identique au contrôleur)
// Boutons (4 boutons, reset = dernier) :
//   - BOUTON 1 : A2 (utilisé en digital)
//   - BOUTON 2 : D10
//   - BOUTON 3 : D3
//   - BOUTON 4 (RESET) : D9
// Sonde IR : D2
// I2C LCD : SDA/SCL (pins dédiées Uno R4 Minima)
// nRF24L01 : CE = D4, CSN = D5
// =============================

// LCD 20x4 I2C
LiquidCrystal_I2C lcd(0x27, 20, 4);

// RF24 : CE = 4, CSN = 5 (adapter si besoin)
#define CE_PIN 4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const uint8_t adresse[6] = "00001"; // Doit matcher le maître

const int POMPE_MAX_CONSEC_MIN = 6; // Doit matcher le maître

// Statuts texte (doivent matcher le maître)
const char* statusText[] = {"Oui", "Non", "Oui*", "Non*", "On", "Off", "Chalet", "Purge"};

// Timer pour PING?
unsigned long lastPingTime = 0;
bool rfRecuDepuisReset = false;

// =============================
// Définition des pins boutons (harmonisé avec contrôleur)
const int PIN_BOUTON_1 = A2;  // A2 (digital)
const int PIN_BOUTON_2 = 10;  // D10
const int PIN_BOUTON_3 = 3;   // D3
const int PIN_BOUTON_4 = 9;   // D9 (RESET)
// Sonde IR : D2 (à lire côté contrôleur)
// =============================

void setup() {
  Serial.begin(9600);
  Serial.println("[BOOT] Afficheur démarré");
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("En attente RF...");

  // Init timer pour PING?
  lastPingTime = millis();

  // Initialisation boutons (pullup interne)
  pinMode(PIN_BOUTON_1, INPUT_PULLUP);
  pinMode(PIN_BOUTON_2, INPUT_PULLUP);
  pinMode(PIN_BOUTON_3, INPUT_PULLUP);
  pinMode(PIN_BOUTON_4, INPUT_PULLUP);

  if (!radio.begin()) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("NRF24L01 FAIL");
    Serial.println("[RF24] Erreur d'initialisation du module radio!");
    while (1);
  }
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.openReadingPipe(0, adresse);
  radio.startListening();
  Serial.println("[RF24] Module radio initialisé");
}

void loop() {
  // --- Réception de messages RF prioritaire à chaque tour de loop ---
  if (radio.available()) {
    char rfMsg[32] = "";
    radio.read(&rfMsg, sizeof(rfMsg));
    Serial.print("[RF DEBUG] Message RF reçu: ");
    Serial.println(rfMsg);
    if (strcmp(rfMsg, "RESET!") == 0) {
      Serial.println("[RF DEBUG] Reçu RESET! → envoi RESET_ACK! et reset");
      radio.stopListening();
      radio.write("RESET_ACK!", 10);
      radio.startListening();
      delay(100); // Laisse le temps à l'ACK de partir
      NVIC_SystemReset();
      return;
    } else if (strcmp(rfMsg, "RESET_ACK!") == 0) {
      Serial.println("[RF DEBUG] Reçu RESET_ACK! → reset");
      delay(100);
      NVIC_SystemReset();
      return;
    }
    // Affiche tout message reçu
    rfRecuDepuisReset = true;
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Etat recu:");
    lcd.setCursor(0,1);
    lcd.print(rfMsg);
  }

  // Envoi périodique de PING? toutes les 5 secondes, sans confirmation
  if (millis() - lastPingTime > 5000) {
    Serial.println("[PING] Envoi PING? au contrôleur");
    radio.stopListening();
    radio.write("PING?", 6);
    radio.startListening();
    lastPingTime = millis();
  }

  // Lecture boutons (LOW = appuyé)
  bool bouton1 = (digitalRead(PIN_BOUTON_1) == LOW); // A2
  bool bouton2 = (digitalRead(PIN_BOUTON_2) == LOW); // D10
  bool bouton3 = (digitalRead(PIN_BOUTON_3) == LOW); // D3
  bool bouton4 = (digitalRead(PIN_BOUTON_4) == LOW); // D9 (RESET)

  // --- Gestion RESET synchronisé RF ---
  static unsigned long boutonResetStart = 0;
  static int lastCountdown = -1;
  static bool etaitEnReset = false;
  static bool resetEnAttenteAck = false;
  static unsigned long resetAckTimeout = 0;
  const unsigned long RESET_ACK_TIMEOUT_MS = 1000;
  if (!resetEnAttenteAck) {
    if (bouton4) {
      if (boutonResetStart == 0) boutonResetStart = millis();
      unsigned long elapsed = millis() - boutonResetStart;
      int countdown = 3 - (int)(elapsed / 1000);
      if (countdown < 1) countdown = 1;
      if (countdown != lastCountdown) {
        lcd.setCursor(0,0);
        char ligne[21];
        snprintf(ligne, sizeof(ligne), "RESET dans %d", countdown);
        lcd.print("                    ");
        lcd.setCursor(0,0);
        lcd.print(ligne);
        Serial.print("[RESET] Compte à rebours: ");
        Serial.println(countdown);
        lastCountdown = countdown;
      }
      etaitEnReset = true;
      if (elapsed >= 3000) {
        // Envoie le message RESET! et attend l'ACK
        Serial.println("[RESET] Envoi RESET! via RF");
        radio.stopListening();
        radio.write("RESET!", 7);
        radio.startListening();
        resetEnAttenteAck = true;
        boutonResetStart = 0;
        lastCountdown = -1;
        lcd.setCursor(0,0);
        lcd.print("RESET EN COURS     ");
        Serial.println("[RESET] RESET EN COURS...");
        resetAckTimeout = millis();
      }
      delay(10);
      return;
    } else {
      if (etaitEnReset) {
        lcd.setCursor(0,0);
        lcd.print("                    ");
        Serial.println("[RESET] Fin appui RESET, retour affichage normal");
        etaitEnReset = false;
      }
      boutonResetStart = 0;
      lastCountdown = -1;
      resetAckTimeout = 0;
    }
  } else if (resetEnAttenteAck) {
    // Après envoi RESET!, on attend juste l'ACK (pas de décompte)
    lcd.setCursor(0,0);
    lcd.print("RESET EN COURS     ");
    if (resetAckTimeout > 0 && millis() - resetAckTimeout > RESET_ACK_TIMEOUT_MS) {
      Serial.println("[RESET] ACK RESET rate! (timeout)");
      lcd.setCursor(0,0);
      lcd.print("ACK RESET rate!   ");
      delay(1000);
      lcd.setCursor(0,0);
      lcd.print("                    ");
      NVIC_SystemReset();
    }
    delay(10);
    return;
  }
  delay(200);
}
