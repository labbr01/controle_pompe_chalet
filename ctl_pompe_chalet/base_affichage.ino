// ====================
// PARAMÈTRES DE TEST (protection thermique accélérée)
const int POMPE_MAX_CONSEC_MIN = 6;
const int POMPE_MAX_HR_MIN = 9;
const int POMPE_HR_WINDOW_MIN = 18;
const int POMPE_PAUSE_MIN = 3;
// ====================
const unsigned long DELAI_AIR_OUI = 15000;
const unsigned long DELAI_AIR_OUIE = 45000;
const unsigned long DELAI_AIR_NON = 15000;
const unsigned long DELAI_AIR_NONE = 45000;
const unsigned long DELAI_ANOMALIE_MAX = 60000;

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
const char* statusText[] = {"Oui", "Non", "Oui*", "Non*", "On", "Off", "Chalet", "Purge"};

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

void setup() {
  pinMode(PIN_RELAIS_POMPE, OUTPUT);
  pinMode(PIN_RELAIS_PURGE, OUTPUT);
  pinMode(PIN_BOUTON_RESET, INPUT_PULLUP);
  digitalWrite(PIN_RELAIS_POMPE, LOW);
  digitalWrite(PIN_RELAIS_PURGE, LOW);
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  delay(100);
  pinMode(PIN_SONDE_IR, INPUT);
  pinMode(PIN_DEBIT, INPUT);
  lcd.setCursor(0, 0);
  lcd.print("CTL CHALET (RF)");
}

void loop() {
  unsigned long now = millis();
  if (now - lastSample >= INTERVAL_SAMPLE) {
    lastSample = now;
    int val = digitalRead(PIN_DEBIT);
    bufDebit[idxBuf] = val;
    int raw = analogRead(PIN_COURANT);
    float tension = raw * 5.0 / 1023.0;
    float offset = 2.5;
    float sensibilite = 0.185;
    float courant = (tension - offset) / sensibilite;
    bufCourant[idxBuf] = courant;
    int etat = digitalRead(PIN_SONDE_IR);
    bufSondeIR[idxBuf] = etat;
    bufAir[idxBufAir] = (etat < 1) ? 1 : 0;
    idxBuf = (idxBuf + 1) % NBUF;
    idxBufAir = (idxBufAir + 1) % NBUF_AIR;
  }
  if (now - lastAffichage >= INTERVAL_AFFICHAGE) {
    lastAffichage = now;
    int hasZero = 0, hasOne = 0;
    for (int i = 0; i < NBUF; i++) {
      if (bufDebit[i] == 0) hasZero = 1;
      if (bufDebit[i] == 1) hasOne = 1;
    }
    bool etatPompage = (hasZero && hasOne);
    unsigned int debitActif = 0;
    if (hasZero && hasOne) {
      for (int i = 0; i < NBUF; i++) {
        if (bufDebit[i] == 1) debitActif++;
      }
    } else {
      debitActif = 0;
    }
    // AirCount LCD (2 dernières positions ligne 0)
    int airCount = 0;
    for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];
    char airCountStr[3] = "  ";
    snprintf(airCountStr, 3, "%02d", airCount > 99 ? 99 : airCount);

    // --- Gestion anomalie d'air et purge ---
    static bool anomalieAir = false;
    static unsigned long tDebutAnomalie = 0;
    static bool purgeActive = false;
    // Seuils : airCount > 20 = Oui, airCount > 60 = Oui*
    if (!anomalieAir && (airCount > 20)) {
      if (tDebutAnomalie == 0) tDebutAnomalie = now;
      if ((airCount > 20 && now - tDebutAnomalie > DELAI_AIR_OUI) || (airCount > 60 && now - tDebutAnomalie > DELAI_AIR_OUIE)) {
        anomalieAir = true;
        digitalWrite(PIN_RELAIS_PURGE, HIGH); // Purge ON
        purgeActive = true;
        Serial.println("[RELAIS] Purge ON");
      }
    }
    if (anomalieAir && airCount <= 5) {
      anomalieAir = false;
      tDebutAnomalie = 0;
      digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF
      purgeActive = false;
      Serial.println("[RELAIS] Purge OFF");
    }

    // Ligne 0 : Pompe:On Air:Non + airCount
    char ligne0[21];
    snprintf(ligne0, 19, "%s%s %s%s", LABEL_POMPE, "On", LABEL_AIR, "Non");
    int len0 = strlen(ligne0);
    for (int i = len0; i < 18; i++) ligne0[i] = ' ';
    ligne0[18] = airCountStr[0];
    ligne0[19] = airCountStr[1];
    ligne0[20] = '\0';
    lcd.setCursor(0, 0); lcd.print(ligne0);
    // Ligne 1 : Pompage:Oui/Non + compteur minutes (aligné à droite)
    static unsigned int minutesPompageConsecutives = 0;
    static unsigned long debutPompage = 0;
    if (etatPompage) {
      if (debutPompage == 0) debutPompage = now;
      unsigned int newMinutes = (now - debutPompage) / 60000UL;
      if (newMinutes != minutesPompageConsecutives) minutesPompageConsecutives = newMinutes;
    } else {
      debutPompage = 0;
      minutesPompageConsecutives = 0;
    }
    char compteurFinal[6] = "     ";
    char ligneBase[16] = "";
    if (etatPompage) {
      snprintf(compteurFinal, 6, "%02u/%02u", minutesPompageConsecutives, POMPE_MAX_CONSEC_MIN);
      snprintf(ligneBase, 16, "%s%s", LABEL_POMPAGE, "Oui");
      while (strlen(ligneBase) < 15) strcat(ligneBase, " ");
      char ligne1[21];
      snprintf(ligne1, 21, "%s%s", ligneBase, compteurFinal);
      lcd.setCursor(0, 1); lcd.print(ligne1);
    } else {
      char ligne1[21];
      snprintf(ligne1, 21, "%s%s", LABEL_POMPAGE, "Non");
      lcd.setCursor(0, 1); lcd.print(ligne1);
    }
    // Ligne 2 : Valve
    char ligne2[21];
    snprintf(ligne2, 21, "%s%s", LABEL_VALVE, purgeActive ? "Purge" : "Chalet");
    lcd.setCursor(0, 2); lcd.print(ligne2);
    // Ligne 3 : Débit
    char ligne3[21];
    snprintf(ligne3, 21, "%s %2d/%d%10s", LABEL_DEBIT, debitActif, NBUF, "");
    ligne3[20] = '\0';
    lcd.setCursor(0, 3); lcd.print(ligne3);
  }
}
