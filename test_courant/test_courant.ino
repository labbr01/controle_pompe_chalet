/*
 * ============================================================================
 * TEST CAPTEUR DE COURANT (ex: ACS712) - LCD I2C 20x4
 * ============================================================================
 *
 * Ce programme teste un capteur de courant analogique (ex: ACS712) branché sur A1.
 * Il affiche la valeur brute, la tension (U) et le courant estimé (I) sur le LCD et le port série.
 *
 * BRANCHEMENT :
 * - VCC capteur : 5V
 * - GND capteur : GND
 * - OUT capteur : A1 (entrée analogique)
 *
 * LCD I2C 20x4 :
 * - SDA → A4 (I2C)
 * - SCL → A5 (I2C)
 * - VCC → 5V
 * - GND → GND
 *
 * ============================================================================
 *
 * VALEURS TYPIQUES OBSERVÉES (ACS712-5A, pompe 12V) :
 *
 * - Pompe arrêtée (relais ouvert) :
 *     Brut ≈ 515   U ≈ 2.53V   I ≈ 0.09A
 * - Fonctionnement stable (pompe eau) :
 *     Brut ≈ 420-446   U ≈ 2.15V   I ≈ -2.30A
 * - Pompage d'air :
 *     Brut ≈ 470-490   U ≈ 2.30-2.40V   I ≈ -1.1 à -2.0A
 * - Démarrage (pic) :
 *     Brut ≈ 350   U ≈ 1.69V   I ≈ -4A
 * - Par à-coups / fin de cycle :
 *     Brut ≈ 270/521 alternance
 *
 * - U = tension mesurée sur la sortie du capteur (pour diagnostic/calibration)
 * - I = courant estimé (en ampères, négatif si le sens est inversé)
 *
 * Si l'ampérage est négatif alors que la pompe fonctionne normalement,
 * il suffit d'inverser le sens du capteur (swap fils batterie/pompe).
 *
 * Ces valeurs servent de référence pour coder la logique de détection d'état de la pompe.
 * ============================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 20, 4);
const int PIN_COURANT = A1;

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
  lcd.print("TEST COURANT A1");
}

void loop() {
  int raw = analogRead(PIN_COURANT);
  float tension = raw * 5.0 / 1023.0;
  float offset = 2.5; // V (pour ACS712)
  float sensibilite = 0.185; // V/A (pour ACS712-5A)
  float courant = (tension - offset) / sensibilite;

  // Affichage LCD
  lcd.setCursor(0, 1);
  lcd.print("Brut: ");
  lcd.print(raw);
  lcd.print("      ");

  lcd.setCursor(0, 2);
  lcd.print("U: ");
  lcd.print(tension, 2);
  lcd.print("V      ");

  lcd.setCursor(0, 3);
  lcd.print("I: ");
  lcd.print(courant, 2);
  lcd.print("A      ");

  // Affichage série
  Serial.print("Brut: "); Serial.print(raw);
  Serial.print(" | U: "); Serial.print(tension, 3);
  Serial.print(" V | I: "); Serial.print(courant, 3);
  Serial.println(" A");

  delay(500);
}
