// Test LCD + Blink pour Arduino UNO R4 Minima
#include <LiquidCrystal.h>
LiquidCrystal lcd(12, 11, 5, 4, 3, 2); // RS, E, D4, D5, D6, D7

void setup() {
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("13psi   3L/M");
  lcd.setCursor(0, 1);
  lcd.print("Pompe=On 1F,2O");
  pinMode(LED_BUILTIN, OUTPUT); // LED intégrée
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH); // allume la LED
  delay(8000);
  digitalWrite(LED_BUILTIN, LOW);  // éteint la LED
  delay(8000);
}
