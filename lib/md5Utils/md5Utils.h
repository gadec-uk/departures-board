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

#pragma once
#include <Arduino.h>

class md5Utils {
    private:

    public:
        md5Utils();
        String calculateFileMD5(const char* filePath);
        String base64ToHex(String base64Hash);
};
