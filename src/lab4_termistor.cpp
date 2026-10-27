/*
 * Projekt: Lab 4 - Termistor och servomotor (ESP32-S3)
 *
 * HÅRDVARUKOPPLING:
 * -------------------------------------------------------------
 * TERMISTOR (10 kΩ NTC, Beta 3950):
 *   - 3.3V --> 10 kΩ fast motstånd --> GPIO 1 --> termistor --> GND
 *
 * SERVOMOTOR:
 *   - Signalledning --> GPIO 2
 *   - Matning       --> extern lämplig matning
 *   - Matningens GND och ESP32 GND ska vara gemensamma
 * -------------------------------------------------------------
 */

#include <Arduino.h>
#include <math.h>

const uint8_t KPinAdc1 = 1;
const uint8_t KPinServo = 2;
const float KSupplyVoltage = 3.3f;
const float KAdcMaximum = 4095.0f;
const float KThermistorBeta = 3950.0f;
const float KThermistorOhmAt25Celsius = 10000.0f;
const float KReferenceCelsius = 25.0f;
const float KReferenceResistance = 10000.0f;
const float KKelvinAtZeroCelsius = 273.15f;
const float KServoMinimumTemperature = 20.0f;
const float KServoMaximumTemperature = 40.0f;
const uint32_t KServoFrequency = 50;
const uint8_t KServoResolution = 16;
const uint8_t KServoChannel = 0;
const uint16_t KServoMinimumPulseMicros = 500;
const uint16_t KServoMaximumPulseMicros = 2500;
const uint32_t KServoDutyMaximum = (1UL << KServoResolution) - 1;

void writeServoAngle(uint8_t angle) {
    const uint16_t pulseMicros = map(
        angle, 0, 180, KServoMinimumPulseMicros, KServoMaximumPulseMicros);
    const uint32_t duty = static_cast<uint32_t>(
        (static_cast<uint64_t>(pulseMicros) * KServoFrequency * KServoDutyMaximum) /
        1000000ULL);
    ledcWrite(KPinServo, duty);
}

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);
    pinMode(KPinAdc1, INPUT);

    if (!ledcAttachChannel(KPinServo, KServoFrequency, KServoResolution, KServoChannel)) {
        Serial.println("Fel: kunde inte konfigurera servomotorns PWM-utgång.");
        return;
    }

    writeServoAngle(0);
    Serial.println("Lab 4 startad: termistor och servomotor.");
}

void loop() {
    const uint16_t valueAdc = analogRead(KPinAdc1);

    if (valueAdc == 0 || valueAdc >= KAdcMaximum) {
        Serial.println("Fel: termistorns ADC-värde ligger utanför mätområdet.");
        delay(500);
        return;
    }

    const float valueVoltage = KSupplyVoltage * valueAdc / KAdcMaximum;
    const double resistanceThermistor =
        KThermistorOhmAt25Celsius * valueVoltage / (KSupplyVoltage - valueVoltage);
    const double temperatureKelvin =
        1.0 / (1.0 / (KKelvinAtZeroCelsius + KReferenceCelsius) +
               log(resistanceThermistor / KReferenceResistance) / KThermistorBeta);
    const double temperatureCelsius = temperatureKelvin - KKelvinAtZeroCelsius;

    const float boundedTemperature = constrain(
        static_cast<float>(temperatureCelsius),
        KServoMinimumTemperature,
        KServoMaximumTemperature);
    const uint8_t servoAngle = static_cast<uint8_t>(
        round((boundedTemperature - KServoMinimumTemperature) * 180.0f /
              (KServoMaximumTemperature - KServoMinimumTemperature)));

    writeServoAngle(servoAngle);

    Serial.print("Temperatur: ");
    Serial.print(temperatureCelsius, 1);
    Serial.print(" °C, servo: ");
    Serial.print(servoAngle);
    Serial.println("°");

    delay(500);
}
