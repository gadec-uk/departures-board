/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * MD5 Utilities Library - calculate the MD5 hash of a file on the LittleFS file system. Convert a base64 MD5 hash to Hex.
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */

#include <md5Utils.h>
#include <LittleFS.h>
#include <mbedtls/md5.h>

md5Utils::md5Utils() {}

String md5Utils::calculateFileMD5(const char* filePath) {
  File file = LittleFS.open(filePath, "r");
  if (!file || file.isDirectory()) {
    return String();  // Return empty result if failure
  }

  // Initialize MD5 context
  mbedtls_md5_context ctx;
  mbedtls_md5_init(&ctx);
  mbedtls_md5_starts(&ctx);

  // Read file and update MD5 hash
  const size_t bufferSize = 512;
  uint8_t buffer[bufferSize];
  while (file.available()) {
    size_t bytesRead = file.read(buffer, bufferSize);
    mbedtls_md5_update(&ctx, buffer, bytesRead);
  }

  // Finalize hash
  uint8_t hash[16];
  mbedtls_md5_finish(&ctx, hash);
  mbedtls_md5_free(&ctx);
  file.close();

  // Convert hash to 32-character hex string
  char hexString[33];
  for (int i = 0; i < 16; i++) {
    sprintf(&hexString[i * 2], "%02x", hash[i]);
  }
  hexString[32] = '\0';

  return String(hexString);
}

//
// Converts a base64 encoded MD5 hash (as provided in the GitHub response headers) and converts to Hex
//
String md5Utils::base64ToHex(String base64Hash) {
    // Base64 decoding map
    const char base64Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int base64Value[256];
    for (int i = 0; i < 256; i++) base64Value[i] = -1;
    for (int i = 0; i < 64; i++) base64Value[(int)base64Table[i]] = i;

    // Decode Base64 into binary data
    int len = base64Hash.length();
    int padding = (base64Hash[len - 1] == '=') + (base64Hash[len - 2] == '=');
    int binarySize = (len * 3) / 4 - padding;
    uint8_t binaryData[binarySize];

    int index = 0, buffer = 0, bits = 0;
    for (int i = 0; i < len; i++) {
        int value = base64Value[(int)base64Hash[i]];
        if (value == -1) continue;
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            binaryData[index++] = (buffer >> bits) & 0xFF;
        }
    }

    // Convert binary data to hex string
    String hexHash = "";
    for (int i = 0; i < binarySize; i++) {
        if (binaryData[i] < 16) hexHash += '0'; // Add leading zero for single-digit hex
        hexHash += String(binaryData[i], HEX);
    }

    return hexHash;
}
