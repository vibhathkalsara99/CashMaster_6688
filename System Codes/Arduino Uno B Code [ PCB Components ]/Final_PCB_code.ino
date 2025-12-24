#include <SPI.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <I2CKeyPad.h>
#include <avr/pgmspace.h>

// ---------- LCD & Keypad ----------
U8G2_ST7920_128X64_1_HW_SPI u8g2(U8G2_R0, 10, A3); // CS=10, Reset=A3
#define I2CADDR 0x20
I2CKeyPad keypad(I2CADDR);

// Relay Pins
#define RELAY_COIN_LOCK A0
#define RELAY_NOTE_LOCK A1

// ---------- Coin Detection ----------
#define NUM_SENSORS 6
const int sensorPins[NUM_SENSORS] = {2,3,4,5,6,7};
const int coinValues[NUM_SENSORS] = {1,2,5,1,10,2};
unsigned long lastDetectTimes[NUM_SENSORS] = {0};
bool lastSensorState[NUM_SENSORS] = {HIGH,HIGH,HIGH,HIGH,HIGH,HIGH};
bool coinDetected[NUM_SENSORS] = {false,false,false,false,false,false};
unsigned long coinDetectedTime[NUM_SENSORS] = {0};
const unsigned long COIN_DETECT_DURATION = 5;

// Individual debounce delays for each sensor (ULTRA-LOW for test purposes)
const unsigned long DEBOUNCE_DELAYS[NUM_SENSORS] = {
  0,  // Sensor 0 (Pin 2, Rs.1) - NO DEBOUNCE - INSTANT
  0,  // Sensor 1 (Pin 3, Rs.2) - NO DEBOUNCE - INSTANT 
  0,  // Sensor 2 (Pin 4, Rs.5) - NO DEBOUNCE - INSTANT
  0,  // Sensor 3 (Pin 5, Rs.1) - NO DEBOUNCE - INSTANT
  0,  // Sensor 4 (Pin 6, Rs.10) - NO DEBOUNCE - INSTANT
  0   // Sensor 5 (Pin 7, Rs.2) - NO DEBOUNCE - INSTANT
};

// Individual interrupt debounce delays (microseconds) - ZERO FOR TESTING
const unsigned long INTERRUPT_DEBOUNCE[NUM_SENSORS] = {
  0,   // Sensor 0 - NO DEBOUNCE - INSTANT
  0,   // Sensor 1 - NO DEBOUNCE - INSTANT
  0,   // Sensor 2 - NO DEBOUNCE - INSTANT
  0,   // Sensor 3 - NO DEBOUNCE - INSTANT
  0,   // Sensor 4 - NO DEBOUNCE - INSTANT
  0    // Sensor 5 - NO DEBOUNCE - INSTANT
};

// Interrupt-based detection for pins 2 and 3
volatile bool coinPending[6] = {false, false, false, false, false, false}; // All sensors
volatile unsigned long coinInterruptTime[6] = {0, 0, 0, 0, 0, 0};

// Multiple reading confirmation for reliable detection
#define CONFIRM_READINGS 1 // MINIMUM - Only 1 reading needed for max speed
bool stableReading[NUM_SENSORS] = {false};
int consecutiveReadings[NUM_SENSORS] = {0};
unsigned long lastReadingTime[NUM_SENSORS] = {0};

// Totals
unsigned long totalCoinValue = 0;
unsigned long totalNoteValue = 0;

// Password
char password[5] = "1234";
char inputPassword[5] = "";
byte inputIndex = 0;

// State Control
enum SystemState {
  MAIN_MENU,
  DETECTING,
  ENTER_PASSWORD,
  OPTIONS_MENU,
  COMPARTMENTS_MENU,
  COIN_CONTROL,
  NOTE_CONTROL
};

SystemState currentState = MAIN_MENU;
unsigned long stateStartTime = 0;
const unsigned long STATE_TIMEOUT = 30000;

// Note Detection via Serial
int detectedNoteValue = 0;

// Keypad State
uint8_t lastValidKey = 0xFF;
uint8_t lastProcessedKey = 0xFF;
unsigned long lastKeyTime = 0;
const unsigned long KEYPAD_DEBOUNCE_DELAY = 250; // 250ms debounce
const uint8_t MAX_KEY_INDEX = 15;

// Keypad Layout (4x4)
const char KEYS[] PROGMEM = {
  '1', '2', '3', 'A',
  '4', '5', '6', 'B',
  '7', '8', '9', 'C',
  '*', '0', '#', 'D'
};

// Unlock Type (0: none, 1: coin, 2: note)
int unlockType = 0;

// ---------- Interrupt Service Routines ----------
void coinISR() {
  // INSTANT RESPONSE - No rate limiting for maximum speed
  unsigned long now = micros();
  
  // Check ALL pins for interrupts (maximum coverage)
  for(int i = 0; i < 6; i++) {
    if(digitalRead(sensorPins[i]) == LOW && !coinPending[i]) {
      // NO DEBOUNCE - INSTANT TRIGGER
      coinPending[i] = true;
      coinInterruptTime[i] = now;
    }
  }
}

// Function to display centered text
void displayCentered(const char* text, int y, bool largeFont = false) {
  if (largeFont) {
    u8g2.setFont(u8g2_font_logisoso16_tr); // Beautiful, modern serif font
  } else {
    u8g2.setFont(u8g2_font_6x10_tr); // Smaller font for menus
  }
  int width = u8g2.getStrWidth(text);
  u8g2.setCursor((128 - width) / 2, y);
  u8g2.print(text);
}

// Function to display temporary status
void displayTempStatus(const char* message) {
  u8g2.firstPage();
  do {
    displayCentered(message, 30);
  } while (u8g2.nextPage());
  Serial.print(F("Displaying: "));
  Serial.println(message);
  delay(2000);
}

// Function to display detection status
void displayTempStatus(int value, bool isCoin) {
  char buf[20];
  u8g2.firstPage();
  do {
    if (isCoin) {
      sprintf_P(buf, PSTR("Coin Detected: Rs.%d"), value);
    } else {
      sprintf_P(buf, PSTR("Note Detected: Rs.%d"), value);
    }
    displayCentered(buf, 30);
  } while (u8g2.nextPage());
  Serial.print(F("Displaying: "));
  Serial.println(buf);
  delay(2000);
  currentState = MAIN_MENU;
  stateStartTime = millis();
}

void setup() {
  // Initialize Serial for debugging
  Serial.begin(9600);
  while (!Serial) {
    delay(10);
  }
  Serial.println(F("CASHMASTER System: Initializing..."));

  // Initialize LCD
  u8g2.begin();
  Serial.println(F("LCD Initialized"));

  // Initialize I2C and Keypad
  Wire.begin();
  keypad.begin();
  if (!keypad.isConnected()) {
    Serial.println(F("Error: Keypad not detected at 0x20"));
    u8g2.firstPage();
    do {
      displayCentered("Keypad Error", 30);
      displayCentered("Check I2C (A4/A5)", 50);
    } while (u8g2.nextPage());
    Serial.println(F("Displaying: Keypad Error"));
    while (1); // Halt if keypad fails
  } else {
    Serial.println(F("Keypad Initialized"));
  }

  // Initialize Relays
  pinMode(RELAY_COIN_LOCK, OUTPUT);
  pinMode(RELAY_NOTE_LOCK, OUTPUT);
  digitalWrite(RELAY_COIN_LOCK, HIGH); // Relays OFF (active LOW)
  digitalWrite(RELAY_NOTE_LOCK, HIGH);
  Serial.println(F("Relays Initialized"));

  // Initialize IR Sensors
  for (int i = 0; i < NUM_SENSORS; i++) {
    pinMode(sensorPins[i], INPUT_PULLUP); // Removed pgm_read_word for direct access
    lastSensorState[i] = digitalRead(sensorPins[i]) == LOW; // Initialize sensor states
    coinDetected[i] = false; // Initialize detection flags
  }
  
  // Setup interrupts for ALL POSSIBLE PINS for maximum coverage
  attachInterrupt(digitalPinToInterrupt(2), coinISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(3), coinISR, FALLING);
  // Note: Arduino Uno only has 2 external interrupt pins (2,3)
  // But we'll poll the others at maximum speed
  
  Serial.println(F("IR Sensors Initialized - MAXIMUM SPEED MODE"));

  // Startup Sequence
  u8g2.firstPage();
  do {
    displayCentered("CASHMASTER", 40, true); // Centered vertically at y=40
  } while (u8g2.nextPage());
  Serial.println(F("Displaying: CASHMASTER (Startup)"));
  delay(2000);

  u8g2.firstPage();
  do {
    displayCentered("Initializing...", 30);
  } while (u8g2.nextPage());
  Serial.println(F("Displaying: Initializing..."));
  delay(2000);

  u8g2.firstPage();
  do {
    displayCentered("Ready to sort", 30);
  } while (u8g2.nextPage());
  Serial.println(F("Displaying: Ready to sort"));
  delay(2000);

  currentState = MAIN_MENU;
  stateStartTime = millis();
}

void loop() {
  // COIN DETECTION FIRST - HIGHEST PRIORITY
  // Check for coin detection via IR sensors - ULTRA AGGRESSIVE NO BLOCKING
  for (int i = 0; i < NUM_SENSORS; i++) {
    // Check interrupt-flagged coins first (fastest response)
    if (coinPending[i]) {
      Serial.print(F("COIN:"));
      Serial.println(coinValues[i]);
      Serial.flush(); // Force immediate send
      
      coinPending[i] = false;
      lastDetectTimes[i] = millis();
      coinDetected[i] = true;
      coinDetectedTime[i] = millis();
      totalCoinValue += coinValues[i];
      // NO DISPLAY - NO BLOCKING OPERATIONS
      continue; // Skip polling for this sensor this cycle
    }
    
    // ULTRA-FAST polling method - immediate response
    bool currentReading = (digitalRead(sensorPins[i]) == LOW);
    unsigned long now = millis();
    
    // IMMEDIATE detection - NO debounce for maximum speed
    if (currentReading && !lastSensorState[i] && !coinDetected[i]) {
      
      Serial.print(F("COIN:"));
      Serial.println(coinValues[i]);
      Serial.flush(); // Force immediate send
      
      totalCoinValue += coinValues[i];
      lastDetectTimes[i] = now;
      coinDetected[i] = true;
      coinDetectedTime[i] = now;
      
      // NO BLOCKING DISPLAY OPERATIONS
    }
    
    // Reset detection flag when sensor goes HIGH again (coin passed completely)
    if (!currentReading && coinDetected[i]) {
      coinDetected[i] = false;
    }
    
    lastSensorState[i] = currentReading; // Update previous state immediately
  }

  // LOWER PRIORITY OPERATIONS (only if no coins detected)
  // Read keypad - MINIMAL processing
  uint8_t keyIndex = keypad.getKey();
  char key = 0;

  // Process key only if valid and within debounce period
  if (keyIndex <= MAX_KEY_INDEX) {
    if (millis() - lastKeyTime >= KEYPAD_DEBOUNCE_DELAY) {
      // New key press or different key from last processed
      if (lastValidKey == 0xFF || keyIndex != lastValidKey) {
        key = pgm_read_byte(&KEYS[keyIndex]);
        lastValidKey = keyIndex;
        lastProcessedKey = keyIndex;
        lastKeyTime = millis();
      }
    }
  } else if (keyIndex == 0xFF || keyIndex > MAX_KEY_INDEX) {
    // Key released, reset state
    lastValidKey = 0xFF;
  }

  // Check for note detection via Serial (Raspberry Pi) - MINIMAL processing
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    detectedNoteValue = input.toInt();
    if (detectedNoteValue > 0) {
      totalNoteValue += detectedNoteValue;
      
      // Send note data back for Firebase upload (in format that read_arduino.py expects)
      Serial.print(F("Rs."));
      Serial.println(detectedNoteValue);
      // NO BLOCKING DISPLAY
    }
  }

  // MINIMAL State Machine - NO BLOCKING LCD OPERATIONS
  static unsigned long lastDisplayUpdate = 0;
  unsigned long now = millis();
  
  // Update display only every 100ms and only if no recent coin detection
  if (now - lastDisplayUpdate > 100) {
    switch (currentState) {
      case MAIN_MENU:
        // Minimal display update
        if (key == 'A') {
          currentState = OPTIONS_MENU;
          stateStartTime = now;
        }
        break;
        
      case OPTIONS_MENU:
        if (now - stateStartTime > STATE_TIMEOUT) {
          currentState = MAIN_MENU;
          stateStartTime = now;
        }
        if (key == '1') {
          currentState = COMPARTMENTS_MENU;
          stateStartTime = now;
        } else if (key == '#') {
          currentState = MAIN_MENU;
          stateStartTime = now;
        }
        break;
        
      case COMPARTMENTS_MENU:
        if (now - stateStartTime > STATE_TIMEOUT) {
          currentState = MAIN_MENU;
          stateStartTime = now;
        }
        if (key == '1') {
          currentState = COIN_CONTROL;
          stateStartTime = now;
        } else if (key == '2') {
          currentState = NOTE_CONTROL;
          stateStartTime = now;
        } else if (key == '#') {
          currentState = OPTIONS_MENU;
          stateStartTime = now;
        }
        break;
        
      case COIN_CONTROL:
        if (now - stateStartTime > STATE_TIMEOUT) {
          currentState = MAIN_MENU;
          stateStartTime = now;
        }
        if (key != 0) {
          if (key == '1') {
            unlockType = 1;
            currentState = ENTER_PASSWORD;
            inputPassword[0] = '\0';
            inputIndex = 0;
            stateStartTime = now;
          } else if (key == '2') {
            digitalWrite(RELAY_COIN_LOCK, HIGH);
            Serial.println(F("Coin Locked"));
            stateStartTime = now;
          } else if (key == '#') {
            currentState = COMPARTMENTS_MENU;
            stateStartTime = now;
          }
        }
        break;
        
      case NOTE_CONTROL:
        if (now - stateStartTime > STATE_TIMEOUT) {
          currentState = MAIN_MENU;
          stateStartTime = now;
        }
        if (key != 0) {
          if (key == '1') {
            unlockType = 2;
            currentState = ENTER_PASSWORD;
            inputPassword[0] = '\0';
            inputIndex = 0;
            stateStartTime = now;
          } else if (key == '2') {
            digitalWrite(RELAY_NOTE_LOCK, HIGH);
            Serial.println(F("Note Locked"));
            stateStartTime = now;
          } else if (key == '#') {
            currentState = COMPARTMENTS_MENU;
            stateStartTime = now;
          }
        }
        break;
        
      case ENTER_PASSWORD:
        if (now - stateStartTime > STATE_TIMEOUT) {
          currentState = (unlockType == 1) ? COIN_CONTROL : NOTE_CONTROL;
          unlockType = 0;
          inputPassword[0] = '\0';
          inputIndex = 0;
          stateStartTime = now;
        }
        if (key != 0) {
          if (key == '#') {
            currentState = (unlockType == 1) ? COIN_CONTROL : NOTE_CONTROL;
            unlockType = 0;
            inputPassword[0] = '\0';
            inputIndex = 0;
            stateStartTime = now;
          } else if (inputIndex < 4) {
            inputPassword[inputIndex++] = key;
            inputPassword[inputIndex] = '\0';
            if (inputIndex == 4) {
              if (strcmp(inputPassword, password) == 0) {
                if (unlockType == 1) {
                  digitalWrite(RELAY_COIN_LOCK, LOW);
                  Serial.println(F("Coin Unlocked"));
                } else if (unlockType == 2) {
                  digitalWrite(RELAY_NOTE_LOCK, LOW);
                  Serial.println(F("Note Unlocked"));
                }
                currentState = (unlockType == 1) ? COIN_CONTROL : NOTE_CONTROL;
                unlockType = 0;
                inputPassword[0] = '\0';
                inputIndex = 0;
                stateStartTime = now;
              } else {
                Serial.println(F("Wrong Password"));
                inputPassword[0] = '\0';
                inputIndex = 0;
                stateStartTime = now;
              }
            }
          }
        }
        break;
    }
    lastDisplayUpdate = now;
  }

  // NO DELAY - Maximum responsiveness for coin detection
}