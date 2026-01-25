/*
 * Version LCD I2C 20x4 (Freenove LCD2004)
 * Remplace OLED par LCD I2C robuste
 * 
 * Branchement LCD I2C:
 * - SDA → A4
 * - SCL → A5
 * - VCC → 5V
 * - GND → GND
 * 
 * IMPORTANT: Installer la librairie "LiquidCrystal I2C" par Frank de Brabander
 * (Menu: Croquis → Inclure une bibliothèque → Gérer les bibliothèques → chercher "LiquidCrystal I2C")
 */

// ============================================================================
// BIBLIOTHÈQUES
// ============================================================================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============================================================================
// CONFIGURATION MATÉRIELLE
// ============================================================================

// LCD I2C 20x4 (adresse 0x27 ou 0x3F selon le module)
LiquidCrystal_I2C lcd(0x27, 20, 4); // Essayez 0x3F si 0x27 ne fonctionne pas

// --- Capteurs ---
const int PIN_PRESSION = A0;
const int PIN_AMPERAGE = A1;
const int PIN_DEBIT = 6;

// --- Relais ---
const int RELAIS_PURGE = 8;
const int RELAIS_POMPE = 7;

// ============================================================================
// PARAMÈTRES
// ============================================================================
const unsigned long MAX_POMPE = 30UL * 60UL * 1000UL;
const unsigned long TEMPS_REPOS = 5UL * 60UL * 1000UL;
const int SEUIL_PRESSION_PURGE = 125;
const unsigned long DUREE_PURGE = 20UL * 1000UL;

// ============================================================================
// BUFFERS
// ============================================================================
const int TAILLE_PILE = 10;
const int N_LISSAGE = 10;
const int NB_COND_PURGE = 10;

int pression = 0;
unsigned int debit = 0;
int amperage = 0;

bool pompeActive = false;
bool reposActive = false;
unsigned long debutPompe = 0;
unsigned long debutRepos = 0;

bool purgeActive = false;
unsigned long debutPurge = 0;
volatile bool purgePulseEnCours = false;

int pileEtat[TAILLE_PILE] = {0};
int indexPile = 0;

int ampBuffer[N_LISSAGE] = {0};
int ampIndex = 0;

bool condPurgeBuffer[NB_COND_PURGE] = {0};
int condPurgeIndex = 0;
int nbTrue = 0;

bool firstPrint = true;
bool traceInit = false;

void setup() {
  Serial.begin(9600);
  
  // Initialisation LCD I2C
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Demarrage...");
  
  Serial.println("[INIT] LCD I2C 20x4 OK");
  
  // Initialisation relais
  pinMode(RELAIS_PURGE, OUTPUT);
  pinMode(RELAIS_POMPE, OUTPUT);
  
  // Test relais purge
  digitalWrite(RELAIS_PURGE, HIGH);
  Serial.println("[TEST] RELAIS_PURGE HIGH (5s)");
  lcd.setCursor(0, 1);
  lcd.print("Test relais...");
  delay(5000);
  digitalWrite(RELAIS_PURGE, LOW);
  Serial.println("[TEST] RELAIS_PURGE LOW");
  
  digitalWrite(RELAIS_POMPE, LOW);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Systeme pret");
  delay(2000);
}

void gestionAutomatique() {
  if (purgePulseEnCours) return;
  
  unsigned long now = millis();
  
  // Attente remplissage buffers
  if (indexPile < TAILLE_PILE || ampIndex < N_LISSAGE) {
    if (digitalRead(RELAIS_PURGE) != LOW) {
      digitalWrite(RELAIS_PURGE, LOW);
    }
    if (digitalRead(RELAIS_POMPE) != LOW) {
      digitalWrite(RELAIS_POMPE, LOW);
    }
    if (!traceInit) {
      Serial.println("[INIT] Remplissage buffers...");
      traceInit = true;
    }
    return;
  }
  
  bool demandePompe = (debit != 0);
  bool demandePurge = (nbTrue >= 5);
  
  // Gestion purge active
  static bool purgeJustStarted = false;
  static bool pompeCoupeeApresPurge = false;
  if (purgeActive) {
    if (!purgeJustStarted) {
      Serial.println("[DEBUT PURGE]");
      purgeJustStarted = true;
      pompeCoupeeApresPurge = false;
    }
    
    if (digitalRead(RELAIS_PURGE) != HIGH) {
      digitalWrite(RELAIS_PURGE, HIGH);
    }
    
    if (!pompeCoupeeApresPurge) {
      if (digitalRead(RELAIS_POMPE) != LOW) {
        digitalWrite(RELAIS_POMPE, LOW);
      }
    }
    
    if (debit > 0 || pression > SEUIL_PRESSION_PURGE) {
      purgeActive = false;
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[FIN PURGE] OK");
      purgeJustStarted = false;
      lcd.clear();
      lcd.setCursor(0, 1);
      lcd.print("Purge terminee");
      return;
    }
    
    if ((now - debutPurge) >= 15000 && !pompeCoupeeApresPurge) {
      digitalWrite(RELAIS_POMPE, HIGH);
      pompeCoupeeApresPurge = true;
      Serial.println("[SECURITE] Pompe coupee");
    }
    
    if (now - debutPurge >= DUREE_PURGE) {
      purgeActive = false;
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[FIN PURGE] Timeout");
      purgeJustStarted = false;
      lcd.clear();
      lcd.setCursor(0, 1);
      lcd.print("Purge timeout");
      return;
    }
    
    lcd.setCursor(0, 1);
    lcd.print("PURGE EN COURS");
    return;
  }
  
  if (demandePurge) {
    Serial.print("[COND PURGE] nbTrue: ");
    Serial.println(nbTrue);
  }
  
  // Gestion repos
  if (reposActive) {
    if (now - debutRepos >= TEMPS_REPOS) {
      reposActive = false;
    } else {
      digitalWrite(RELAIS_POMPE, HIGH);
      digitalWrite(RELAIS_PURGE, LOW);
      return;
    }
  }
  
  // Gestion pompe
  if (demandePompe) {
    if (!pompeActive) {
      debutPompe = now;
      pompeActive = true;
    }
    
    if (now - debutPompe >= MAX_POMPE) {
      digitalWrite(RELAIS_POMPE, LOW);
      digitalWrite(RELAIS_PURGE, LOW);
      reposActive = true;
      debutRepos = now;
      pompeActive = false;
      return;
    }
    
    digitalWrite(RELAIS_POMPE, LOW);
    digitalWrite(RELAIS_PURGE, LOW);
  } else {
    digitalWrite(RELAIS_POMPE, HIGH);
    digitalWrite(RELAIS_PURGE, LOW);
    pompeActive = false;
  }
  
  // Activation purge
  if (demandePurge && !purgeActive && !reposActive) {
    if (indexPile >= TAILLE_PILE && ampIndex >= N_LISSAGE) {
      Serial.println("[ACTION] Purge activee");
      purgeActive = true;
      debutPurge = now;
      purgeJustStarted = false;
      return;
    }
  }
}

void loop() {
  gestionAutomatique();
  
  static unsigned long lastMesure = 0;
  unsigned long now = millis();
  
  pression = analogRead(PIN_PRESSION);
  
  ampBuffer[ampIndex] = analogRead(PIN_AMPERAGE);
  ampIndex = (ampIndex + 1) % N_LISSAGE;
  long ampSum = 0;
  for (int i = 0; i < N_LISSAGE; i++) ampSum += ampBuffer[i];
  amperage = ampSum / N_LISSAGE;
  
  int currentState = digitalRead(PIN_DEBIT);
  pileEtat[indexPile] = currentState;
  indexPile = (indexPile + 1) % TAILLE_PILE;
  
  static bool pulsePurgeActive = false;
  static unsigned long pulsePurgeStart = 0;
  
  if (now - lastMesure >= 1000) {
    lastMesure = now;
    
    int nbZero = 0;
    for (int i = 0; i < TAILLE_PILE; i++) {
      if (pileEtat[i] == 0) nbZero++;
    }
    debit = nbZero;
    
    bool condPurge = (pression <= SEUIL_PRESSION_PURGE && (debit == 0 || debit == 1000));
    condPurgeBuffer[condPurgeIndex] = condPurge;
    condPurgeIndex = (condPurgeIndex + 1) % NB_COND_PURGE;
    nbTrue = 0;
    for (int i = 0; i < NB_COND_PURGE; i++) {
      if (condPurgeBuffer[i]) nbTrue++;
    }
    
    Serial.print("[COND_PURGE] ");
    Serial.print(nbTrue);
    Serial.print("/");
    Serial.print(NB_COND_PURGE);
    Serial.print(" | ");
    Serial.println(condPurge ? "O" : "N");
    
    if (nbTrue >= 5 && !pulsePurgeActive) {
      Serial.println("[ACTION] Pulse purge 10s");
      digitalWrite(RELAIS_PURGE, HIGH);
      pulsePurgeActive = true;
      pulsePurgeStart = now;
      purgePulseEnCours = true;
    }
    
    static int lastPrintPression = -1000, lastPrintDebit = -1000, lastPrintAmp = -1000;
    int deltaPression = abs(pression - lastPrintPression);
    int deltaDebit = abs(debit - lastPrintDebit);
    int deltaAmp = abs(amperage - lastPrintAmp);
    
    if (firstPrint || deltaPression > 10 || deltaDebit > 10 || deltaAmp > 10) {
      Serial.print("P:");
      Serial.print(pression);
      Serial.print(" D:");
      Serial.print(debit);
      Serial.print(" A:");
      Serial.println(amperage);
      
      lastPrintPression = pression;
      lastPrintDebit = debit;
      lastPrintAmp = amperage;
      firstPrint = false;
    }
    
    // Affichage LCD I2C 20x4
    lcd.clear();
    
    // Ligne 1: Pression
    lcd.setCursor(0, 0);
    lcd.print("Pression: ");
    lcd.print(pression);
    
    // Ligne 2: Debit
    lcd.setCursor(0, 1);
    lcd.print("Debit: ");
    lcd.print(debit);
    
    // Ligne 3: Amperage
    lcd.setCursor(0, 2);
    lcd.print("Amperage: ");
    lcd.print(amperage);
    
    // Ligne 4: Statut
    lcd.setCursor(0, 3);
    if (purgeActive) {
      lcd.print("*** PURGE ***");
    } else if (reposActive) {
      lcd.print("Repos pompe");
    } else if (pompeActive) {
      lcd.print("Pompe active");
    } else {
      lcd.print("En attente");
    }
  }
  
  // Gestion pulse purge
  static bool pulsePurgeWasActive = false;
  if (pulsePurgeActive) {
    if (now - pulsePurgeStart >= 10000UL) {
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[ACTION] Fin pulse");
      pulsePurgeActive = false;
      pulsePurgeWasActive = true;
      purgePulseEnCours = false;
    }
  } else if (pulsePurgeWasActive && nbTrue < 5) {
    pulsePurgeWasActive = false;
  }
}
