/*
 * ============================================================================
 * Contrôleur Pompe Chalet - Structure modulaire, acquisition rapide, affichage capteurs
 * ============================================================================
 *
 * Cette version lit les capteurs (sonde IR, débitmètre, courant) toutes les 100ms,
 * calcule la moyenne/min/max sur 1s, puis affiche les valeurs sur le LCD 20x4.
 * Les fonctions de relais et communication sont présentes mais inactives.
 *
 * LCD I2C 20x4 :
 * - SDA → A4 (I2C)
 * - SCL → A5 (I2C)
 * - VCC → 5V
 * - GND → GND
 *
 * ============================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 20, 4);

// Broches capteurs (adapter si besoin)
const int PIN_SONDE_IR = 2;   // D2
const int PIN_DEBIT = 6;      // D6
const int PIN_COURANT = A1;   // A1

// Buffers pour 1 seconde (10 échantillons)
const int NBUF = 10;
int bufDebit[NBUF];
float bufCourant[NBUF];
int bufSondeIR[NBUF];
int idxBuf = 0;

// Moyennes/min/max sur 1s
float courantMoy = 0, courantMin = 0, courantMax = 0;
float debitMoy = 0, debitMin = 0, debitMax = 0;
float sondeIRMoy = 0;

// Timing
unsigned long lastSample = 0;
unsigned long lastAffichage = 0;
const unsigned long INTERVAL_SAMPLE = 100;   // 100ms
const unsigned long INTERVAL_AFFICHAGE = 1000; // 1s

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  delay(100);

  pinMode(PIN_SONDE_IR, INPUT);
  pinMode(PIN_DEBIT, INPUT);
  // PIN_COURANT = A1 (analogique)

  lcd.setCursor(0, 0);
  lcd.print("CTL CHALET (RF)");
}

void loop() {
  unsigned long now = millis();

  // Acquisition rapide (tous les 100ms)
  if (now - lastSample >= INTERVAL_SAMPLE) {
    lastSample = now;
    traiter_capteur_debimetre();
    traiter_capteur_courant();
    traiter_sonde_ir();
    idxBuf = (idxBuf + 1) % NBUF;
  }

  // Traitement et affichage toutes les secondes
  if (now - lastAffichage >= INTERVAL_AFFICHAGE) {
    lastAffichage = now;
    calculer_stats();
    maj_affichage();
    ajuster_relais_pompe();
    ajuster_relais_purge();
    communiquer_chalet();
  }
}

void traiter_capteur_debimetre() {
  int val = digitalRead(PIN_DEBIT);
  bufDebit[idxBuf] = val;
}

void traiter_capteur_courant() {
  int raw = analogRead(PIN_COURANT);
  float tension = raw * 5.0 / 1023.0;
  float offset = 2.5; // V (pour ACS712)
  float sensibilite = 0.185; // V/A (pour ACS712-5A)
  float courant = (tension - offset) / sensibilite;
  bufCourant[idxBuf] = courant;
}

void traiter_sonde_ir() {
  int etat = digitalRead(PIN_SONDE_IR);
  bufSondeIR[idxBuf] = etat;
}

void calculer_stats() {
  // Débit
  int sumD = 0, minD = 1, maxD = 0;
  for (int i = 0; i < NBUF; i++) {
    sumD += bufDebit[i];
    if (bufDebit[i] < minD) minD = bufDebit[i];
    if (bufDebit[i] > maxD) maxD = bufDebit[i];
  }
  debitMoy = sumD / (float)NBUF;
  debitMin = minD;
  debitMax = maxD;

  // Courant
  float sumC = 0, minC = bufCourant[0], maxC = bufCourant[0];
  for (int i = 0; i < NBUF; i++) {
    sumC += bufCourant[i];
    if (bufCourant[i] < minC) minC = bufCourant[i];
    if (bufCourant[i] > maxC) maxC = bufCourant[i];
  }
  courantMoy = sumC / NBUF;
  courantMin = minC;
  courantMax = maxC;

  // Sonde IR
  int sumIR = 0;
  for (int i = 0; i < NBUF; i++) sumIR += bufSondeIR[i];
  sondeIRMoy = sumIR / (float)NBUF;
}

void maj_affichage() {
  lcd.setCursor(0, 1);
  lcd.print("Eau: ");
  lcd.print(sondeIRMoy > 0.5 ? "OUI " : "NON ");
  lcd.print(" D: ");
  lcd.print(debitMoy, 2);
  lcd.print("   ");

  lcd.setCursor(0, 2);
  lcd.print("I: ");
  lcd.print(courantMoy, 2);
  lcd.print("A ");
  lcd.print("Min:");
  lcd.print(courantMin, 1);
  lcd.print(" Max:");
  lcd.print(courantMax, 1);

  lcd.setCursor(0, 3);
  lcd.print("Relais: P N/A  V N/A   ");
}

void ajuster_relais_pompe() {
  // À implémenter : logique de contrôle de la pompe
}

void ajuster_relais_purge() {
  // À implémenter : logique de contrôle de la purge
}

void communiquer_chalet() {
  // À implémenter : communication RF ou autre
}
