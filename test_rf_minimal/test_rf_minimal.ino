// test_rf_minimal.ino
// Adapte de l'exemple officiel RF24 GettingStarted.
//
// ROLE 0 = emetteur : envoie un uint32_t toutes les 2 secondes
// ROLE 1 = recepteur : ecoute et affiche
//
// *** CHANGER ICI AVANT D'UPLOADER ***
#define ROLE 0   // 0 = emetteur  |  1 = recepteur

// Branchement : CE=4, CSN=5, 3.3V, GND, MOSI, MISO, SCK
// IMPORTANT : nRF24L01 PA/LNA consomme 130mA en TX.
//   La pin 3.3V de l'UNO ne fournit que 50mA.
//   Alimentation externe 3.3V recommandee + condo 10uF entre VCC/GND du module.

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

LiquidCrystal_I2C lcd(0x27, 20, 4);
RF24 radio(4, 5);  // CE, CSN

// Adresses officielles de l'exemple GettingStarted
const byte ADDR[2][6] = { "1Node", "2Node" };
//   ROLE 0 : ecrit sur ADDR[1]="2Node", lit sur ADDR[0]="1Node"
//   ROLE 1 : ecrit sur ADDR[0]="1Node", lit sur ADDR[1]="2Node"

uint32_t compteur = 0;
uint32_t nbRecus  = 0;
uint32_t nbEchecs = 0;

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0); lcd.print("Role:");
  lcd.print(ROLE);
  lcd.print(" Init RF...     ");

  if (!radio.begin()) {
    lcd.setCursor(0, 1); lcd.print("RF BEGIN FAIL!      ");
    Serial.println("ERREUR: radio.begin() = false");
    while (1);
  }

  // Parametres identiques a l'exemple officiel RF24
  radio.setPALevel(RF24_PA_LOW);
  // setDataRate non appele = defaut RF24_1MBPS (exemple officiel)
  // setChannel non appele = defaut canal 76
  radio.setPayloadSize(sizeof(uint32_t));  // 4 bytes fixe

#if ROLE == 0
  radio.openWritingPipe(ADDR[1]);    // TX vers "2Node"
  radio.openReadingPipe(1, ADDR[0]); // RX depuis "1Node"
  radio.stopListening();             // mode TX permanent
#else
  radio.openWritingPipe(ADDR[0]);    // TX vers "1Node" (pour futur usage)
  radio.openReadingPipe(1, ADDR[1]); // RX depuis "2Node"
  radio.startListening();            // mode RX permanent
#endif

  if (!radio.isChipConnected()) {
    lcd.setCursor(0, 1); lcd.print("RF CHIP ABSENT!     ");
    Serial.println("ERREUR: isChipConnected() = false");
    while (1);
  }

  radio.printPrettyDetails(); // Affiche config complete dans Serial Monitor

#if ROLE == 0
  lcd.setCursor(0, 0); lcd.print("Role:0 EMETTEUR     ");
  lcd.setCursor(0, 1); lcd.print("Env toutes les 2s   ");
  lcd.setCursor(0, 2); lcd.print("RF OK - canal 76    ");
  lcd.setCursor(0, 3); lcd.print("Env:0000 OK  Ech:000");
  Serial.println("Mode EMETTEUR - envoi toutes les 2s");
#else
  lcd.setCursor(0, 0); lcd.print("Role:1 RECEPTEUR    ");
  lcd.setCursor(0, 1); lcd.print("En attente...       ");
  lcd.setCursor(0, 2); lcd.print("RF OK - canal 76    ");
  lcd.setCursor(0, 3); lcd.print("Recu:0000           ");
  Serial.println("Mode RECEPTEUR - en attente");
#endif
}

void loop() {
#if ROLE == 0
  // --- Emetteur : envoie toutes les 2 secondes ---
  compteur++;
  bool ok = radio.write(&compteur, sizeof(compteur));

  if (ok) {
    Serial.print("Envoye #"); Serial.print(compteur); Serial.println(" : OK");
  } else {
    nbEchecs++;
    Serial.print("Envoye #"); Serial.print(compteur); Serial.print(" : ECHEC (total="); Serial.print(nbEchecs); Serial.println(")");
  }

  char buf[21];
  snprintf(buf, 21, "Env:%04lu %s Ech:%03lu", compteur, ok ? "OK " : "ERR", nbEchecs);
  lcd.setCursor(0, 3); lcd.print(buf);

  delay(2000);

#else
  // --- Recepteur : ecoute ---
  if (radio.available()) {
    uint32_t payload = 0;
    radio.read(&payload, sizeof(payload));
    nbRecus++;

    Serial.print("Recu: "); Serial.print(payload);
    Serial.print("  (total recu: "); Serial.print(nbRecus); Serial.println(")");

    char buf[21];
    snprintf(buf, 21, "Recu:%04lu  Total:%04lu", payload, nbRecus);
    lcd.setCursor(0, 3); lcd.print(buf);
    lcd.setCursor(0, 1); lcd.print("Signal recu!        ");
  }
#endif
}
