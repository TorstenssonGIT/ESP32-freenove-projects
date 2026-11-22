/*
 * =============================================================================
 *  Lab 8 - Motor and sensor test: 28BYJ-48, TMAG5273 index, HS3003, DHT11
 *          (and SHT31-D if it is connected at 0x45)
 * =============================================================================
 *
 *  What it does
 *  ------------
 *  1. Scans the 3.3 V I2C bus (Wire1) once and reports which devices answer.
 *  2. Turns the motor one full revolution clockwise, then one back, again
 *     and again. While the motor turns, the TMAG5273 is read every 10 ms and
 *     the strongest field of the turn (the magnet passing = the index) is
 *     recorded together with the motor position.
 *  3. Between moves the coils are switched off (all IN low) and the climate
 *     sensors are read: measurements only while the motor is at rest, the
 *     same rule as in the radar design (lab8_wifi_rt_design.md).
 *
 *  No Wi-Fi in this test.
 *
 *  Hardware (board A, camera ribbon cable unplugged)
 *  -------------------------------------------------
 *    ULN2003       : IN1 GPIO 4, IN2 GPIO 5, IN3 GPIO 6, IN4 GPIO 7, + 5V, - GND
 *    LCD1602       : Wire,  SDA GPIO 1,  SCL GPIO 2,  0x27 (5 V)
 *    3.3 V bus     : Wire1, SDA GPIO 47, SCL GPIO 21, 100 kHz, 10 kΩ pull-ups
 *      Modulino Thermo (HS3003) 0x44  -> TMAG5273 0x35 -> SHT31-D 0x45 (Qwiic)
 *    DHT11         : DATA GPIO 14 (10 kΩ pull-up on the module)
 *
 *    INA226        : on the LCD bus (Wire, GPIO 1/2), 0x40-0x4F (found by ID),
 *                    VCC 3V3, VBS to IN-;
 *                    (0x44 is taken by the Modulino on the 3.3 V bus)
 *                    100 mΩ shunt in the USB cable's 5 V wire
 *                    (measures the ESP32 + motor, the motor runs from 5V)
 *
 *  Serial protocol
 *  ---------------
 *    I2C_SCAN bus=lcd sda=1 scl=2 found=<addresses>                (once)
 *    I2C_SCAN bus=wire1 sda=47 scl=21 found=<addresses>            (once)
 *    TMAG5273 ok=<0|1> range_mt=80                                  (once)
 *    MOTOR_MOVE dir=<cw|ccw> steps=<n>
 *    MOTOR_DONE dir=<cw|ccw> ms=<duration> position=<steps>
 *    INDEX_PEAK dir=<cw|ccw> position=<steps> b_mt=<|B| at peak> background_mt=<|B| min>
 *    INA226 ok=<0|1> addr=<0x40-0x4F> manufacturer=<0x5449> device=<0x2260>  (once)
 *    POWER_MOVE dir=<cw|ccw> v=<V> i_ma_avg=<mA> i_ma_max=<mA> p_mw_avg=<mW>
 *    POWER_REST v=<V> i_ma=<mA> p_mw=<mW>                          (coils off)
 *    SENSOR_BEGIN
 *    HS3003 ok=<0|1> t_c=<°C> rh=<%>
 *    SHT31 ok=<0|1> t_c=<°C> rh=<%>          (only if found at 0x45)
 *    DHT11 ok=<0|1> t_c=<°C> rh=<%>
 *    SENSOR_END
 *
 *  platformio.ini needs (lib_deps):
 *    mathertel/LiquidCrystal_PCF8574, adafruit/DHT sensor library,
 *    adafruit/Adafruit Unified Sensor, waspinator/AccelStepper
 * =============================================================================
 */

#include <AccelStepper.h>
#include <Arduino.h>
#include <DHT.h>
#include <LiquidCrystal_PCF8574.h>
#include <Wire.h>
#include <math.h>

//----------------------------------------------------------------------------
// Demo mode for filming: during each rest the LCD shows one value per screen,
// KDemoScreenMs each, so a viewer can read them. Measurements are unchanged.
//----------------------------------------------------------------------------
constexpr bool     KDemoMode         = true;
constexpr uint32_t KDemoScreenMs     = 3000;
constexpr uint32_t KTmagIntervalMs   = 10;      // index sampling while turning

// ---------------------------------------------------------------------------
//  Pins, addresses and settings
// ---------------------------------------------------------------------------
constexpr uint8_t  KPinIn1 = 4, KPinIn2 = 5, KPinIn3 = 6, KPinIn4 = 7; // ULN2003 IN1-IN4 (motor)

constexpr uint8_t  KLcdSda = 1, KLcdScl = 2, KLcdAddress = 0x27;       // LCD I2C bus (5 V)

constexpr uint8_t  KSensorSda   = 47;      // 3.3 V I2C bus, data, 10 kΩ pull-up
constexpr uint8_t  KSensorScl   = 21;      // 3.3 V I2C bus, clock, 10 kΩ pull-up
constexpr uint32_t KSensorBusHz = 100000;  // 100 kHz

constexpr uint8_t  KHs3003Address = 0x44;  // Modulino Thermo (inside the box)
constexpr uint8_t  KTmagAddress   = 0x35;  // TMAG5273 A1 (index)
constexpr uint8_t  KSht31Address  = 0x45;  // SHT31-D with address jumper (steam)

constexpr uint8_t  KDhtPin = 14;           // DHT11 DATA (room reference)

// INA226 power monitor (Hailege module) on the LCD bus (Wire, GPIO 1/2).
// Its address 0x44 is taken by the Modulino on the 3.3 V sensor bus.
// setupIna() searches 0x40-0x4F for TI's ID, so the address is found at start
// (0x44 on this module).
uint8_t            inaAddress      = 0;      // set by setupIna()
constexpr float    KShuntOhm       = 0.100f; // shunt marked R100 = 0.1 Ω (checked on the board)
constexpr uint8_t  KInaRegShunt    = 0x01;   // 16-bit signed, 2.5 µV/LSB
constexpr uint8_t  KInaRegBus      = 0x02;   // 16-bit, 1.25 mV/LSB
constexpr uint8_t  KInaRegManufId  = 0xFE;   // 0x5449 = "TI"
constexpr uint8_t  KInaRegDieId    = 0xFF;   // 0x2260 = INA226
constexpr uint32_t KInaIntervalMs  = 50;     // sampling while the motor turns

constexpr long     KHalfStepsPerTurn = 4076;    // ≈ 2 × 2037.9 (measured later)
constexpr float    KMaxSpeed         = 400.0f;  // half steps/s ≈ 5.9 rpm
constexpr float    KAcceleration     = 200.0f;  // half steps/s², soft start and stop
constexpr uint32_t KPauseMs          = 2500;    // rest: coils off, sensors read

// TMAG5273 registers (datasheet)
constexpr uint8_t  KTmagDeviceConfig2 = 0x01;   // operating mode
constexpr uint8_t  KTmagSensorConfig1 = 0x02;   // channel enable
constexpr uint8_t  KTmagSensorConfig2 = 0x03;   // range
constexpr uint8_t  KTmagXMsb          = 0x12;   // X, Y, Z results (6 bytes)
constexpr float    KTmagRangeMt       = 80.0f;  // A1 with range bits set

// ---------------------------------------------------------------------------
//  Objects and state
// ---------------------------------------------------------------------------
// AccelStepper: 4-wire half stepper, ULN2003, IN1-IN4
// The constructor sets the motor pins to OUTPUT and calls enableOutputs().
// The motor is not moving until setMaxSpeed() and setAcceleration() are called.
AccelStepper          motor(AccelStepper::HALF4WIRE, KPinIn1, KPinIn3, KPinIn2, KPinIn4);
LiquidCrystal_PCF8574 lcd(KLcdAddress);
DHT                   dht(KDhtPin, DHT11);

bool hasHs3003 = false, hasTmag = false, hasSht31 = false, hasIna = false;

bool     movingClockwise = true;
uint32_t moveStartMillis = 0;
uint32_t lastTmagMillis  = 0;

// Index search during one move
// The strongest field (the magnet passing = the index) is recorded together
// with the motor position. The background field is also recorded.
float peakField = 0, minField = 1e9;
long  peakPosition = 0;

// Power during one move
//  The INA226 is read every KInaIntervalMs while the motor turns, and the
//  average and maximum current and average power are calculated.
uint32_t lastInaMillis = 0;
float    moveCurrentSum = 0, moveCurrentMax = 0, movePowerSum = 0, moveVoltage = 0;
uint32_t moveSamples = 0;

struct Reading { bool ok = false; float temperatureC = NAN, humidityPercent = NAN; };

uint8_t hs3003Raw[4] = {0, 0, 0, 0};   // last raw bytes (debug)

// ---------------------------------------------------------------------------
//  I2C helpers (Wire1)
// ---------------------------------------------------------------------------
//  Wire1 is the 3.3 V sensor bus, with 10 kΩ pull-ups to 3.3 V on the board.
//  Wire is the 5 V LCD bus, with 10 kΩ pull-ups to 5 V on the board.
bool deviceAnswers(uint8_t address) {
    Wire1.beginTransmission(address);                 // send address, no data
    return Wire1.endTransmission() == 0;
}
//  Write one byte to a register on a device. Returns true if the device
//  answered, false if not.  
bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
    Wire1.beginTransmission(address);                 // send address, then register and value
    Wire1.write(reg);
    Wire1.write(value);
    return Wire1.endTransmission() == 0;
}
//  Read multiple bytes from a register on a device. Returns true if the device
//  answered, false if not. The data is stored in the provided buffer.
bool readRegisters(uint8_t address, uint8_t reg, uint8_t* data, uint8_t count) {
    Wire1.beginTransmission(address);                 // send address, then register
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return false;   // repeated start
    if (Wire1.requestFrom(address, count) != count) return false;   // read the bytes
    for (uint8_t i = 0; i < count; i++) data[i] = Wire1.read();
    return true;
}

// Prints every address 0x08-0x77 that answers on the given bus.
// The bus is already initialized with begin() and the correct pins.
// The scan is synchronous: this function does not return until the scan is done.
void scanBus(TwoWire& bus, const char* name, uint8_t sda, uint8_t scl) {
    Serial.printf("I2C_SCAN bus=%s sda=%u scl=%u found=", name, sda, scl);
    bool any = false;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        bus.beginTransmission(a);
        if (bus.endTransmission() == 0) { Serial.printf("%s0x%02X", any ? "," : "", a); any = true; }
    }
    Serial.println(any ? "" : "none");
}

// ---------------------------------------------------------------------------
//  TMAG5273: continuous X/Y/Z measurement, ±80 mT
// ---------------------------------------------------------------------------
//  The TMAG5273 is a 3-axis magnetometer with a Hall sensor. It can measure
//  the magnetic field in X, Y and Z direction. The magnitude of the field is
//  calculated as |B| = sqrt(X² + Y² + Z²). The TMAG5273 is read every 10 ms while the motor turns, and the strongest
//  field (the magnet passing = the index) is recorded together with the motor position.
//  The background field is also recorded.
bool setupTmag() {
    return writeRegister(KTmagAddress, KTmagSensorConfig1, 0x70)    // X, Y, Z on
        && writeRegister(KTmagAddress, KTmagSensorConfig2, 0x03)    // ±80 mT on X/Y and Z
        && writeRegister(KTmagAddress, KTmagDeviceConfig2, 0x02);   // continuous mode
}

// Returns |B| in mT, or NAN on a read error.
float readTmagMagnitude() {
    uint8_t d[6];
    if (!readRegisters(KTmagAddress, KTmagXMsb, d, 6)) return NAN;
    float scale = KTmagRangeMt / 32768.0f;
    float x = (int16_t)((d[0] << 8) | d[1]) * scale;
    float y = (int16_t)((d[2] << 8) | d[3]) * scale;
    float z = (int16_t)((d[4] << 8) | d[5]) * scale;
    return sqrtf(x * x + y * y + z * z);
}

// ---------------------------------------------------------------------------
//  INA226 over I2C: register address, repeated start, 2 bytes MSB first.
//  Default configuration: shunt and bus measured continuously.
// ---------------------------------------------------------------------------
// The INA226 sits on the LCD bus (Wire), not on the 3.3 V sensor bus (Wire1).
//  The INA226 is read every KInaIntervalMs while the motor turns, and the
//  average and maximum current and average power are calculated.
uint16_t inaRead(uint8_t reg) {
    Wire.beginTransmission(inaAddress);               // send address, then register
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;   // repeated start
    if (Wire.requestFrom(inaAddress, (uint8_t)2) != 2) return 0;
    uint16_t high = Wire.read();
    return (high << 8) | Wire.read();
}

struct PowerReading { float volts = NAN, milliamps = NAN, milliwatts = NAN; };
//  Reads the INA226 and returns volts, milliamps and milliwatts.
//  If the INA226 is not present, returns NAN for all values.
PowerReading readIna() {
    PowerReading r;
    if (!hasIna) return r;
    float shuntVolts = (int16_t)inaRead(KInaRegShunt) * 2.5e-6f;
    r.volts      = inaRead(KInaRegBus) * 1.25e-3f;
    r.milliamps  = shuntVolts / KShuntOhm * 1000.0f;
    r.milliwatts = r.volts * r.milliamps;
    return r;
}

// Searches 0x40-0x4F on the LCD bus for a chip with TI's ID (0x5449).
bool setupIna() {
    for (uint8_t address = 0x40; address <= 0x4F; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() != 0) continue;
        inaAddress = address;
        uint16_t manufacturer = inaRead(KInaRegManufId);
        uint16_t device       = inaRead(KInaRegDieId);
        if (manufacturer == 0x5449) {
            Serial.printf("INA226 ok=1 addr=0x%02X manufacturer=0x%04X device=0x%04X\n",
                          address, manufacturer, device);
            return true;
        }
    }
    //  If no INA226 was found, set inaAddress to 0 and print a message.
    inaAddress = 0;
    Serial.println("INA226 ok=0 (not found at 0x40-0x4F on the LCD bus)");
    return false;
}

// While turning: sample current every 50 ms (motor + ESP32).
// The INA226 is read every KInaIntervalMs while the motor turns, and the average and maximum current and average power are calculated.
void samplePower() {
    if (!hasIna || millis() - lastInaMillis < KInaIntervalMs) return;
    lastInaMillis = millis();
    PowerReading p = readIna();
    if (isnan(p.milliamps)) return;
    moveCurrentSum += p.milliamps;
    movePowerSum   += p.milliwatts;
    moveVoltage     = p.volts;
    if (p.milliamps > moveCurrentMax) moveCurrentMax = p.milliamps;
    moveSamples++;
}

// ---------------------------------------------------------------------------
//  HS3003 (Modulino Thermo): measurement request, wait, 4 bytes
// ---------------------------------------------------------------------------
//  The HS3003 is a temperature and humidity sensor. It is read while the motor is at rest, and the values are printed to the LCD and serial.
Reading readHs3003() {
    Reading r;
    if (!hasHs3003 || !deviceAnswers(KHs3003Address)) return r;   // also starts a measurement
    delay(40);
    if (Wire1.requestFrom(KHs3003Address, (uint8_t)4) != 4) return r;
    uint8_t d[4];
    for (uint8_t& v : d) v = Wire1.read();
    memcpy(hs3003Raw, d, 4);
    if ((d[0] >> 6) != 0) return r;                               // stale value
    r.humidityPercent = (((d[0] & 0x3F) << 8) | d[1]) * 100.0f / 16383.0f;
    r.temperatureC    = (((d[2] << 8) | d[3]) >> 2) * 165.0f / 16383.0f - 40.0f;
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------------------
//  SHT31-D: single shot, high repeatability, CRC-8 checked
// ---------------------------------------------------------------------------
//  The SHT31-D is a temperature and humidity sensor. It is read while the motor is at rest, and the values are printed to the LCD and serial.
//  The SHT31-D has a CRC-8 checksum for each measurement. The polynomial is 0x31, initial value 0xFF.
//  The SHT31-D is read while the motor is at rest, and the values are printed to the LCD and serial.
uint8_t crc8(const uint8_t* data, uint8_t length) {
    uint8_t crc = 0xFF;                                 // SHT3x: poly 0x31, init 0xFF
    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
    return crc;
}

//  Reads the SHT31-D and returns temperature and humidity. If the SHT31-D is not present, returns ok=false.
Reading readSht31() {
    Reading r;
    if (!hasSht31) return r;
    Wire1.beginTransmission(KSht31Address);
    Wire1.write(0x24);                                  // 0x2400: single shot, no clock stretching
    Wire1.write(0x00);
    if (Wire1.endTransmission() != 0) return r;
    delay(20);
    if (Wire1.requestFrom(KSht31Address, (uint8_t)6) != 6) return r;
    uint8_t d[6];
    for (uint8_t& v : d) v = Wire1.read();
    if (crc8(d, 2) != d[2] || crc8(d + 3, 2) != d[5]) return r;
    r.temperatureC    = -45.0f + 175.0f * ((d[0] << 8) | d[1]) / 65535.0f;
    r.humidityPercent = 100.0f * ((d[3] << 8) | d[4]) / 65535.0f;
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------------------
//  DHT11
// ---------------------------------------------------------------------------
//  The DHT11 is a temperature and humidity sensor. It is read while the motor is at rest, and the values are printed to the LCD and serial.
Reading readDht11() {
    Reading r;
    r.humidityPercent = dht.readHumidity();
    r.temperatureC    = dht.readTemperature();
    r.ok = !isnan(r.humidityPercent) && !isnan(r.temperatureC);
    return r;
}

// ---------------------------------------------------------------------------
//  LCD
// ---------------------------------------------------------------------------
//  Prints two lines on the LCD, padded with spaces to 16 characters.
void showOnLcd(const char* line1, const char* line2) {
    char padded[17];
    lcd.setCursor(0, 0); snprintf(padded, sizeof(padded), "%-16s", line1); lcd.print(padded);
    lcd.setCursor(0, 1); snprintf(padded, sizeof(padded), "%-16s", line2); lcd.print(padded);
}

// ---------------------------------------------------------------------------
//  Motor cycle
// ---------------------------------------------------------------------------
//
const char* directionName() { return movingClockwise ? "cw" : "ccw"; }

void startMove() {
    long steps = movingClockwise ? KHalfStepsPerTurn : -KHalfStepsPerTurn;
    peakField = 0; minField = 1e9; peakPosition = motor.currentPosition();
    moveCurrentSum = 0; moveCurrentMax = 0; movePowerSum = 0; moveSamples = 0;
    motor.enableOutputs();
    motor.move(steps);
    moveStartMillis = millis();
    Serial.printf("MOTOR_MOVE dir=%s steps=%ld\n", directionName(), steps);
    showOnLcd("Motor turning", movingClockwise ? "CW, index search" : "CCW, index search");
}

// While turning: sample the field every 10 ms and keep the strongest.
// The TMAG5273 is read every 10 ms while the motor turns, and the strongest magnetic field (the magnet passing = the index) is recorded together with the motor position. The background field is also recorded.
void sampleIndex() {
    if (!hasTmag || millis() - lastTmagMillis < KTmagIntervalMs) return;
    lastTmagMillis = millis();
    float field = readTmagMagnitude();
    if (isnan(field)) return;
    if (field > peakField) { peakField = field; peakPosition = motor.currentPosition(); }
    if (field < minField)  minField = field;
}

// Demo mode: one value per LCD screen, KDemoScreenMs each (coils stay off).
// The measurements are unchanged, only the display is different.
//  A delay is introduced to alow the viewer to read the values. The measurements are unchanged, only the display is different.
void showDemoScreens(const Reading& hs, const Reading& sh, const Reading& dh, const PowerReading& p) {
    char line[17];
    auto screen = [](const char* title, const char* value) {
        showOnLcd(title, value);
        delay(KDemoScreenMs);
    };
    if (hasTmag) {
        snprintf(line, sizeof(line), "pos %ld %.1fmT", peakPosition, peakField);
        screen("Index peak", line);
    }
    if (hasIna && moveSamples > 0) {
        snprintf(line, sizeof(line), "%.0fmA %.0fmW", moveCurrentSum / moveSamples, movePowerSum / moveSamples);
        screen("Power, turning", line);
    }
    if (hasIna) {
        snprintf(line, sizeof(line), "%.2fV %.0fmA", p.volts, p.milliamps);
        screen("Power, at rest", line);
    }
    snprintf(line, sizeof(line), hs.ok ? "%.1fC %.1f%%" : "no data", hs.temperatureC, hs.humidityPercent);
    screen("HS3003 (box)", line);
    if (hasSht31) {
        snprintf(line, sizeof(line), sh.ok ? "%.1fC %.1f%%" : "no data", sh.temperatureC, sh.humidityPercent);
        screen("SHT31-D (steam)", line);
    }
    snprintf(line, sizeof(line), dh.ok ? "%.1fC %.1f%%" : "no data", dh.temperatureC, dh.humidityPercent);
    screen("DHT11 (room)", line);
}

// At rest: coils off, report the move and the index, read the climate sensors.
// The TMAG5273 is read every 10 ms while the motor turns, and the strongest magnetic field (the magnet passing = the index) is recorded together with the motor position.
// The background field is also recorded. The INA226 is read every KInaIntervalMs while the motor turns, and the average and maximum current and average power are calculated.
// The HS3003, SHT31-D and DHT11 are read while the motor is at rest, and the values are printed to the LCD and serial.
void finishMove() {
    motor.disableOutputs();                                       // all IN low
    Serial.printf("MOTOR_DONE dir=%s ms=%lu position=%ld\n", directionName(),
                  (unsigned long)(millis() - moveStartMillis), motor.currentPosition());
    if (hasTmag) {
        Serial.printf("INDEX_PEAK dir=%s position=%ld b_mt=%.2f background_mt=%.2f\n",
                      directionName(), peakPosition, peakField, minField);
    }
    if (hasIna && moveSamples > 0) {
        Serial.printf("POWER_MOVE dir=%s v=%.3f i_ma_avg=%.1f i_ma_max=%.1f p_mw_avg=%.0f\n",
                      directionName(), moveVoltage, moveCurrentSum / moveSamples,
                      moveCurrentMax, movePowerSum / moveSamples);
    }

    uint32_t restStart = millis();
    Reading hs = readHs3003();
    Reading sh = readSht31();
    Reading dh = readDht11();
    PowerReading p;
    if (hasIna) {
        delay(20);                                            // let the current settle, coils off
        p = readIna();
        Serial.printf("POWER_REST v=%.3f i_ma=%.1f p_mw=%.0f\n",
                      p.volts, p.milliamps, p.milliwatts);
    }
    Serial.println("SENSOR_BEGIN");
    Serial.printf("HS3003 ok=%d t_c=%.2f rh=%.1f raw=%02X %02X %02X %02X\n", hs.ok, hs.temperatureC,
                  hs.humidityPercent, hs3003Raw[0], hs3003Raw[1], hs3003Raw[2], hs3003Raw[3]);
    if (hasSht31) Serial.printf("SHT31 ok=%d t_c=%.2f rh=%.1f\n", sh.ok, sh.temperatureC, sh.humidityPercent);
    Serial.printf("DHT11 ok=%d t_c=%.1f rh=%.1f\n", dh.ok, dh.temperatureC, dh.humidityPercent);
    Serial.println("SENSOR_END");

    char line1[17], line2[17];
    if (hs.ok) snprintf(line1, sizeof(line1), "HS %4.1fC %4.1f%%", hs.temperatureC, hs.humidityPercent);
    else       snprintf(line1, sizeof(line1), "HS3003 no data");
    if (hasTmag) snprintf(line2, sizeof(line2), "Idx %ld %.0fmT", peakPosition, peakField);
    else if (dh.ok) snprintf(line2, sizeof(line2), "DH %4.1fC %4.1f%%", dh.temperatureC, dh.humidityPercent);
    else       snprintf(line2, sizeof(line2), "DHT11 no data");
    showOnLcd(line1, line2);

    if (KDemoMode) showDemoScreens(hs, sh, dh, p);

    // Rest for the remainder of the pause, then turn the other way.
    while (millis() - restStart < KPauseMs) delay(10);
    movingClockwise = !movingClockwise;
    startMove();
}

// ---------------------------------------------------------------------------
//  Setup and loop
// ---------------------------------------------------------------------------
//  The setup() function runs once when the microcontroller starts. It initializes the serial port, I2C buses, LCD, sensors, and motor. It also scans the I2C buses for devices and starts the first motor move.
void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(KLcdSda, KLcdScl);
    lcd.begin(16, 2);
    lcd.setBacklight(255);
    showOnLcd("Motor + sensors", "Starting...");

    Wire1.begin(KSensorSda, KSensorScl, KSensorBusHz);
    scanBus(Wire,  "lcd",   KLcdSda,    KLcdScl);
    scanBus(Wire1, "wire1", KSensorSda, KSensorScl);
    hasHs3003 = deviceAnswers(KHs3003Address);
    hasSht31  = deviceAnswers(KSht31Address);
    hasTmag   = deviceAnswers(KTmagAddress) && setupTmag();
    Serial.printf("TMAG5273 ok=%d range_mt=%.0f\n", hasTmag, KTmagRangeMt);
    hasIna = setupIna();

    dht.begin();

    motor.setMaxSpeed(KMaxSpeed);
    motor.setAcceleration(KAcceleration);

    Serial.println("Lab 8 motor and sensor test ready.");
    delay(1500);                     // DHT11 needs about 1 s after power-up
    startMove();
}

//  
void loop() {
    motor.run();                     // one step when it is due
    sampleIndex();                   // index search while turning
    samplePower();                   // motor + ESP32 current while turning
    if (motor.distanceToGo() == 0) finishMove();
}
