// test_comm_client.ino
// Arduino client (récepteur NRF24L01)
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>

#define CE_PIN 4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const byte adresse[6] = "00001";
const byte adresse_reponse[6] = "00002";

LiquidCrystal_I2C lcd(0x27, 20, 4);

// Buffer pour log LCD (4 lignes de 20 caractères)
char logLcd[4][21] = {"", "", "", ""};
void lcdLog(const char* msg) {
  for (int i = 3; i > 0; i--) strncpy(logLcd[i], logLcd[i-1], 21);
  strncpy(logLcd[0], msg, 20);
  int len = strlen(msg);
  for (int i = len; i < 20; i++) logLcd[0][i] = ' ';
  logLcd[0][20] = '\0';
  for (int i = 0; i < 4; i++) {
    lcd.setCursor(0, i);
    lcd.print(logLcd[i]);
  }
}

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  lcdLog("CLIENT NRF24L01");
  lcdLog("LCD OK!");
  radio.begin();
  if (!radio.begin()) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("NRF24L01 FAIL");
    while (1); // Stop tout
}
  radio.openReadingPipe(0, adresse);
  radio.openWritingPipe(adresse_reponse); // pour répondre au maître
  radio.setPALevel(RF24_PA_LOW);
  radio.startListening();
}

int compteurLCD = 0;
unsigned long lastUpdate = 0;
unsigned long lastStatus = 0;

void loop() {
  // Compteur de secondes modulo 10
  int sec = (millis() / 1000) % 10;

  // Valeurs à afficher
  char statut[8] = "---";
  static unsigned long lastMsgTime = 0;
  static char lastTexte[15] = "---";

  // Lecture des données
  if (radio.available()) {
    char texte[32] = {0};
    radio.read(&texte, sizeof(texte));
    strncpy(lastTexte, texte, 14);
    lastTexte[14] = '\0';
    strcpy(statut, "RECU");
    lastMsgTime = millis();

    // Inverser le message reçu
    int len = strlen(lastTexte);
    char reponse[16] = "";
    for (int i = 0; i < len; i++) {
      reponse[i] = lastTexte[len - 1 - i];
    }
    reponse[len] = '\0';

    // Passer en mode émission pour répondre
    radio.stopListening();
    delay(5); // court délai pour stabilité
    radio.write(&reponse, sizeof(reponse));
    radio.startListening();
  } else {
    // Si pas de message depuis 5s, statut = "---"
    if (millis() - lastMsgTime > 5000) {
      strcpy(statut, "---");
      strcpy(lastTexte, "---");
    } else {
      strcpy(statut, "RECU");
    }
  }

  // Affichage LCD fixe avec compteur de secondes
  char buf[21];
  // Ligne 0 : Statut réception
  snprintf(buf, 21, "%d Reception: %-7s", sec, statut);
  lcd.setCursor(0,0); lcd.print(buf); lcd.print("         ");
  // Ligne 1 : Message reçu
  snprintf(buf, 21, "%d Msg: %-12s", sec, lastTexte);
  lcd.setCursor(0,1); lcd.print(buf); lcd.print("   ");
  // Ligne 2 : Uptime
  snprintf(buf, 21, "%d Uptime: %5lus", sec, millis()/1000);
  lcd.setCursor(0,2); lcd.print(buf); lcd.print("   ");
  // Ligne 3 : Attente RF (compteur)
  static unsigned long attenteRF = 0;
  snprintf(buf, 21, "%d Attente: %5lu", sec, attenteRF);
  lcd.setCursor(0,3); lcd.print(buf); lcd.print("   ");
  attenteRF = (attenteRF + 1) % 10000;

  delay(2000);
}
