// =============================
// ctl_affiche_chalet_v2.ino
// Afficheur LCD pour protocole compact SSSAPDCMM (compatible ctl_pompe_chalet_v2)
// =============================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

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

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("En attente RF...");
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
