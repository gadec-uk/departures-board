/*
 * Departures Board (c) 2025-2026 Gadec Software
 * Refactored for v3.0 by Matt McNeill 2026 CB Labs
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons
 * Attribution-NonCommercial-ShareAlike 4.0 International. To view a copy of
 * this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 *
 * Module: lib/logger/logger.cpp
 * Description: Implementation of the lightweight logging utility.
 *
 * Exported Functions/Classes:
 * - Logger: Static utility class for system-wide logging.
 *   - begin(): Initializes Serial communication.
 *   - logSplashMessage(): Prints a framed splash message.
 *   - registerSecret(): Adds a string to the redaction list.
 *   - redact(): Filters sensitive strings from logs.
 */

#include "logger.hpp"
// #include "boardLED.hpp"
#include <HardwareSerial.h>
#include <stdarg.h>

#if APP_DEBUG_LEVEL >= APP_LOG_LEVEL_ERROR

std::vector<String>
    Logger::secrets; // Registry of sensitive strings to be redacted

/**
 * @brief Initializes the Serial port and waits for it to stabilize.
 * @param baud The baud rate for the Serial connection.
 */
void Logger::begin(unsigned long baud) {
#ifndef UNIT_TEST
  Serial.begin(baud);
  delay(10); // Standard brief delay
#endif
}

/**
 * @brief Blocks execution until Serial connects or timeout expires, flashing
 * LED.
 * @param timeout_ms Maximum time to wait in milliseconds.
 */
void Logger::waitForSerial(uint32_t timeout_ms) {
  // BoardLED::init();
  uint32_t startAt = millis();
  uint32_t lastFlip = 0;
  uint32_t lastPrint = 0;

  // Run a single continuous loop for the specified timeout duration
  while ((millis() - startAt) < timeout_ms) {
    uint32_t now = millis();
    uint32_t currentInterval = Serial ? 500 : 100;

    // LED Flip logic
    if ((now - lastFlip) >= currentInterval) {
      // BoardLED::flip();
      lastFlip = now;
    }

    // Terminal print logic (every 1 second)
    if ((now - lastPrint) >= 1000) {
      Serial.print(Serial ? "o" : ".");
      lastPrint = now;
    }

    // Yield to prevent watchdog starvation
    delay(50);
  }

  if (Serial) {
    Serial.println();
  }

  // Guarantee LED is OFF when exiting this wait loop
  // BoardLED::off();
}

/**
 * @brief Logs a framed splash message to the Serial console.
 * @param message The text to be framed.
 */
void Logger::logSplashMessage(const char *message) {
  if (message == nullptr)
    return;
  size_t len = strlen(message);

  Serial.println("");
  for (size_t i = 0; i < len + 8; i++)
    Serial.print("#");
  Serial.println("");
  Serial.print("### ");
  Serial.print(message);
  Serial.println(" ###");
  for (size_t i = 0; i < len + 8; i++)
    Serial.print("#");
  Serial.println("\n");
}

/**
 * @brief Registers a sensitive string (e.g., API key) to be redacted from all
 * future log output.
 * @param secret The plaintext string that should not appear in the logs.
 */
void Logger::registerSecret(const String &secret) {
  if (secret.length() > 0) {
    secrets.push_back(secret);
  }
}

/**
 * @brief Scans a log message and replaces any registered secrets with
 * asterisks.
 * @param message The original unredacted log string.
 * @return A safe version of the string with sensitive data hidden.
 */
String Logger::redact(const String &message) {
  if (secrets.empty() || message.length() == 0)
    return message;

  bool found = false;
  for (const String &secret : secrets) {
    if (secret.length() > 0 && message.indexOf(secret) >= 0) {
      found = true;
      break;
    }
  }
  if (!found)
    return message;

  String redactedMessage = message;
  for (const String &secret : secrets) {
    if (secret.length() > 0)
      redactedMessage.replace(secret, "***REDACTED***");
  }
  return redactedMessage;
}

/**
 * @brief Scans a char buffer and replaces any registered secrets with asterisks
 * in-place.
 */
void Logger::redactInPlace(char *buffer, size_t bufferSize) {
  if (secrets.empty() || buffer == nullptr || bufferSize == 0)
    return;

  for (const String &secret : secrets) {
    if (secret.length() == 0)
      continue;
    const char *secretStr = secret.c_str();
    size_t secretLen = secret.length();

    char *match = strstr(buffer, secretStr);
    while (match) {
      // Overwrite with '*' in-place
      for (size_t i = 0; i < secretLen; i++) {
        match[i] = '*';
      }
      match = strstr(match + secretLen, secretStr);
    }
  }
}

/**
 * @brief Internal function that formats, redacts, and outputs the final log
 * message to Serial (String version).
 * @param icon An emoji icon representing the severity.
 * @param category The subsystem or tag.
 * @param message The raw message to be logged.
 */
void Logger::printRedacted(const String &icon, const char *category,
                           const String &message) {
  Serial.print(icon);
  Serial.print(" [");
  Serial.print(category);
  Serial.print("] ");
  Serial.println(redact(message));
}

/**
 * @brief Internal function that formats and outputs the final log message to
 * Serial (Literal version). Bypasses String allocation and redaction if no
 * secrets are registered for maximum efficiency.
 * @param icon An emoji icon representing the severity.
 * @param category The subsystem or tag.
 * @param message The raw literal string to be logged.
 */
void Logger::printRedacted(const String &icon, const char *category,
                           const char *message) {
  Serial.print(icon);
  Serial.print(" [");
  Serial.print(category);
  Serial.print("] ");

  if (secrets.empty() || message == nullptr) {
    Serial.println(message);
  } else {
    char buffer[256];
    strlcpy(buffer, message, sizeof(buffer));
    redactInPlace(buffer, sizeof(buffer));
    Serial.println(buffer);
  }
}

/**
 * @brief Logs an informational message. Called internally by the LOG_INFO
 * macro.
 */
void Logger::_info(const char *category, const String &message) {
  printRedacted("🔘", category, message);
}

void Logger::_info(const char *category, const char *message) {
  printRedacted("🔘", category, message);
}

void Logger::_infof(const char *category, const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  printRedacted("🔘", category, buffer);
}

/**
 * @brief Logs a warning message. Called internally by the LOG_WARN macro.
 */
void Logger::_warn(const char *category, const String &message) {
  printRedacted("🟡", category, message);
}

void Logger::_warn(const char *category, const char *message) {
  printRedacted("🟡", category, message);
}

void Logger::_warnf(const char *category, const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  printRedacted("🟡", category, buffer);
}

/**
 * @brief Logs an error message. Called internally by the LOG_ERROR macro.
 */
void Logger::_error(const char *category, const String &message) {
  printRedacted("🔴", category, message);
}

void Logger::_error(const char *category, const char *message) {
  printRedacted("🔴", category, message);
}

void Logger::_errorf(const char *category, const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  printRedacted("🔴", category, buffer);
}

/**
 * @brief Logs a debug message. Called internally by the LOG_DEBUG macro.
 */
void Logger::_debug(const char *category, const String &message) {
  printRedacted("🔵", category, message);
}

void Logger::_debug(const char *category, const char *message) {
  printRedacted("🔵", category, message);
}

void Logger::_debugf(const char *category, const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  printRedacted("🔵", category, buffer);
}

/**
 * @brief Logs a verbose message. Called internally by the LOG_VERBOSE macro.
 */
void Logger::_verbose(const char *category, const String &message) {
  printRedacted("🟣", category, message);
}

void Logger::_verbose(const char *category, const char *message) {
  printRedacted("🟣", category, message);
}

void Logger::_verbosef(const char *category, const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  printRedacted("🟣", category, buffer);
}

#endif // APP_DEBUG_LEVEL >= APP_LOG_LEVEL_ERROR
