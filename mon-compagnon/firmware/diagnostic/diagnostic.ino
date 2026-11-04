// Mon compagnon: diagnostic only, no application behavior and no persistent writes.
// Pin provenance and limitations: ../README.md.
#include <Arduino.h>
#include <Wire.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "This diagnostic requires an ESP32-S3 target."
#endif

namespace {
constexpr uint8_t kSda = 41;
constexpr uint8_t kScl = 42;
constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kHeartbeatMs = 15000;

struct Input {
  uint8_t pin;
  const char* name;
  bool candidate;
  bool stable;
  uint32_t changedAt;
};

Input inputs[] = {
    {0, "BOOT", HIGH, HIGH, 0},
    {4, "ROCKER_UP", HIGH, HIGH, 0},
    {5, "ROCKER_CLICK", HIGH, HIGH, 0},
    {6, "ROCKER_DOWN", HIGH, HIGH, 0},
};

struct Probe {
  uint8_t address;
  const char* expected;
};

constexpr Probe probes[] = {
    {0x34, "PMIC (TG28 documentation / AXP2101 driver)"},
    {0x18, "ES8311 audio codec"},
    {0x51, "PCF85063 RTC"},
    {0x6A, "QMI8658 alternative address"},
    {0x6B, "QMI8658 reference address"},
    {0x70, "SHTC3 temperature/humidity"},
};

void printIdentity() {
  Serial.println();
  Serial.println("=== MON COMPAGNON : DIAGNOSTIC PAS APPLICATION ===");
  Serial.println("No recording, display update, SD mount, PMIC configuration or network.");
  Serial.printf("Chip: %s rev %u, cores: %u, CPU: %u MHz\n",
                ESP.getChipModel(), static_cast<unsigned>(ESP.getChipRevision()),
                static_cast<unsigned>(ESP.getChipCores()),
                static_cast<unsigned>(ESP.getCpuFreqMHz()));
  Serial.printf("SDK: %s\n", ESP.getSdkVersion());
  Serial.printf("Flash detected: %lu bytes (expected 16777216)\n",
                static_cast<unsigned long>(ESP.getFlashChipSize()));
  Serial.printf("PSRAM usable by this build: %lu bytes (board expected 8388608)\n",
                static_cast<unsigned long>(ESP.getPsramSize()));
  Serial.printf("Free heap: %lu bytes; free PSRAM: %lu bytes\n",
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getFreePsram()));
  Serial.println("PSRAM=0 can mean a build configuration issue; it does not prove absent hardware.");
  Serial.println("BOOT must be released at reset/power-on; holding it can enter ROM download.");
  Serial.println("PWR is managed by the PMIC: it is not a direct button GPIO in this test.");
  Serial.println("A sustained PWR hold may cut power in hardware. This test cannot prevent it.");
  Serial.println("SD: NOT TESTED (kept unmounted; no read-only mount guarantee here).");
  Serial.println("Display/audio/sensors: not initialized; no functional test claimed.");
}

void probeI2c() {
  Serial.println("I2C targeted address-only probes: SDA41 / SCL42, 100 kHz.");
  if (!Wire.begin(kSda, kScl, 100000)) {
    Serial.println("I2C bus initialization FAILED; probes skipped.");
    return;
  }
  Wire.setTimeOut(50);
  for (const Probe& probe : probes) {
    // No register address, command or data byte is queued. Only START/address/STOP.
    Wire.beginTransmission(probe.address);
    const uint8_t status = Wire.endTransmission(true);
    Serial.printf("I2C 0x%02X: %s [status %u] expected: %s\n", probe.address,
                  status == 0 ? "ACK (presence only)" : "NO ACK / bus error",
                  static_cast<unsigned>(status), probe.expected);
  }
  Wire.end();
  Serial.println("ACK does not identify the chip or validate its functions.");
  Serial.println("No ACK may indicate power, sleep, address variant or bus trouble.");
}

void initializeInputs() {
  for (Input& input : inputs) {
    pinMode(input.pin, INPUT_PULLUP);
    input.candidate = digitalRead(input.pin);
    input.stable = input.candidate;
    input.changedAt = millis();
    Serial.printf("INPUT %s GPIO%u initial=%s\n", input.name, input.pin,
                  input.stable == LOW ? "PRESSED" : "RELEASED");
  }
}

void pollInputs(uint32_t now) {
  for (Input& input : inputs) {
    const bool level = digitalRead(input.pin);
    if (level != input.candidate) {
      input.candidate = level;
      input.changedAt = now;
    }
    if (input.candidate != input.stable &&
        static_cast<uint32_t>(now - input.changedAt) >= kDebounceMs) {
      input.stable = input.candidate;
      Serial.printf("%lu ms INPUT %s GPIO%u %s\n", static_cast<unsigned long>(now),
                    input.name, input.pin, input.stable == LOW ? "PRESSED" : "RELEASED");
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStarted = millis();
  // CDC hosts may attach late; never wait indefinitely for a serial terminal.
  while (!Serial && static_cast<uint32_t>(millis() - waitStarted) < 3000) {
    delay(10);
  }
  printIdentity();
  initializeInputs();
  probeI2c();
  Serial.println("Ready: press/release rocker and BOOT. i = identity; p = I2C probes.");
}

void loop() {
  const uint32_t now = millis();
  pollInputs(now);
  if (Serial.available() > 0) {
    const int command = Serial.read();
    if (command == 'i') printIdentity();
    else if (command == 'p') probeI2c();
  }
  static uint32_t lastHeartbeat = 0;
  if (static_cast<uint32_t>(now - lastHeartbeat) >= kHeartbeatMs) {
    lastHeartbeat = now;
    Serial.printf("DIAGNOSTIC PAS APPLICATION — alive, uptime %lu ms; i = identity; p = I2C\n",
                  static_cast<unsigned long>(now));
  }
  delay(2);
}
