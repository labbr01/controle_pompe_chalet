/*
 * ============================================================================
 * TEST DÉBITMÈTRE - FRÉQUENCE D'ALTERNANCE
 * ============================================================================
 * 
 * Ce programme teste le débitmètre (flow meter) en comptant les alternances
 * 0/1 pour déterminer le débit d'eau.
 * 
 * PRINCIPE DE FONCTIONNEMENT:
 * - Le débitmètre génère des impulsions (alternances 0/1) proportionnelles au débit
 * - Plus le débit est élevé, plus les alternances sont rapides
 * - Quand il n'y a pas de débit, le signal reste stable (généralement sur 0)
 * - On échantillonne le signal 10 fois par seconde dans un buffer circulaire
 * - On compte le nombre de 0 comme indicateur de débit
 * 
 * BRANCHEMENT:
 * - Signal débitmètre → D6 (lecture digitale)
 * - VCC débitmètre → 5V (borne)
 * - GND débitmètre → GND (borne)
 * 
 * LCD I2C 20x4:
 * - SDA → A4 (I2C)
 * - SCL → A5 (I2C)
 * - VCC → 5V (borne)
 * - GND → GND (borne)
 * 
 * AFFICHAGE LCD:
 * - Ligne 1: Titre du test
 * - Ligne 2: État actuel du signal (0 ou 1)
 * - Ligne 3: Débit calculé (nbZero sur 10 échantillons)
 * - Ligne 4: Buffer visuel des 10 derniers états
 * 
 * INTERPRÉTATION:
 * - Débit = 0 ou 10 (1000) → Pas de débit (signal stable)
 * - Débit entre 1 et 9 → Débit présent (alternances en cours)
 * - Plus le nombre est proche de 5, plus le débit est élevé (alternance rapide)
 * 
 * MATÉRIEL:
 * - Arduino UNO R4 Minima
 * - LCD I2C 20x4: Freenove FNK0079 A1B0
 * - Débitmètre à effet Hall (YF-S201 ou similaire)
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

// Pin du débitmètre
const int PIN_DEBIT = 6;  // D6 - Entrée digitale

// ============================================================================
// PARAMÈTRES DE MESURE
// ============================================================================

// Buffer circulaire pour stocker les 10 derniers états (0 ou 1)
const int TAILLE_BUFFER = 10;
int bufferEtat[TAILLE_BUFFER] = {0};
int indexBuffer = 0;

// Variables de mesure
int etatActuel = 0;      // État actuel du signal (0 ou 1)
int debit = 0;           // Nombre de 0 dans le buffer (indicateur de débit)

// Timing pour les mesures périodiques (toutes les 1 seconde)
unsigned long derniereMesure = 0;
const unsigned long INTERVALLE_MESURE = 1000;  // 1 seconde

// Timing pour l'échantillonnage rapide (10 fois par seconde)
unsigned long dernierEchantillon = 0;
const unsigned long INTERVALLE_ECHANTILLON = 100;  // 100ms → 10 échantillons/sec

// ============================================================================
// INITIALISATION
// ============================================================================
void setup() {
  // Initialisation du port série
  Serial.begin(9600);
  Serial.println("=== TEST DÉBITMÈTRE ===");
  Serial.println("Échantillonnage: 10 fois/sec");
  Serial.println("Calcul débit: toutes les 1 sec");
  Serial.println();
  
  // Configuration de la pin du débitmètre en entrée
  pinMode(PIN_DEBIT, INPUT);
  
  // Initialisation du LCD I2C
  lcd.init();
  lcd.backlight();
  
  // Affichage titre sur le LCD
  lcd.setCursor(0, 0);
  lcd.print("TEST DEBITMETRE D6");
  
  // Initialisation du buffer à 0
  for (int i = 0; i < TAILLE_BUFFER; i++) {
    bufferEtat[i] = 0;
  }
  
  Serial.println("Système prêt!");
  Serial.println("Ouvrez un robinet pour voir les alternances...");
  Serial.println();
}

// ============================================================================
// BOUCLE PRINCIPALE
// ============================================================================
void loop() {
  unsigned long maintenant = millis();
  
  // -----------------------------------------------------------------------
  // ÉCHANTILLONNAGE RAPIDE: Lecture du signal toutes les 100ms
  // -----------------------------------------------------------------------
  if (maintenant - dernierEchantillon >= INTERVALLE_ECHANTILLON) {
    dernierEchantillon = maintenant;
    
    // Lecture de l'état actuel du débitmètre (0 ou 1)
    etatActuel = digitalRead(PIN_DEBIT);
    
    // Stockage dans le buffer circulaire
    bufferEtat[indexBuffer] = etatActuel;
    indexBuffer = (indexBuffer + 1) % TAILLE_BUFFER;  // Avance avec wrap-around
    
    // Affichage de l'état actuel sur le LCD (ligne 2)
    lcd.setCursor(0, 1);
    lcd.print("Signal: ");
    lcd.print(etatActuel);
    lcd.print("  ");  // Efface les caractères résiduels
  }
  
  // -----------------------------------------------------------------------
  // CALCUL DU DÉBIT: Analyse du buffer toutes les 1 seconde
  // -----------------------------------------------------------------------
  if (maintenant - derniereMesure >= INTERVALLE_MESURE) {
    derniereMesure = maintenant;
    
    // Comptage du nombre de 0 dans le buffer
    int nbZero = 0;
    for (int i = 0; i < TAILLE_BUFFER; i++) {
      if (bufferEtat[i] == 0) {
        nbZero++;
      }
    }
    debit = nbZero;
    
    // -----------------------------------------------------------------------
    // AFFICHAGE LCD
    // -----------------------------------------------------------------------
    
    // Ligne 3: Valeur du débit
    lcd.setCursor(0, 2);
    lcd.print("Debit: ");
    lcd.print(debit);
    lcd.print("/10  ");  // Efface les caractères résiduels
    
    // Interprétation du débit
    if (debit == 0 || debit == 10) {
      lcd.print("ARRET");
    } else if (debit >= 4 && debit <= 6) {
      lcd.print("MAX  ");
    } else {
      lcd.print("ACTIF");
    }
    
    // Ligne 4: Visualisation du buffer (10 caractères)
    lcd.setCursor(0, 3);
    lcd.print("Buf:");
    for (int i = 0; i < TAILLE_BUFFER; i++) {
      lcd.print(bufferEtat[i]);
    }
    lcd.print("      ");  // Efface les caractères résiduels
    
    // -----------------------------------------------------------------------
    // AFFICHAGE SÉRIE (pour debug et analyse)
    // -----------------------------------------------------------------------
    Serial.print("Debit: ");
    Serial.print(debit);
    Serial.print("/10 | Buffer: [");
    for (int i = 0; i < TAILLE_BUFFER; i++) {
      Serial.print(bufferEtat[i]);
      if (i < TAILLE_BUFFER - 1) Serial.print(" ");
    }
    Serial.print("] | État: ");
    
    // Interprétation pour le moniteur série
    if (debit == 0 || debit == 10) {
      Serial.println("ARRÊTÉ (signal stable)");
    } else if (debit >= 4 && debit <= 6) {
      Serial.println("DÉBIT MAXIMUM (alternance rapide)");
    } else {
      Serial.println("DÉBIT ACTIF");
    }
  }
}

/*
 * ============================================================================
 * NOTES TECHNIQUES
 * ============================================================================
 * 
 * COMMENT INTERPRÉTER LES VALEURS:
 * 
 * 1. Signal stable (pas de débit):
 *    - debit = 0 (tous les échantillons à 1) ou debit = 10 (tous à 0)
 *    - Buffer: [0 0 0 0 0 0 0 0 0 0] ou [1 1 1 1 1 1 1 1 1 1]
 *    - Le signal ne change pas car il n'y a pas de rotation
 * 
 * 2. Débit faible:
 *    - debit entre 1-3 ou 7-9
 *    - Buffer: [0 0 0 1 1 1 1 0 0 0] (alternance lente)
 *    - Le débitmètre tourne lentement, peu d'alternances par seconde
 * 
 * 3. Débit élevé:
 *    - debit proche de 5
 *    - Buffer: [0 1 0 1 0 1 0 1 0 1] (alternance rapide)
 *    - Le débitmètre tourne vite, alternances à chaque échantillon
 * 
 * CALIBRATION:
 * - Ce code compte les alternances mais ne calcule pas de litres/minute
 * - Pour convertir en L/min, il faudrait connaître le coefficient du débitmètre
 * - Exemple typique YF-S201: ~7.5 impulsions par litre (coefficient K=7.5)
 * - Formule: Débit (L/min) = Fréquence (Hz) / K * 60
 * 
 * ADAPTATION FUTURE:
 * - Pour une mesure précise en L/min, utiliser attachInterrupt() sur RISING
 * - Compter le nombre d'impulsions sur une période donnée
 * - Appliquer le coefficient K du débitmètre utilisé
 * 
 * ============================================================================
 */

