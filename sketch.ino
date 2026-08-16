/*************************************************************
 *  ANTI-THEFT LOCKER SYSTEM
 *  Arduino UNO + MFRC522 RFID + R305 Fingerprint + I2C LCD + Relay/Solenoid
 *
 *  Authenticates via EITHER a registered RFID card OR an
 *  enrolled fingerprint. On success, the relay energizes to
 *  release the solenoid lock for a set duration, then re-locks.
 *
 *  Wiring summary:
 *    MFRC522   SDA=10 SCK=13 MOSI=11 MISO=12 RST=9   (3.3V!)
 *    R305      TX->D2 (Arduino RX)  RX->D3 (Arduino TX)
 *    LCD I2C   SDA=A4  SCL=A5
 *    Relay     IN=D8
 *************************************************************/

#include <SPI.h>
#include <MFRC522.h>
#include <SoftwareSerial.h>
#include <Adafruit_Fingerprint.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ------------------- PIN CONFIG -------------------
#define RST_PIN     9
#define SS_PIN      10
#define RELAY_PIN   8

#define FP_RX_PIN   2   // Arduino RX <- Fingerprint TX
#define FP_TX_PIN   3   // Arduino TX -> Fingerprint RX

// ------------------- LOCK CONFIG -------------------
const unsigned long UNLOCK_DURATION_MS = 5000; // how long solenoid stays unlocked
const bool RELAY_ACTIVE_HIGH = true;           // set false if your relay module is active-LOW

// ------------------- OBJECTS -------------------
MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2); // change 0x27 to 0x3F if your LCD backpack uses that address

SoftwareSerial fpSerial(FP_RX_PIN, FP_TX_PIN);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fpSerial);

// ------------------- AUTHORIZED RFID UIDs -------------------
// Add your own card UIDs here (run the sketch, scan a card, copy the UID
// printed on Serial Monitor, and paste it into this array).
byte authorizedUIDs[][4] = {
  {11, 22, 33, 44},   // example UID #1 - replace with your real card
  {0x12, 0x34, 0x56, 0x78}    // example UID #2 - replace with your real card
};
const int numAuthorizedUIDs = sizeof(authorizedUIDs) / sizeof(authorizedUIDs[0]);

// ------------------- SETUP -------------------
void setup() {
  Serial.begin(9600);

  pinMode(RELAY_PIN, OUTPUT);
  lockSolenoid(); // ensure locked at startup

  // LCD init
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Anti-Theft Lock");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  // RFID init
  SPI.begin();
  rfid.PCD_Init();

  // Fingerprint init
  fpSerial.begin(57600);
  delay(500);
  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor found.");
  } else {
    Serial.println("Fingerprint sensor NOT found — check wiring.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("FP Sensor Error");
    lcd.setCursor(0, 1);
    lcd.print("Check wiring");
    // Continue anyway — RFID can still work even if fingerprint fails
    delay(2000);
  }

  delay(1000);
  showReadyScreen();
}

// ------------------- LOOP -------------------
void loop() {
  // --- Try RFID first ---
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    handleRFID();
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    showReadyScreen();
  }

  // --- Try fingerprint ---
  int fpResult = checkFingerprint();
  if (fpResult == FINGERPRINT_OK) {
    grantAccess("Fingerprint OK");
    showReadyScreen();
  } else if (fpResult == FINGERPRINT_NOTFOUND) {
    denyAccess("Unknown Finger");
    showReadyScreen();
  }
  // FINGERPRINT_NOFINGER / other codes just mean "no finger placed" — ignore silently

  delay(100);
}

// ------------------- RFID HANDLING -------------------
void handleRFID() {
  Serial.print("Card UID:");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();

  if (isAuthorizedUID(rfid.uid.uidByte, rfid.uid.size)) {
    grantAccess("RFID Verified");
  } else {
    denyAccess("Unknown Card");
  }
}

bool isAuthorizedUID(byte *scannedUID, byte size) {
  if (size != 4) return false; // adjust if your cards have different UID length
  for (int i = 0; i < numAuthorizedUIDs; i++) {
    bool match = true;
    for (int j = 0; j < 4; j++) {
      if (authorizedUIDs[i][j] != scannedUID[j]) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

// ------------------- FINGERPRINT HANDLING -------------------
// Returns FINGERPRINT_OK, FINGERPRINT_NOTFOUND, FINGERPRINT_NOFINGER, or an error code
int checkFingerprint() {
  int p = finger.getImage();
  if (p != FINGERPRINT_OK) return p; // usually FINGERPRINT_NOFINGER — no finger placed

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return p;

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    Serial.print("Fingerprint match, ID #");
    Serial.println(finger.fingerID);
    return FINGERPRINT_OK;
  } else {
    Serial.println("Fingerprint not recognized.");
    return FINGERPRINT_NOTFOUND;
  }
}

// ------------------- ACCESS GRANTED / DENIED -------------------
void grantAccess(const char *method) {
  Serial.print("ACCESS GRANTED: ");
  Serial.println(method);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Access Granted");
  lcd.setCursor(0, 1);
  lcd.print(method);

  unlockSolenoid();
  delay(UNLOCK_DURATION_MS);
  lockSolenoid();
}

void denyAccess(const char *reason) {
  Serial.print("ACCESS DENIED: ");
  Serial.println(reason);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Access Denied");
  lcd.setCursor(0, 1);
  lcd.print(reason);

  delay(2000);
}

// ------------------- RELAY / SOLENOID CONTROL -------------------
void unlockSolenoid() {
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? HIGH : LOW);
  Serial.println("Solenoid UNLOCKED");
}

void lockSolenoid() {
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
  Serial.println("Solenoid LOCKED");
}

// ------------------- LCD READY SCREEN -------------------
void showReadyScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scan Card or");
  lcd.setCursor(0, 1);
  lcd.print("Place Finger");
}