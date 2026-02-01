// === Gabarits de lignes LCD ===
const char* LABEL_POMPE = "Pompe:";
const char* LABEL_AIR = "Air:";
const char* LABEL_POMPAGE = "Pompage:";
const char* LABEL_VALVE = "Valve:";
const char* LABEL_DEBIT = "Debit:";
// === Textes variables pour affichage et protocole compact ===
const char* statusText[] = {"Oui", "Non", "Oui*", "Non*", "On", "Off", "Chalet", "Purge"}; // 0=Oui, 1=Non, 2=Oui*, 3=Non*, 4=On, 5=Off, 6=Chalet, 7=Purge
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>

// Pour indexation future: On peut utiliser des codes numériques dans le protocole RF

#define CE_PIN 4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const byte adresse[6] = "00001";
const byte adresse_reponse[6] = "00002";

LiquidCrystal_I2C lcd(0x27, 20, 4);

// Bouton toggle debug (D10)
const int PIN_BOUTON_DEBUG = 10;
bool debugMode = false;
bool lastButtonState = true;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

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
  pinMode(PIN_BOUTON_DEBUG, INPUT_PULLUP);
  radio.begin();
  if (!radio.begin()) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("NRF24L01 FAIL");
    while (1);
  }
  radio.openReadingPipe(0, adresse);
  radio.openWritingPipe(adresse_reponse);
  radio.setPALevel(RF24_PA_LOW);
  radio.startListening();
}

static char lignes[4][21] = {"", "", "", ""};
static int lastSeq = -2;
static int repeatCount = 0;
static char lastCmd[8] = "";

void loop() {
  // Gestion bouton debug (toggle sur front descendant)
  bool buttonState = digitalRead(PIN_BOUTON_DEBUG);
  if (buttonState != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (lastButtonState && !buttonState) { // front descendant
      debugMode = !debugMode;
    }
  }
  lastButtonState = buttonState;

  static bool firstReception = false;
  if (radio.available()) {
    if (!firstReception) {
      lcd.clear();
      firstReception = true;
    }
    char msg[32] = "";
    radio.read(&msg, sizeof(msg));
    // Parse le message compact : SEQ|idxPompe|idxAir|idxPompage|idxValve|debitActif
    int seq = 0, idxPompe = 0, idxAir = 0, idxPompage = 0, idxValve = 0, debitActif = 0;
    char *token = strtok(msg, "|");
    if (token) seq = atoi(token);
    token = strtok(NULL, "|"); if (token) idxPompe = atoi(token);
    token = strtok(NULL, "|"); if (token) idxAir = atoi(token);
    token = strtok(NULL, "|"); if (token) idxPompage = atoi(token);
    token = strtok(NULL, "|"); if (token) idxValve = atoi(token);
    token = strtok(NULL, "|"); if (token) debitActif = atoi(token);

    // Détecte les signaux sur D6, D7, D8
    int codeRetour = 0;
    if (digitalRead(6) == LOW) codeRetour = 1;
    else if (digitalRead(7) == LOW) codeRetour = 2;
    else if (digitalRead(8) == LOW) codeRetour = 3;
    // Si demande de reset (bouton spécial ou logique), retourner 0000|0
    bool demandeReset = false;
    // Ajoutez ici la logique de reset si besoin (ex: bouton dédié)

    // Gestion répétition/sync
    if (seq == lastSeq) {
      repeatCount++;
    } else {
      repeatCount = 0;
      lastSeq = seq;
    }
    char reponse[16] = "";
    if (demandeReset) {
      snprintf(reponse, sizeof(reponse), "0000|0");
    } else if (repeatCount >= 10) {
      snprintf(reponse, sizeof(reponse), "-1|SYNC");
    } else {
      snprintf(reponse, sizeof(reponse), "%d|%d", seq, codeRetour);
    }
    radio.stopListening();
    delay(5);
    radio.write(&reponse, sizeof(reponse));
    radio.startListening();

    // Affichage LCD à partir des index
    char buf[21];
    if (debugMode) {
      snprintf(buf, 21, "DBG SEQ:%4d RET:%d", seq, codeRetour);
      lcd.setCursor(0,0); lcd.print(buf); lcd.print("   ");
    } else {
      snprintf(buf, 21, "%s%s %s%s", LABEL_POMPE, statusText[idxPompe], LABEL_AIR, statusText[idxAir]);
      lcd.setCursor(0,0); lcd.print(buf); lcd.print("   ");
    }
    snprintf(buf, 21, "%s%s %02d/06", LABEL_POMPAGE, statusText[idxPompage], debitActif); // Pompage + minutes
    lcd.setCursor(0,1); lcd.print(buf); lcd.print("   ");
    snprintf(buf, 21, "%s%s", LABEL_VALVE, statusText[idxValve]);
    lcd.setCursor(0,2); lcd.print(buf); lcd.print("   ");
    snprintf(buf, 21, "%s %2d/10", LABEL_DEBIT, debitActif);
    lcd.setCursor(0,3); lcd.print(buf); lcd.print("   ");
  }
  delay(200);
}
