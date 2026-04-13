
// ============================================================================
// TEST SONDE IR - LCD I2C 20x4
// ============================================================================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 20, 4);
const int PIN_SONDE_IR = 2; // D2

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  delay(100);
  pinMode(PIN_SONDE_IR, INPUT);
  lcd.setCursor(0, 0);
  lcd.print("TEST SONDE IR D2");
}

void loop() {
  int etat = digitalRead(PIN_SONDE_IR);
  lcd.setCursor(0, 1);
  lcd.print("Etat: ");
  lcd.print(etat);
  lcd.print("      ");

  lcd.setCursor(0, 2);
  if (etat == 0) {
    lcd.print("AIR detecte     ");
  } else {
    lcd.print("EAU detectee    ");
  }

  // Affichage série pour debug
  Serial.print("Etat sonde IR (D2): ");
  Serial.println(etat);
  delay(200);
}
