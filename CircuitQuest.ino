#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>


// PIN DEFINITIONS

#define SS_PIN      10
#define RST_PIN      9
#define LED_PIN      3
#define LED_COUNT    8
#define BUZZER_PIN   2
// TCS3200 — OE pin on the sensor board must be wired directly to GND
#define TCS_S0       4
#define TCS_S1       5
#define TCS_S2       6
#define TCS_S3       7
#define TCS_OUT      8


// COLOUR SENSOR CALIBRATION
// Paste your values from the calibration sketch here

const unsigned long WHITE_R = 1026,  WHITE_G = 1149, WHITE_B = 919;
const unsigned long BLACK_R = 4883, BLACK_G = 9997, BLACK_B = 9302;
const int UNKNOWN_THRESHOLD = 80;


// OBJECTS

MFRC522            rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
LiquidCrystal_I2C  lcd(0x27, 16, 2);
Adafruit_NeoPixel  strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);


// GAME SETTINGS

const int  MAX_PLAYERS     = 4;
const byte RFID_DATA_BLOCK = 4;

int  scores[MAX_PLAYERS]       = {0, 0, 0, 0};
bool skipNextTurn[MAX_PLAYERS] = {false, false, false, false};
int  currentPlayer = 0;


// ENUMS & COLOUR PALETTE

enum TileType {
  RFID_TILE,
  COLOUR_TILE,
  MEMORY_TILE,
  TIMED_RFID_TILE,
  TIMED_COLOUR_TILE,
  TIMED_MEMORY_TILE,
  BONUS_TILE
};

enum GameColour {
  RED_COLOUR     = 0,
  GREEN_COLOUR   = 1,
  BLUE_COLOUR    = 2,
  YELLOW_COLOUR  = 3,
  UNKNOWN_COLOUR = 99
};

struct KnownColor {
  GameColour   id;
  const char*  name;
  int r, g, b;
};

const KnownColor PALETTE[] = {
  { RED_COLOUR,    "RED",    227, 190, 194 },
  { GREEN_COLOUR,  "GREEN",  150, 220, 210 },
  { BLUE_COLOUR,   "BLUE",   0, 146, 196 },
  { YELLOW_COLOUR, "YELLOW", 225, 245, 203 }
};
const int PALETTE_SIZE = 4;


// LCD MIRROR
// Buffers each line and prints to Serial with [LCD] prefix.
// Use lcdClear / lcdSetCursor / lcdPrint instead of lcd.*

String _lcdBuf[2] = {"", ""};
int    _lcdCurRow = 0;

void _lcdFlushLine(int row) {
  if (_lcdBuf[row].length() > 0) {
    Serial.print(F("[LCD] "));
    Serial.println(_lcdBuf[row]);
    _lcdBuf[row] = "";
  }
}

void lcdClear() {
  _lcdFlushLine(0);
  _lcdFlushLine(1);
  _lcdCurRow = 0;
  lcd.clear();
}

void lcdSetCursor(uint8_t col, uint8_t row) {
  if (row == 1 && _lcdCurRow == 0) _lcdFlushLine(0);
  _lcdCurRow = row;
  lcd.setCursor(col, row);
}

void lcdPrint(const char* s)    { _lcdBuf[_lcdCurRow] += s;         lcd.print(s); }
void lcdPrint(const String &s)  { _lcdBuf[_lcdCurRow] += s;         lcd.print(s); }
void lcdPrint(int n)             { _lcdBuf[_lcdCurRow] += String(n); lcd.print(n); }
void lcdPrint(char c)            { _lcdBuf[_lcdCurRow] += c;         lcd.print(c); }



// FUNCTION DECLARATIONS

void showWelcome();
void showCurrentPlayer();
void showScores();
void nextPlayer();

bool readPlayerDataFromCard(int &playerID, String &playerName);
bool authenticateBlock(byte block);
void haltCard();

void runRFIDTile(bool timedMode);
void runColourTile(bool timedMode);
void runMemoryTile(bool timedMode);
void runBonusTile();

void applyRandomRFIDOutcome(int playerIndex);
void lcdEventAnimation(const String outcomes[], int count, int finalIndex);
void changeScore(int playerIndex, int amount);

void initColourSensor();
unsigned long readColourChannel(bool s2, bool s3);
int toRGB(unsigned long val, unsigned long minVal, unsigned long maxVal);
GameColour detectColour();
String colourName(GameColour c);

bool waitForColourInput(GameColour expectedColour, unsigned long timeoutMs, bool showLiveStatus);
bool runMemoryChallenge(int patternLength, unsigned long totalTimeoutMs);

void setAllPixels(uint32_t color);
void clearStrip();
void flashColour(uint32_t color, int times, int delayMs);
void showSingleColour(GameColour colour, int holdMs);

void successTone();
void failTone();
void bonusTone();
void timeoutTone();
void rainbowAnimation(int waitMs);



// SETUP

void setup() {
  Serial.begin(9600);

  SPI.begin();
  rfid.PCD_Init();
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  lcd.init();
  lcd.backlight();

  strip.begin();
  strip.show();

  pinMode(BUZZER_PIN, OUTPUT);

  initColourSensor();

  randomSeed(analogRead(A0));
  showWelcome();
}



// MAIN LOOP

void loop() {
  if (skipNextTurn[currentPlayer]) {
    lcdClear();
    lcdPrint("Player ");
    lcdPrint(currentPlayer + 1);
    lcdSetCursor(0, 1);
    lcdPrint("Turn skipped!");
    failTone();
    delay(1800);
    skipNextTurn[currentPlayer] = false;
    nextPlayer();
    return;
  }

  showCurrentPlayer();
  delay(1200);

  TileType tile = (TileType)random(0, 7);

  switch (tile) {
    case RFID_TILE:         runRFIDTile(false);  break;
    case COLOUR_TILE:       runColourTile(false); break;
    case MEMORY_TILE:       runMemoryTile(false); break;
    case TIMED_RFID_TILE:   runRFIDTile(true);   break;
    case TIMED_COLOUR_TILE: runColourTile(true);  break;
    case TIMED_MEMORY_TILE: runMemoryTile(true);  break;
    case BONUS_TILE:        runBonusTile();       break;
  }

  showScores();
  delay(2500);
  nextPlayer();
}



// DISPLAY

void showWelcome() {
  lcdClear();
  lcdPrint("Circuit Quest");
  lcdSetCursor(0, 1);
  lcdPrint("Starting...");
  rainbowAnimation(20);
  delay(1000);
}

void showCurrentPlayer() {
  lcdClear();
  lcdPrint("Player ");
  lcdPrint(currentPlayer + 1);
  lcdSetCursor(0, 1);
  lcdPrint("Take your turn");
}

void showScores() {
  lcdClear();
  lcdPrint("P1:");
  lcdPrint(scores[0]);
  lcdPrint(" P2:");
  lcdPrint(scores[1]);
  lcdSetCursor(0, 1);
  lcdPrint("P3:");
  lcdPrint(scores[2]);
  lcdPrint(" P4:");
  lcdPrint(scores[3]);
}

void nextPlayer() {
  currentPlayer = (currentPlayer + 1) % MAX_PLAYERS;
}



// RFID

bool readPlayerDataFromCard(int &playerID, String &playerName) {
  if (!rfid.PICC_IsNewCardPresent()) return false;
  if (!rfid.PICC_ReadCardSerial())   return false;
  if (!authenticateBlock(RFID_DATA_BLOCK)) { haltCard(); return false; }

  byte buffer[18];
  byte size = sizeof(buffer);

  MFRC522::StatusCode status = rfid.MIFARE_Read(RFID_DATA_BLOCK, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Read failed: ");
    Serial.println(rfid.GetStatusCodeName(status));
    haltCard();
    return false;
  }

  playerID   = buffer[0];
  playerName = "";
  for (byte i = 1; i < 16 && buffer[i] != 0; i++) {
    playerName += (char)buffer[i];
  }

  haltCard();
  return true;
}

bool authenticateBlock(byte block) {
  MFRC522::StatusCode status = rfid.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(rfid.uid)
  );
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Auth failed: ");
    Serial.println(rfid.GetStatusCodeName(status));
    return false;
  }
  return true;
}

void haltCard() {
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}



// TILE RUNNERS

void runRFIDTile(bool timedMode) {
  unsigned long timeoutMs = timedMode ? 8000 : 16000;

  lcdClear();
  lcdPrint("RFID Event Tile");
  lcdSetCursor(0, 1);
  lcdPrint(timedMode ? "Scan in 4 sec" : "Scan your card");

  unsigned long start   = millis();
  bool          scanned = false;

  while (millis() - start < timeoutMs) {
    int    cardPlayerID = -1;
    String cardName     = "";

    if (readPlayerDataFromCard(cardPlayerID, cardName)) {
      scanned = true;
      if (cardPlayerID == currentPlayer + 1) {
        lcdClear();
        lcdPrint(cardName);
        lcdSetCursor(0, 1);
        lcdPrint("Card accepted");
        delay(900);
        applyRandomRFIDOutcome(currentPlayer);
      } else {
        lcdClear();
        lcdPrint("Wrong player");
        lcdSetCursor(0, 1);
        lcdPrint("card scanned");
        failTone();
        flashColour(strip.Color(255, 0, 0), 3, 150);
        changeScore(currentPlayer, -5);
      }
      break;
    }
  }

  if (!scanned) {
    lcdClear();
    lcdPrint("RFID timeout");
    lcdSetCursor(0, 1);
    lcdPrint("-5 points");
    timeoutTone();
    flashColour(strip.Color(255, 0, 0), 3, 150);
    changeScore(currentPlayer, -5);
  }
}

void runColourTile(bool timedMode) {
  GameColour    target    = (GameColour)random(0, 4);
  unsigned long timeoutMs = timedMode ? 4000 : 8000;

  lcdClear();
  lcdPrint("Show colour:");
  lcdSetCursor(0, 1);
  lcdPrint(colourName(target));
  showSingleColour(target, 800);

  lcdClear();
  lcdPrint("Show ");
  lcdPrint(colourName(target));
  lcdSetCursor(0, 1);
  lcdPrint(timedMode ? "4 sec..." : "Present card...");

  bool matched = waitForColourInput(target, timeoutMs, false);

  if (matched) {
    lcdClear();
    lcdPrint("Correct!");
    lcdSetCursor(0, 1);
    lcdPrint("+10 points");
    successTone();
    showSingleColour(target, 600);
    flashColour(strip.Color(0, 255, 0), 2, 150);
    changeScore(currentPlayer, 10);
  } else {
    lcdClear();
    lcdPrint("Wrong/timeout");
    lcdSetCursor(0, 1);
    lcdPrint("-5 points");
    failTone();
    flashColour(strip.Color(255, 0, 0), 3, 150);
    changeScore(currentPlayer, -5);
  }
}

void runMemoryTile(bool timedMode) {
  int           patternLength = timedMode ? 3 : 4;
  unsigned long timeoutMs     = timedMode ? 7000 : 14000;

  lcdClear();
  lcdPrint("Memory Tile");
  lcdSetCursor(0, 1);
  lcdPrint(timedMode ? "Fast mode!" : "Watch pattern");
  delay(1000);

  bool passed = runMemoryChallenge(patternLength, timeoutMs);

  if (passed) {
    lcdClear();
    lcdPrint("Pattern right!");
    lcdSetCursor(0, 1);
    lcdPrint("+12 points");
    successTone();
    flashColour(strip.Color(0, 255, 0), 3, 120);
    changeScore(currentPlayer, 12);
  } else {
    lcdClear();
    lcdPrint("Pattern wrong");
    lcdSetCursor(0, 1);
    lcdPrint("-5 points");
    failTone();
    flashColour(strip.Color(255, 0, 0), 3, 150);
    changeScore(currentPlayer, -5);
  }
}

void runBonusTile() {
  lcdClear();
  lcdPrint("Bonus Tile!");
  lcdSetCursor(0, 1);
  lcdPrint("+5 points");
  bonusTone();
  rainbowAnimation(20);
  changeScore(currentPlayer, 5);
}



// RFID RANDOM OUTCOME

void applyRandomRFIDOutcome(int playerIndex) {
  const int outcomeCount = 6;
  String outcomes[outcomeCount] = {
    "+10 points",
    "-5 points",
    "Extra turn!",
    "Skip next",
    "+15 points",
    "Double bonus"
  };

  int finalIndex = random(0, outcomeCount);
  lcdEventAnimation(outcomes, outcomeCount, finalIndex);

  switch (finalIndex) {
    case 0:
      changeScore(playerIndex, 10);
      successTone();
      flashColour(strip.Color(0, 255, 0), 2, 150);
      break;

    case 1:
      changeScore(playerIndex, -5);
      failTone();
      flashColour(strip.Color(255, 0, 0), 2, 150);
      break;

    case 2:
      lcdClear();
      lcdPrint("Extra turn!");
      lcdSetCursor(0, 1);
      lcdPrint("Play again");
      successTone();
      flashColour(strip.Color(0, 0, 255), 3, 120);
      delay(1500);
      currentPlayer = (currentPlayer - 1 + MAX_PLAYERS) % MAX_PLAYERS;
      break;

    case 3:
      skipNextTurn[playerIndex] = true;
      lcdClear();
      lcdPrint("Skip next turn");
      failTone();
      flashColour(strip.Color(255, 150, 0), 3, 120);
      delay(1500);
      break;

    case 4:
      changeScore(playerIndex, 15);
      lcdClear();
      lcdPrint("+15 points!");
      successTone();
      flashColour(strip.Color(0, 255, 150), 3, 120);
      delay(1500);
      break;

    case 5:
      changeScore(playerIndex, 20);
      lcdClear();
      lcdPrint("Double bonus!");
      lcdSetCursor(0, 1);
      lcdPrint("+20 points!");
      bonusTone();
      rainbowAnimation(20);
      delay(1500);
      break;
  }
}

void lcdEventAnimation(const String outcomes[], int count, int finalIndex) {
  lcdClear();
  lcdPrint("Generating...");
  delay(500);

  for (int i = 0; i < 10; i++) {
    int idx = random(0, count);
    lcdClear();
    lcdPrint("Player ");
    lcdPrint(currentPlayer + 1);
    lcdSetCursor(0, 1);
    lcdPrint(outcomes[idx]);
    delay(180);
  }

  lcdClear();
  lcdPrint("Player ");
  lcdPrint(currentPlayer + 1);
  lcdSetCursor(0, 1);
  lcdPrint(outcomes[finalIndex]);
  delay(1000);
}

void changeScore(int playerIndex, int amount) {
  scores[playerIndex] = max(0, scores[playerIndex] + amount);
}



// COLOUR SENSOR

void initColourSensor() {
  pinMode(TCS_S0,  OUTPUT);
  pinMode(TCS_S1,  OUTPUT);
  pinMode(TCS_S2,  OUTPUT);
  pinMode(TCS_S3,  OUTPUT);
  pinMode(TCS_OUT, INPUT);

  digitalWrite(TCS_S0, HIGH);
  digitalWrite(TCS_S1, LOW);
}

unsigned long readColourChannel(bool s2, bool s3) {
  digitalWrite(TCS_S2, s2);
  digitalWrite(TCS_S3, s3);
  delay(10);
  return pulseIn(TCS_OUT, LOW, 500000UL);
}

int toRGB(unsigned long val, unsigned long minVal, unsigned long maxVal) {
  if (val == 0) return 0;
  val = constrain(val, minVal, maxVal);
  return (int)map(val, minVal, maxVal, 255, 0);
}

GameColour detectColour() {
  int r = toRGB(readColourChannel(LOW,  LOW),  WHITE_R, BLACK_R);
  int g = toRGB(readColourChannel(HIGH, HIGH), WHITE_G, BLACK_G);
  int b = toRGB(readColourChannel(LOW,  HIGH), WHITE_B, BLACK_B);

  if (r < 15 && g < 15 && b < 15) return UNKNOWN_COLOUR;

  float bestDist = (float)UNKNOWN_THRESHOLD;
  GameColour best = UNKNOWN_COLOUR;

  for (int i = 0; i < PALETTE_SIZE; i++) {
    float d = sqrt(
      pow(r - PALETTE[i].r, 2) +
      pow(g - PALETTE[i].g, 2) +
      pow(b - PALETTE[i].b, 2)
    );
    if (d < bestDist) {
      bestDist = d;
      best     = PALETTE[i].id;
    }
  }

  return best;
}

String colourName(GameColour c) {
  switch (c) {
    case RED_COLOUR:    return "RED";
    case GREEN_COLOUR:  return "GREEN";
    case BLUE_COLOUR:   return "BLUE";
    case YELLOW_COLOUR: return "YELLOW";
    default:            return "UNKNOWN";
  }
}

bool waitForColourInput(GameColour expectedColour, unsigned long timeoutMs, bool showLiveStatus) {
  unsigned long start    = millis();
  bool          cardSeen = false;

  while (millis() - start < timeoutMs) {
    GameColour detected = detectColour();

    if (showLiveStatus) {
      lcdSetCursor(0, 1);
      lcdPrint("Seen: ");
      lcdPrint(colourName(detected));
      lcdPrint("   ");
    }

    if (detected == UNKNOWN_COLOUR) {
      cardSeen = false;
    } else if (!cardSeen) {
      cardSeen = true;
      delay(150);
      return (detected == expectedColour);
    }

    delay(50);
  }

  return false;
}



// MEMORY CHALLENGE

bool runMemoryChallenge(int patternLength, unsigned long totalTimeoutMs) {
  GameColour pattern[10];
  for (int i = 0; i < patternLength; i++) {
    pattern[i] = (GameColour)random(0, 4);
  }

  lcdClear();
  lcdPrint("Watch closely!");
  lcdSetCursor(0, 1);
  lcdPrint("Remember it...");
  delay(700);

  for (int i = 0; i < patternLength; i++) {
    showSingleColour(pattern[i], 500);
    clearStrip();
    delay(250);
  }

  lcdClear();
  lcdPrint("Your turn!");
  lcdSetCursor(0, 1);
  lcdPrint("Use colour cards");
  delay(800);

  unsigned long challengeStart = millis();

  for (int i = 0; i < patternLength; i++) {
    unsigned long elapsed = millis() - challengeStart;
    if (elapsed >= totalTimeoutMs) return false;
    unsigned long remaining = totalTimeoutMs - elapsed;

    lcdClear();
    lcdPrint("Step ");
    lcdPrint(i + 1);
    lcdPrint(" of ");
    lcdPrint(patternLength);
    lcdSetCursor(0, 1);
    lcdPrint("Show colour...");

    if (!waitForColourInput(pattern[i], remaining, false)) return false;

    showSingleColour(pattern[i], 300);
    successTone();
    delay(200);
  }

  return true;
}



// LED HELPERS

void setAllPixels(uint32_t color) {
  for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, color);
}

void clearStrip() {
  strip.clear();
  strip.show();
}

void flashColour(uint32_t color, int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    setAllPixels(color);
    strip.show();
    delay(delayMs);
    clearStrip();
    delay(delayMs);
  }
}

void showSingleColour(GameColour colour, int holdMs) {
  clearStrip();
  switch (colour) {
    case RED_COLOUR:    setAllPixels(strip.Color(255,   0,   0)); break;
    case GREEN_COLOUR:  setAllPixels(strip.Color(  0, 255,   0)); break;
    case BLUE_COLOUR:   setAllPixels(strip.Color(  0,   0, 255)); break;
    case YELLOW_COLOUR: setAllPixels(strip.Color(255, 180,   0)); break;
    default:            setAllPixels(strip.Color(255, 255, 255)); break;
  }
  strip.show();
  delay(holdMs);
  clearStrip();
}

void rainbowAnimation(int waitMs) {
  for (long hue = 0; hue < 3L * 65536; hue += 256) {
    for (int i = 0; i < strip.numPixels(); i++) {
      int pixelHue = hue + (i * 65536L / strip.numPixels());
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue)));
    }
    strip.show();
    delay(waitMs);
  }
}



// SOUND

void successTone() {
  tone(BUZZER_PIN, 1000, 120); delay(150);
  tone(BUZZER_PIN, 1400, 150); delay(180);
  noTone(BUZZER_PIN);
}

void failTone() {
  tone(BUZZER_PIN, 250, 250); delay(300);
  noTone(BUZZER_PIN);
}

void bonusTone() {
  tone(BUZZER_PIN,  900, 100); delay(120);
  tone(BUZZER_PIN, 1200, 100); delay(120);
  tone(BUZZER_PIN, 1500, 150); delay(180);
  noTone(BUZZER_PIN);
}

void timeoutTone() {
  tone(BUZZER_PIN, 400, 150); delay(180);
  tone(BUZZER_PIN, 300, 220); delay(250);
  noTone(BUZZER_PIN);
}
