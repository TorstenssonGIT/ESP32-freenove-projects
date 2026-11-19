/*
 * =============================================================================
 *  Lab 8 - Sensor test: Modulino Thermo (HS3003) + DHT11
 * =============================================================================
 *
 *  What it does
 *  ------------
 *  1. Scans the 3.3 V I2C bus (Wire1) once at start and prints every address
 *     that answers. The Modulino Thermo is at 0x45 (board V0.5 or later) or
 *     0x44 (older boards); the scan tells which one this board uses.
 *  2. Every 2 s: reads temperature and humidity from both sensors and prints
 *     them side by side, plus the difference (DHT11 minus HS3003).
 *  3. Shows both readings on the LCD.
 *
 *  No Wi-Fi in this test: only the sensors.
 *
 *  Hardware (see lab8_wiring_sensors.svg)
 *  --------------------------------------
 *    LCD1602 + PCF8574T  : Wire,  SDA = GPIO 1,  SCL = GPIO 2,  0x27 (5 V)
 *    Modulino Thermo     : Wire1, SDA = GPIO 47, SCL = GPIO 21, 3.3 V,
 *                          pull-ups on the breadboard (the Modulino has none)
 *    DHT11               : DATA = GPIO 14, 3.3 V (pull-up on the module)
 *
 *  Serial protocol (one block every 2 s)
 *  -------------------------------------
 *    I2C_SCAN bus=wire1 sda=47 scl=21 found=<addr list or none>   (once)
 *    SENSOR_BEGIN
 *    HS3003 ok=<0|1> addr=<0x44|0x45> t_c=<°C> rh=<%>
 *    DHT11 ok=<0|1> t_c=<°C> rh=<%>
 *    DIFF dt_c=<°C> drh=<%>                     (only when both are ok)
 *    SENSOR_END
 *
 *  platformio.ini needs (lib_deps):
 *    adafruit/DHT sensor library
 *    adafruit/Adafruit Unified Sensor
 * =============================================================================
 */

#include <Arduino.h>
#include <DHT.h>
#include <LiquidCrystal_PCF8574.h>
#include <Wire.h>

// ---------------------------------------------------------------------------
//  Pins and addresses
// ---------------------------------------------------------------------------
constexpr uint8_t  KLcdSda        = 1;
constexpr uint8_t  KLcdScl        = 2;
constexpr uint8_t  KLcdAddress    = 0x27;

constexpr uint8_t  KSensorSda     = 47;     // 3.3 V Qwiic bus (Wire1)
constexpr uint8_t  KSensorScl     = 21;
constexpr uint32_t KSensorBusHz   = 100000; // 100 kHz: safe with 10 kΩ pull-ups

constexpr uint8_t  KDhtPin        = 14;

constexpr uint32_t KReadIntervalMs = 2000;  // DHT11 allows at most one read per 2 s

// ---------------------------------------------------------------------------
//  Objects and state
// ---------------------------------------------------------------------------
LiquidCrystal_PCF8574 lcd(KLcdAddress);
DHT                   dht(KDhtPin, DHT11);

uint8_t  hs3003Address = 0;      // 0 = not found
uint32_t lastReadMillis = 0;

struct Reading {
    bool  ok = false;
    float temperatureC = NAN;
    float humidityPercent = NAN;
};

// ---------------------------------------------------------------------------
//  I2C helpers
// ---------------------------------------------------------------------------

// True if a device acknowledges its address on Wire1.
bool deviceAnswers(uint8_t address) {
    Wire1.beginTransmission(address);
    return Wire1.endTransmission() == 0;
}

// Prints every address 0x08-0x77 that answers on Wire1.
void scanSensorBus() {
    Serial.printf("I2C_SCAN bus=wire1 sda=%u scl=%u found=", KSensorSda, KSensorScl);
    bool any = false;
    for (uint8_t address = 0x08; address <= 0x77; address++) {
        if (deviceAnswers(address)) {
            Serial.printf("%s0x%02X", any ? "," : "", address);
            any = true;
        }
    }
    Serial.println(any ? "" : "none");
}

// ---------------------------------------------------------------------------
//  HS3003 (Modulino Thermo), read directly over I2C
//
//  1. Measurement request: address + write, no data.
//  2. Wait for the conversion (14-bit humidity + 14-bit temperature, ~35 ms).
//  3. Read 4 bytes: [status(2) humidity(14)] [temperature(14) unused(2)]
// ---------------------------------------------------------------------------
Reading readHs3003() {
    Reading reading;
    if (hs3003Address == 0) return reading;

    Wire1.beginTransmission(hs3003Address);
    if (Wire1.endTransmission() != 0) return reading;

    delay(40);

    if (Wire1.requestFrom(hs3003Address, (uint8_t)4) != 4) return reading;
    uint8_t data[4];
    for (uint8_t& value : data) value = Wire1.read();

    uint8_t status = data[0] >> 6;          // 0 = new value, 1 = stale value
    if (status != 0) return reading;

    uint16_t rawHumidity    = ((data[0] & 0x3F) << 8) | data[1];
    uint16_t rawTemperature = ((data[2] << 8) | data[3]) >> 2;

    reading.humidityPercent = rawHumidity    * 100.0f / 16383.0f;
    reading.temperatureC    = rawTemperature * 165.0f / 16383.0f - 40.0f;
    reading.ok = true;
    return reading;
}

// ---------------------------------------------------------------------------
//  DHT11 (one-wire protocol, Adafruit library)
// ---------------------------------------------------------------------------
Reading readDht11() {
    Reading reading;
    reading.humidityPercent = dht.readHumidity();
    reading.temperatureC    = dht.readTemperature();
    reading.ok = !isnan(reading.humidityPercent) && !isnan(reading.temperatureC);
    return reading;
}

// ---------------------------------------------------------------------------
//  Output
// ---------------------------------------------------------------------------
void printReadings(const Reading& hs, const Reading& dh) {
    Serial.println("SENSOR_BEGIN");
    Serial.printf("HS3003 ok=%d addr=0x%02X t_c=%.2f rh=%.1f\n",
                  hs.ok, hs3003Address, hs.temperatureC, hs.humidityPercent);
    Serial.printf("DHT11 ok=%d t_c=%.1f rh=%.1f\n",
                  dh.ok, dh.temperatureC, dh.humidityPercent);
    if (hs.ok && dh.ok) {
        Serial.printf("DIFF dt_c=%.2f drh=%.1f\n",
                      dh.temperatureC - hs.temperatureC,
                      dh.humidityPercent - hs.humidityPercent);
    }
    Serial.println("SENSOR_END");
}

// LCD: line 1 = Modulino, line 2 = DHT11, e.g. "HS 21.4C 38.2%"
void showOnLcd(const Reading& hs, const Reading& dh) {
    char line[17];
    lcd.setCursor(0, 0);
    if (hs.ok) snprintf(line, sizeof(line), "HS %4.1fC %4.1f%% ", hs.temperatureC, hs.humidityPercent);
    else       snprintf(line, sizeof(line), "HS3003 no data  ");
    lcd.print(line);
    lcd.setCursor(0, 1);
    if (dh.ok) snprintf(line, sizeof(line), "DH %4.1fC %4.1f%% ", dh.temperatureC, dh.humidityPercent);
    else       snprintf(line, sizeof(line), "DHT11 no data   ");
    lcd.print(line);
}

// ---------------------------------------------------------------------------
//  Setup and loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(KLcdSda, KLcdScl);
    lcd.begin(16, 2);
    lcd.setBacklight(255);
    lcd.clear();
    lcd.print("Sensor test");

    Wire1.begin(KSensorSda, KSensorScl, KSensorBusHz);
    scanSensorBus();
    if (deviceAnswers(0x45))      hs3003Address = 0x45;
    else if (deviceAnswers(0x44)) hs3003Address = 0x44;
    if (hs3003Address == 0) Serial.println("HS3003 not found at 0x44 or 0x45: check wiring and pull-ups");

    dht.begin();
    Serial.println("Lab 8 sensor test ready.");
    delay(1500);   // DHT11 needs about 1 s after power-up
}

void loop() {
    if (millis() - lastReadMillis < KReadIntervalMs) return;
    lastReadMillis = millis();

    Reading hs = readHs3003();
    Reading dh = readDht11();
    printReadings(hs, dh);
    showOnLcd(hs, dh);
}
