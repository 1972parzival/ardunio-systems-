#include <Servo.h>

Servo esc;

const int ESC_PIN = 9;

// ===== THROTTLE SETTINGS =====

// Minimum percentage where the motor begins moving
const int MIN_MOTION_PERCENT = 20;

// Maximum percentage the motor is allowed to reach
const int MAX_THROTTLE_PERCENT = 40;

// ===== ESC SETTINGS =====

const int ESC_MIN = 1000;
const int ESC_MAX = 2000;


void setup() {
Serial.begin(9600);  // Attach ESC esc.attach(ESC_PIN, ESC_MIN, ESC_MAX); // Immediately send minimum throttle esc.writeMicroseconds(ESC_MIN); Serial.println("Starting ESC..."); Serial.println("Sending minimum throttle..."); // Continuously hold minimum throttle while ESC powers up unsigned long startTime = millis(); while (millis() - startTime < 8000) { esc.writeMicroseconds(ESC_MIN); delay(20); } // Make absolutely sure ESC is still at minimum esc.writeMicroseconds(ESC_MIN); delay(500); Serial.println("ESC ready."); Serial.println("Enter throttle (0-100):"); }

  void loop() {

    if (Serial.available() > 0) {

      // Read percentage from Serial Monitor
      int throttle = Serial.parseInt();

      // Keep input between 0 and 100
      throttle = constrain(throttle, 0, 100);

      int pulseWidth;

      if (throttle == 0) {

        // 0% = stopped
        pulseWidth = ESC_MIN;

      } else {

        // Convert input range:
        //
        // 1%   → MIN_MOTION_PERCENT
        // 100% → MAX_THROTTLE_PERCENT

        int adjustedThrottle = map(
          throttle,
          1,
          100,
          MIN_MOTION_PERCENT,
          MAX_THROTTLE_PERCENT);

        // Safety cap
        adjustedThrottle = constrain(
          adjustedThrottle,
          MIN_MOTION_PERCENT,
          MAX_THROTTLE_PERCENT);

        // Convert percentage to ESC pulse width
        pulseWidth = map(
          adjustedThrottle,
          0,
          100,
          ESC_MIN,
          ESC_MAX);
      }

      // Send signal to ESC
      esc.writeMicroseconds(pulseWidth);

      // Display information
      Serial.print("Input: ");
      Serial.print(throttle);
      Serial.print("%  |  ESC output: ");
      Serial.print(pulseWidth);
      Serial.println(" us");

      Serial.println("Enter new throttle (0-100):");

      // Clear remaining Serial data
      while (Serial.available()) {
        Serial.read();
      }
    }
  }