/*
 * ============================================================================
 * TEST BOUTONS - AFFICHEUR LCD I2C 20x4
 * ============================================================================
 *
 * Ce programme teste les 4 boutons câblés sur l'afficheur.
 *
 * Branchement :
 *   - Bouton 1 : A2
 *   - Bouton 2 : D10
 *   - Bouton 3 : D3
 *   - Bouton 4 (reset) : D9
 *
 * LCD I2C 20x4 :
 *   - SDA → A4 (I2C)
 *   - SCL → A5 (I2C)
 *   - VCC → 5V
 *   - GND → GND
 *
 * ============================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD I2C 20x4 (adresse 0x27)
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Définition des pins boutons
const int PIN_BOUTON_1 = A2;
const int PIN_BOUTON_2 = 10;
const int PIN_BOUTON_3 = 3;
const int PIN_BOUTON_4 = 9; // reset

void setup() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TEST BOUTONS");

  pinMode(PIN_BOUTON_1, INPUT_PULLUP);
  pinMode(PIN_BOUTON_2, INPUT_PULLUP);
  pinMode(PIN_BOUTON_3, INPUT_PULLUP);
  pinMode(PIN_BOUTON_4, INPUT_PULLUP);
}

void loop() {
  // Lecture boutons (LOW = appuyé)
  bool b1 = (digitalRead(PIN_BOUTON_1) == LOW);
  bool b2 = (digitalRead(PIN_BOUTON_2) == LOW);
  bool b3 = (digitalRead(PIN_BOUTON_3) == LOW);
  bool b4 = (digitalRead(PIN_BOUTON_4) == LOW);

  lcd.setCursor(0, 1);
  lcd.print("B1(A2): "); lcd.print(b1 ? "ON " : "OFF");
  lcd.setCursor(0, 2);
  lcd.print("B2(D10): "); lcd.print(b2 ? "ON " : "OFF");
  lcd.setCursor(0, 3);
  lcd.print("B3(D3): "); lcd.print(b3 ? "ON " : "OFF");
  lcd.setCursor(12, 3);
  lcd.print("B4(D9): "); lcd.print(b4 ? "ON " : "OFF");

  delay(100);
}
