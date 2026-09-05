#include <Servo.h>

Servo esc1;
Servo esc2;

const char* logo[] = {
  "  _______________________________________________  ",
  " /                                               \\ ",
  "|             P E R I O D I C A L L Y             |",
  " \\_______________________________________________/ ",
  "",
  "     a  i  i  3            Research",
  "     a  i  i  3            Vehicle",
  "     a  i  i  3            2026",
  " ",
  " "
};

void printLogo() {
  for (int i = 0; i < 10; i++) {
    Serial.println(logo[i]);
  }
}
// =====================================================
//                    PIN SETTINGS
// =====================================================

const int ESC1_PIN = 9;   // Motor 1 - Left
const int ESC2_PIN = 10;  // Motor 2 - Right

// Drives a MOSFET/relay that switches the ESCs' power rail.
// NOT wired directly to the ESCs — a GPIO pin can't source
// enough current. Goes HIGH once at startup, before the ESCs
// are attached/armed, and stays HIGH for the whole session.
const int ESC_POWER_PIN = 8;

// =====================================================
//                 THROTTLE SETTINGS
// =====================================================

// Minimum percentage where the motor begins moving
const int MIN_MOTION_PERCENT = 20;

// Maximum throttle percentage allowed
const int MAX_THROTTLE_PERCENT = 60;

// =====================================================
//                    ESC SETTINGS
// =====================================================

const int ESC_MIN = 1000;
const int ESC_MAX = 2000;

// Time the ESC is given to initialize
const unsigned long ESC_STARTUP_TIME = 3000;

// =====================================================
//                  SAFETY / STEP TIMING
// =====================================================

// This single value controls TWO things:
//   1) How long the rover waits with no command before
//      the safety timeout stops the motors.
//   2) How long EACH step of the Q sequence runs for.
//
// Total Q sequence duration = COMMAND_TIMEOUT * Q_SEQUENCE_LENGTH
const unsigned long COMMAND_TIMEOUT = 5000;

// =====================================================
//                 MOTOR COMMAND SETTINGS
// =====================================================

// Motor 1 = Left
// Motor 2 = Right

const int W_LEFT  = 10;
const int W_RIGHT = 10;

const int S_LEFT  = 0;
const int S_RIGHT = 0;

const int A_LEFT  = 0;
const int A_RIGHT = 10;

const int D_LEFT  = 10;
const int D_RIGHT = 0;

const int E_LEFT  = 20;
const int E_RIGHT = 20;

// =====================================================
//                   Q SEQUENCE
// =====================================================

const char Q_SEQUENCE[] = {
  'W',
  'W',
  'W',
  'D',
  'W',
  'D',
  'S'
};

const int Q_SEQUENCE_LENGTH =
  sizeof(Q_SEQUENCE) / sizeof(Q_SEQUENCE[0]);

bool qSequenceRunning = false;
int qSequenceStep = 0;

// =====================================================
//                  TIMEOUT VARIABLES
// =====================================================

// Shared clock: used for BOTH the safety timeout
// and Q-sequence step advancement.
unsigned long lastCommandTime = 0;
unsigned long lastCountdownPrint = 0;

const unsigned long COUNTDOWN_INTERVAL = 1000;

bool timeoutMessagePrinted = false;

// =====================================================
//               COUNTDOWN DISPLAY
// =====================================================

void updateCountdown(unsigned long seconds) {

  static unsigned long previousSeconds = 0xFFFFFFFF; // sentinel, never a valid second

  if (seconds == previousSeconds) {
    return;
  }

  Serial.print("Countdown: ");
  Serial.println(seconds);

  previousSeconds = seconds;
}

// =====================================================
//                 THROTTLE CONVERSION
// =====================================================

int throttleToPulse(int throttle) {

  if (throttle <= 0) {
    return ESC_MIN;
  }

  throttle = constrain(throttle, 1, 100);

  // Convert user throttle scale:
  //
  // 1%   = minimum motion
  // 100% = maximum throttle
  //
  // Example:
  // 1%  -> 20%
  // 100% -> 60%

  int adjustedThrottle = map(
    throttle,
    1,
    100,
    MIN_MOTION_PERCENT,
    MAX_THROTTLE_PERCENT
  );

  adjustedThrottle = constrain(
    adjustedThrottle,
    MIN_MOTION_PERCENT,
    MAX_THROTTLE_PERCENT
  );

  return map(
    adjustedThrottle,
    0,
    100,
    ESC_MIN,
    ESC_MAX
  );
}

// =====================================================
//                    STOP MOTORS
// =====================================================

void stopMotors() {

  esc1.writeMicroseconds(ESC_MIN);
  esc2.writeMicroseconds(ESC_MIN);
}

// =====================================================
//            RESET SHARED TIMER (given timestamp)
// =====================================================

// Always pass in a timestamp already captured earlier in the
// current function call (e.g. currentTime), NOT a fresh millis()
// call taken after a batch of Serial prints. Serial printing at
// 9600 baud takes real time, and calling millis() again afterward
// would desync this timer from the one used to trigger the reset.
void resetTimeoutTo(unsigned long timestamp) {

  lastCommandTime = timestamp;
  lastCountdownPrint = timestamp;

  timeoutMessagePrinted = false;
}

// Convenience wrapper for manual (non-sequence) commands, where
// there's no earlier timestamp to reuse.
void resetTimeout() {
  resetTimeoutTo(millis());
}

// =====================================================
//               EXECUTE MOTOR COMMAND
// =====================================================

void executeCommand(char command) {

  command = toupper(command);

  int leftThrottle = 0;
  int rightThrottle = 0;

  switch (command) {

    case 'W':
      leftThrottle = W_LEFT;
      rightThrottle = W_RIGHT;
      break;

    case 'S':
      leftThrottle = S_LEFT;
      rightThrottle = S_RIGHT;
      break;

    case 'A':
      leftThrottle = A_LEFT;
      rightThrottle = A_RIGHT;
      break;

    case 'D':
      leftThrottle = D_LEFT;
      rightThrottle = D_RIGHT;
      break;

    case 'E':
      leftThrottle = E_LEFT;
      rightThrottle = E_RIGHT;
      break;

    default:
      Serial.print("Unknown command: ");
      Serial.println(command);
      return;
  }

  int leftPulse = throttleToPulse(leftThrottle);
  int rightPulse = throttleToPulse(rightThrottle);

  esc1.writeMicroseconds(leftPulse);
  esc2.writeMicroseconds(rightPulse);

  Serial.print("Command: ");
  Serial.println(command);

  Serial.print("Motor 1: ");
  Serial.print(leftThrottle);
  Serial.print("%  | Pulse: ");
  Serial.println(leftPulse);

  Serial.print("Motor 2: ");
  Serial.print(rightThrottle);
  Serial.print("%  | Pulse: ");
  Serial.println(rightPulse);
}

// =====================================================
//        MANUAL NUMERIC THROTTLE (e.g. "10 15")
// =====================================================

// Bypasses the WASD lookup table entirely: left/right are
// throttle percentages (0-100) supplied directly over serial,
// e.g. "10 15" or "0 1".
void executeManualThrottle(int leftThrottle, int rightThrottle) {

  leftThrottle = constrain(leftThrottle, 0, 100);
  rightThrottle = constrain(rightThrottle, 0, 100);

  int leftPulse = throttleToPulse(leftThrottle);
  int rightPulse = throttleToPulse(rightThrottle);

  esc1.writeMicroseconds(leftPulse);
  esc2.writeMicroseconds(rightPulse);

  Serial.print("Manual throttle -> Left: ");
  Serial.print(leftThrottle);
  Serial.print("%  Right: ");
  Serial.print(rightThrottle);
  Serial.println("%");

  Serial.print("Motor 1: ");
  Serial.print(leftThrottle);
  Serial.print("%  | Pulse: ");
  Serial.println(leftPulse);

  Serial.print("Motor 2: ");
  Serial.print(rightThrottle);
  Serial.print("%  | Pulse: ");
  Serial.println(rightPulse);
}

// =====================================================
//                 START Q SEQUENCE
// =====================================================

void startQSequence() {

  unsigned long now = millis();

  qSequenceRunning = true;
  qSequenceStep = 0;

  Serial.println();
  Serial.println("****************************************");
  Serial.println("          Q SEQUENCE START");
  Serial.println("****************************************");

  Serial.print("Total steps: ");
  Serial.println(Q_SEQUENCE_LENGTH);

  Serial.print("Step time: ");
  Serial.print(COMMAND_TIMEOUT);
  Serial.println(" ms");

  Serial.println();

  executeCommand(Q_SEQUENCE[qSequenceStep]);

  Serial.print("[STEP ");
  Serial.print(qSequenceStep + 1);
  Serial.print("/");
  Serial.print(Q_SEQUENCE_LENGTH);
  Serial.print("] -> ");
  Serial.println(Q_SEQUENCE[qSequenceStep]);

  // Reuse the timestamp captured at the top of this function,
  // not a fresh millis() call taken after all the printing above.
  resetTimeoutTo(now);
}

// =====================================================
//         ADVANCE Q SEQUENCE (called by the shared timer)
// =====================================================

void advanceQSequence(unsigned long now) {

  qSequenceStep++;

  // Sequence finished
  if (qSequenceStep >= Q_SEQUENCE_LENGTH) {

    qSequenceRunning = false;

    stopMotors();

    Serial.println();
    Serial.println("****************************************");
    Serial.println("          Q SEQUENCE COMPLETE");
    Serial.println("****************************************");

    Serial.println("Motors stopped.");
    Serial.println();

    resetTimeoutTo(now);

    return;
  }

  // Execute next command
  executeCommand(Q_SEQUENCE[qSequenceStep]);

  Serial.print("[STEP ");
  Serial.print(qSequenceStep + 1);
  Serial.print("/");
  Serial.print(Q_SEQUENCE_LENGTH);
  Serial.print("] -> ");
  Serial.println(Q_SEQUENCE[qSequenceStep]);

  resetTimeoutTo(now);
}

// =====================================================
//        UPDATE SHARED TIMER (timeout + Q sequence)
// =====================================================

void updateTimeout() {

  unsigned long currentTime = millis();

  unsigned long elapsed =
    currentTime - lastCommandTime;

  // ===================================================
  // SHARED TIMER EXPIRED
  // ===================================================

  if (elapsed >= COMMAND_TIMEOUT) {

    if (qSequenceRunning) {

      // Timer expiring during a Q sequence means it's
      // time to move to the next step.
      advanceQSequence(currentTime);

    } else {

      // Timer expiring with no sequence running means a
      // real safety timeout: no command received in time.
      stopMotors();

      if (!timeoutMessagePrinted) {

        Serial.println();
        Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        Serial.println("                 TIMEOUT");
        Serial.println("           MOTORS HAVE STOPPED");
        Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");

        timeoutMessagePrinted = true;
      }
    }

    return;
  }

  // ===================================================
  // COUNTDOWN
  // ===================================================

  if (currentTime - lastCountdownPrint >= COUNTDOWN_INTERVAL) {

    lastCountdownPrint = currentTime;

    unsigned long remainingSeconds =
      (COMMAND_TIMEOUT - elapsed + 999) / 1000;

    updateCountdown(remainingSeconds);
  }
}

// =====================================================
//                       SETUP
// =====================================================

void setup() {

  Serial.begin(9600);
  delay(2000);
  Serial.println("************************************************************");

  // Power on the ESCs (via MOSFET/relay) BEFORE attaching/arming
  // them, and give the rail a moment to settle.
  pinMode(ESC_POWER_PIN, OUTPUT);
  digitalWrite(ESC_POWER_PIN, HIGH);
  delay(200);

  esc1.attach(ESC1_PIN);
  esc2.attach(ESC2_PIN);

  // Send minimum throttle during ESC startup
  esc1.writeMicroseconds(ESC_MIN);
  esc2.writeMicroseconds(ESC_MIN);

  Serial.println();
  Serial.println("========================================");
  Serial.println("           ESC INITIALIZATION");
  Serial.println("========================================");

  Serial.println("Initializing ESCs...");
  Serial.print("Startup time: ");
  Serial.print(ESC_STARTUP_TIME / 1000);
  Serial.println(" seconds");

  delay(ESC_STARTUP_TIME);

  stopMotors();

  lastCommandTime = millis();
  lastCountdownPrint = millis();

  Serial.println();
  Serial.println("========================================");
  Serial.println("              SYSTEM READY");
  Serial.println("========================================");
  Serial.println();
  Serial.println("========================================");
  printLogo();
  Serial.println("========================================");
  Serial.println();
  Serial.println("Manual commands:");
  Serial.println("  W = Forward");
  Serial.println("  A = Left");
  Serial.println("  S = Stop");
  Serial.println("  D = Right");
  Serial.println("  E = Boost");
  Serial.println("  Q = Run sequence");

  Serial.println();
  Serial.print("Timeout / step time: ");
  Serial.print(COMMAND_TIMEOUT / 1000);
  Serial.println(" seconds");

  Serial.println();
}

// =====================================================
//                        LOOP
// =====================================================

void loop() {

  // ===================================================
  // CHECK FOR SERIAL COMMAND
  // ===================================================

  if (Serial.available() > 0) {

    char peekChar = Serial.peek();

    // =================================================
    // NUMERIC MANUAL THROTTLE ("<left> <right>")
    // =================================================

    if (isDigit(peekChar) || peekChar == '-') {

      int leftThrottle = Serial.parseInt();
      int rightThrottle = Serial.parseInt();

      // Manual command cancels Q sequence
      if (qSequenceRunning) {

        qSequenceRunning = false;

        Serial.println();
        Serial.println("Q sequence cancelled.");

      }

      executeManualThrottle(leftThrottle, rightThrottle);

      resetTimeout();

      Serial.println();
      Serial.println("----------------------------------------");
      Serial.print("Timeout timer reset to ");
      Serial.print(COMMAND_TIMEOUT / 1000);
      Serial.println(" seconds.");
      Serial.println("========================================");

      return;
    }

    char command = Serial.read();

    command = toupper(command);

    // Ignore newline and carriage return
    if (command == '\n' || command == '\r') {
      return;
    }

    // =================================================
    // MANUAL COMMAND
    // =================================================

    if (
      command == 'W' ||
      command == 'A' ||
      command == 'S' ||
      command == 'D' ||
      command == 'E'
    ) {

      // Manual command cancels Q sequence
      if (qSequenceRunning) {

        qSequenceRunning = false;

        Serial.println();
        Serial.println("Q sequence cancelled.");
      }

      executeCommand(command);

      resetTimeout();

      Serial.println();
      Serial.println("----------------------------------------");
      Serial.print("Timeout timer reset to ");
      Serial.print(COMMAND_TIMEOUT / 1000);
      Serial.println(" seconds.");
      Serial.println("========================================");
    }

    // =================================================
    // Q SEQUENCE
    // =================================================

    else if (command == 'Q') {

      // Start Q sequence
      startQSequence();
    }
  }

  // ===================================================
  // UPDATE SHARED TIMER (drives both timeout and
  // Q-sequence step advancement)
  // ===================================================

  updateTimeout();
}