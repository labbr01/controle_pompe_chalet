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
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("En attente comm...");
  pinMode(6, INPUT_PULLUP); // CMD1
  pinMode(7, INPUT_PULLUP); // CMD2
  pinMode(8, INPUT_PULLUP); // CMD3
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

  static int lastSeq = -2;
  static int repeatCount = 0;
  static char lastCmd[8] = "";
  static char lignes[4][21] = {"", "", "", ""};
  char buf[21];

  static bool firstReception = false;
  if (radio.available()) {
    if (!firstReception) {
      lcd.clear();
      firstReception = true;
    }
    char msg[64] = "";
    radio.read(&msg, sizeof(msg));
    // Parse le message : SEQ|L1|L2|L3|L4
    int seq = 0;
    char l1[21] = "", l2[21] = "", l3[21] = "", l4[21] = "";
    char *token = strtok(msg, "|");
    if (token) seq = atoi(token);
    token = strtok(NULL, "|"); if (token) strncpy(l1, token, 20);
    token = strtok(NULL, "|"); if (token) strncpy(l2, token, 20);
    token = strtok(NULL, "|"); if (token) strncpy(l3, token, 20);
    token = strtok(NULL, "|"); if (token) strncpy(l4, token, 20);
    l1[20] = l2[20] = l3[20] = l4[20] = '\0';

    // Affiche les 4 lignes reçues
    strncpy(lignes[0], l1, 20); lignes[0][20] = '\0';
    strncpy(lignes[1], l2, 20); lignes[1][20] = '\0';
    strncpy(lignes[2], l3, 20); lignes[2][20] = '\0';
    strncpy(lignes[3], l4, 20); lignes[3][20] = '\0';

    // Détecte les signaux sur D6, D7, D8
    char cmd[8] = "";
    if (digitalRead(6) == LOW) strcat(cmd, "CMD1");
    if (digitalRead(7) == LOW) strcat(cmd, "CMD2");
    if (digitalRead(8) == LOW) strcat(cmd, "CMD3");
    if (cmd[0] == '\0') strncpy(cmd, "NONE", 7);

    // Si même séquence, incrémente le compteur de répétition
    if (seq == lastSeq) {
      repeatCount++;
    } else {
      repeatCount = 0;
      lastSeq = seq;
      strncpy(lastCmd, cmd, 7); lastCmd[7] = '\0';
    }

    // Si 10 répétitions, demande SYNC
    char reponse[32] = "";
    if (repeatCount >= 10) {
      snprintf(reponse, sizeof(reponse), "-1|SYNC");
    } else {
      snprintf(reponse, sizeof(reponse), "%d|%s", seq, lastCmd);
    }

    // Répond au maître
    radio.stopListening();
    delay(5);
    radio.write(&reponse, sizeof(reponse));
    radio.startListening();

    // Affichage LCD unique ici :
    for (int i = 0; i < 4; i++) {
      lcd.setCursor(0, i); lcd.print(lignes[i]);
    }
    // Ligne 0 : Statut
    snprintf(buf, 21, "%2d RX:%4d CMD:%-7s", sec, seq, lastCmd);
    lcd.setCursor(0,0); lcd.print(buf); lcd.print("   ");
    // Ligne 1 : L1
    snprintf(buf, 21, "%s", lignes[0]);
    lcd.setCursor(0,1); lcd.print(buf); lcd.print("   ");
    // Ligne 2 : L2
    snprintf(buf, 21, "%s", lignes[1]);
    lcd.setCursor(0,2); lcd.print(buf); lcd.print("   ");
    // Ligne 3 : L3
    snprintf(buf, 21, "%s", lignes[2]);
    lcd.setCursor(0,3); lcd.print(buf); lcd.print("   ");
  }
  delay(200);
}
