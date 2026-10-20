#include <Arduino.h>

// Definiera vilken pinne potentiometern sitter på.
// På ESP32-S3 motsvarar den första analoga kanalen (ADC1_CH0) GPIO 1.
#define POT_PIN 1

void setup() {
    // Starta den seriella porten för kommunikation med datorn.
    // Vi sätter hastigheten till 115200 baud.
    Serial.begin(115200);
    
    // Konfigurera pinnen som en ingång
    pinMode(POT_PIN, INPUT);
    
    Serial.println("Systemet startat! Vrid på potentiometern...");
}

void loop() {
    // Läs av det analoga värdet (Returnerar ett värde mellan 0 och 4095)
    int potValue = analogRead(POT_PIN);
    
    // Skriv ut resultatet till terminalen på datorn
    Serial.print("Potentiometer värde: ");
    Serial.println(potValue);
    
    // Vänta 100 millisekunder (0.1 sekund) innan nästa avläsning
    delay(100);
}
