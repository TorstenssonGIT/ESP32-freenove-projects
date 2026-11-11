/*
 * =============================================================================
 *  Lab 8 - Wi-Fi credentials stored on the board (NVS), not in the firmware
 * =============================================================================
 *
 *  Why
 *  ---
 *  Credentials compiled into the firmware end up in the .bin file and in
 *  any copy of the source. Here they are typed in ONCE over Serial and saved
 *  in the ESP32's NVS memory (non-volatile key-value storage in flash).
 *  Uploading new firmware does not erase them.
 *
 *  What this protects - and what it does not
 *  -----------------------------------------
 *    + No passwords in git, in the source, or in the firmware .bin file.
 *    + Passwords are never printed: typed characters are echoed as '*'.
 *    + SSIDs and passwords are stored XOR-obfuscated with a keystream
 *      derived from this chip's unique eFuse MAC address. A flash dump
 *      shows no readable text, and a flash copy does not decode on another
 *      ESP32.
 *    - This is OBFUSCATION, not encryption: someone with the board, this
 *      source code and the chip's MAC (readable with esptool) can decode
 *      it. Real protection needs ESP32 flash/NVS encryption or a
 *      read-protected eFuse HMAC key (permanent eFuse changes - not used
 *      on this lab board).
 *
 *  How to use (in the program's setup())
 *  -------------------------------------
 *      lab8::Credentials creds = lab8::loadCredentials();
 *      if (!creds.hasRouter() || lab8::setupRequested()) {
 *          lab8::runSetupMenu();
 *          creds = lab8::loadCredentials();
 *      }
 *
 *  At boot the board waits KMenuWaitMillis for 'c' on Serial. Open the
 *  monitor (pio device monitor), press 'c', then follow the menu.
 *  `pio run -t erase` wipes the whole flash, including these credentials.
 *
 *  Serial line (parsed by tests, passwords never included):
 *      CRED_STATUS router=<0|1> printer=<0|1>
 * =============================================================================
 */

#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <Preferences.h>

namespace lab8 {

// -----------------------------------------------------------------------------
//  Settings
// -----------------------------------------------------------------------------
constexpr const char* KNamespace          = "lab8wifi";  // NVS namespace (max 15)
constexpr uint32_t    KMenuWaitMillis     = 3000;        // Time to press 'c'
constexpr uint32_t    KInputTimeoutMillis = 120000;      // Max wait per answer
constexpr size_t      KMaxSsidLength      = 32;          // Wi-Fi standard limit
constexpr size_t      KMinPasswordLength  = 8;           // WPA2/WPA3 minimum
constexpr size_t      KMaxPasswordLength  = 63;          // WPA2/WPA3 maximum
constexpr uint16_t    KDefaultPrinterPort = 9100;        // JetDirect / raw
constexpr size_t      KMaxSecretLength    = 64;          // Largest stored value

// Mixed into the keystream seed. Changing it makes stored values unreadable
// (the user must enter them again).
constexpr uint32_t    KObfuscationSalt    = 0x4C414238;  // "LAB8"

// NVS key names (max 15 characters each).
constexpr const char* KKeyRouterSsid      = "router_ssid";
constexpr const char* KKeyRouterPassword  = "router_pass";
constexpr const char* KKeyPrinterSsid     = "printer_ssid";
constexpr const char* KKeyPrinterPassword = "printer_pass";
constexpr const char* KKeyPrinterIp       = "printer_ip";
constexpr const char* KKeyPrinterPort     = "printer_port";

// -----------------------------------------------------------------------------
//  Stored values
// -----------------------------------------------------------------------------
struct Credentials {
    String   routerSsid;       // 2.4 GHz router network (features 1-3)
    String   routerPassword;
    String   printerSsid;      // Printer Wi-Fi Direct network (feature 4)
    String   printerPassword;
    String   printerIp;        // Printer IP address IPv4
    uint16_t printerPort = KDefaultPrinterPort;

    bool hasRouter() const  { return !routerSsid.isEmpty(); }
    bool hasPrinter() const { return !printerSsid.isEmpty() && !printerIp.isEmpty(); }
};


// =============================================================================
//  Obfuscation (XOR with a chip-specific keystream)
// =============================================================================

// Seed = FNV-1a hash of salt + chip MAC + NVS key name. Using the key name
// means two equal values (e.g. same password twice) are stored differently.
// The MAC is unique per chip, so a flash copy does not decode on another ESP32.
inline uint32_t keystreamSeed(const char* keyName) {
    uint32_t hash = 2166136261u ^ KObfuscationSalt;
    auto mix = [&hash](uint8_t byte) {
        hash ^= byte;
        hash *= 16777619u;
    };

    const uint64_t mac = ESP.getEfuseMac();   // Unique per chip
    for (uint8_t shift = 0; shift < 64; shift += 8) {
        mix(static_cast<uint8_t>(mac >> shift));
    }
    for (const char* character = keyName; *character != '\0'; ++character) {
        mix(static_cast<uint8_t>(*character));
    }
    return hash != 0 ? hash : 1;              // xorshift must not start at 0
}

// XOR each byte with the next value of a xorshift32 keystream.
// Running it twice restores the original, so it both hides and reveals.
// The keystream is seeded with the chip's MAC and the NVS key name, so a flash
// copy does not decode on another ESP32.
inline void xorWithKeystream(uint8_t* data, size_t length, const char* keyName) {
    uint32_t state = keystreamSeed(keyName);
    for (size_t index = 0; index < length; ++index) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        data[index] ^= static_cast<uint8_t>(state);
    }
}

// Store a text value obfuscated, as raw bytes.
//  The value is truncated to KMaxSecretLength if too long.
//  The keyName is used to derive the keystream, so two equal values (e.g. same password twice) are stored differently.
//  The chip's MAC is used to seed the keystream, so a flash copy does not decode on another ESP32.
inline void putSecret(Preferences& prefs, const char* keyName, const String& value) {
    uint8_t buffer[KMaxSecretLength];
    const size_t length = value.length() < KMaxSecretLength ? value.length()
                                                            : KMaxSecretLength;
    for (size_t index = 0; index < length; ++index) {
        buffer[index] = static_cast<uint8_t>(value[index]);
    }
    xorWithKeystream(buffer, length, keyName);
    prefs.putBytes(keyName, buffer, length);
}

// Read and decode a value stored with putSecret(). Missing -> empty.
//  The keyName is used to derive the keystream, so two equal values (e.g. same password twice) are stored differently.
//  The chip's MAC is used to seed the keystream, so a flash copy does not decode on another ESP32.
inline String getSecret(Preferences& prefs, const char* keyName) {
    const size_t length = prefs.getBytesLength(keyName);
    if (length == 0 || length > KMaxSecretLength) {
        return String();
    }

    uint8_t buffer[KMaxSecretLength];
    prefs.getBytes(keyName, buffer, length);
    xorWithKeystream(buffer, length, keyName);

    String value;
    for (size_t index = 0; index < length; ++index) {
        value += static_cast<char>(buffer[index]);
    }
    return value;
}


// =============================================================================
//  NVS read / write
// =============================================================================

// Read everything from NVS. Missing values come back empty.
//  The chip's MAC is used to seed the keystream, so a flash copy does not decode on another ESP32.
//  The output is parsed by tests, so it must not change.
inline Credentials loadCredentials() {
    Credentials creds;
    Preferences prefs;
    prefs.begin(KNamespace, true);   // true = read-only
    creds.routerSsid      = getSecret(prefs, KKeyRouterSsid);
    creds.routerPassword  = getSecret(prefs, KKeyRouterPassword);
    creds.printerSsid     = getSecret(prefs, KKeyPrinterSsid);
    creds.printerPassword = getSecret(prefs, KKeyPrinterPassword);
    creds.printerIp       = prefs.getString(KKeyPrinterIp, "");
    creds.printerPort     = prefs.getUShort(KKeyPrinterPort, KDefaultPrinterPort);
    prefs.end();
    return creds;
}

// Print which credential sets exist. Passwords are never printed.
//  The chip's MAC is used to seed the keystream, so a flash copy does not decode on another ESP32.
//  The output is parsed by tests, so it must not change.
inline void printCredentialStatus(const Credentials& creds) {
    Serial.print("CRED_STATUS router=");
    Serial.print(creds.hasRouter() ? 1 : 0);
    Serial.print(" printer=");
    Serial.println(creds.hasPrinter() ? 1 : 0);
}


// =============================================================================
//  Serial input
// =============================================================================

// Read one line from Serial.
//   - Enter (CR or LF) finishes the line; empty lines are ignored.
//   - Backspace removes the last character.
//   - hidden = true echoes '*' instead of the character (passwords).
//   - Only printable ASCII is accepted, up to maxLength characters.
// Returns false if nothing was entered within KInputTimeoutMillis.

inline bool readLine(String& out, bool hidden, size_t maxLength) {
    out = "";
    const uint32_t startMillis = millis();

    while (millis() - startMillis < KInputTimeoutMillis) {
        if (!Serial.available()) {
            delay(5);
            continue;
        }
        const char character = static_cast<char>(Serial.read());

        if (character == '\r' || character == '\n') {
            if (out.isEmpty()) {
                continue;            // Skip the LF of a CR+LF pair
            }
            Serial.println();
            return true;
        }
        if (character == '\b' || character == 0x7F) {
            if (!out.isEmpty()) {
                out.remove(out.length() - 1);
                Serial.print("\b \b");   // Erase the echoed character
            }
            continue;
        }
        const bool isPrintable = character >= 32 && character <= 126;
        if (!isPrintable || out.length() >= maxLength) {
            continue;
        }
        out += character;
        Serial.print(hidden ? '*' : character);
    }

    Serial.println();
    Serial.println("Timed out - nothing saved.");
    return false;
}

// Ask for a value until it is valid or the user times out.
//  The prompt is printed each time. The answer is stored in out.
inline bool askText(const char* prompt, String& out, bool hidden,
                    size_t minLength, size_t maxLength) {
    while (true) {
        Serial.print(prompt);
        if (!readLine(out, hidden, maxLength)) {
            return false;
        }
        if (out.length() >= minLength) {
            return true;
        }
        Serial.print("Too short - at least ");
        Serial.print(minLength);
        Serial.println(" characters.");
    }
}


// =============================================================================
//  Setup menu
// =============================================================================

// Wait up to waitMillis for the user to send 'c' on Serial.
//  Returns true if 'c' was received, false if the timeout expired.

inline bool setupRequested(uint32_t waitMillis = KMenuWaitMillis) {
    Serial.print("Press 'c' within ");
    Serial.print(waitMillis / 1000);
    Serial.println(" s to configure Wi-Fi credentials.");

    const uint32_t startMillis = millis();
    while (millis() - startMillis < waitMillis) {
        if (Serial.available()) {
            const char character = static_cast<char>(Serial.read());
            if (character == 'c' || character == 'C') {
                return true;
            }
        }
        delay(10);
    }
    return false;
}

// Ask for and save the router network.
//  The chip's MAC is used to seed the keystream, so a flash copy does not decode on another ESP32.

inline void setRouterCredentials() {
    String ssid;
    String password;
    if (!askText("Router SSID (2.4 GHz): ", ssid, false, 1, KMaxSsidLength) ||
        !askText("Router password: ", password, true,
                 KMinPasswordLength, KMaxPasswordLength)) {
        return;
    }

    Preferences prefs;
    prefs.begin(KNamespace, false);   // false = read/write
    putSecret(prefs, KKeyRouterSsid, ssid);
    putSecret(prefs, KKeyRouterPassword, password);
    prefs.end();
    Serial.println("Router credentials saved.");
}

// Ask for and save the printer's Wi-Fi Direct network and address.
//  The chip's MAC is used to seed the keystream, so a flash copy does not decode on another ESP32.
inline void setPrinterCredentials() {
    String ssid;
    String password;
    String ipText;
    String portText;

    if (!askText("Printer Wi-Fi Direct SSID: ", ssid, false, 1, KMaxSsidLength) ||
        !askText("Printer Wi-Fi Direct password: ", password, true,
                 KMinPasswordLength, KMaxPasswordLength)) {
        return;
    }

    IPAddress ip;
    while (true) {
        if (!askText("Printer IP (e.g. 192.168.223.1): ", ipText, false, 7, 15)) {
            return;
        }
        if (ip.fromString(ipText)) {
            break;
        }
        Serial.println("Not a valid IPv4 address.");
    }

    long port = 0;
    while (true) {
        if (!askText("Printer port (9100 = JetDirect): ", portText, false, 1, 5)) {
            return;
        }
        port = portText.toInt();
        if (port >= 1 && port <= 65535) {
            break;
        }
        Serial.println("Port must be 1-65535.");
    }

    Preferences prefs;
    prefs.begin(KNamespace, false);
    putSecret(prefs, KKeyPrinterSsid, ssid);
    putSecret(prefs, KKeyPrinterPassword, password);
    prefs.putString(KKeyPrinterIp, ip.toString());
    prefs.putUShort(KKeyPrinterPort, static_cast<uint16_t>(port));
    prefs.end();
    Serial.println("Printer credentials saved.");
}

// Show what is stored. Passwords are only reported as set / not set.
inline void showCredentials() {
    const Credentials creds = loadCredentials();

    Serial.print("Router SSID     : ");
    Serial.println(creds.hasRouter() ? creds.routerSsid : String("(not set)"));
    Serial.print("Router password : ");
    Serial.println(creds.routerPassword.isEmpty() ? "(not set)" : "(set, hidden)");

    Serial.print("Printer SSID    : ");
    Serial.println(creds.printerSsid.isEmpty() ? String("(not set)") : creds.printerSsid);
    Serial.print("Printer password: ");
    Serial.println(creds.printerPassword.isEmpty() ? "(not set)" : "(set, hidden)");
    Serial.print("Printer address : ");
    if (creds.printerIp.isEmpty()) {
        Serial.println("(not set)");
    } else {
        Serial.print(creds.printerIp);
        Serial.print(':');
        Serial.println(creds.printerPort);
    }
}

// Remove all Lab 8 credentials from NVS.
inline void eraseCredentials() {
    Preferences prefs;
    prefs.begin(KNamespace, false);
    prefs.clear();
    prefs.end();
    Serial.println("All Lab 8 credentials erased.");
}

// Interactive menu on Serial. Returns when the user chooses 0 or times out.
inline void runSetupMenu() {
    while (true) {
        Serial.println();
        Serial.println("=== Lab 8 Wi-Fi credentials ===");
        Serial.println(" 1  Set router network (2.4 GHz)");
        Serial.println(" 2  Set printer Wi-Fi Direct network");
        Serial.println(" 3  Show stored values (passwords hidden)");
        Serial.println(" 4  Erase all");
        Serial.println(" 0  Exit and continue");

        String choice;
        if (!askText("Choice: ", choice, false, 1, 1)) {
            return;                       // Timeout: continue booting
        }

        switch (choice[0]) {
            case '1': setRouterCredentials();  break;
            case '2': setPrinterCredentials(); break;
            case '3': showCredentials();       break;
            case '4': eraseCredentials();      break;
            case '0': return;
            default:  Serial.println("Unknown choice."); break;
        }
    }
}

}  // namespace lab8
