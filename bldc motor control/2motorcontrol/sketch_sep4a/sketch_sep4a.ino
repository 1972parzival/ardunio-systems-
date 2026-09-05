#include <Servo.h>

Servo esc1;
Servo esc2;

// ===== ESC PINS =====

const int ESC1_PIN = 9;
const int ESC2_PIN = 10;

// ===== THROTTLE SETTINGS =====

// Minimum percentage where the motor begins moving
const int MIN_MOTION_PERCENT = 20;

// Maximum percentage the motor is allowed to reach
const int MAX_THROTTLE_PERCENT = 60;

// ===== ESC SETTINGS =====

const int ESC_MIN = 1000;
const int ESC_MAX = 2000;

// ===== SAFETY SETTINGS =====

// Stop both motors if no new command is received
const unsigned long COMMAND_TIMEOUT = 30000;

// Time the ESCs are held at minimum during startup
const unsigned long ESC_STARTUP_TIME = 8000;

// Stores when the last valid speed command was received
unsigned long lastCommandTime = 0;


// =====================================================
// Convert throttle percentage into ESC pulse width
// =====================================================

int throttleToPulse(int throttle) {

  // 0% = stopped
  if (throttle <= 0) {
    return ESC_MIN;
  }

  // Convert:
  // 1%   -> MIN_MOTION_PERCENT
  // 100% -> MAX_THROTTLE_PERCENT

  int adjustedThrottle = map(
    throttle,
    1,
    100,
    MIN_MOTION_PERCENT,
    MAX_THROTTLE_PERCENT
  );

  // Safety limits
  adjustedThrottle = constrain(
    adjustedThrottle,
    MIN_MOTION_PERCENT,
    MAX_THROTTLE_PERCENT
  );

  // Convert percentage to ESC pulse width
  return map(
    adjustedThrottle,
    0,
    100,
    ESC_MIN,
    ESC_MAX
  );
}


// =====================================================
// Stop both motors
// =====================================================

void stopMotors() {

  esc1.writeMicroseconds(ESC_MIN);
  esc2.writeMicroseconds(ESC_MIN);

}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(9600);

  // Attach both ESCs
  esc1.attach(ESC1_PIN, ESC_MIN, ESC_MAX);
  esc2.attach(ESC2_PIN, ESC_MIN, ESC_MAX);

  // Immediately send minimum throttle
  stopMotors();

  Serial.println("Starting ESCs...");
  Serial.println("Sending minimum throttle...");

  // Hold minimum throttle while ESCs power up
  unsigned long startTime = millis();

  while (millis() - startTime < ESC_STARTUP_TIME) {

    stopMotors();

    delay(20);
  }

  // Make absolutely sure both ESCs are at minimum
  stopMotors();

  delay(500);

  // Start timeout timer
  lastCommandTime = millis();

  Serial.println("ESCs ready.");
  Serial.println();
  Serial.println("Enter throttle:");
  Serial.println("Motor1 Motor2");
  Serial.println("Example: 40 30");
  Serial.println();
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  // ===================================================
  // COMMAND TIMEOUT
  // ===================================================

  if (millis() - lastCommandTime > COMMAND_TIMEOUT) {

    // No command received for 10 seconds
    stopMotors();

  }


  // ===================================================
  // SERIAL COMMAND
  // ===================================================

  if (Serial.available() > 0) {

    // Read first motor
    int throttle1 = Serial.parseInt();

    // Read second motor
    int throttle2 = Serial.parseInt();

    // Constrain both inputs
    throttle1 = constrain(throttle1, 0, 100);
    throttle2 = constrain(throttle2, 0, 100);


    // Convert percentages to ESC signals
    int pulseWidth1 = throttleToPulse(throttle1);
    int pulseWidth2 = throttleToPulse(throttle2);


    // Send signals to ESCs
    esc1.writeMicroseconds(pulseWidth1);
    esc2.writeMicroseconds(pulseWidth2);


    // Record time of valid command
    lastCommandTime = millis();


    // Display information
    Serial.print("Motor 1: ");
    Serial.print(throttle1);
    Serial.print("% -> ");
    Serial.print(pulseWidth1);
    Serial.println(" us");

    Serial.print("Motor 2: ");
    Serial.print(throttle2);
    Serial.print("% -> ");
    Serial.print(pulseWidth2);
    Serial.println(" us");

    Serial.println();


    // Clear remaining Serial data
    while (Serial.available()) {
      Serial.read();
    }
  }
}