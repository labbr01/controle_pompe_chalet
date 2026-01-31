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

// Ajout pour incrémenter le message et recevoir la réponse
void loop() {
  static int compteur = 0;
  static char reponse[16] = "";
  int sec = (millis() / 1000) % 10;

  // Préparer message helloN
  char texte[16];
  snprintf(texte, sizeof(texte), "hello%d", compteur);

  // Passer en mode réception AVANT d'envoyer
  radio.stopListening();
  delay(2); // court délai pour stabilité

  // Envoyer le message
  bool ok = false;
  ok = radio.write(&texte, sizeof(texte));
  Serial.print("Envoi: ");
  Serial.print(texte);
  Serial.print(" | Statut: ");
  Serial.println(ok ? "OK" : "ECHEC");

  // Passer en mode réception pour attendre une réponse
  radio.startListening();
  unsigned long startWait = millis();
  bool recu = false;
  char bufRecu[32] = "";
  while (millis() - startWait < 500 && !recu) {
    if (radio.available()) {
      radio.read(&bufRecu, sizeof(bufRecu));
      recu = true;
    }
  }
  radio.stopListening();

  char buf[21];
  // Affichage toujours identique : ENV:OK REC:xxxxxxx
  char env[8], rec[11];
  snprintf(env, 8, "ENV:%s", ok ? "OK " : "ECHEC");
  env[7] = '\0';
  if (recu && strlen(bufRecu) > 0) {
    snprintf(rec, 11, "REC:%-7s", bufRecu);
  } else {
    snprintf(rec, 11, "REC:       ");
  }
  rec[10] = '\0';
  snprintf(buf, 21, "%2d %s%s", sec, env, rec);
  lcd.setCursor(0,0); lcd.print(buf);
  // Ligne 1 : Message envoyé
  snprintf(buf, 21, "%d Msg: %-12s", sec, texte);
  lcd.setCursor(0,1); lcd.print(buf); lcd.print("   ");
  // Ligne 2 : Uptime
  snprintf(buf, 21, "%d Uptime: %5lus", sec, millis()/1000);
  lcd.setCursor(0,2); lcd.print(buf); lcd.print("   ");
  // Ligne 3 : Attente RF (compteur)
  snprintf(buf, 21, "%d Attente: %5lu", sec, attenteRF);
  lcd.setCursor(0,3); lcd.print(buf); lcd.print("   ");

  attenteRF = (attenteRF + 1) % 10000;
  compteur = (compteur + 1) % 10000;
  delay(2000);
}
