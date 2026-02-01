

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 20, 4);

// === Gabarits de lignes LCD ===
const char* LABEL_POMPE = "Pompe:";
const char* LABEL_AIR = "Air:";
const char* LABEL_POMPAGE = "Pompage:";
const char* LABEL_VALVE = "Valve:";
const char* LABEL_DEBIT = "Debit:";
// === Textes variables pour affichage ===
const char* statusText[] = {"Oui", "Non", "Oui*", "Non*", "On ", "Off", "Chalet", "Purge"};

// Broches capteurs
const int PIN_SONDE_IR = 2;
const int PIN_DEBIT = 6;
const int PIN_COURANT = A1;
const int PIN_RELAIS_POMPE = 7;
const int PIN_RELAIS_PURGE = 8;
const int PIN_BOUTON_RESET = 9;

const int NBUF = 10;
int bufDebit[NBUF];
float bufCourant[NBUF];
int bufSondeIR[NBUF];
int idxBuf = 0;
const int NBUF_AIR = 100;
int bufAir[NBUF_AIR];
int idxBufAir = 0;

unsigned long lastSample = 0;
unsigned long lastAffichage = 0;
const unsigned long INTERVAL_SAMPLE = 10;
const unsigned long INTERVAL_AFFICHAGE = 1000;

// Protection thermique
const unsigned int MAX_MINUTES_POMPAGE = 2; // Pour debug, mettre 20 en réel
const unsigned int MINUTES_PAUSE_THERMIQUE = 1; // Pour debug, mettre 10 en réel
unsigned int minutesPompageConsecutives = 0;
unsigned long debutPompage = 0;
bool pauseThermiqueActive = false;
unsigned long debutPauseThermique = 0;

void setup() {
  pinMode(PIN_RELAIS_POMPE, OUTPUT);
  pinMode(PIN_RELAIS_PURGE, OUTPUT);
  pinMode(PIN_BOUTON_RESET, INPUT_PULLUP);
  // Relais NC : LOW = pompe ON, HIGH = pompe OFF
  digitalWrite(PIN_RELAIS_POMPE, LOW);
  digitalWrite(PIN_RELAIS_PURGE, LOW);
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  delay(100);
  pinMode(PIN_SONDE_IR, INPUT);
  pinMode(PIN_DEBIT, INPUT);
  // Initialisation explicite du buffer débit et index
  for (int i = 0; i < NBUF; i++) bufDebit[i] = 0;
  idxBuf = 0;
  lcd.setCursor(0, 0);
  lcd.print("CTL CHALET (RF)");
}

void loop() {
  unsigned long now = millis();
  // Protection thermique : si active, on attend la fin de la pause
  if (pauseThermiqueActive) {
    unsigned long tempsPause = now - debutPauseThermique;
    unsigned int minutesPause = tempsPause / 60000UL;
    if (tempsPause >= MINUTES_PAUSE_THERMIQUE * 60000UL) {
      // Fin de pause thermique
      pauseThermiqueActive = false;
      minutesPompageConsecutives = 0;
      debutPompage = 0;
      // Pompe ON
      digitalWrite(PIN_RELAIS_POMPE, LOW);
    } else {
      // Pompe OFF
      digitalWrite(PIN_RELAIS_POMPE, HIGH);
      // Affichage pendant la pause thermique
      char ligne0[21];
      snprintf(ligne0, sizeof(ligne0), "%s%s %s%s %2s", LABEL_POMPE, statusText[5], LABEL_AIR, statusText[1], "00");
      ligne0[20] = '\0';
      lcd.setCursor(0, 0); lcd.print(ligne0);
      char ligne1[21];
      char compteur[7];
      snprintf(compteur, sizeof(compteur), "%02u/%02u", minutesPause, MINUTES_PAUSE_THERMIQUE);
      int prefixLen = strlen(LABEL_POMPAGE) + strlen(statusText[3]);
      int spaces = 20 - prefixLen - strlen(compteur);
      if (spaces < 0) spaces = 0;
      snprintf(ligne1, sizeof(ligne1), "%s%s%*s%s", LABEL_POMPAGE, statusText[3], spaces, "", compteur);
      ligne1[20] = '\0';
      lcd.setCursor(0, 1); lcd.print(ligne1);
      char ligne2[21];
      snprintf(ligne2, 21, "%s%s", LABEL_VALVE, statusText[6]);
      lcd.setCursor(0, 2); lcd.print(ligne2);
      char ligne3[21];
      snprintf(ligne3, 21, "%s %2d/%d%10s", LABEL_DEBIT, 0, NBUF, "");
      ligne3[20] = '\0';
      lcd.setCursor(0, 3); lcd.print(ligne3);
      return;
    }
  }
  // Acquisition débit
  if (now - lastSample >= INTERVAL_SAMPLE) {
    lastSample = now;
    int val = digitalRead(PIN_DEBIT);
    bufDebit[idxBuf] = val;
    idxBuf = (idxBuf + 1) % NBUF;
  }
  if (now - lastAffichage >= INTERVAL_AFFICHAGE) {
    lastAffichage = now;
    // Calcul du débit actif
    unsigned int debitActif = 0;
    for (int i = 0; i < NBUF; i++) {
      if (bufDebit[i] == 1) debitActif++;
    }
    // Pompage actif si débit n'est ni 0/10 ni 10/10
    bool etatPompe = (debitActif > 0 && debitActif < NBUF);
    // Comptage des minutes consécutives de pompage
    static unsigned long lastMinuteTick = 0;
    if (etatPompe) {
      if (debutPompage == 0) debutPompage = now;
      if (now - lastMinuteTick >= 60000UL) {
        minutesPompageConsecutives++;
        lastMinuteTick = now;
      }
      // Déclenche la pause thermique si limite atteinte
      if (minutesPompageConsecutives >= MAX_MINUTES_POMPAGE) {
        pauseThermiqueActive = true;
        debutPauseThermique = now;
        // Pompe OFF
        digitalWrite(PIN_RELAIS_POMPE, HIGH);
        // Reset buffers débit
        for (int i = 0; i < NBUF; i++) bufDebit[i] = 0;
        idxBuf = 0;
        return;
      }
    } else {
      debutPompage = 0;
      lastMinuteTick = now;
      minutesPompageConsecutives = 0;
    }
    // Ligne 0 : Pompe:On/Off Air:Oui/Non + airCount (airCount fictif à 0)
    int airCount = 0;
    char airCountStr[3];
    snprintf(airCountStr, sizeof(airCountStr), "%02d", airCount); // Toujours 2 chiffres
    char ligne0[21];
    snprintf(ligne0, sizeof(ligne0), "%s%s %s%s %2s", LABEL_POMPE, statusText[4], LABEL_AIR, statusText[1], airCountStr);
    ligne0[20] = '\0'; // Sécurité
    lcd.setCursor(0, 0); lcd.print(ligne0);
    // Ligne 1 : Pompage:Oui 00/02 ou Non
    char ligne1[21];
    char compteur[7];
    if (etatPompe) {
      snprintf(compteur, sizeof(compteur), "%02u/%02u", minutesPompageConsecutives, MAX_MINUTES_POMPAGE);
      int prefixLen = strlen(LABEL_POMPAGE) + strlen(statusText[0]);
      int spaces = 20 - prefixLen - strlen(compteur);
      if (spaces < 0) spaces = 0;
      snprintf(ligne1, sizeof(ligne1), "%s%s%*s%s", LABEL_POMPAGE, statusText[0], spaces, "", compteur);
    } else {
      snprintf(compteur, sizeof(compteur), "%02u/%02u", 0, MAX_MINUTES_POMPAGE);
      int prefixLen = strlen(LABEL_POMPAGE) + strlen(statusText[1]);
      int spaces = 20 - prefixLen - strlen(compteur);
      if (spaces < 0) spaces = 0;
      snprintf(ligne1, sizeof(ligne1), "%s%s%*s%s", LABEL_POMPAGE, statusText[1], spaces, "", compteur);
    }
    ligne1[20] = '\0';
    lcd.setCursor(0, 1); lcd.print(ligne1);
    // Ligne 2 : Valve:Chalet
    char ligne2[21];
    snprintf(ligne2, 21, "%s%s", LABEL_VALVE, statusText[6]);
    lcd.setCursor(0, 2); lcd.print(ligne2);
    // Ligne 3 : Débit: x/10
    char ligne3[21];
    snprintf(ligne3, 21, "%s %2d/%d%10s", LABEL_DEBIT, debitActif, NBUF, "");
    ligne3[20] = '\0';
    lcd.setCursor(0, 3); lcd.print(ligne3);
  }
}
