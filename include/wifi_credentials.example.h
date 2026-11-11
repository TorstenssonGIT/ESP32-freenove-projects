/*
 * Lab 8 - Wi-Fi credentials TEMPLATE (safe to commit).
 *
 * How to use:
 *   1. Copy this file to include/wifi_credentials.h
 *   2. Fill in the real SSID and password in the copy.
 *   3. Never commit include/wifi_credentials.h - it is listed in
 *      .git/info/exclude. Check `git status` before every commit.
 *
 * Use only a network you own or have permission to use
 * (Lab 8 test router: ZTE MC888 Pro, 2.4 GHz band).
 */
// The SSID and password are used in lab 8.1 to connect to a Wi-Fi network.
// This file is included in lab8_wifi_scan.cpp, but the credentials are not used in that lab.
// This file is included in lab8_wifi_connect.cpp, which uses the credentials to connect to a Wi-Fi network.
// The credentials are also used in lab8_wifi_ap.cpp, which uses the credentials to connect to a Wi-Fi network.
// This file is only read one and will not be updated during the program execution. 
// The credentials are stored in the flash memory of the ESP32-S3 and are not visible to the user after the program
// is uploaded to the ESP32-S3.

#pragma once
#include <stdint.h>

// Copy to include/wifi_credentials.h and fill in. Never commit that file.

// 2.4 GHz router network (features 1-3, web page).
const char* const KWifiSsid     = "YOUR_2G4_SSID";
const char* const KWifiPassword = "YOUR_WIFI_PASSWORD";

// Printer Wi-Fi Direct (feature 4, printing).
const char* const KPrinterWifiSsid     = "DIRECT-XX-HP OfficeJet 8010";
const char* const KPrinterWifiPassword = "YOUR_PRINTER_WIFI_DIRECT_PASSWORD";
const char* const KPrinterIp           = "192.168.223.1";
const uint16_t    KPrinterPort         = 9100;
