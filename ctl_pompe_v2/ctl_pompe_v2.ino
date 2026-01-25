// OBSOLÈTE : Voir README_CAPTEURS.md
// Ajout pour la détection rapide de la purge
const int NB_PURGE_TS = 5;
unsigned long purgeTs[NB_PURGE_TS] = {0};
int purgeTsIndex = 0;
#include <LiquidCrystal.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
// Relais (à brancher sur sorties digitales Arduino)
// Relais 1 : électrovanne purge (ex: D7)
// Relais 2 : pompe (ex: D8)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// LCD classique: RS, E, D4, D5, D6, D7
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// Capteurs
const int PIN_PRESSION = A0; // manomètre
const int PIN_AMPERAGE = A1; // capteur d'ampérage
const int PIN_DEBIT = 6;    // débitmètre à impulsions (entrée digitale)
// Relais (à brancher sur sorties digitales Arduino)
// Relais 1 : électrovanne purge (ex: D7)
// Relais 2 : pompe (ex: D8)
const int RELAIS_PURGE = 8; // D8 Arduino -> IN du relais électrovanne
const int RELAIS_POMPE = 7; // D7 Arduino -> IN du relais pompe

const int TAILLE_PILE = 10; // Lissage sur 10 mesures
int pileEtat[TAILLE_PILE] = {0};
int indexPile = 0;
unsigned int debit = 0;
int pression = 0;
int amperage = 0;
const int N_LISSAGE = 10; // Lissage sur 10 mesures
int ampBuffer[N_LISSAGE] = {0};
int ampIndex = 0;

// --- Variables d'état globales pour la gestion automatique ---
bool pompeActive = false;
bool reposActive = false;
unsigned long debutPompe = 0;
unsigned long debutRepos = 0;
bool purgeActive = false;
unsigned long debutPurge = 0;
int lastPression = -1;
int lastDebit = -1;
int lastAmperage = -1;
bool traceInit = false;

bool firstPrint = true;

// Flag global pour désactiver la gestion centrale pendant la purge pulse
volatile bool purgePulseEnCours = false;

// Variable pour mémoriser la dernière pression lors d'un débit 0 ou 1000
int lastPressionDebitZero = -1;

// Mode debug : bloque la vanne de purge (true = purge toujours fermée)
bool purgeBloquee = false; // Passe à false pour revenir au mode normal


// Buffer circulaire pour mémoriser l'état de la condition de purge sur 10 secondes
const int NB_COND_PURGE = 10; // 10 mesures (1 par seconde)
bool condPurgeBuffer[NB_COND_PURGE] = {0};
int condPurgeIndex = 0;
// Pourcentage de conditions de purge remplies sur la fenêtre glissante
// float pctCond = 0.0; // Supprimé, on utilise nbTrue
int nbTrue = 0;

  // === PARAMÈTRES AJUSTABLES ===
  const unsigned long MAX_POMPE = 30UL * 60UL * 1000UL; // 30 minutes en ms
  const unsigned long TEMPS_REPOS = 5UL * 60UL * 1000UL; // 5 minutes de repos
  const int SEUIL_PRESSION_PURGE = 125; // Pression max pour déclencher la purge
  const unsigned long DUREE_PURGE = 20UL * 1000UL; // 20 secondes de purge
  // =============================

void setup() {
  Serial.begin(9600);
  // Initialisation OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;);
  }
  // Initialisation des relais
  pinMode(RELAIS_PURGE, OUTPUT);
  pinMode(RELAIS_POMPE, OUTPUT);
  // Test manuel : active le relais purge 5 secondes pour vérifier le pilotage
  digitalWrite(RELAIS_PURGE, HIGH); // vanne ouverte (LED relais doit s'allumer)
  Serial.println("[TEST] RELAIS_PURGE HIGH (LED doit s'allumer 5s)");
  delay(5000);
  digitalWrite(RELAIS_PURGE, LOW); // vanne fermée
  Serial.println("[TEST] RELAIS_PURGE LOW (LED doit s'éteindre)");
  digitalWrite(RELAIS_POMPE, LOW); // pompe arrêtée par défaut (LOW = coupée)
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("OLED OK!");
}


// --- Gestion automatique de la pompe et de la purge ---
void gestionAutomatique() {
    // Désactive toute la gestion centrale si pulse purge en cours
    if (purgePulseEnCours) return;
  // Mode debug : purge bloquée
  if (purgeBloquee) {
    if (digitalRead(RELAIS_PURGE) != LOW) {
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[ACTION] Vanne purge FERMEE (debug)");
    }
    purgeActive = false;
    static bool msgBloque = false;
    if (!msgBloque) {
      Serial.println("[DEBUG] Mode purge bloquée : vanne toujours fermée");
      msgBloque = true;
    }
    return;
  }

  unsigned long now = millis();

  // Attente remplissage buffers
  if (indexPile < TAILLE_PILE || ampIndex < N_LISSAGE) {
    if (digitalRead(RELAIS_PURGE) != LOW) {
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[ACTION] Vanne purge FERMEE (init)");
    }
    if (digitalRead(RELAIS_POMPE) != LOW) {
      digitalWrite(RELAIS_POMPE, LOW);
      Serial.println("[ACTION] Pompe COUPEE (init)");
    }
    if (!traceInit) {
      Serial.println("[INIT] Remplissage des buffers, relais à 0, attente avant monitoring...");
      traceInit = true;
    }
    return;
  }

  // Critères de fonctionnement
  bool demandePompe = (debit != 0);
  // Activation de la purge uniquement si au moins 5 conditions de purge dans la pile de 10
  bool demandePurge = (nbTrue >= 5); // 5 mesures sur 10 dans la pile

  // --- Gestion de la séquence de purge ---
  static bool purgeJustStarted = false;
  static bool pompeCoupeeApresPurge = false;
  if (purgeActive) {
    // Début de purge : trace unique
    if (!purgeJustStarted) {
      Serial.println("[DEBUT PURGE] Purge activée, vanne ouverte");
      purgeJustStarted = true;
      pompeCoupeeApresPurge = false;
    }
    // Pilotage du relais purge : toujours HIGH tant que purgeActive
    if (digitalRead(RELAIS_PURGE) != HIGH) {
      digitalWrite(RELAIS_PURGE, HIGH);
      Serial.println("[ACTION] Vanne purge OUVERTE (RELAIS HIGH)");
    }
    // Pompe ON sauf si coupée par sécurité
    if (!pompeCoupeeApresPurge) {
      if (digitalRead(RELAIS_POMPE) != LOW) {
        digitalWrite(RELAIS_POMPE, LOW);
        Serial.println("[ACTION] Pompe ACTIVE");
      }
    }
    // Si le débit redevient positif ou la pression remonte, on termine la purge
    if (debit > 0 || pression > SEUIL_PRESSION_PURGE) {
      purgeActive = false;
      if (digitalRead(RELAIS_PURGE) != LOW) {
        digitalWrite(RELAIS_PURGE, LOW);
        Serial.println("[ACTION] Vanne purge FERMEE (RELAIS LOW)");
      }
      Serial.println("[FIN PURGE] Débit ou pression OK, purge terminée");
      purgeJustStarted = false;
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Purge finie");
      display.clearDisplay(); display.setTextSize(2); display.setCursor(0,0); display.print("Purge finie"); display.display();
      return;
    }
    // Sécurité : coupe pompe après 15s si pas d'amélioration
    if ((now - debutPurge) >= 15000 && !pompeCoupeeApresPurge) {
      if (digitalRead(RELAIS_POMPE) != HIGH) {
        digitalWrite(RELAIS_POMPE, HIGH);
        Serial.println("[ACTION] Pompe COUPEE (sécurité)");
      }
      pompeCoupeeApresPurge = true;
      Serial.println("[SECURITE] Pompe coupée après 15s de purge sans amélioration");
    }
    // Fin de purge après timeout
    if (now - debutPurge >= DUREE_PURGE) {
      purgeActive = false;
      if (digitalRead(RELAIS_PURGE) != LOW) {
        digitalWrite(RELAIS_PURGE, LOW);
        Serial.println("[ACTION] Vanne purge FERMEE (RELAIS LOW)");
      }
      Serial.println("[FIN PURGE] Durée max atteinte, purge terminée");
      purgeJustStarted = false;
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Purge finie");
      display.clearDisplay(); display.setTextSize(2); display.setCursor(0,0); display.print("Purge finie"); display.display();
      return;
    }
    lcd.clear(); lcd.setCursor(0,0); lcd.print("Purge en cours");
    display.clearDisplay(); display.setTextSize(2); display.setCursor(0,0); display.print("Purge en cours"); display.display();
    return; // Pas de monitoring pendant la purge
  }

  // Trace explicite si condition de purge détectée (basée uniquement sur nbTrue)
  if (demandePurge) {
    Serial.print("[CONDITION PURGE DETECTEE] nbTrue: "); Serial.print(nbTrue); Serial.print("/"); Serial.print(NB_COND_PURGE);
    Serial.print(" | P:"); Serial.print(pression);
    Serial.print(" D:"); Serial.print(debit);
    Serial.print(" A:"); Serial.print(amperage);
    Serial.print(" | Relais purge (D8): "); Serial.print(digitalRead(RELAIS_PURGE));
    Serial.print(" | Relais pompe (D7): ");
    if (digitalRead(RELAIS_POMPE) == LOW) {
      Serial.print("LOW (moteur ON)");
    } else {
      Serial.print("HIGH (moteur OFF)");
    }
    if (purgeActive) {
      Serial.print(" | Purge déjà active");
    } else if (reposActive) {
      Serial.print(" | Repos actif, purge bloquée");
    } else if (indexPile < TAILLE_PILE || ampIndex < N_LISSAGE) {
      Serial.print(" | Buffers non remplis, purge bloquée");
    } else {
      Serial.print(" | Purge va être activée");
    }
    Serial.println();

    // Ajout : trace explicite si la purge n'est pas activée alors que la condition est remplie
    if (!purgeActive && (!reposActive && !purgeBloquee && (indexPile >= TAILLE_PILE && ampIndex >= N_LISSAGE))) {
      // Ici la purge va être activée au prochain appel, donc pas besoin de trace supplémentaire
    } else if (!purgeActive) {
      Serial.print("[INFO] Purge non activée : ");
      if (reposActive) Serial.print("repos actif, ");
      if (purgeBloquee) Serial.print("mode debug purge bloquée, ");
      if (indexPile < TAILLE_PILE || ampIndex < N_LISSAGE) Serial.print("buffers non remplis, ");
      Serial.println();
    }
  }

  // Gestion du repos
  if (reposActive) {
    if (now - debutRepos >= TEMPS_REPOS) {
      reposActive = false;
      // Affiche "Repos terminé" ou autre message
    } else {
      if (digitalRead(RELAIS_POMPE) != HIGH) {
        digitalWrite(RELAIS_POMPE, HIGH);
        Serial.println("[ACTION] Pompe COUPEE (repos)");
      }
      if (digitalRead(RELAIS_PURGE) != LOW) {
        digitalWrite(RELAIS_PURGE, LOW);
        Serial.println("[ACTION] Vanne purge FERMEE (repos)");
      }
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Repos pompe");
      display.clearDisplay(); display.setTextSize(2); display.setCursor(0,0); display.print("Repos pompe"); display.display();
      return;
    }
  }

  // Gestion du temps de fonctionnement continu
  if (demandePompe) {
    if (!pompeActive) {
      debutPompe = now;
      pompeActive = true;
      Serial.println("[ACTION] Pompe ACTIVE (demande)");
    }
    if (now - debutPompe >= MAX_POMPE) {
      if (digitalRead(RELAIS_POMPE) != LOW) {
        digitalWrite(RELAIS_POMPE, LOW);
        Serial.println("[ACTION] Pompe COUPEE (max temps)");
      }
      if (digitalRead(RELAIS_PURGE) != LOW) {
        digitalWrite(RELAIS_PURGE, LOW);
        Serial.println("[ACTION] Vanne purge FERMEE (max temps)");
      }
      reposActive = true;
      debutRepos = now;
      pompeActive = false;
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Repos pompe");
      display.clearDisplay(); display.setTextSize(2); display.setCursor(0,0); display.print("Repos pompe"); display.display();
      return;
    }
    // Pompe ON
    if (digitalRead(RELAIS_POMPE) != LOW) {
      digitalWrite(RELAIS_POMPE, LOW);
      Serial.println("[ACTION] Pompe ACTIVE");
    }
    // Si condition purge détectée, on lance la purge
    // La gestion de la purge est maintenant indépendante
    // Sinon, vanne purge fermée
    if (digitalRead(RELAIS_PURGE) != LOW) {
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[ACTION] Vanne purge FERMEE");
    }
  } else {
    // Pompe OFF
    if (digitalRead(RELAIS_POMPE) != HIGH) {
      digitalWrite(RELAIS_POMPE, HIGH);
      Serial.println("[ACTION] Pompe COUPEE");
    }
    if (digitalRead(RELAIS_PURGE) != LOW) {
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[ACTION] Vanne purge FERMEE");
    }
    pompeActive = false;
  }

  // Activation de la purge indépendante de la pompe
  if (demandePurge && !purgeActive) {
    if (!reposActive && !purgeBloquee && (indexPile >= TAILLE_PILE && ampIndex >= N_LISSAGE)) {
      Serial.println("[ACTION] Activation de la purge (nbTrue >= 5/10)");
      purgeActive = true;
      debutPurge = now;
      purgeJustStarted = false;
      // La gestion de la purge se fera au prochain appel
      return;
    } else {
      Serial.print("[ERREUR] Condition purge OK mais purge non activée : ");
      if (reposActive) Serial.print("repos actif, ");
      if (purgeBloquee) Serial.print("mode debug purge bloquée, ");
      if (indexPile < TAILLE_PILE || ampIndex < N_LISSAGE) Serial.print("buffers non remplis, ");
      Serial.println();
    }
  }
}

void loop() {
  // On ne gère ici que la séquence (arrêt, timeout, etc.)
  gestionAutomatique();
  static bool purgeDeclencheeLoop = false;
  static unsigned long lastMesure = 0;
  static unsigned int lastImpulsions = 0;
  unsigned long now = millis();

  pression = analogRead(PIN_PRESSION);
  // Lissage sur 10 lectures
  ampBuffer[ampIndex] = analogRead(PIN_AMPERAGE);
  ampIndex = (ampIndex + 1) % N_LISSAGE;
  long ampSum = 0;
  for (int i = 0; i < N_LISSAGE; i++) ampSum += ampBuffer[i];
  amperage = ampSum / N_LISSAGE;

  // Tampon circulaire des 100 derniers états
  int currentState = digitalRead(PIN_DEBIT);
  pileEtat[indexPile] = currentState;
  indexPile = (indexPile + 1) % TAILLE_PILE;

  // Mesure toutes les 1 seconde sans bloquer
  static bool pulsePurgeActive = false;
  static unsigned long pulsePurgeStart = 0;
  if (now - lastMesure >= 1000) {
    lastMesure = now;
    // Compte le nombre de 0 dans la pile
    int nbZero = 0;
    for (int i = 0; i < TAILLE_PILE; i++) {
      if (pileEtat[i] == 0) nbZero++;
    }
    debit = nbZero;

    // --- Mémorisation de la condition de purge sur 10 secondes (mise à jour à chaque nouvelle mesure) ---
    // Condition d'origine : pression basse ET débit nul (ou 1000)
    bool condPurge = (pression <= SEUIL_PRESSION_PURGE && (debit == 0 || debit == 1000));
    condPurgeBuffer[condPurgeIndex] = condPurge;
    condPurgeIndex = (condPurgeIndex + 1) % NB_COND_PURGE;
    nbTrue = 0;
    for (int i = 0; i < NB_COND_PURGE; i++) {
      if (condPurgeBuffer[i]) nbTrue++;
    }

    // Trace claire : uniquement le nombre de conditions remplies et l'état de condPurge
    Serial.print("[COND_PURGE] "); Serial.print(nbTrue); Serial.print("/"); Serial.print(NB_COND_PURGE);
    Serial.print(" | condPurge (remplissage buffer): "); Serial.println(condPurge ? "O" : "N");

    // Déclenchement direct du relais purge pendant 10s dès que nbTrue >= 5
    if (nbTrue >= 5 && !pulsePurgeActive) {
      Serial.println("[ACTION][LOOP] Déclenchement direct du relais purge (10s) (nbTrue >= 5/10)");
      digitalWrite(RELAIS_PURGE, HIGH);
      pulsePurgeActive = true;
      pulsePurgeStart = now;
      purgePulseEnCours = true;
    }

    // Affichage conditionnel si variation >= 10 sur au moins une valeur OU premier affichage
    static int lastPrintPression = -1000, lastPrintDebit = -1000, lastPrintAmp = -1000;
    int deltaPression = abs(pression - lastPrintPression);
    int deltaDebit = abs(debit - lastPrintDebit);
    int deltaAmp = abs(amperage - lastPrintAmp);
    if (firstPrint || deltaPression > 10 || deltaDebit > 10 || deltaAmp > 10) {
      Serial.print("Pression: "); Serial.print(pression);
      Serial.print("  Nb0: "); Serial.print(debit);
      Serial.print("  Amp: "); Serial.print(amperage);
      Serial.print(" | Purge: "); Serial.print(digitalRead(RELAIS_PURGE) == HIGH ? "PIN HIGH (tension présente)" : "PIN LOW (pas de tension)");
      Serial.print(" | Pompe: ");
      if (digitalRead(RELAIS_POMPE) == LOW) {
        Serial.print("PIN LOW (moteur ON)");
      } else {
        Serial.print("PIN HIGH (moteur OFF)");
      }
      Serial.println();
      lastPrintPression = pression;
      lastPrintDebit = debit;
      lastPrintAmp = amperage;
      firstPrint = false;
    }

    // Affichage LCD classique
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("P:"); lcd.print(pression); lcd.print(" D:"); lcd.print(debit);
    lcd.setCursor(0, 1);
    lcd.print("A:"); lcd.print(amperage); // Affiche l'ampérage sur la 2e ligne

    // Affichage OLED SSD1306
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.print("P:"); display.print(pression);
    display.setCursor(0, 20);
    display.print("D:"); display.print(debit);
    display.setTextSize(2);
    display.setCursor(0, 40);
    display.print("A:"); display.print(amperage); // Affiche l'ampérage sur la 3e ligne
    display.display();
  }

  // Gestion du pulse relais purge (10s)
  static bool pulsePurgeWasActive = false;
  if (pulsePurgeActive) {
    if (now - pulsePurgeStart >= 10000UL) {
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[ACTION][LOOP] Fin du pulse relais purge (10s)");
      pulsePurgeActive = false;
      pulsePurgeWasActive = true;
      purgePulseEnCours = false;
    }
  } else if (pulsePurgeWasActive && nbTrue < 5) {
    // Réarmement du pulse si la condition retombe sous 5/10
    pulsePurgeWasActive = false;
  }

  // Ici tu peux ajouter d'autres capteurs ou traitements non bloquants
}

