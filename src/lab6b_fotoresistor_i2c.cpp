/*
 * Projekt: Lab 6B - Fotoresistor med I2C-display (ESP32-S3)
 *
 * KOPPLING:
 * -------------------------------------------------------------
 * FOTORESISTOR GL5516:
 *   - 3.3V --> fotoresistor --> GPIO 4/mätpunkt --> 10 kΩ --> GND
 *
 * LCD1602 med PCF8574T:
 *   - VCC --> 3.3V
 *   - GND --> GND
 *   - SDA --> GPIO 1
 *   - SCK/SCL --> GPIO 2
 *
 * LED:
 *   - GPIO 13 --> 220 Ω --> LED-anod (långt ben)
 *   - LED-katod (kort ben) --> GND
 * -------------------------------------------------------------
 */

#include <Arduino.h>
#include <LiquidCrystal_PCF8574.h>
#include <Wire.h>

const uint8_t KPinPhotoresistor = 4;
const uint8_t KPinLed = 13;
const uint8_t KPinLcdSda = 1;
const uint8_t KPinLcdScl = 2;
const uint8_t KLcdAddress = 0x27;
const uint8_t KMovingAverageSampleCount = 10;
const uint16_t KAdcMaximum = 4095;
const uint16_t KLightThreshold = 2050;
const uint32_t KSampleIntervalMillis = 250;

LiquidCrystal_PCF8574 lcd(KLcdAddress);
uint16_t adcSamples[KMovingAverageSampleCount] = {};
uint8_t sampleCount = 0;
uint8_t nextSampleIndex = 0;
uint32_t adcSampleSum = 0;
uint32_t lastSampleMillis = 0;

void printLcdValue(uint8_t row, const char* label, uint16_t value) {
    lcd.setCursor(0, row);
    lcd.print(label);
    lcd.print(value);
    lcd.print("    ");
}

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);
    analogSetPinAttenuation(KPinPhotoresistor, ADC_11db);
    pinMode(KPinPhotoresistor, INPUT);
    pinMode(KPinLed, OUTPUT);
    digitalWrite(KPinLed, LOW);

    Wire.begin(KPinLcdSda, KPinLcdScl);
    lcd.begin(16, 2);
    lcd.setBacklight(255);
    lcd.setCursor(0, 0);
    lcd.print("Lab 6B: Light");
    lcd.setCursor(0, 1);
    lcd.print("Starting...");

    Serial.println("Lab 6B startad: fotoresistor med I2C-display.");
    Serial.println("Justera KLightThreshold efter dina ADC-varden.");
    Serial.println("Om LCD:n ar blank, kontrollera adress 0x27 och SDA/SCL.");
}

void loop() {
    const uint32_t nowMillis = millis();
    if (nowMillis - lastSampleMillis < KSampleIntervalMillis) {
        return;
    }
    lastSampleMillis = nowMillis;

    const uint16_t adcValue = analogRead(KPinPhotoresistor);
    if (sampleCount == KMovingAverageSampleCount) {
        adcSampleSum -= adcSamples[nextSampleIndex];
    } else {
        ++sampleCount;
    }

    adcSamples[nextSampleIndex] = adcValue;
    adcSampleSum += adcValue;
    nextSampleIndex = (nextSampleIndex + 1) % KMovingAverageSampleCount;

    const uint16_t averageValue =
        static_cast<uint16_t>(adcSampleSum / sampleCount);
    const bool isBright = averageValue >= KLightThreshold;
    digitalWrite(KPinLed, isBright ? HIGH : LOW);

    printLcdValue(0, "ADC: ", adcValue);
    printLcdValue(1, "Mean: ", averageValue);

    Serial.print("Fotoresistor ADC: ");
    Serial.print(adcValue);
    Serial.print("/");
    Serial.print(KAdcMaximum);
    Serial.print(" - medelvarde (");
    Serial.print(sampleCount);
    Serial.print(" prov): ");
    Serial.print(averageValue);
    Serial.print(" - ");
    Serial.println(isBright ? "ljust: LED tand" : "morkt: LED slackt");
}
