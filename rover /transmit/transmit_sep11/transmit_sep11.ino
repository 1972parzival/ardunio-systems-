#include <SPI.h>
#include <LoRa.h>

// =====================================================
//                    LoRa SETTINGS
// =====================================================
// This is the CONTROLLER/transmitter board - separate from
// the rover, so there's no clash with ESC pins here. Using
// the same pins as the original two-way example.
#define nss 7
#define rst 8
#define dio0 9

#define LORA_FREQUENCY 433E6

// Addressing must match the rover receiver:
//   receiver's LOCAL_ADDRESS = 0x01  -> that's our destination
//   this controller's own address     -> 0xBB (from the example)
byte localAddress = 0xBB;
byte destination  = 0x01;

byte msgCount = 0;

// =====================================================
//                       SETUP
// =====================================================

void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println("************************************************");
  Serial.println("        ROVER LoRa CONTROLLER (transmitter)");
  Serial.println("************************************************");

  LoRa.setPins(nss, rst, dio0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("Starting LoRa failed!");
    while (1) {
      delay(100);
    }
  }

  Serial.print("LoRa ready @ ");
  Serial.print(LORA_FREQUENCY / 1E6);
  Serial.println(" MHz");

  Serial.print("Local address: 0x");
  Serial.println(localAddress, HEX);

  Serial.print("Target rover address: 0x");
  Serial.println(destination, HEX);

  Serial.println();
  Serial.println("Type a command and press enter:");
  Serial.println("  W = Forward");
  Serial.println("  A = Left");
  Serial.println("  S = Stop");
  Serial.println("  D = Right");
  Serial.println("  E = Boost");
  Serial.println("  Q = Run sequence");
  Serial.println("  \"L R\" = manual throttle percentages, e.g. \"10 15\"");
  Serial.println();
}

// =====================================================
//                  SEND ONE PACKET
// =====================================================
// Same header format the rover receiver expects:
// [destination][sender][msgId][length][payload...]
void sendMessage(String outgoing) {

  LoRa.beginPacket();
  LoRa.write(destination);
  LoRa.write(localAddress);
  LoRa.write(msgCount);
  LoRa.write((byte)outgoing.length());
  LoRa.print(outgoing);
  LoRa.endPacket();

  Serial.print("Sent -> \"");
  Serial.print(outgoing);
  Serial.print("\"  (id ");
  Serial.print(msgCount);
  Serial.println(")");

  msgCount++;
  // byte wraps 255 -> 0 on its own, no manual check needed.
}

// =====================================================
//                        LOOP
// =====================================================
// Reads one line at a time from Serial (the operator's
// keyboard / a joystick bridge / whatever is driving this
// controller) and forwards it as a LoRa packet, unmodified,
// to the rover. The rover's own command parsing (single
// letters or "L R" throttle pairs) handles the payload -
// this side just needs to relay text reliably.
void loop() {

  if (Serial.available() > 0) {

    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) {
      return;
    }

    sendMessage(line);
  }
}