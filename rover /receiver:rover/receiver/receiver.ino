#include <Servo.h>
#include <SPI.h>
#include <LoRa.h>

Servo esc1;
Servo esc2;

// =====================================================
//                    LoRa SETTINGS
// =====================================================
// NOTE: the LoRa example used nss=7, rst=8, dio0=9, which
// collide with this rover's ESC_POWER_PIN (8) and ESC1_PIN (9).
// Moved to free pins instead. SPI data lines (MOSI/MISO/SCK)
// still use the board's fixed hardware SPI pins (11/12/13 on
// an Uno/Nano) - verify this matches your actual board.
#define LORA_NSS   4
#define LORA_RST   5
#define LORA_DIO0  6

#define LORA_FREQUENCY 433E6

// Simple addressing so multiple rovers/controllers can share
// the same frequency. 0xFF = accept broadcast to everyone.
const byte LOCAL_ADDRESS = 0x01;
const byte BROADCAST_ADDRESS = 0xFF;

// =====================================================
//                       LOGO
// =====================================================

const char* logo[] = {
  "  _______________________________________________  ",
  " /                                               \\ ",
  "|             P E R I O D I C A L L Y             |",
  " \\_______________________________________________/ ",
  "",
  "     a  i  i  3            Research",
  "     a  i  i  3            Vehicle",
  "     a  i  i  3            2026 (LoRa)",
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

const int MIN_MOTION_PERCENT = 20;
const int MAX_THROTTLE_PERCENT = 60;

// =====================================================
//                    ESC SETTINGS
// =====================================================

const int ESC_MIN = 1000;
const int ESC_MAX = 2000;
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
//
// With a wireless link this also acts as a "lost link" cutoff:
// if packets stop arriving (out of range, jammed, transmitter
// died) the rover still stops itself after this many ms.
const unsigned long COMMAND_TIMEOUT = 5000;

// =====================================================
//                 MOTOR COMMAND SETTINGS
// =====================================================

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
  'W', 'W', 'W', 'D', 'W', 'D', 'S'
};

const int Q_SEQUENCE_LENGTH =
  sizeof(Q_SEQUENCE) / sizeof(Q_SEQUENCE[0]);

bool qSequenceRunning = false;
int qSequenceStep = 0;

// =====================================================
//                  TIMEOUT VARIABLES
// =====================================================

unsigned long lastCommandTime = 0;
unsigned long lastCountdownPrint = 0;
const unsigned long COUNTDOWN_INTERVAL = 1000;
bool timeoutMessagePrinted = false;

// =====================================================
//               COUNTDOWN DISPLAY
// =====================================================

void updateCountdown(unsigned long seconds) {

  static unsigned long previousSeconds = 0xFFFFFFFF;

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

void resetTimeoutTo(unsigned long timestamp) {
  lastCommandTime = timestamp;
  lastCountdownPrint = timestamp;
  timeoutMessagePrinted = false;
}

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

  resetTimeoutTo(now);
}

// =====================================================
//         ADVANCE Q SEQUENCE (called by the shared timer)
// =====================================================

void advanceQSequence(unsigned long now) {

  qSequenceStep++;

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
  unsigned long elapsed = currentTime - lastCommandTime;

  if (elapsed >= COMMAND_TIMEOUT) {

    if (qSequenceRunning) {

      advanceQSequence(currentTime);

    } else {

      stopMotors();

      if (!timeoutMessagePrinted) {

        Serial.println();
        Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        Serial.println("                 TIMEOUT");
        Serial.println("       (no LoRa command received)");
        Serial.println("           MOTORS HAVE STOPPED");
        Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");

        timeoutMessagePrinted = true;
      }
    }

    return;
  }

  if (currentTime - lastCountdownPrint >= COUNTDOWN_INTERVAL) {

    lastCountdownPrint = currentTime;

    unsigned long remainingSeconds =
      (COMMAND_TIMEOUT - elapsed + 999) / 1000;

    updateCountdown(remainingSeconds);
  }
}

// =====================================================
//        PROCESS A COMMAND STRING (from LoRa payload)
// =====================================================
// Same dispatch logic that used to live inline in loop() reading
// from Serial. Handles two payload shapes:
//   "10 15"  -> manual left/right throttle percentages
//   "W"      -> single-letter WASD/E/Q command
void processIncomingCommand(String msg) {

  Serial.print("processIncomingCommand entered, raw length: ");
  Serial.println(msg.length());

  msg.trim();

  Serial.print("After trim, length: ");
  Serial.print(msg.length());
  Serial.print(", first byte as int: ");
  if (msg.length() > 0) {
    Serial.println((int)msg.charAt(0));
  } else {
    Serial.println("N/A (empty)");
  }

  if (msg.length() == 0) {
    return;
  }

  char firstChar = msg.charAt(0);

  // ---------------------------------------------------
  // NUMERIC MANUAL THROTTLE ("<left> <right>")
  // ---------------------------------------------------
  if (isDigit(firstChar) || firstChar == '-') {

    int spaceIndex = msg.indexOf(' ');

    if (spaceIndex == -1) {
      Serial.println("Malformed manual throttle payload (need 'L R').");
      return;
    }

    int leftThrottle = msg.substring(0, spaceIndex).toInt();
    int rightThrottle = msg.substring(spaceIndex + 1).toInt();

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

  // ---------------------------------------------------
  // SINGLE-LETTER COMMAND
  // ---------------------------------------------------
  char command = toupper(firstChar);

  if (
    command == 'W' ||
    command == 'A' ||
    command == 'S' ||
    command == 'D' ||
    command == 'E'
  ) {

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

  } else if (command == 'Q') {

    startQSequence();

  } else {

    Serial.print("Unknown LoRa command: ");
    Serial.println(msg);
  }
}

// =====================================================
//              LoRa PACKET RECEPTION
// =====================================================
// Mirrors the header format from the LoRa receiver example:
// [destination][sender][msgId][length][payload...]
// Filters on address so a rover only reacts to packets meant
// for it (or a broadcast).
void checkLoRa() {

  int packetSize = LoRa.parsePacket();

  if (packetSize == 0) {
    return;
  }

  if (packetSize < 4) {
    // Too short to contain the header; drain and ignore.
    while (LoRa.available()) {
      LoRa.read();
    }
    Serial.println("LoRa: dropped undersized packet.");
    return;
  }

  byte destinationAddress = LoRa.read();
  byte senderAddress = LoRa.read();
  byte incomingMsgId = LoRa.read();
  byte incomingLength = LoRa.read();

  String incoming = "";
  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  if (incoming.length() != incomingLength) {
    Serial.println("LoRa: length mismatch, dropping packet.");
    return;
  }

  if (
    destinationAddress != LOCAL_ADDRESS &&
    destinationAddress != BROADCAST_ADDRESS
  ) {
    // Not addressed to this rover - ignore silently.
    return;
  }

  Serial.print("LoRa RX <- from 0x");
  Serial.print(senderAddress, HEX);
  Serial.print(" id ");
  Serial.print(incomingMsgId);
  Serial.print(" len ");
  Serial.print(incomingLength);
  Serial.print(" msg: ");
  Serial.println(incoming);

  Serial.println("Calling processIncomingCommand...");
  processIncomingCommand(incoming);
  Serial.println("Returned from processIncomingCommand.");
}

// =====================================================
//                       SETUP
// =====================================================

void setup() {

  Serial.begin(115200);
  delay(2000);
  Serial.println("************************************************************");

  // Power on the ESCs (via MOSFET/relay) BEFORE attaching/arming
  // them, and give the rail a moment to settle.
  pinMode(ESC_POWER_PIN, OUTPUT);
  digitalWrite(ESC_POWER_PIN, HIGH);
  delay(200);

  esc1.attach(ESC1_PIN);
  esc2.attach(ESC2_PIN);

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

  // -----------------------------------------------------
  // LoRa init
  // -----------------------------------------------------
  Serial.println();
  Serial.println("========================================");
  Serial.println("           LoRa INITIALIZATION");
  Serial.println("========================================");

  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("Starting LoRa failed! Halting.");
    Serial.println("Check wiring/pins and frequency for your module region.");
    stopMotors();
    while (1) {
      // Also cut ESC power on a hard LoRa failure, so a motor
      // can't be left commanded with a dead radio link.
      digitalWrite(ESC_POWER_PIN, LOW);
      delay(1000);
    }
  }

  Serial.print("LoRa ready @ ");
  Serial.print(LORA_FREQUENCY / 1E6);
  Serial.println(" MHz");

  Serial.print("Local address: 0x");
  Serial.println(LOCAL_ADDRESS, HEX);

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
  Serial.println("Commands accepted over LoRa (payload text):");
  Serial.println("  W = Forward");
  Serial.println("  A = Left");
  Serial.println("  S = Stop");
  Serial.println("  D = Right");
  Serial.println("  E = Boost");
  Serial.println("  Q = Run sequence");
  Serial.println("  \"L R\" = manual throttle percentages, e.g. \"10 15\"");

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

  // Commands now arrive over LoRa instead of Serial.
  checkLoRa();

  // Same shared safety-timeout / Q-sequence driver as before.
  updateTimeout();
}