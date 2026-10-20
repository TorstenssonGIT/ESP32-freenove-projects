/*
 * Projekt: lab1b Potentiometerstyrd LED med PWM (ESP32-S3)
 * Datum: 20 Oktober 2026
 * Författare: Johan Torstensson 
 * Instruktör: Martin Harnevie
 * 
 * HÅRDVARUKOPPLING (BREADBOARD):
 * -------------------------------------------------------------
 * POTENTIOMETER:
 *   - Ytterpinne 1  -->  0V / GND (Blå skena)
 *   - Mittenpinne   -->  KPinAdc1 / GPIO 1 (Analog ingång)
 *   - Ytterpinne 2  -->  3.3V (Röd skena)
 * 
 * LYSODIOD (LED):
 *   - GPIO 14       -->  1 kΩ Motstånd  -->  LED Långt ben (+ / Anod)
 *   - LED Kort ben  -->  0V / GND (Blå skena / - / Katod)
 * -------------------------------------------------------------
 */
#include <Arduino.h>

// Syntaxkrav: Konstanter och inställningar
const uint8_t KPinAdc1 = 1;         // Potentiometer ansluten till GPIO 1
const uint8_t KPinLed = 14;          // Lysdiod (LED) ansluten till GPIO 14
const uint32_t KFrequency = 2000;    // PWM-frekvens på 2 kHz
const uint8_t KChannelPwm = 0;       // PWM-kanal 0
const uint8_t KBitResolution = 12;   // 12-bitars upplösning (0 - 4095)

// Global variabel för att lagra det analoga värdet
uint16_t valueAdc;

void setup() {
    // Starta seriell kommunikation för felsökning
    Serial.begin(115200);
    
    // Konfigurera den analoga pinnen som ingång
    pinMode(KPinAdc1, INPUT);
    
    // Syntaxkrav: Konfigurera och aktivera PWM-kanalen på LED-pinnen
    ledcAttachChannel(KPinLed, KFrequency, KBitResolution, KChannelPwm);
}

void loop() {
    // Syntaxkrav: Läs av det analoga värdet från potentiometern
    valueAdc = analogRead(KPinAdc1);
    
    // Syntaxkrav: Skriv värdet direkt till LED-pinnen med PWM
    // Eftersom både ADC och PWM kör 12-bitar (0-4095) behövs ingen skalning!
    ledcWrite(KPinLed, valueAdc);
    
    // Skriv ut värdet till Serial Monitor så du kan övervaka det
    Serial.print("ADC Värde: ");
    Serial.println(valueAdc);
    
    delay(20); // Kort paus för stabilitet
}
