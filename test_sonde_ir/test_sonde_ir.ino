/*
 * ============================================================================
 * TEST SONDE INFRA-ROUGE CQRobot - Détection d'eau/air
 * ============================================================================
 *
 * Ce programme teste la sonde IR de niveau d'eau (CQRobot) en affichant sur le LCD
 * si la sonde détecte de l'eau ou de l'air devant sa pointe.
 *
 * BRANCHEMENT :
 * - Rouge  : 5V (VCC)
 * - Noir   : GND
 * - Vert   : D2 (signal digital)
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

// LCD I2C 20x4 (adresse 0x27)
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Broche du capteur IR
const int PIN_SONDE_IR = 2; // D2

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  delay(100);

  // Efface l'écran
  for (int i = 0; i < 4; i++) {
    lcd.setCursor(0, i);
    lcd.print("                    ");
  }
  lcd.setCursor(0, 0);
  lcd.print("TEST SONDE IR D2");

  pinMode(PIN_SONDE_IR, INPUT);
  Serial.println("Sonde IR prête. Placez la pointe dans l'eau ou l'air.");
}

void loop() {
  int etat = digitalRead(PIN_SONDE_IR);

  lcd.setCursor(0, 1);
  if (etat == HIGH) {
    lcd.print("VOIT EAU         ");
    Serial.println("Sonde: EAU (dans l'eau)");
  } else {
    lcd.print("VOIT AIR         ");
    Serial.println("Sonde: AIR (hors de l'eau)");
  }
  delay(200);
}
