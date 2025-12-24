/*
 * Complete Note Sorting System
 * - Automatic pickup, sorting, and homing
 * - Relay-controlled pump motor on D17
 * - Serial input for note values (20, 50, 100, 500, 1000, 5000)
 * - Emergency stop functionality
 * - Ensured homing with extra steps
 * - Added NO_NOTE command for visible position (Y+ 21500)
 */

#include <Arduino.h>

#define STEP_PIN_X 2
#define DIR_PIN_X  5
#define STEP_PIN_Y 3
#define DIR_PIN_Y  6
#define STEP_PIN_Z 4
#define DIR_PIN_Z  7
#define ENABLE_PIN 8

#define X_LIMIT_PIN 9   // X- limit (homing)
#define Y_LIMIT_PIN 10  // Y- limit (homing)
#define Z_LIMIT_PIN 11  // Z+ limit (homing)
#define ESTOP_PIN   12
#define PUMP_RELAY_PIN 17  // CoolEn pin of shield connected to D17

// Motion parameters
#define STEP_DELAY_XY    155      // µs XY step delay (faster)
#define STEP_DELAY_Z     800      // µs Z step delay
#define HOMING_EXTRA_STEPS 300    // Extra steps to ensure limit switch hit
#define PICKUP_WAIT_TIME 3000     // 3 seconds wait with pump on

// Position definitions (steps from homing)
#define PICKUP_Y_STEPS    18900   // Y+ steps to pickup location
#define PICKUP_Z_STEPS    13180   // Z- steps to pickup/drop level

// Compartment positions (steps from pickup location)
#define COMP_20_Y_BACK    5900    // Y- steps from pickup
#define COMP_50_Y_BACK    11950   // Y- steps from pickup
#define COMP_100_Y_BACK   17990   // Y- steps from pickup
#define COMP_500_Y_BACK   6100    // Y- steps from pickup
#define COMP_500_X_POS    13900   // X+ steps for 500
#define COMP_1000_Y_BACK  12100   // Y- steps from pickup
#define COMP_1000_X_POS   13900   // X+ steps for 1000
#define COMP_5000_Y_BACK  18100   // Y- steps from pickup
#define COMP_5000_X_POS   13900   // X+ steps for 5000

// Direction constants
#define DIR_POS  true
#define DIR_NEG  false
#define DIR_UP   true
#define DIR_DOWN false

// Z backoff parameters
#define Z_BACKOFF_STEPS  180

// System states
enum SystemState {
  SYSTEM_IDLE,
  MOVING_TO_PICKUP,
  PICKING_UP,
  MOVING_TO_COMPARTMENT,
  DROPPING_NOTE,
  RETURNING_HOME
};

SystemState systemState = SYSTEM_IDLE;
int currentNoteValue = 0;

void setup() {
  // Pin setup
  pinMode(STEP_PIN_X, OUTPUT);
  pinMode(DIR_PIN_X, OUTPUT);
  pinMode(STEP_PIN_Y, OUTPUT);
  pinMode(DIR_PIN_Y, OUTPUT);
  pinMode(STEP_PIN_Z, OUTPUT);
  pinMode(DIR_PIN_Z, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(PUMP_RELAY_PIN, OUTPUT);

  pinMode(X_LIMIT_PIN, INPUT_PULLUP);
  pinMode(Y_LIMIT_PIN, INPUT_PULLUP);
  pinMode(Z_LIMIT_PIN, INPUT_PULLUP);
  pinMode(ESTOP_PIN, INPUT_PULLUP);

  // Initialize outputs
  digitalWrite(ENABLE_PIN, LOW);      // Enable motors
  digitalWrite(PUMP_RELAY_PIN, HIGH); // Relay OFF (assuming active LOW relay)
  
  Serial.begin(115200);
  Serial.println(F("=== Note Sorting System Ready ==="));
  Serial.println(F("Enter note value: 20,50,100,500,1000,5000"));

  // Ensure we start at home position - do this only once
  homeAllAxes();
}

void loop() {
  // Always check emergency stop first
  if (digitalRead(ESTOP_PIN) == LOW) {
    emergencyStop();
    return;
  }

  // Handle serial input for note values and commands
  if (Serial.available() && systemState == SYSTEM_IDLE) {
    String input = Serial.readString();
    input.trim();
    
    if (input == "NO_NOTE") {
      // Handle no_note command - move to visible position
      Serial.println(F("NO_NOTE received - Moving to visible position"));
      homeAllAxes();  // Home first
      moveAxis('Y', 21500, DIR_POS);  // Move to Y+ 21500
      Serial.println(F("DONE"));
    } else if (input == "VIEW_COMPARTMENT") {
      // Move to compartment viewing position
      Serial.println(F("Moving to compartment viewing position..."));
      homeAllAxes();  // Home first for safety
      moveAxis('Y', 22000, DIR_POS);  // Move to Y+ 21500
      Serial.println(F("COMPARTMENT_VIEW_DONE"));
    } else if (input == "HOME") {
      // Manual home command
      Serial.println(F("Homing..."));
      homeAllAxes();
      Serial.println(F("HOME_DONE"));
    } else {
      int noteValue = input.toInt();
      
      if (isValidNoteValue(noteValue)) {
        currentNoteValue = noteValue;
        Serial.print(F("Sorting: "));
        Serial.println(noteValue);
        systemState = MOVING_TO_PICKUP;
      } else {
        Serial.println(F("Invalid! Use: 20,50,100,500,1000,5000, VIEW_COMPARTMENT, HOME, or NO_NOTE"));
      }
    }
  }

  // Execute current system state
  switch (systemState) {
    case SYSTEM_IDLE:
      // Waiting for input
      break;
      
    case MOVING_TO_PICKUP:
      moveToPickupLocation();
      break;
      
    case PICKING_UP:
      pickupNote();
      break;
      
    case MOVING_TO_COMPARTMENT:
      moveToCompartment();
      break;
      
    case DROPPING_NOTE:
      dropNote();
      break;
      
    case RETURNING_HOME:
      returnToHome();
      break;
  }
}

bool isValidNoteValue(int value) {
  return (value == 20 || value == 50 || value == 100 || 
          value == 500 || value == 1000 || value == 5000);
}

void emergencyStop() {
  Serial.println(F("EMERGENCY STOP!"));
  systemState = SYSTEM_IDLE;
  currentNoteValue = 0;
  turnOffPump();
  digitalWrite(ENABLE_PIN, HIGH);

  while (digitalRead(ESTOP_PIN) == LOW) {
    delay(100);
  }

  digitalWrite(ENABLE_PIN, LOW);
  Serial.println(F("E-stop released - Homing..."));
  homeAllAxes();
  Serial.println(F("Ready. Enter note value:"));
}

void moveToPickupLocation() {
  Serial.println(F("Moving to pickup..."));
  moveAxis('Y', PICKUP_Y_STEPS, DIR_POS);
  systemState = PICKING_UP;
}

void pickupNote() {
  Serial.println(F("Z down to pickup..."));
  moveAxis('Z', PICKUP_Z_STEPS, DIR_DOWN);

  Serial.println(F("Pump ON - picking..."));
  turnOnPump();
  delay(PICKUP_WAIT_TIME);

  Serial.println(F("Z up with note..."));
  moveAxis('Z', PICKUP_Z_STEPS, DIR_UP);
  systemState = MOVING_TO_COMPARTMENT;
}

void moveToCompartment() {
  Serial.print(F("To compartment "));
  Serial.println(currentNoteValue);

  switch (currentNoteValue) {
    case 20:
      moveAxis('Y', COMP_20_Y_BACK, DIR_NEG);
      break;
    case 50:
      moveAxis('Y', COMP_50_Y_BACK, DIR_NEG);
      break;
    case 100:
      moveAxis('Y', COMP_100_Y_BACK, DIR_NEG);
      break;
    case 500:
      moveAxis('Y', COMP_500_Y_BACK, DIR_NEG);
      moveAxis('X', COMP_500_X_POS, DIR_POS);
      break;
    case 1000:
      moveAxis('Y', COMP_1000_Y_BACK, DIR_NEG);
      moveAxis('X', COMP_1000_X_POS, DIR_POS);
      break;
    case 5000:
      moveAxis('Y', COMP_5000_Y_BACK, DIR_NEG);
      moveAxis('X', COMP_5000_X_POS, DIR_POS);
      break;
  }
  systemState = DROPPING_NOTE;
}

void dropNote() {
  Serial.println(F("Dropping note..."));
  moveAxis('Z', PICKUP_Z_STEPS, DIR_DOWN);

  Serial.println(F("Pump OFF"));
  turnOffPump();
  delay(500);

  moveAxis('Z', PICKUP_Z_STEPS, DIR_UP);
  systemState = RETURNING_HOME;
}

void returnToHome() {
  Serial.println(F("Returning home..."));
  homeAllAxes();
  Serial.println(F("DONE"));
  systemState = SYSTEM_IDLE;
  currentNoteValue = 0;
}

void homeAllAxes() {
  Serial.println(F("Homing..."));

  // Home Z axis first
  digitalWrite(DIR_PIN_Z, DIR_UP);
  while (digitalRead(Z_LIMIT_PIN) == HIGH && digitalRead(ESTOP_PIN) == HIGH) {
    stepZ();
  }
  if (digitalRead(ESTOP_PIN) == LOW) return;

  // Z backoff
  if (digitalRead(Z_LIMIT_PIN) == LOW) {
    digitalWrite(DIR_PIN_Z, DIR_DOWN);
    for (int i = 0; i < Z_BACKOFF_STEPS && digitalRead(ESTOP_PIN) == HIGH; i++) {
      digitalWrite(STEP_PIN_Z, HIGH);
      delayMicroseconds(10);
      digitalWrite(STEP_PIN_Z, LOW);
      delayMicroseconds(1000);
    }
  }

  // Home Y axis
  digitalWrite(DIR_PIN_X, DIR_NEG);
  digitalWrite(DIR_PIN_Y, DIR_POS);
  while (digitalRead(Y_LIMIT_PIN) == HIGH && digitalRead(ESTOP_PIN) == HIGH) {
    stepXY();
  }
  if (digitalRead(ESTOP_PIN) == LOW) return;
  for (int i = 0; i < HOMING_EXTRA_STEPS && digitalRead(ESTOP_PIN) == HIGH; i++) {
    stepXY();
  }

  // Home X axis
  digitalWrite(DIR_PIN_X, DIR_NEG);
  digitalWrite(DIR_PIN_Y, DIR_NEG);
  while (digitalRead(X_LIMIT_PIN) == HIGH && digitalRead(ESTOP_PIN) == HIGH) {
    stepXY();
  }
  if (digitalRead(ESTOP_PIN) == LOW) return;
  for (int i = 0; i < HOMING_EXTRA_STEPS && digitalRead(ESTOP_PIN) == HIGH; i++) {
    stepXY();
  }
}

void moveAxis(char axis, int steps, bool direction) {
  switch (axis) {
    case 'X':
      digitalWrite(DIR_PIN_X, direction);
      digitalWrite(DIR_PIN_Y, direction);
      for (int i = 0; i < steps; i++) {
        if (digitalRead(ESTOP_PIN) == LOW) return;
        if (!direction && digitalRead(X_LIMIT_PIN) == LOW) break;
        stepXY();
      }
      break;

    case 'Y':
      digitalWrite(DIR_PIN_X, direction);
      digitalWrite(DIR_PIN_Y, !direction);
      for (int i = 0; i < steps; i++) {
        if (digitalRead(ESTOP_PIN) == LOW) return;
        if (!direction && digitalRead(Y_LIMIT_PIN) == LOW) break;
        stepXY();
      }
      break;

    case 'Z':
      // Check if already at limit before starting
      if (direction == DIR_UP && digitalRead(Z_LIMIT_PIN) == LOW) {
        Serial.println(F("Z already at top"));
        return;
      }

      digitalWrite(DIR_PIN_Z, direction);
      delay(10); // Small delay after direction change

      for (int i = 0; i < steps; i++) {
        if (digitalRead(ESTOP_PIN) == LOW) return;
        if (direction == DIR_UP && digitalRead(Z_LIMIT_PIN) == LOW) {
          Serial.println(F("Z limit hit"));
          break;
        }
        stepZ();
      }
      break;
  }
}

void stepXY() {
  digitalWrite(STEP_PIN_X, HIGH);
  digitalWrite(STEP_PIN_Y, HIGH);
  delayMicroseconds(10);
  digitalWrite(STEP_PIN_X, LOW);
  digitalWrite(STEP_PIN_Y, LOW);
  delayMicroseconds(STEP_DELAY_XY);
}

void stepZ() {
  digitalWrite(STEP_PIN_Z, HIGH);
  delayMicroseconds(50);  // Longer pulse for Z motor
  digitalWrite(STEP_PIN_Z, LOW);
  delayMicroseconds(1000); // Balanced speed - faster but still smooth
}

void turnOnPump() {
  digitalWrite(PUMP_RELAY_PIN, LOW);
}

void turnOffPump() {
  digitalWrite(PUMP_RELAY_PIN, HIGH);
}
