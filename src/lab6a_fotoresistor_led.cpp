/*
 * Projekt: Lab 6A - Fotoresistorstyrd LED (ESP32-S3)
 *
 * HÅRDVARUKOPPLING:
 * -------------------------------------------------------------
 * FOTORESISTOR:
 *   - 3.3V --> fotoresistor --> GPIO 1/mätpunkt --> 10 kΩ --> GND
 *
 * LED:
 *   - GPIO 14 --> 1 kΩ motstånd --> LED-anod (långt ben)
 *   - LED-katod (kort ben) --> GND
 * -------------------------------------------------------------
 */

#include <Arduino.h>

const uint8_t KPinPhotoresistor = 1;
const uint8_t KPinLed = 14;
const uint16_t KDarkThreshold = 3200;
const uint16_t KAdcMaximum = 4095;

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);
    analogSetPinAttenuation(KPinPhotoresistor, ADC_11db);
    pinMode(KPinPhotoresistor, INPUT);
    pinMode(KPinLed, OUTPUT);

    Serial.println("Lab 6A startad: LED tänds när det blir mörkt.");
    Serial.println("Justera KDarkThreshold efter ADC-värdena i ljus och mörker.");
}

void loop() {
    const uint16_t photoresistorValue = analogRead(KPinPhotoresistor);
    const bool isDark = photoresistorValue < KDarkThreshold;

    digitalWrite(KPinLed, isDark ? HIGH : LOW);

    Serial.print("Fotoresistor ADC: ");
    Serial.print(photoresistorValue);
    Serial.print("/");
    Serial.print(KAdcMaximum);
    Serial.print(" - ");
    Serial.println(isDark ? "Mörkt: LED tänd" : "Ljust: LED släckt");

    delay(250);
}
