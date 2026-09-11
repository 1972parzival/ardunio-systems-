#include <SPI.h>
#include <LoRa.h>

// =====================================================
//                    LoRa SETTINGS
// =====================================================
#define nss 7
#define rst 8
#define dio0 9

#define LORA_FREQUENCY 433E6

byte localAddress = 0xBB;
byte destination  = 0x01;

byte msgCount = 0;

// Fixed-size input buffer instead of String - avoids heap
// allocation/fragmentation on a memory-constrained Uno.
#define MAX_LINE_LEN 32
char lineBuf[MAX_LINE_LEN + 1];
int lineLen = 0;

// =====================================================
//                       SETUP
// =====================================================

void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println(F("************************************************"));
  Serial.println(F("        ROVER LoRa CONTROLLER (transmitter)"));
  Serial.println(F("************************************************"));

  LoRa.setPins(nss, rst, dio0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println(F("Starting LoRa failed!"));
    while (1) {
      delay(100);
    }
  }

  Serial.print(F("LoRa ready @ "));
  Serial.print(LORA_FREQUENCY / 1E6);
  Serial.println(F(" MHz"));

  Serial.print(F("Local address: 0x"));
  Serial.println(localAddress, HEX);

  Serial.print(F("Target rover address: 0x"));
  Serial.println(destination, HEX);

  Serial.println();
  Serial.println(F("Type a command and press enter:"));
  Serial.println(F("  W = Forward"));
  Serial.println(F("  A = Left"));
  Serial.println(F("  S = Stop"));
  Serial.println(F("  D = Right"));
  Serial.println(F("  E = Boost"));
  Serial.println(F("  Q = Run sequence"));
  Serial.println(F("  \"L R\" = manual throttle percentages, e.g. \"10 15\""));
  Serial.println();
}

// =====================================================
//                  SEND ONE PACKET
// =====================================================
// [destination][sender][msgId][length][payload...]
void sendMessage(char* outgoing, int len) {

  LoRa.beginPacket();
  LoRa.write(destination);
  LoRa.write(localAddress);
  LoRa.write(msgCount);
  LoRa.write((byte)len);
  LoRa.write((const uint8_t*)outgoing, len);
  LoRa.endPacket();

  Serial.print(F("Sent -> \""));
  Serial.print(outgoing);
  Serial.print(F("\"  (id "));
  Serial.print(msgCount);
  Serial.println(F(")"));

  msgCount++;
}

// =====================================================
//                        LOOP
// =====================================================
// Reads one line at a time from Serial into a fixed buffer
// (no String growth) and forwards it as-is to the rover.
void loop() {

  while (Serial.available() > 0) {

    char c = Serial.read();

    if (c == '\n' || c == '\r') {

      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        sendMessage(lineBuf, lineLen);
        lineLen = 0;
      }

    } else if (lineLen < MAX_LINE_LEN) {

      lineBuf[lineLen++] = c;
    }
    // characters beyond MAX_LINE_LEN are silently dropped
  }
}