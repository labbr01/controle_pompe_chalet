/*
 * Test simple OLED SSD1306 - 3 lignes en taille 2
 * Arduino UNO R4 Minima
 * 
 * Branchements I2C:
 * - SDA → A4
 * - SCL → A5
 * - VCC → 5V
 * - GND → GND
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Configuration écran - ESSAI 64 comme v3
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(9600);
  
  // Init OLED - exactement comme ctl_pompe_v3
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("ERREUR OLED");
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("Ligne 1");
  
  display.setCursor(0, 20);
  display.println("Ligne 2");
  
  display.setCursor(0, 40);
  display.println("Ligne 3");
  
  display.display();
  
  Serial.println("Affichage terminé");
}

void loop() {
  // Rien dans la boucle pour l'instant
  delay(1000);
}
