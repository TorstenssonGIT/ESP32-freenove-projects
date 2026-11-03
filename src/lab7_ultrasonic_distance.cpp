/*
 * Projekt: Lab 7 - Ultraljudsavstånd med LCD1602 (ESP32-S3)
 *
 * KOPPLING:
 * -------------------------------------------------------------
 * HC-SR04:
 *   - VCC  --> 5V
 *   - GND  --> GND
 *   - TRIG --> GPIO 13
 *   
 *   - ECHO --> 5.1 kΩ --> GPIO 21 --> 6.8 kΩ --> GND
 *
 * LCD1602 med PCF8574T:
 *   - VCC --> 3.3V (prova först; vissa LCD-moduler behöver 5V)
 *   - GND --> GND
 *   - SDA --> GPIO 1
 *   - SCK/SCL --> GPIO 2
 * -------------------------------------------------------------
 */

#include <Arduino.h>
#include <LiquidCrystal_PCF8574.h>
#include <Wire.h>

const uint8_t KPinUltrasonicTrigger = 13;
const uint8_t KPinUltrasonicEcho = 21;
const uint8_t KPinLcdSda = 1;
const uint8_t KPinLcdScl = 2;
const uint8_t KLcdAddress = 0x27;
const uint32_t KEchoTimeoutMicros = 30000;
const float KSoundSpeedCmPerMicrosecond = 0.0343f;

LiquidCrystal_PCF8574 lcd(KLcdAddress);

void setup() {
    Serial.begin(115200);

    pinMode(KPinUltrasonicTrigger, OUTPUT);
    pinMode(KPinUltrasonicEcho, INPUT);
    digitalWrite(KPinUltrasonicTrigger, LOW);

    Wire.begin(KPinLcdSda, KPinLcdScl);
    lcd.begin(16, 2);
    lcd.setBacklight(255);
    lcd.setCursor(0, 0);
    lcd.print("Distance meter");
    lcd.setCursor(0, 1);
    lcd.print("Starting...");

    Serial.println("Lab 7 startad: ultraljudsavstand med LCD1602.");
    Serial.println("Om LCD:n ar blank, prova I2C-adressen 0x3F.");
}

void loop() {
    digitalWrite(KPinUltrasonicTrigger, LOW);
    delayMicroseconds(2);
    digitalWrite(KPinUltrasonicTrigger, HIGH);
    delayMicroseconds(10);
    digitalWrite(KPinUltrasonicTrigger, LOW);

    const uint32_t echoDurationMicros =
        pulseIn(KPinUltrasonicEcho, HIGH, KEchoTimeoutMicros);

    lcd.setCursor(0, 0);
    lcd.print("Distance:       ");
    lcd.setCursor(0, 1);

    if (echoDurationMicros == 0) {
        lcd.print("No echo         ");
        Serial.println("Ingen echo-signal. Kontrollera sensor och kablar.");
    } else {
        const float distanceCm =
            echoDurationMicros * KSoundSpeedCmPerMicrosecond / 2.0f;

        lcd.print(distanceCm, 1);
        lcd.print(" cm           ");

        Serial.print("Avstand: ");
        Serial.print(distanceCm, 1);
        Serial.println(" cm");
    }

    delay(100);
}
