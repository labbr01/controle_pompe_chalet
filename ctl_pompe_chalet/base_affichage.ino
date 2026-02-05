#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
// === Communication RF (structure, mais pas d'envoi RF pour l'instant) ===
#define CE_PIN 4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const byte adresse[6] = "00001";
const byte adresse_reponse[6] = "00002";
static int seq = 1;


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
    // Initialisation du module radio (structure, pas d'envoi RF pour l'instant)
    radio.begin();
    radio.openWritingPipe(adresse);
    radio.openReadingPipe(1, adresse_reponse);
    radio.setPALevel(RF24_PA_LOW);
    radio.stopListening();
  pinMode(PIN_RELAIS_POMPE, OUTPUT);
  pinMode(PIN_RELAIS_PURGE, OUTPUT);
  pinMode(PIN_BOUTON_RESET, INPUT_PULLUP);
  // Relais NC : LOW = pompe ON, HIGH = pompe OFF
  digitalWrite(PIN_RELAIS_POMPE, LOW);
  digitalWrite(PIN_RELAIS_PURGE, LOW);
  Serial.begin(9600);
  Serial.println("Demarrage minimal OK");
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

// --- Ajout logique protocole communication + astérisque LCD ---
unsigned long lastMsgSent = 0;
const unsigned long INTERVAL_PING = 1000; // 1s

unsigned long lastAsterisk = 0;
const unsigned long ASTERISK_DURATION = 90000UL; // 1 min 30 s
bool showAsterisk = false;

// Appeler cette fonction lors de la réception d'un message/ACK RF
void onMessageRecu() {
  lastAsterisk = millis();
  showAsterisk = true;
  lcd.setCursor(19, 3);
  lcd.print("*");
}

// Calcule le checksum (somme ASCII modulo 256, retourne 2 caractères hex)
String calcChecksum(const char* msg) {
  unsigned int sum = 0;
  for (size_t i = 0; msg[i] != '\0'; i++) sum += (unsigned char)msg[i];
  char hex[3];
  snprintf(hex, sizeof(hex), "%02X", sum & 0xFF);
  return String(hex);
}

void loop() {
    // Construction du message compact pour le chalet
    unsigned int debitActif = 0;
    for (int i = 0; i < NBUF; i++) {
      if (bufDebit[i] == 1) debitActif++;
    }
    bool etatPompe = (debitActif > 0 && debitActif < NBUF);
    int debitActifMsg = (debitActif > 0) ? 1 : 0;
    int idxPompe = pauseThermiqueActive ? 5 : (etatPompe ? 4 : 5);
    int idxPompage = etatPompe ? 0 : 1;
    int airCount = 0;
    for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];
    int idxAir = (airCount > 10) ? 0 : 1;
    int idxValve = 6; // Chalet
    static char lastMsgState[32] = "";
    char msgState[32];
    snprintf(msgState, sizeof(msgState), "%d|%d|%d|%d|%02d", idxPompe, idxAir, idxPompage, idxValve, debitActifMsg);

    // Détection de changement d'état capteur (hors astérisque écran)
    bool capteurChange = (strcmp(msgState, lastMsgState) != 0);
    unsigned long now = millis();
    bool doitEmettre = false;
    if (capteurChange) {
      seq++;
      if (seq > 9999) seq = 1;
      doitEmettre = true;
    } else if (now - lastMsgSent >= INTERVAL_PING) {
      doitEmettre = true; // ping périodique
    }

    if (doitEmettre) {
      char msg[48];
      snprintf(msg, sizeof(msg), "%04d|%s", seq, msgState);
      String msgStr = String(msg);
      String chk = calcChecksum(msg);
      msgStr += "|" + chk;
      Serial.print("[A émettre au chalet] ");
      Serial.println(msgStr);
      if (capteurChange) {
        strncpy(lastMsgState, msgState, sizeof(lastMsgState));
        lastMsgState[sizeof(lastMsgState)-1] = '\0';
      }
      lastMsgSent = now;
      // L'astérisque n'est plus affichée à l'émission
    }
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
  // Acquisition débit et sonde IR (air)
  if (now - lastSample >= INTERVAL_SAMPLE) {
    lastSample = now;
    int val = digitalRead(PIN_DEBIT);
    bufDebit[idxBuf] = val;
    // Lecture sonde IR et mise à jour du buffer air
    int etatIR = digitalRead(PIN_SONDE_IR);
    bufAir[idxBufAir] = (etatIR < 1) ? 1 : 0; // IR bas = Air
    idxBufAir = (idxBufAir + 1) % NBUF_AIR;
    idxBuf = (idxBuf + 1) % NBUF;
  }
  if (now - lastAffichage >= INTERVAL_AFFICHAGE) {
    lastAffichage = now;
    // Calcul du débit actif
    unsigned int debitActif = 0;
    for (int i = 0; i < NBUF; i++) {
      if (bufDebit[i] == 1) debitActif++;
    }
    bool etatPompe = (debitActif > 0 && debitActif < NBUF);
    static unsigned long lastMinuteTick = 0;
    if (etatPompe) {
      if (debutPompage == 0) debutPompage = now;
      if (now - lastMinuteTick >= 60000UL) {
        minutesPompageConsecutives++;
        lastMinuteTick = now;
      }
      if (minutesPompageConsecutives >= MAX_MINUTES_POMPAGE) {
        pauseThermiqueActive = true;
        debutPauseThermique = now;
        digitalWrite(PIN_RELAIS_POMPE, HIGH);
        for (int i = 0; i < NBUF; i++) bufDebit[i] = 0;
        idxBuf = 0;
        return;
      }
    } else {
      debutPompage = 0;
      lastMinuteTick = now;
      minutesPompageConsecutives = 0;
    }
    int airCount = 0;
    for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];
    char airCountStr[3];
    snprintf(airCountStr, sizeof(airCountStr), "%02d", airCount > 99 ? 99 : airCount);

    // Buffers pour éviter les rafraîchissements inutiles
    static char prevLigne0[21] = "";
    static char prevLigne1[21] = "";
    static char prevLigne2[21] = "";
    static char prevLigne3[21] = "";

    char ligne0[21];
    snprintf(ligne0, sizeof(ligne0), "%s%s %s%s %2s", LABEL_POMPE, statusText[4], LABEL_AIR, statusText[1], airCountStr);
    ligne0[20] = '\0';
    if (strcmp(ligne0, prevLigne0) != 0) {
      lcd.setCursor(0, 0); lcd.print(ligne0);
      strncpy(prevLigne0, ligne0, sizeof(prevLigne0));
      prevLigne0[sizeof(prevLigne0)-1] = '\0';
    }

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
    if (strcmp(ligne1, prevLigne1) != 0) {
      lcd.setCursor(0, 1); lcd.print(ligne1);
      strncpy(prevLigne1, ligne1, sizeof(prevLigne1));
      prevLigne1[sizeof(prevLigne1)-1] = '\0';
    }

    char ligne2[21];
    snprintf(ligne2, 21, "%s%s", LABEL_VALVE, statusText[6]);
    if (strcmp(ligne2, prevLigne2) != 0) {
      lcd.setCursor(0, 2); lcd.print(ligne2);
      strncpy(prevLigne2, ligne2, sizeof(prevLigne2));
      prevLigne2[sizeof(prevLigne2)-1] = '\0';
    }

    char ligne3[21];
    snprintf(ligne3, 21, "%s %2d/%d%10s", LABEL_DEBIT, debitActif, NBUF, "");
    ligne3[20] = '\0';
    if (strcmp(ligne3, prevLigne3) != 0) {
      lcd.setCursor(0, 3); lcd.print(ligne3);
      strncpy(prevLigne3, ligne3, sizeof(prevLigne3));
      prevLigne3[sizeof(prevLigne3)-1] = '\0';
    }

    // Efface l'astérisque si le délai est écoulé
    if (showAsterisk && (now - lastAsterisk > ASTERISK_DURATION)) {
      lcd.setCursor(19, 3);
      lcd.print(" ");
      showAsterisk = false;
    }
  }
}
