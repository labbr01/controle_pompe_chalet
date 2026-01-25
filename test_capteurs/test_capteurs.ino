// OBSOLÈTE : Ce fichier n'est plus maintenu, voir README_CAPTEURS.md
/*
 * Test individuel des capteurs - Arduino fonctionnel
 * Basé sur ctl_pompe_v3 qui fonctionne
 * 
 * Pins:
 * - OLED: SDA=A4, SCL=A5, VCC=5V, GND=GND
 * - Pression: A0
 * - Ampérage: A1
 * - Débit: D6
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Configuration écran (même que ctl_pompe_v3)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pins capteurs
const int PIN_PRESSION = A0;
const int PIN_AMPERAGE = A1;
const int PIN_DEBIT = 6;

void setup() {
  Serial.begin(9600);
  delay(1000);
  
  Serial.println("=== Test Capteurs Individuels ===");
  Serial.println("Arduino fonctionnel avec ctl_pompe_v3");
  Serial.println();
  
  // Init OLED (même code que v3)
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("ERREUR: OLED non détecté!");
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Test capteurs");
  display.display();
  
  pinMode(PIN_DEBIT, INPUT);
  
  Serial.println("Affichage toutes les secondes:");
  Serial.println("P=Pression(A0) | A=Amperage(A1) | D=Debit(D6)");
  Serial.println("-----------------------------------------------");
}

void loop() {
  static unsigned long lastPrint = 0;
  unsigned long now = millis();
  
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    
    // Lecture capteurs
    int pression = analogRead(PIN_PRESSION);
    int amperageRaw = analogRead(PIN_AMPERAGE);
    int debitState = digitalRead(PIN_DEBIT);

    // Conversion tension
    float tensionPression = pression * 5.0 / 1023.0;
    float tensionAmp = amperageRaw * 5.0 / 1023.0;

    // Conversion ampérage (exemple ACS712 5A, sensibilité 185mV/A, offset 2.5V)
    float offset = 2.5; // V
    float sensibilite = 0.185; // V/A
    float courant = (tensionAmp - offset) / sensibilite; // en ampères

    // Affichage Serial
    Serial.print("P:");
    Serial.print(pression);
    Serial.print(" (");
    Serial.print(tensionPression, 2);
    Serial.print("V) | A:");
    Serial.print(amperageRaw);
    Serial.print(" (");
    Serial.print(tensionAmp, 2);
    Serial.print("V, ");
    Serial.print(courant, 2);
    Serial.print("A) | D:");
    Serial.println(debitState);

    // Affichage OLED
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print("P:");
    display.println(pression);

    display.setCursor(0, 20);
    display.print("A:");
    display.print(courant, 2);
    display.println("A");

    display.setCursor(0, 40);
    display.print("D:");
    display.println(debitState);

    display.display();
  }
}
