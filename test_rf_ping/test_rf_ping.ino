// test_rf_ping.ino
// Protocole base sur COMMUNICATION.md (teste et valide en conditions reelles).
// UN seul fichier, ROLE definit le comportement.
//
// ROLE 0 = Maitre : envoie en premier, affiche NOMBRES IMPAIRS  (1, 3, 5, ...)
// ROLE 1 = Client : repond en premier, affiche NOMBRES PAIRS    (2, 4, 6, ...)
//
// Logique ping-pong :
//   Maitre envoie N=1 → Client affiche 1, repond N+1=2 → Maitre affiche 2,
//   Maitre envoie N=3 → Client affiche 3, repond N+1=4 → Maitre affiche 4, ...
//
// Parametres RF identiques au code valide (test_comm_maitre/client) :
//   - Aucun setDataRate() : defaut = RF24_1MBPS
//   - Aucun setChannel()  : defaut = canal 76
//   - PA = RF24_PA_LOW
//   - Adresses : maitre ecrit sur "00001", lit sur "00002"
//                client  lit  sur "00001", ecrit sur "00002"
//
// Branchement : CE=4, CSN=5, alimentation 3.3V externe + condo 10uF sur VCC/GND

// *** CHANGER ICI AVANT D'UPLOADER ***
#define ROLE 0   // 0 = Maitre (impairs)  |  1 = Client (pairs)

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

LiquidCrystal_I2C lcd(0x27, 20, 4);
RF24 radio(4, 5);  // CE, CSN

// Adresses identiques au protocole COMMUNICATION.md
const byte ADDR_MAITRE[6] = "00001";  // maitre ecrit ici, client lit ici
const byte ADDR_CLIENT[6] = "00002";  // client ecrit ici, maitre lit ici

// Format message : "CNT:NNNNN" (9 chars max)
#define MSG_SIZE 16  // fixe pour le nRF24L01

unsigned long nbEnvois   = 0;
unsigned long nbRecus    = 0;
unsigned long nbEchecs   = 0;

void afficherEtat(const char* role, unsigned long valAffiche, unsigned long envois,
                  unsigned long recus, unsigned long echecs, const char* statut) {
  char buf[21];
  // Ligne 0 : role + valeur principale
  snprintf(buf, 21, "%-7s  Valeur:%5lu", role, valAffiche);
  lcd.setCursor(0, 0); lcd.print(buf);
  // Ligne 1 : envois OK et echecs
  snprintf(buf, 21, "Env:%5lu  Ech:%5lu", envois, echecs);
  lcd.setCursor(0, 1); lcd.print(buf);
  // Ligne 2 : receptions
  snprintf(buf, 21, "Recu:          %5lu", recus);
  lcd.setCursor(0, 2); lcd.print(buf);
  // Ligne 3 : statut derniere operation
  snprintf(buf, 21, "%-20s", statut);
  lcd.setCursor(0, 3); lcd.print(buf);
}

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
#if ROLE == 0
  lcd.print("Maitre init RF...   ");
#else
  lcd.print("Client init RF...   ");
#endif

  if (!radio.begin()) {
    lcd.setCursor(0, 1); lcd.print("RF BEGIN FAIL!      ");
    Serial.println("ERREUR: radio.begin() = false");
    while (1);
  }

  // Parametres identiques au protocole valide (COMMUNICATION.md)
  radio.setPALevel(RF24_PA_LOW);
  // PAS de setDataRate() : defaut RF24_1MBPS
  // PAS de setChannel()  : defaut canal 76

#if ROLE == 0
  // Maitre : ecrit vers client, lit les reponses
  radio.openWritingPipe(ADDR_MAITRE);
  radio.openReadingPipe(1, ADDR_CLIENT);
  radio.stopListening();
  Serial.println("Mode MAITRE (impairs)");
#else
  // Client : lit les messages du maitre, ecrit les reponses
  radio.openWritingPipe(ADDR_CLIENT);
  radio.openReadingPipe(1, ADDR_MAITRE);
  radio.startListening();
  Serial.println("Mode CLIENT (pairs)");
#endif

  if (!radio.isChipConnected()) {
    lcd.setCursor(0, 1); lcd.print("RF CHIP ABSENT!     ");
    Serial.println("ERREUR: isChipConnected() = false");
    while (1);
  }

  radio.printPrettyDetails();  // Diagnostic complet dans Serial Monitor

#if ROLE == 0
  afficherEtat("MAITRE", 0, 0, 0, 0, "Demarrage...");
#else
  afficherEtat("CLIENT", 0, 0, 0, 0, "En attente maitre...");
#endif
}

// ============================================================
// MAITRE : envoie N (impair), attend reponse N+1 (pair)
// ============================================================
#if ROLE == 0

unsigned long compteur = 1;  // Demarre a 1 (impair)

void loop() {
  char msgEnvoi[MSG_SIZE];
  char msgRecu[MSG_SIZE];

  // Compose le message
  memset(msgEnvoi, 0, MSG_SIZE);
  snprintf(msgEnvoi, MSG_SIZE, "CNT:%05lu", compteur);

  // Envoi avec handshake (jusqu'a 5 tentatives)
  bool ok = false;
  unsigned long valRecue = 0;

  for (int tentative = 0; tentative < 5 && !ok; tentative++) {
    radio.stopListening();
    delay(2);
    bool writeOk = radio.write(msgEnvoi, MSG_SIZE);
    nbEnvois++;
    Serial.print("Envoi #"); Serial.print(compteur);
    Serial.print(" write="); Serial.println(writeOk ? "OK" : "ERR");

    radio.startListening();
    unsigned long debut = millis();
    while (millis() - debut < 500) {
      if (radio.available()) {
        memset(msgRecu, 0, MSG_SIZE);
        radio.read(msgRecu, MSG_SIZE);
        // Parse "CNT:NNNNN"
        if (strncmp(msgRecu, "CNT:", 4) == 0) {
          valRecue = atol(msgRecu + 4);
          ok = true;
          nbRecus++;
          Serial.print("Recu: "); Serial.println(valRecue);
        }
        break;
      }
    }
    radio.stopListening();

    if (!ok) {
      nbEchecs++;
      delay(100);
    }
  }

  // Affiche la valeur recue (pair) comme valeur principale du maitre
  // Le maitre "voit" les pairs retournes par le client
  unsigned long valAffiche = ok ? valRecue : compteur;
  afficherEtat("MAITRE", valAffiche, nbEnvois, nbRecus, nbEchecs,
               ok ? "OK: pair recu       " : "!!! ECHEC reponse   ");

  // Prochain envoi : valeur recue + 1 (impair suivant)
  // Si echec : on reessaie le meme compteur (impair) au prochain cycle
  if (ok) {
    compteur = valRecue + 1;  // pair + 1 = impair suivant
  }

  delay(1000);
}

// ============================================================
// CLIENT : attend N (impair), repond N+1 (pair)
// ============================================================
#else

void loop() {
  if (radio.available()) {
    char msgRecu[MSG_SIZE];
    memset(msgRecu, 0, MSG_SIZE);
    radio.read(msgRecu, MSG_SIZE);

    unsigned long valRecue = 0;
    if (strncmp(msgRecu, "CNT:", 4) == 0) {
      valRecue = atol(msgRecu + 4);
      nbRecus++;
      Serial.print("Recu: "); Serial.println(valRecue);
    } else {
      // Message invalide - repond quand meme pour debloquer le maitre
      Serial.print("Message invalide: "); Serial.println(msgRecu);
    }

    unsigned long valReponse = valRecue + 1;  // impair + 1 = pair

    // Repond au maitre
    char msgEnvoi[MSG_SIZE];
    memset(msgEnvoi, 0, MSG_SIZE);
    snprintf(msgEnvoi, MSG_SIZE, "CNT:%05lu", valReponse);

    radio.stopListening();
    delay(5);
    bool writeOk = radio.write(msgEnvoi, MSG_SIZE);
    radio.startListening();
    nbEnvois++;
    if (!writeOk) nbEchecs++;

    Serial.print("Repond: "); Serial.print(valReponse);
    Serial.print(" write="); Serial.println(writeOk ? "OK" : "ERR");

    // Affiche la valeur envoyee (pair) comme valeur principale du client
    afficherEtat("CLIENT", valReponse, nbEnvois, nbRecus, nbEchecs,
                 writeOk ? "OK: pair envoye     " : "!!! ECHEC envoi     ");
  }
}

#endif
