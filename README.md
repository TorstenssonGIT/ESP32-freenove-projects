# ESP32 Freenove projects

Lab exercises for the **Freenove ESP32-S3-WROOM** board, built with
**PlatformIO** (Arduino framework). Each lab is one source file in `src/`,
with notes, wiring diagrams and photos next to it.

## Labs

| Lab | Main file | Topic |
|---|---|---|
| 1B | `lab1b_led_and_pot.cpp` | LED controlled by a potentiometer |
| 2 | `lab2_lampor_o_knapp.cpp` | LEDs and a push button |
| 4 | `lab4_termistor.cpp` | Temperature with a thermistor |
| 6A | `lab6a_fotoresistor_led.cpp` | Photoresistor controlling an LED |
| 6B | `lab6b_fotoresistor_i2c.cpp` | Photoresistor with I2C LCD |
| 7 | `lab7_ultrasonic_distance.cpp` | Ultrasonic distance sensor |
| 8 | [`lab8_wifi.md`](src/lab8_wifi.md) | Wi-Fi: scan, connect, web page, printing |

## Hardware

- Freenove ESP32-S3-WROOM board (built-in 2.4 GHz Wi-Fi)
- 16x2 LCD with PCF8574T I2C adapter: SDA GPIO 1, SCL GPIO 2, address `0x27`
- Yellow status LED: GPIO 13

## Building

`platformio.ini` selects the program with `build_src_filter`; only **one**
line may be active:

```ini
build_src_filter = -<*> +<lab8_wifi_connect.cpp>
```

```bash
pio run -t upload -t monitor
```

## Wi-Fi credentials

Lab 8 programs ask for the Wi-Fi SSID and password over Serial the first time
and store them in the ESP32's flash, not in any file. See
[How Wi-Fi credentials work](src/lab8_wifi.md#how-wi-fi-credentials-work).
