/*
 * ============================================================================
 * TEST CAPTEUR DE PRESSION - Valeur Analogique
 * ============================================================================
 * 
 * Ce programme teste le capteur de pression en affichant les valeurs brutes
 * (0-1023) pour établir les valeurs de référence.
 * 
 * OBJECTIF:
 * - Trouver la valeur minimale (sans pression)
 * - Trouver la valeur maximale (pression normale de fonctionnement)
 * - Établir le seuil de détection de purge
 * 
 * BRANCHEMENT:
 * - Signal capteur pression → A0 (lecture analogique)
 * - VCC capteur → 5V (borne)
 * - GND capteur → GND (borne)
 * 
 * LCD I2C 20x4:
 * - SDA → A4 (I2C)
 * - SCL → A5 (I2C)
 * - VCC → 5V (borne)
 * - GND → GND (borne)
 * 
 * AFFICHAGE LCD:
 * - Ligne 1: Titre du test
 * - Ligne 2: Valeur brute actuelle (0-1023)
 * - Ligne 3: Valeur minimale enregistrée
 * - Ligne 4: Valeur maximale enregistrée
 * 
 * MATÉRIEL:
 * - Arduino UNO R4 Minima
 * - LCD I2C 20x4: Freenove FNK0079 A1B0
 * - Capteur de pression analogique
 * - Borniers 5V et GND
 * 
 * ============================================================================
 */

// ============================================================================
// BIBLIOTHÈQUES
// ============================================================================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============================================================================
// CONFIGURATION MATÉRIELLE
// ============================================================================

// LCD I2C 20x4 (adresse 0x27)
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Pin du capteur de pression
const int PIN_PRESSION = A0;  // A0 - Entrée analogique

// ============================================================================
// VARIABLES DE MESURE
// ============================================================================

int pressionActuelle = 0;   // Valeur actuelle (0-1023)
int pressionMin = 1023;     // Valeur minimale enregistrée
int pressionMax = 0;        // Valeur maximale enregistrée

// Timing pour les mesures (10 fois par seconde)
unsigned long derniereMesure = 0;
const unsigned long INTERVALLE_MESURE = 100;  // 100ms

// ============================================================================
// INITIALISATION
// ============================================================================
void setup() {
  // Initialisation du port série
  Serial.begin(9600);
  Serial.println("=== TEST CAPTEUR PRESSION ===");
  Serial.println("Pin: A0");
  Serial.println("Plage: 0-1023 (10 bits)");
  Serial.println();
  Serial.println("Instructions:");
  Serial.println("1. Relâchez la pression complètement");
  Serial.println("2. Notez la valeur minimale");
  Serial.println("3. Faites monter la pression");
  Serial.println("4. Notez la valeur maximale");
  Serial.println();
  
  // Initialisation du LCD I2C
  lcd.init();
  lcd.backlight();
  delay(100);  // Attente stabilisation
  
  // Effacement manuel des 4 lignes
  lcd.setCursor(0, 0);
  lcd.print("                    ");  // 20 espaces
  lcd.setCursor(0, 1);
  lcd.print("                    ");
  lcd.setCursor(0, 2);
  lcd.print("                    ");
  lcd.setCursor(0, 3);
  lcd.print("                    ");
  
  // Affichage titre sur le LCD
  lcd.setCursor(0, 0);
  lcd.print("TEST PRESSION A0");
  
  Serial.println("Système prêt!");
  Serial.println("Mesures affichées toutes les 100ms");
  Serial.println();
}

// ============================================================================
// BOUCLE PRINCIPALE
// ============================================================================
void loop() {
  unsigned long maintenant = millis();
  
  // Lecture toutes les 100ms (10 fois par seconde)
  if (maintenant - derniereMesure >= INTERVALLE_MESURE) {
    derniereMesure = maintenant;
    
    // Lecture de la valeur analogique (0-1023)
    pressionActuelle = analogRead(PIN_PRESSION);

    // Conversion en tension (0-4.7V)
    float tension = pressionActuelle * (4.7 / 1023.0);

    // Mise à jour des valeurs min/max
    if (pressionActuelle < pressionMin) {
      pressionMin = pressionActuelle;
    }
    if (pressionActuelle > pressionMax) {
      pressionMax = pressionActuelle;
    }

    // -----------------------------------------------------------------------
    // AFFICHAGE LCD
    // -----------------------------------------------------------------------

    // Ligne 2: Valeur actuelle + tension
    lcd.setCursor(0, 1);
    lcd.print("Actuel: ");
    lcd.print(pressionActuelle);
    lcd.print(" ");
    lcd.print(tension, 2); // 2 décimales
    lcd.print("V   "); // Efface les caractères résiduels

    // Ligne 3: Valeur minimale
    lcd.setCursor(0, 2);
    lcd.print("Min: ");
    lcd.print(pressionMin);
    lcd.print("    ");

    // Ligne 4: Valeur maximale
    lcd.setCursor(0, 3);
    lcd.print("Max: ");
    lcd.print(pressionMax);
    lcd.print("    ");

    // -----------------------------------------------------------------------
    // AFFICHAGE SÉRIE (pour debug et archivage)
    // -----------------------------------------------------------------------
    Serial.print("Pression: ");
    Serial.print(pressionActuelle);
    Serial.print(" ( ");
    Serial.print(tension, 3);
    Serial.print(" V )");
    Serial.print(" | Min: ");
    Serial.print(pressionMin);
    Serial.print(" | Max: ");
    Serial.print(pressionMax);
    Serial.print(" | Plage: ");
    Serial.println(pressionMax - pressionMin);
  }
}

/*
 * ============================================================================
 * NOTES TECHNIQUES
 * ============================================================================
 * 
 * INTERPRÉTATION DES VALEURS:
 * 
 * 1. Valeur minimale (sans pression):
 *    - Correspond à la pression atmosphérique
 *    - Généralement entre 0-100 selon le type de capteur
 *    - Cette valeur est le "zéro" de référence
 * 
 * 2. Valeur maximale (pression de travail):
 *    - Correspond à la pression normale de la pompe
 *    - Dépend de la puissance de la pompe
 *    - Permet de calibrer les seuils de détection
 * 
 * 3. Plage de travail:
 *    - Max - Min = plage utile du capteur
 *    - Plus la plage est grande, meilleure est la résolution
 *    - Permet de définir les seuils de purge
 * 
 * CALIBRATION POUR LE SYSTÈME:
 * 
 * D'après l'ancien code (ctl_pompe_v5.ino):
 * - SEUIL_PRESSION_PURGE = 125
 * - Ce seuil indique quand déclencher une purge
 * 
 * À DÉTERMINER PENDANT LE TEST:
 * 1. Valeur au repos (sans pression) → définir le zéro
 * 2. Valeur normale de travail → pression attendue
 * 3. Ajuster le seuil de purge si nécessaire
 * 
 * EXEMPLE D'INTERPRÉTATION:
 * - Si Min = 50 et Max = 300
 * - Plage = 250 unités
 * - Seuil purge = 125 → environ 30% de la pression max
 * - En dessous de 125 → besoin de purge (présence d'air)
 * 
 * ============================================================================
 */
