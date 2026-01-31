// test_comm_maitre.ino
// Arduino pompe (émetteur NRF24L01)
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

void lcdStatut(const char* statut, const char* msg, unsigned long uptime, unsigned long attente) {
  char buf[21];
  // Ligne 0 : Statut envoi
  snprintf(buf, 21, "0: Envoi: %-7s", statut);
  lcd.setCursor(0,0); lcd.print(buf); lcd.print("         ");
  // Ligne 1 : Message envoyé
  snprintf(buf, 21, "1: Msg: %-12s", msg);
  lcd.setCursor(0,1); lcd.print(buf); lcd.print("   ");
  // Ligne 2 : Uptime
  snprintf(buf, 21, "2: Uptime: %5lus", uptime);
  lcd.setCursor(0,2); lcd.print(buf); lcd.print("   ");
  // Ligne 3 : Attente RF (compteur)
  snprintf(buf, 21, "3: Attente: %5lu", attente);
  lcd.setCursor(0,3); lcd.print(buf); lcd.print("   ");
}

void setup() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcdStatut("---", "---", 0, 0);
  Serial.begin(9600);
  radio.begin();
  if (!radio.begin()) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("NRF24L01 FAIL");
    while (1); // Stop tout
  }
  radio.openWritingPipe(adresse);
  radio.openReadingPipe(1, adresse_reponse); // pipe 1 pour la réponse
  radio.setPALevel(RF24_PA_LOW);
  radio.stopListening();
  Serial.println("Emetteur pret");
}

unsigned long attenteRF = 0;

// Protocole handshake V1 : SEQ|L1|L2|L3|L4, réponse SEQ|CMDx, synchro -1|SYNC
void loop() {
  static int seq = 0;
  static int retry = 0;
  static char lastCmd[16] = "";
  static char lignes[4][21] = {"LIGNE 1", "LIGNE 2", "LIGNE 3", "LIGNE 4"};
  int sec = (millis() / 1000) % 10;

  // Préparer le message à envoyer
  char msg[64];
  snprintf(msg, sizeof(msg), "%d|%s|%s|%s|%s", seq, lignes[0], lignes[1], lignes[2], lignes[3]);

  // Handshake : tant que la réponse n'est pas correcte, on réémet
  bool handshake_ok = false;
  bool last_write_ok = false;
  char bufRecu[32] = "";
  int tentatives = 0;
  while (tentatives < 10) {
    radio.stopListening();
    delay(2);
    last_write_ok = radio.write(&msg, sizeof(msg));
    Serial.print("Envoi: "); Serial.print(msg); Serial.print(" | Statut: "); Serial.println(last_write_ok ? "OK" : "ECHEC");
    radio.startListening();
    unsigned long startWait = millis();
    bool recu = false;
    while (millis() - startWait < 500 && !recu) {
      if (radio.available()) {
        radio.read(&bufRecu, sizeof(bufRecu));
        recu = true;
      }
    }
    radio.stopListening();
    if (recu) {
      // Parse la réponse : SEQ|CMDx ou -1|SYNC
      int rseq = 0;
      char rcmd[16] = "";
      char *token = strtok(bufRecu, "|");
      if (token) rseq = atoi(token);
      token = strtok(NULL, "|");
      if (token) strncpy(rcmd, token, 15);
      rcmd[15] = '\0';
      if (rseq == seq) {
        strncpy(lastCmd, rcmd, 15); lastCmd[15] = '\0';
        handshake_ok = true;
        break; // bonne réponse, on sort
      } else if (rseq == -1) {
        // SYNC demandé
        snprintf(bufRecu, sizeof(bufRecu), "-1|SYNC");
        radio.stopListening(); delay(2); radio.write(&bufRecu, sizeof(bufRecu));
        seq = 0; retry = 0; // repart à zéro
        return;
      }
    }
    tentatives++;
    delay(100);
  }
  // Affichage LCD : statut + commande reçue
  char buf[21];
  char env[8], rec[11];
  snprintf(env, 8, "ENV:%s", handshake_ok ? "OK " : "ECHEC");
  env[7] = '\0';
  if (strlen(lastCmd) > 0) {
    snprintf(rec, 11, "CMD:%-7s", lastCmd);
  } else {
    snprintf(rec, 11, "CMD:       ");
  }
  rec[10] = '\0';
  snprintf(buf, 21, "%2d %s%s", sec, env, rec);
  lcd.setCursor(0,0); lcd.print(buf);
  // Affiche les 4 lignes
  for (int i = 0; i < 4; i++) {
    lcd.setCursor(0, i+1); lcd.print(lignes[i]);
  }
  attenteRF = (attenteRF + 1) % 10000;
  seq = (seq + 1) % 10000;
  delay(2000);
}
