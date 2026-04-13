// =============================
// ctl_affiche_chalet_v2.ino
// Afficheur LCD pour protocole compact SSSAPDCMM (compatible ctl_pompe_chalet_v2)
// =============================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <nRF24L01.h>
#include <RF24.h>

// =============================
// FILAGE HARMONISÉ AFFICHEUR (identique au contrôleur)
// Boutons (4 boutons, reset = dernier) :
//   - BOUTON 1 : A2 (utilisé en digital)
//   - BOUTON 2 : D10
//   - BOUTON 3 : D3
//   - BOUTON 4 (RESET) : D9
// Sonde IR : D2
// I2C LCD : SDA/SCL (pins dédiées Uno R4 Minima)
// nRF24L01 : CE = D4, CSN = D5
// =============================

// LCD 20x4 I2C
LiquidCrystal_I2C lcd(0x27, 20, 4);

// RF24 : CE = 4, CSN = 5 (adapter si besoin)
#define CE_PIN 4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const uint8_t adresse[6] = "00001"; // Doit matcher le maître

const int POMPE_MAX_CONSEC_MIN = 6; // Doit matcher le maître

// Statuts texte (doivent matcher le maître)
const char* statusText[] = {"Oui", "Non", "Oui*", "Non*", "On", "Off", "Chalet", "Purge"};

// =============================
// Définition des pins boutons (harmonisé avec contrôleur)
const int PIN_BOUTON_1 = A2;  // A2 (digital)
const int PIN_BOUTON_2 = 10;  // D10
const int PIN_BOUTON_3 = 3;   // D3
const int PIN_BOUTON_4 = 9;   // D9 (RESET)
// Sonde IR : D2 (à lire côté contrôleur)
// =============================

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("En attente RF...");

  // Initialisation boutons (pullup interne)
  pinMode(PIN_BOUTON_1, INPUT_PULLUP);
  pinMode(PIN_BOUTON_2, INPUT_PULLUP);
  pinMode(PIN_BOUTON_3, INPUT_PULLUP);
  pinMode(PIN_BOUTON_4, INPUT_PULLUP);

  if (!radio.begin()) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("NRF24L01 FAIL");
    while (1);
  }
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.openReadingPipe(0, adresse);
  radio.startListening();
}

void loop() {
  // Lecture boutons (LOW = appuyé)
  bool bouton1 = (digitalRead(PIN_BOUTON_1) == LOW); // A2
  bool bouton2 = (digitalRead(PIN_BOUTON_2) == LOW); // D10
  bool bouton3 = (digitalRead(PIN_BOUTON_3) == LOW); // D3
  bool bouton4 = (digitalRead(PIN_BOUTON_4) == LOW); // D9 (RESET)

  // TODO : Ajouter la logique d'action sur appui bouton si besoin

  if (radio.available()) {
    char msg[16] = "";
    radio.read(&msg, sizeof(msg));
    // Format attendu : SSSAPDCMM\0
    int seq = 0, idxAir = 0, idxPompe = 0, debit = 0, courant = 0, minPompe = 0;
    if (strlen(msg) >= 9) {
      char tmp[4] = "";
      strncpy(tmp, msg, 3); tmp[3] = '\0'; seq = atoi(tmp);
      idxAir = msg[3] - '0';
      idxPompe = msg[4] - '0';
      debit = msg[5] - '0';
      courant = msg[6] - '0';
      strncpy(tmp, msg+7, 2); tmp[2] = '\0'; minPompe = atoi(tmp);
    }
    // Affichage LCD
    lcd.clear();
    char ligne0[21];
    snprintf(ligne0, 21, "Pompe:%s Air:%s", statusText[idxPompe], statusText[idxAir]);
    lcd.setCursor(0,0); lcd.print(ligne0);
    char ligne1[21];
    snprintf(ligne1, 21, "Pompage:%s %02d/%02d", statusText[idxPompe], minPompe, POMPE_MAX_CONSEC_MIN);
    lcd.setCursor(0,1); lcd.print(ligne1);
    char ligne2[21];
    snprintf(ligne2, 21, "Courant:%dA Debit:%d", courant, debit);
    lcd.setCursor(0,2); lcd.print(ligne2);
    char ligne3[21];
    snprintf(ligne3, 21, "Seq:%03d", seq);
    lcd.setCursor(0,3); lcd.print(ligne3);
    Serial.print("[RF] Recu: "); Serial.println(msg);
  }
  delay(200);
}
