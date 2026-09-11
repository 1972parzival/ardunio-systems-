#include <Servo.h>
#include <SPI.h>
#include <LoRa.h>


Servo esc1;
Servo esc2;

// =====================================================
//                    LoRa SETTINGS
// =====================================================
#define LORA_NSS   4
#define LORA_RST   5
#define LORA_DIO0  6

#define LORA_FREQUENCY 433E6

const byte LOCAL_ADDRESS = 0x01;
const byte BROADCAST_ADDRESS = 0xFF;

// Max characters accepted in one LoRa payload. Fixed-size buffer
// instead of String to avoid heap allocation on every packet -
// an Uno only has 2KB SRAM total and heap fragmentation from
// repeated String concatenation can corrupt other state.
#define MAX_INCOMING_LEN 32

// =====================================================
//                       LOGO
// =====================================================
// Printed directly with F() (keeps the text in flash instead of
// copying it into RAM at boot) rather than via a char* array,
// since a plain array of string literals still lives in RAM.

void printLogo() {
  Serial.println(F("  _______________________________________________  "));
  Serial.println(F(" /                                               \\ "));
  Serial.println(F("|             P E R I O D I C A L L Y             |"));
  Serial.println(F(" \\_______________________________________________/ "));
  Serial.println();
  Serial.println(F("     a  i  i  3            Research"));
  Serial.println(F("     a  i  i  3            Vehicle"));
  Serial.println(F("     a  i  i  3            2026 (LoRa)"));
  Serial.println();
  Serial.println();
}

// =====================================================
//                    PIN SETTINGS
// =====================================================

const int ESC1_PIN = 10;   // Motor 1 - Left
const int ESC2_PIN = 9;  // Motor 2 - Right

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

const unsigned long COMMAND_TIMEOUT = 5000;

// =====================================================
//                 MOTOR COMMAND SETTINGS
// =====================================================

const int W_LEFT  = 10;
const int W_RIGHT = 10;

const int S_LEFT  = 0;
const int S_RIGHT = 0;

const int A_LEFT  = 7;
const int A_RIGHT = 10;

const int D_LEFT  = 10;
const int D_RIGHT = 7;

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

  Serial.print(F("Countdown: "));
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
      Serial.print(F("Unknown command: "));
      Serial.println(command);
      return;
  }

  int leftPulse = throttleToPulse(leftThrottle);
  int rightPulse = throttleToPulse(rightThrottle);

  esc1.writeMicroseconds(leftPulse);
  esc2.writeMicroseconds(rightPulse);

  Serial.print(F("Command: "));
  Serial.println(command);

  Serial.print(F("Motor 1: "));
  Serial.print(leftThrottle);
  Serial.print(F("%  | Pulse: "));
  Serial.println(leftPulse);

  Serial.print(F("Motor 2: "));
  Serial.print(rightThrottle);
  Serial.print(F("%  | Pulse: "));
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

  Serial.print(F("Manual throttle -> Left: "));
  Serial.print(leftThrottle);
  Serial.print(F("%  Right: "));
  Serial.print(rightThrottle);
  Serial.println(F("%"));

  Serial.print(F("Motor 1: "));
  Serial.print(leftThrottle);
  Serial.print(F("%  | Pulse: "));
  Serial.println(leftPulse);

  Serial.print(F("Motor 2: "));
  Serial.print(rightThrottle);
  Serial.print(F("%  | Pulse: "));
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
  Serial.println(F("****************************************"));
  Serial.println(F("          Q SEQUENCE START"));
  Serial.println(F("****************************************"));

  Serial.print(F("Total steps: "));
  Serial.println(Q_SEQUENCE_LENGTH);

  Serial.print(F("Step time: "));
  Serial.print(COMMAND_TIMEOUT);
  Serial.println(F(" ms"));

  Serial.println();

  executeCommand(Q_SEQUENCE[qSequenceStep]);

  Serial.print(F("[STEP "));
  Serial.print(qSequenceStep + 1);
  Serial.print(F("/"));
  Serial.print(Q_SEQUENCE_LENGTH);
  Serial.print(F("] -> "));
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
    Serial.println(F("****************************************"));
    Serial.println(F("          Q SEQUENCE COMPLETE"));
    Serial.println(F("****************************************"));
    Serial.println(F("Motors stopped."));
    Serial.println();

    resetTimeoutTo(now);
    return;
  }

  executeCommand(Q_SEQUENCE[qSequenceStep]);

  Serial.print(F("[STEP "));
  Serial.print(qSequenceStep + 1);
  Serial.print(F("/"));
  Serial.print(Q_SEQUENCE_LENGTH);
  Serial.print(F("] -> "));
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
        Serial.println(F("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"));
        Serial.println(F("                 TIMEOUT"));
        Serial.println(F("       (no LoRa command received)"));
        Serial.println(F("           MOTORS HAVE STOPPED"));
        Serial.println(F("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"));

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
// Takes a plain, null-terminated char buffer instead of a
// String - avoids heap allocation on every packet, which on
// an Uno's 2KB SRAM can fragment the heap and cause exactly
// the kind of "runs fine, then silently stalls" behavior we
// were chasing.
void processIncomingCommand(char* msg) {

  // Trim trailing whitespace/control characters in place.
  int len = strlen(msg);
  while (len > 0 && isspace((unsigned char)msg[len - 1])) {
    msg[--len] = '\0';
  }

  // Skip leading whitespace.
  char* start = msg;
  while (*start != '\0' && isspace((unsigned char)*start)) {
    start++;
  }

  if (*start == '\0') {
    return;
  }

  char firstChar = start[0];

  // ---------------------------------------------------
  // NUMERIC MANUAL THROTTLE ("<left> <right>")
  // ---------------------------------------------------
  if (isDigit((unsigned char)firstChar) || firstChar == '-') {

    char* spacePos = strchr(start, ' ');

    if (spacePos == NULL) {
      Serial.println(F("Malformed manual throttle payload (need 'L R')."));
      return;
    }

    int leftThrottle = atoi(start);
    int rightThrottle = atoi(spacePos + 1);

    if (qSequenceRunning) {
      qSequenceRunning = false;
      Serial.println();
      Serial.println(F("Q sequence cancelled."));
    }

    executeManualThrottle(leftThrottle, rightThrottle);
    resetTimeout();

    Serial.println();
    Serial.println(F("----------------------------------------"));
    Serial.print(F("Timeout timer reset to "));
    Serial.print(COMMAND_TIMEOUT / 1000);
    Serial.println(F(" seconds."));
    Serial.println(F("========================================"));

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
      Serial.println(F("Q sequence cancelled."));
    }

    executeCommand(command);
    resetTimeout();

    Serial.println();
    Serial.println(F("----------------------------------------"));
    Serial.print(F("Timeout timer reset to "));
    Serial.print(COMMAND_TIMEOUT / 1000);
    Serial.println(F(" seconds."));
    Serial.println(F("========================================"));

  } else if (command == 'Q') {

    startQSequence();

  } else {

    Serial.print(F("Unknown LoRa command: "));
    Serial.println(start);
  }
}

// =====================================================
//              LoRa PACKET RECEPTION
// =====================================================
// [destination][sender][msgId][length][payload...]
void checkLoRa() {

  int packetSize = LoRa.parsePacket();

  if (packetSize == 0) {
    return;
  }

  if (packetSize < 4) {
    while (LoRa.available()) {
      LoRa.read();
    }
    Serial.println(F("LoRa: dropped undersized packet."));
    return;
  }

  byte destinationAddress = LoRa.read();
  byte senderAddress = LoRa.read();
  byte incomingMsgId = LoRa.read();
  byte incomingLength = LoRa.read();

  static char incomingBuf[MAX_INCOMING_LEN + 1];
  int idx = 0;

  while (LoRa.available()) {
    char c = (char)LoRa.read();
    if (idx < MAX_INCOMING_LEN) {
      incomingBuf[idx++] = c;
    }
    // Extra bytes beyond MAX_INCOMING_LEN are drained but dropped.
  }
  incomingBuf[idx] = '\0';

  if (idx != incomingLength) {
    Serial.println(F("LoRa: length mismatch, dropping packet."));
    return;
  }

  if (
    destinationAddress != LOCAL_ADDRESS &&
    destinationAddress != BROADCAST_ADDRESS
  ) {
    return;
  }

  Serial.print(F("LoRa RX <- from 0x"));
  Serial.print(senderAddress, HEX);
  Serial.print(F(" id "));
  Serial.print(incomingMsgId);
  Serial.print(F(" len "));
  Serial.print(incomingLength);
  Serial.print(F(" msg: "));
  Serial.println(incomingBuf);

  processIncomingCommand(incomingBuf);
}

// =====================================================
//                       SETUP
// =====================================================

void setup() {

  Serial.begin(115200);
  delay(2000);
  Serial.println(F("************************************************************"));

  pinMode(ESC_POWER_PIN, OUTPUT);
  digitalWrite(ESC_POWER_PIN, HIGH);
  delay(200);

  esc1.attach(ESC1_PIN);
  esc2.attach(ESC2_PIN);

  esc1.writeMicroseconds(ESC_MIN);
  esc2.writeMicroseconds(ESC_MIN);

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("           ESC INITIALIZATION"));
  Serial.println(F("========================================"));

  Serial.println(F("Initializing ESCs..."));
  Serial.print(F("Startup time: "));
  Serial.print(ESC_STARTUP_TIME / 1000);
  Serial.println(F(" seconds"));

  delay(ESC_STARTUP_TIME);

  stopMotors();

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("           LoRa INITIALIZATION"));
  Serial.println(F("========================================"));

  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println(F("Starting LoRa failed! Halting."));
    Serial.println(F("Check wiring/pins and frequency for your module region."));
    stopMotors();
    while (1) {
      digitalWrite(ESC_POWER_PIN, LOW);
      delay(1000);
    }
  }

  Serial.print(F("LoRa ready @ "));
  Serial.print(LORA_FREQUENCY / 1E6);
  Serial.println(F(" MHz"));

  Serial.print(F("Local address: 0x"));
  Serial.println(LOCAL_ADDRESS, HEX);

  lastCommandTime = millis();
  lastCountdownPrint = millis();

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("              SYSTEM READY"));
  Serial.println(F("========================================"));
  Serial.println();
  Serial.println(F("========================================"));
  printLogo();
  Serial.println(F("========================================"));
  Serial.println();
  Serial.println(F("Commands accepted over LoRa (payload text):"));
  Serial.println(F("  W = Forward"));
  Serial.println(F("  A = Left"));
  Serial.println(F("  S = Stop"));
  Serial.println(F("  D = Right"));
  Serial.println(F("  E = Boost"));
  Serial.println(F("  Q = Run sequence"));
  Serial.println(F("  \"L R\" = manual throttle percentages, e.g. \"10 15\""));

  Serial.println();
  Serial.print(F("Timeout / step time: "));
  Serial.print(COMMAND_TIMEOUT / 1000);
  Serial.println(F(" seconds"));

  Serial.println();
}

// =====================================================
//                        LOOP
// =====================================================

void loop() {

  checkLoRa();
  updateTimeout();
}