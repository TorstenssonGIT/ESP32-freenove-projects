/*
 * Projekt: Lab 2 - Lampor och tryckknapp (ESP32-S3)
 * Datum: 20 Oktober 2026
 * Författare: TorstenssonGIT
 * Instruktör: Martin Harnevie
 * 
 * HÅRDVARUKOPPLING (BREADBOARD):
 * -------------------------------------------------------------
 * LYSDIODER (LED):
 *   - LED 1 (Grön): GPIO 14  -->  1 kΩ Motstånd  -->  LED1 Långt ben (+)
 *   - LED 2 (Röd):  GPIO 15  -->  1 kΩ Motstånd  -->  LED2 Långt ben (+)
 *   - Båda LED korta ben     -->  0V / GND (Blå skena)
 * 
 * TRYCKKNAPP (Pull-down via mjukvara):
 *   - Ena sidan av knappen   -->  3.3V (Röd skena)
 *   - Andra sidan (Signalen) -->  GPIO 4 (Digital ingång)
 * -------------------------------------------------------------
 */

#include <Arduino.h>

// Definiera pinnar
const uint8_t KPinLedGreen = 14; // Grön LED på GPIO 14
const uint8_t KPinLedRed = 15;   // Röd LED på GPIO 15
const uint8_t KPinButton = 4;    // Tryckknapp på GPIO 4

// Variabel för att hålla reda på knappens status
int buttonState = 0;

void setup() {
    // Starta seriell port för övervakning
    Serial.begin(115200);
    
    // Konfigurera lamporna som utgångar
    pinMode(KPinLedGreen, OUTPUT);
    pinMode(KPinLedRed, OUTPUT);
    
    // Konfigurera knappen som ingång med ESP32:s inbyggda pull-down-motstånd.
    // Detta garanterar att pinnen är stabil på 0V tills vi trycker ner knappen.
    pinMode(KPinButton, INPUT_PULLDOWN);
    
    Serial.println("Lab 2 startad! Tryck på knappen...");
}

void loop() {
    // Läs av om knappen är nedtryckt (HIGH) eller släppt (LOW)
    buttonState = digitalRead(KPinButton);
    
    if (buttonState == HIGH) {
        // Om knappen är nedtryckt: Tänd grön, släck röd
        digitalWrite(KPinLedGreen, HIGH);
        digitalWrite(KPinLedRed, LOW);
        Serial.println("Knapp nedtryckt -> Grön LED lyser");
    } else {
        // Om knappen är släppt: Släck grön, tänd röd
        digitalWrite(KPinLedGreen, LOW);
        digitalWrite(KPinLedRed, HIGH);
    }
    
    delay(50); // Kort fördröjning för att undvika kontaktstuds (debounce)
}
