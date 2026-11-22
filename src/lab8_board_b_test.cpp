/*
 * =============================================================================
 *  Lab 8 - Board B test: INA226 power monitor + HC-SR501 PIR
 * =============================================================================
 *
 *  What it does
 *  ------------
 *  Board B (echo server, lab8-echo) has no LCD and no climate sensors. This
 *  test checks its own hardware before the echo server (8.6) is written:
 *    1. Scans the I2C bus on GPIO 1/2 and finds the INA226 by its ID.
 *    2. Every 2 s: prints voltage, current and power (POWER line).
 *    3. Prints a PIR line every time the PIR output changes (movement starts
 *       or ends). Changes during the first 60 s after power-on are marked
 *       warmup=1: the HC-SR501 needs that time to settle and they are not
 *       real movement.
 *
 *  No Wi-Fi in this test.
 *
 *  Hardware (board B)
 *  ------------------
 *    INA226 : Wire, SDA GPIO 1, SCL GPIO 2, VCC 3V3, 0x40-0x4F (found by ID),
 *             10 kΩ pull-ups GPIO 1/2 to 3V3 (board B has no LCD to provide
 *             them); VBUS of the USB cable through the 100 mΩ shunt
 *    PIR    : HC-SR501, VCC 5V, OUT GPIO 14, GND; jumper on H (repeat trigger),
 *             time pot fully anticlockwise (about 3 s hold)
 *
 *  Serial protocol
 *  ---------------
 *    I2C_SCAN bus=wire sda=1 scl=2 found=<addresses>                    (once)
 *    INA226 ok=<0|1> addr=<0x4?> manufacturer=<0x5449> device=<0x2260>  (once)
 *    POWER uptime_s=<s> v=<V> i_ma=<mA> p_mw=<mW>                       (every 2 s)
 *    PIR uptime_s=<s> state=<0|1> warmup=<0|1>                          (on change)
 * =============================================================================
 */

#include <Arduino.h>
#include <Wire.h>

// ---------------------------------------------------------------------------
//  Pins and settings
// ---------------------------------------------------------------------------
constexpr uint8_t  KBusSda = 1, KBusScl = 2;   // INA226 bus, 10 kΩ pull-ups to 3V3
constexpr uint32_t KBusHz  = 100000;           // 100 kHz

constexpr uint8_t  KPinPir = 14;               // HC-SR501 OUT (3.3 V high)

constexpr uint32_t KPowerIntervalMs = 2000;    // POWER line every 2 s
constexpr uint32_t KPirWarmupMs     = 60000;   // HC-SR501 settling time

// INA226 registers (same as on board A)
constexpr float   KShuntOhm      = 0.100f;     // shunt marked R100 = 0.1 Ω
constexpr uint8_t KInaRegShunt   = 0x01;       // 16-bit signed, 2.5 µV/LSB
constexpr uint8_t KInaRegBus     = 0x02;       // 16-bit, 1.25 mV/LSB
constexpr uint8_t KInaRegManufId = 0xFE;       // 0x5449 = "TI"
constexpr uint8_t KInaRegDieId   = 0xFF;       // 0x2260 = INA226

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
uint8_t  inaAddress      = 0;                  // set by setupIna()
uint32_t lastPowerMillis = 0;
int      lastPirState    = -1;                 // -1 = not read yet

// ---------------------------------------------------------------------------
//  I2C helpers
// ---------------------------------------------------------------------------
bool deviceAnswers(uint8_t address) {
    Wire.beginTransmission(address);           // send address, no data
    return Wire.endTransmission() == 0;
}

void scanBus() {
    Serial.printf("I2C_SCAN bus=wire sda=%u scl=%u found=", KBusSda, KBusScl);
    bool any = false;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (deviceAnswers(a)) { Serial.printf("%s0x%02X", any ? "," : "", a); any = true; }
    }
    Serial.println(any ? "" : "none");
}

// Register address, repeated start, then 2 bytes MSB first.
uint16_t inaRead(uint8_t reg) {
    Wire.beginTransmission(inaAddress);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom(inaAddress, (uint8_t)2) != 2) return 0;
    uint16_t high = Wire.read();
    return (high << 8) | Wire.read();
}

// Searches 0x40-0x4F for TI's ID (0x5449).
bool setupIna() {
    for (uint8_t address = 0x40; address <= 0x4F; address++) {
        if (!deviceAnswers(address)) continue;
        inaAddress = address;
        uint16_t manufacturer = inaRead(KInaRegManufId);
        uint16_t device       = inaRead(KInaRegDieId);
        if (manufacturer == 0x5449) {
            Serial.printf("INA226 ok=1 addr=0x%02X manufacturer=0x%04X device=0x%04X\n",
                          address, manufacturer, device);
            return true;
        }
    }
    inaAddress = 0;
    Serial.println("INA226 ok=0 (check pull-ups and SDA/SCL wires)");
    return false;
}

// ---------------------------------------------------------------------------
//  Measurements
// ---------------------------------------------------------------------------
void reportPower() {
    if (inaAddress == 0) return;
    float shuntVolts = (int16_t)inaRead(KInaRegShunt) * 2.5e-6f;
    float volts      = inaRead(KInaRegBus) * 1.25e-3f;
    float milliamps  = shuntVolts / KShuntOhm * 1000.0f;
    Serial.printf("POWER uptime_s=%lu v=%.3f i_ma=%.1f p_mw=%.0f\n",
                  (unsigned long)(millis() / 1000), volts, milliamps, volts * milliamps);
}

// Prints a line only when the PIR output changes.
void checkPir() {
    int state = digitalRead(KPinPir);
    if (state == lastPirState) return;
    lastPirState = state;
    bool warmup = millis() < KPirWarmupMs;
    Serial.printf("PIR uptime_s=%lu state=%d warmup=%d\n",
                  (unsigned long)(millis() / 1000), state, warmup ? 1 : 0);
}

// ---------------------------------------------------------------------------
//  Setup and loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(KPinPir, INPUT);                   // HC-SR501 drives the line itself

    Wire.begin(KBusSda, KBusScl, KBusHz);
    scanBus();
    setupIna();

    Serial.println("Lab 8 board B test ready (PIR warm-up 60 s).");
}

void loop() {
    checkPir();
    if (millis() - lastPowerMillis >= KPowerIntervalMs) {
        lastPowerMillis = millis();
        reportPower();
    }
    delay(20);
}
