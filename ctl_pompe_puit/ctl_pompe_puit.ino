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
float courant = 0.0;

// Buffer circulaire pour débitmètre
const int NBUF = 10;
int bufDebit[NBUF];
int idxBuf = 0;
unsigned long lastSample = 0;

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  delay(100);

  // Initialisation capteurs
  pinMode(PIN_SONDE_IR, INPUT);
  pinMode(PIN_DEBIT, INPUT);
  // Les relais ne sont pas activés dans cette version

  // Pas d'interruption débitmètre, lecture directe dans le buffer
}

void loop() {
  lireSondeIR();
  lireCourant();

  // Acquisition débitmètre toutes les 100ms (10Hz)
  unsigned long now = millis();
  if (now - lastSample >= 100) {
    lastSample = now;
    bufDebit[idxBuf] = digitalRead(PIN_DEBIT);
    idxBuf = (idxBuf + 1) % NBUF;
  }

  majAffichage();
  delay(50);
}

void lireSondeIR() {
  int etat = digitalRead(PIN_SONDE_IR);
  eauPresente = (etat == HIGH); // Adapter si besoin selon le sens de la sonde
}


void compteurDebit() {
  // plus utilisé
}

void lireCourant() {
  int raw = analogRead(PIN_COURANT);
  float tension = raw * 5.0 / 1023.0;
  float offset = 2.5; // V (pour ACS712)
  float sensibilite = 0.185; // V/A (pour ACS712-5A)
  courant = (tension - offset) / sensibilite;
}

void majAffichage() {
  // Ligne 0 : Pompe: On/Off Air: Oui/Non (format compact)
  char ligne0[21];
  const char* pompeStr = "On ";
  const char* airStr = eauPresente ? "Non" : "Oui";
  snprintf(ligne0, 21, "Pompe:%s Air:%s", pompeStr, airStr);
  lcd.setCursor(0, 0); lcd.print(ligne0);

  // Ligne 1 : Pompage:Oui/Non (si au moins un 1 dans le buffer)
  int sommeDebit = 0;
  for (int i = 0; i < NBUF; i++) sommeDebit += bufDebit[i];
  char ligne1[21];
  snprintf(ligne1, 21, "Pompage:%s", (sommeDebit > 0) ? "Oui" : "Non");
  lcd.setCursor(0, 1); lcd.print(ligne1);

  // Ligne 2 : Direction:Chalet
  char ligne2[21];
  snprintf(ligne2, 21, "Direction:Chalet");
  lcd.setCursor(0, 2); lcd.print(ligne2);

  // Ligne 3 : Débit: séquence binaire et total
  char bufStr[11];
  for (int i = 0; i < NBUF; i++) bufStr[i] = bufDebit[(idxBuf + i) % NBUF] ? '1' : '0';
  bufStr[NBUF] = '\0';
  char ligne3[21];
  snprintf(ligne3, 21, "Debit:%s %2d/10", bufStr, sommeDebit);
  lcd.setCursor(0, 3); lcd.print(ligne3);
}
