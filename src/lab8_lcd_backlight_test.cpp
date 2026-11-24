/*
 * =============================================================================
 *  Lab 8 - LCD backlight current (component characterisation, board A)
 * =============================================================================
 *
 *  Why
 *  ---
 *  No datasheet was found for the kit's LCD 1602 module or its backlight
 *  resistor. For the theoretical energy budget, the backlight current is
 *  therefore characterised once: the backlight is switched on and off in
 *  software while the INA226 measures board A's current; the difference is
 *  the backlight's own current.
 *
 *  How
 *  ---
 *  Wi-Fi off, motor coils off (GPIO 4-7 low), the same static text on the LCD
 *  in both phases. Five cycles of 10 s backlight ON and 10 s OFF. The first
 *  2 s of every phase are skipped (settling); the rest is sampled every
 *  200 ms and averaged.
 *
 *  Hardware (board A)
 *  ------------------
 *    LCD 16x2 + PCF8574 : Wire, SDA GPIO 1, SCL GPIO 2, address 0x27
 *    INA226             : same bus, 0x40-0x4F found by ID, shunt R100
 *
 *  Serial protocol
 *  ---------------
 *    INA226 ok=<0|1> addr=<0x4?>
 *    BL cycle=<n> state=<on|off> v=<V> i_ma=<mA> p_mw=<mW> samples=<n>
 *    BL_DIFF cycle=<n> i_ma=<mA> p_mw=<mW>
 *    BL_RESULT cycles=<n> i_ma_mean=<mA> i_ma_min=<mA> i_ma_max=<mA> p_mw_mean=<mW>
 *
 *  platformio.ini:  build_src_filter = -<*> +<lab8_lcd_backlight_test.cpp>
 * =============================================================================
 */

#include <Arduino.h>
#include <LiquidCrystal_PCF8574.h>
#include <WiFi.h>
#include <Wire.h>

// ---------------------------------------------------------------------------
//  Settings
// ---------------------------------------------------------------------------
constexpr uint8_t  KBusSda = 1, KBusScl = 2;
constexpr uint32_t KBusHz  = 100000;
constexpr uint8_t  KLcdAddress = 0x27;
constexpr uint8_t  KMotorPins[] = {4, 5, 6, 7};   // ULN2003 IN1-IN4: kept low

constexpr int      KCycles       = 5;
constexpr uint32_t KPhaseMs      = 10000;          // backlight on or off
constexpr uint32_t KSettleMs     = 2000;           // skipped at the start of a phase
constexpr uint32_t KSampleMs     = 200;

constexpr float   KShuntOhm      = 0.100f;         // R100
constexpr uint8_t KInaRegShunt   = 0x01;           // 2.5 µV/LSB
constexpr uint8_t KInaRegBus     = 0x02;           // 1.25 mV/LSB
constexpr uint8_t KInaRegManufId = 0xFE;           // 0x5449 = "TI"

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
LiquidCrystal_PCF8574 lcd(KLcdAddress);
uint8_t inaAddress = 0;

struct Average { float volts = 0, milliamps = 0, milliwatts = 0; int samples = 0; };

// ---------------------------------------------------------------------------
//  INA226
// ---------------------------------------------------------------------------
uint16_t inaRead(uint8_t reg) {
    Wire.beginTransmission(inaAddress);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom(inaAddress, (uint8_t)2) != 2) return 0;
    uint16_t high = Wire.read();
    return (high << 8) | Wire.read();
}

bool setupIna() {
    for (uint8_t address = 0x40; address <= 0x4F; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() != 0) continue;
        inaAddress = address;
        if (inaRead(KInaRegManufId) == 0x5449) {
            Serial.printf("INA226 ok=1 addr=0x%02X\n", address);
            return true;
        }
    }
    inaAddress = 0;
    Serial.println("INA226 ok=0");
    return false;
}

// Averages the INA226 over one phase, skipping the settling time.
Average measurePhase() {
    Average a;
    uint32_t start = millis();
    while (millis() - start < KPhaseMs) {
        if (millis() - start >= KSettleMs) {
            float shuntVolts = (int16_t)inaRead(KInaRegShunt) * 2.5e-6f;
            float volts      = inaRead(KInaRegBus) * 1.25e-3f;
            float milliamps  = shuntVolts / KShuntOhm * 1000.0f;
            a.volts += volts;
            a.milliamps += milliamps;
            a.milliwatts += volts * milliamps;
            a.samples++;
        }
        delay(KSampleMs);
    }
    if (a.samples > 0) { a.volts /= a.samples; a.milliamps /= a.samples; a.milliwatts /= a.samples; }
    return a;
}

void printPhase(int cycle, const char* state, const Average& a) {
    Serial.printf("BL cycle=%d state=%s v=%.3f i_ma=%.2f p_mw=%.1f samples=%d\n",
                  cycle, state, a.volts, a.milliamps, a.milliwatts, a.samples);
}

// ---------------------------------------------------------------------------
//  Setup and loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);

    WiFi.mode(WIFI_OFF);                           // no radio in this test
    for (uint8_t pin : KMotorPins) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }

    Wire.begin(KBusSda, KBusScl, KBusHz);
    lcd.begin(16, 2);
    lcd.setCursor(0, 0); lcd.print("Backlight test");
    lcd.setCursor(0, 1); lcd.print("on/off 10 s");
    if (!setupIna()) return;

    Serial.printf("BL_TEST start cycles=%d phase_ms=%lu settle_ms=%lu\n",
                  KCycles, (unsigned long)KPhaseMs, (unsigned long)KSettleMs);

    float sum = 0, sumP = 0, minDiff = 1e9, maxDiff = -1e9;
    for (int cycle = 1; cycle <= KCycles; cycle++) {
        lcd.setBacklight(255);
        Average on = measurePhase();
        printPhase(cycle, "on", on);

        lcd.setBacklight(0);
        Average off = measurePhase();
        printPhase(cycle, "off", off);

        float diff = on.milliamps - off.milliamps;
        float diffP = on.milliwatts - off.milliwatts;
        Serial.printf("BL_DIFF cycle=%d i_ma=%.2f p_mw=%.1f\n", cycle, diff, diffP);
        sum += diff; sumP += diffP;
        if (diff < minDiff) minDiff = diff;
        if (diff > maxDiff) maxDiff = diff;
    }

    Serial.printf("BL_RESULT cycles=%d i_ma_mean=%.2f i_ma_min=%.2f i_ma_max=%.2f p_mw_mean=%.1f\n",
                  KCycles, sum / KCycles, minDiff, maxDiff, sumP / KCycles);
    lcd.setBacklight(255);
    lcd.setCursor(0, 1); lcd.print("done            ");
}

void loop() {
    delay(1000);
}
