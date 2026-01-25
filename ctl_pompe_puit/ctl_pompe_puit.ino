/*
 * ============================================================================
 * Contrôleur Pompe Puits - Version modulaire
 * ============================================================================
 *
 * Ce programme lit tous les capteurs installés (sonde IR, débitmètre, courant)
 * et affiche leur état sur un LCD I2C 20x4. Les relais et la communication RF
 * sont prévus dans la structure mais non activés dans cette version.
 *
 * Capteurs utilisés :
 * - Sonde IR (présence d'eau/air) : D2
 * - Débitmètre : D6
 * - Capteur de courant (ACS712) : A1
 *
 * Relais (prévu) :
 * - Pompe : D7
 * - Purge/vanne : D8
 *
 * LCD I2C 20x4 :
 * - SDA → A4 (I2C)
 * - SCL → A5 (I2C)
 * - VCC → 5V
 * - GND → GND
 *
 * ============================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Broches capteurs
const int PIN_SONDE_IR = 2;   // D2
const int PIN_DEBIT = 6;      // D6
const int PIN_COURANT = A1;   // A1

// Broches relais (prévu)
const int RELAIS_POMPE = 7;   // D7
const int RELAIS_PURGE = 8;   // D8

// Variables globales pour états capteurs
bool eauPresente = false;
int debitEtat = 0;
float courant = 0.0;

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  delay(100);

  // Initialisation capteurs
  pinMode(PIN_SONDE_IR, INPUT);
  pinMode(PIN_DEBIT, INPUT);
  // Les relais ne sont pas activés dans cette version

  lcd.setCursor(0, 0);
  lcd.print("CTL POMPE PUITS");
}

void loop() {
  lireSondeIR();
  lireDebitmetre();
  lireCourant();
  majAffichage();
  delay(500);
}

void lireSondeIR() {
  int etat = digitalRead(PIN_SONDE_IR);
  eauPresente = (etat == HIGH); // Adapter si besoin selon le sens de la sonde
}

void lireDebitmetre() {
  debitEtat = digitalRead(PIN_DEBIT); // 0 ou 1, à adapter si besoin
}

void lireCourant() {
  int raw = analogRead(PIN_COURANT);
  float tension = raw * 5.0 / 1023.0;
  float offset = 2.5; // V (pour ACS712)
  float sensibilite = 0.185; // V/A (pour ACS712-5A)
  courant = (tension - offset) / sensibilite;
}

void majAffichage() {
  lcd.setCursor(0, 1);
  lcd.print("Eau: ");
  lcd.print(eauPresente ? "OUI " : "NON ");
  lcd.print("  D: ");
  lcd.print(debitEtat);
  lcd.print("   ");

  lcd.setCursor(0, 2);
  lcd.print("I: ");
  lcd.print(courant, 2);
  lcd.print("A       ");

  lcd.setCursor(0, 3);
  lcd.print("Relais: P ");
  lcd.print("N/A  ");
  lcd.print("V ");
  lcd.print("N/A");
}
