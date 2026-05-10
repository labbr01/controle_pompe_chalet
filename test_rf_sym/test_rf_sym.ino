// test_rf_sym.ino
// Meme code sur les 2 Arduino, SAUF la ligne #define ROLE (changer 0 ou 1).
// Adresses asymetriques identiques au vrai protocole (00001 / 00002).
// AutoAck actif : write() retourne true seulement si l'autre a bien recu.
//
// Ligne 0 LCD : "RoleX Env:XXXX OK/ERR"
// Ligne 1 LCD : "Recu:  XXXX OK/ERR  "
// Ligne 2 LCD : "Prefixe: OK" ou affiche le prefixe recu
// Ligne 3 LCD : erreur ou instructions
// Bouton A2   : envoie le prochain message
//
// Branchement RF24 : CE=4, CSN=5

// *** CHANGER ICI AVANT D'UPLOADER ***
#define ROLE 0   // 0 = premier Arduino  |  1 = deuxieme Arduino

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

LiquidCrystal_I2C lcd(0x27, 20, 4);

#define CE_PIN  4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);

// ROLE 0 : ecrit sur 00002, lit sur 00001  (comme ctl_affiche_chalet)
// ROLE 1 : ecrit sur 00001, lit sur 00002  (comme ctl_pompe_chalet)
#if ROLE == 0
  const byte ADDR_TX[6] = "00002";
  const byte ADDR_RX[6] = "00001";
#else
  const byte ADDR_TX[6] = "00001";
  const byte ADDR_RX[6] = "00002";
#endif

const int  PIN_BOUTON     = A2;
const char FIXED_PREFIX[] = "0000000001000000"; // 16 chars fixes
const int  PREFIX_LEN     = 16;
// Message total = 16 + 4 = 20 chars (identique au vrai protocole)

unsigned int compteurEnvoi = 0;

void setup() {
  lcd.init();
  lcd.backlight();
  lcd.clear();

  pinMode(PIN_BOUTON, INPUT_PULLUP);

  char ligne0[21];
  snprintf(ligne0, 21, "Role:%d Env:---- --  ", ROLE);
  lcd.setCursor(0, 0); lcd.print(ligne0);
  lcd.setCursor(0, 1); lcd.print("Recu:  ----         ");
  lcd.setCursor(0, 2); lcd.print("Init RF...          ");
  lcd.setCursor(0, 3); lcd.print("Bouton A2 = envoyer ");

  if (!radio.begin()) {
    lcd.setCursor(0, 2); lcd.print("RF FAIL             ");
    while (1);
  }

  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);  // 1MBPS : supporte par tous les modules y compris clones Si24R1
  radio.setChannel(76);           // Canal 76 = defaut officiel RF24, loin du WiFi 2.4GHz
  radio.setAutoAck(false);  // Pas d'ACK : write() = true si hardware TX ok
                            // La reception du compteur confirme l'arrivee reelle
  radio.openWritingPipe(ADDR_TX);
  radio.openReadingPipe(1, ADDR_RX);  // Pipe 1 : jamais ecrase par les ops TX
  radio.startListening();

  // Verifie que le module est bien connecte
  if (!radio.isChipConnected()) {
    lcd.setCursor(0, 2); lcd.print("RF CHIP ABSENT!     ");
    while (1);
  }

  snprintf(ligne0, 21, "Role:%d Env:---- --  ", ROLE);
  lcd.setCursor(0, 0); lcd.print(ligne0);
  lcd.setCursor(0, 2); lcd.print("RF OK               ");
}

void loop() {

  // --- Reception ---
  if (radio.available()) {
    char msg[32] = "";
    radio.read(&msg, sizeof(msg));

    // Extraire le compteur (4 derniers chars)
    int msgLen = strlen(msg);
    char compteurStr[5] = "????";
    if (msgLen >= 4) {
      strncpy(compteurStr, msg + msgLen - 4, 4);
      compteurStr[4] = '\0';
    }

    // Verifier le prefixe (16 premiers chars)
    bool prefixOk = (msgLen == 20) && (strncmp(msg, FIXED_PREFIX, PREFIX_LEN) == 0);

    char ligne1[21];
    snprintf(ligne1, 21, "Recu:  %4s %s      ", compteurStr, prefixOk ? "OK " : "ERR");
    lcd.setCursor(0, 1); lcd.print(ligne1);

    // Ligne 2 : affiche le prefixe recu si erreur, sinon OK
    if (prefixOk) {
      lcd.setCursor(0, 2); lcd.print("Prefixe: OK         ");
    } else {
      char ligne2[21];
      // Affiche les 16 premiers chars recus (tronques a 16 pour tenir sur l'ecran)
      char recuPrefix[17] = "????????????????";
      if (msgLen >= PREFIX_LEN) strncpy(recuPrefix, msg, PREFIX_LEN);
      recuPrefix[16] = '\0';
      snprintf(ligne2, 21, "%.16s    ", recuPrefix);
      lcd.setCursor(0, 2); lcd.print(ligne2);
      lcd.setCursor(0, 3); lcd.print("!!! PREFIXE ERRONE  ");
    }
  }

  // --- Envoi sur front montant du bouton ---
  static bool dernierEtat = HIGH;
  bool etatActuel = (digitalRead(PIN_BOUTON) == LOW);

  if (etatActuel && !dernierEtat) {
    compteurEnvoi++;
    if (compteurEnvoi > 9999) compteurEnvoi = 1;

    // Compose le message 20 chars : 16 fixes + 4 compteur
    char msg[21];
    snprintf(msg, sizeof(msg), "%s%04u", FIXED_PREFIX, compteurEnvoi);
    // msg = "00000000010000000001" (20 chars)

    radio.stopListening();
    bool ok = radio.write(msg, strlen(msg) + 1);
    radio.startListening();

    char ligne0[21];
    snprintf(ligne0, 21, "Role:%d Env:%04u %s  ", ROLE, compteurEnvoi, ok ? "OK " : "ERR");
    lcd.setCursor(0, 0); lcd.print(ligne0);

    if (ok) {
      lcd.setCursor(0, 3); lcd.print("                    ");
    } else {
      lcd.setCursor(0, 3); lcd.print("!!! ENVOI ECHOUE    ");
    }

    delay(50); // anti-rebond
  }

  dernierEtat = etatActuel;
}
