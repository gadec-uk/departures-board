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
 *
 * Module: lib/logger/logger.hpp
 * Description: Lightweight logging utility for ESP32 with color-coded levels
 * and secret redaction.
 *
 * Exported Functions/Classes:
 * - Logger: Static utility class for system-wide logging.
 *   - begin(): Initializes Serial communication.
 *   - logSplashMessage(): Prints a framed splash message.
 *   - registerSecret(): Adds a string to the redaction list.
 *   - redactInPlace(): Efficient in-place redaction for char buffers.
 *   - _info() / _infof(): Info level logging (String / printf-style).
 *   - _warn() / _warnf(): Warning level logging (String / printf-style).
 *   - _error() / _errorf(): Error level logging (String / printf-style).
 *   - _debug() / _debugf(): Debug level logging (String / printf-style).
 *   - _verbose() / _verbosef(): Verbose logging (String / printf-style).
 *
 * Macros:
 * - LOG_BEGIN(baud): Macro-guarded Serial initialization.
 * - LOG_REGISTER_SECRET(secret): Macro-guarded secret registration.
 * - LOG_ERROR(sub, msg): Macro-guarded error logging.
 * - LOG_WARN(sub, msg): Macro-guarded warning logging.
 * - LOG_INFO(sub, msg): Macro-guarded info logging.
 * - LOG_DEBUG(sub, msg): Macro-guarded debug logging.
 * - LOG_VERBOSE(sub, msg): Macro-guarded verbose (Level 5) logging.
 * - LOG_SPLASH(msg): Macro-guarded splash message logging.
 */
#pragma once

#include <Arduino.h>
// #include <vector>

#define APP_LOG_LEVEL_NONE 0
#define APP_LOG_LEVEL_ERROR 1
#define APP_LOG_LEVEL_WARN 2
#define APP_LOG_LEVEL_INFO 3
#define APP_LOG_LEVEL_DEBUG 4
#define APP_LOG_LEVEL_VERBOSE 5

#ifndef APP_DEBUG_LEVEL
#define APP_DEBUG_LEVEL APP_LOG_LEVEL_NONE
#endif

#if APP_DEBUG_LEVEL >= APP_LOG_LEVEL_ERROR
#define LOG_ERROR(sub, msg) Logger::_error(sub, msg)
#define LOG_ERRORf(sub, fmt, ...) Logger::_errorf(sub, fmt, ##__VA_ARGS__)
#define LOG_BEGIN(baud) Logger::begin(baud)
#define LOG_REGISTER_SECRET(secret) Logger::registerSecret(secret)
#define LOG_WAIT_FOR_SERIAL(timeout_ms) Logger::waitForSerial(timeout_ms)
#else
#define LOG_ERROR(sub, msg)
#define LOG_ERRORf(sub, fmt, ...)
#define LOG_BEGIN(baud)
#define LOG_REGISTER_SECRET(secret)
#define LOG_WAIT_FOR_SERIAL(timeout_ms)
#endif

#if APP_DEBUG_LEVEL >= APP_LOG_LEVEL_WARN
#define LOG_WARN(sub, msg) Logger::_warn(sub, msg)
#define LOG_WARNf(sub, fmt, ...) Logger::_warnf(sub, fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(sub, msg)
#define LOG_WARNf(sub, fmt, ...)
#endif

#if APP_DEBUG_LEVEL >= APP_LOG_LEVEL_INFO
#define LOG_INFO(sub, msg) Logger::_info(sub, msg)
#define LOG_INFOf(sub, fmt, ...) Logger::_infof(sub, fmt, ##__VA_ARGS__)
#define LOG_SPLASH(msg) Logger::logSplashMessage(msg)
#else
#define LOG_INFO(sub, msg)
#define LOG_INFOf(sub, fmt, ...)
#define LOG_SPLASH(msg)
#endif

#if APP_DEBUG_LEVEL >= APP_LOG_LEVEL_DEBUG
#define LOG_DEBUG(sub, msg) Logger::_debug(sub, msg)
#define LOG_DEBUGf(sub, fmt, ...) Logger::_debugf(sub, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(sub, msg)
#define LOG_DEBUGf(sub, fmt, ...)
#endif

#if APP_DEBUG_LEVEL >= APP_LOG_LEVEL_VERBOSE
#define LOG_VERBOSE(sub, msg) Logger::_verbose(sub, msg)
#define LOG_VERBOSEf(sub, fmt, ...) Logger::_verbosef(sub, fmt, ##__VA_ARGS__)
#else
#define LOG_VERBOSE(sub, msg)
#define LOG_VERBOSEf(sub, fmt, ...)
#endif

/**
 * @brief Static utility class for system-wide logging with redaction support.
 */
class Logger {
public:
  /**
   * @brief Initializes the Serial port and waits for it to stabilize.
   * @param baud The baud rate for the Serial connection (Default: 115200).
   */
  static void begin(unsigned long baud = 115200);

  /**
   * @brief Blocks execution until Serial connects or timeout expires, flashing
   * LED.
   * @param timeout_ms Maximum time to wait in milliseconds.
   */
  static void waitForSerial(uint32_t timeout_ms = 3000);

  /**
   * @brief Logs a framed splash message to the Serial console.
   * @param message The text to be framed.
   */
  static void logSplashMessage(const char *message);

  /**
   * @brief Registers a sensitive string (e.g., API key) to be redacted from all
   * future log output.
   * @param secret The plaintext string that should not appear in the logs.
   */
  static void registerSecret(const String &secret);

  /**
   * @brief Logs an informational message. Called internally by the LOG_INFO
   * macro.
   * @param category Subsystem or tag.
   * @param message Message content.
   */
  static void _info(const char *category, const String &message);
  static void _info(const char *category, const char *message);
  static void _infof(const char *category, const char *format, ...);

  /**
   * @brief Logs a warning message. Called internally by the LOG_WARN macro.
   * @param category Subsystem or tag.
   * @param message Message content.
   */
  static void _warn(const char *category, const String &message);
  static void _warn(const char *category, const char *message);
  static void _warnf(const char *category, const char *format, ...);

  /**
   * @brief Logs an error message. Called internally by the LOG_ERROR macro.
   * @param category Subsystem or tag.
   * @param message Message content.
   */
  static void _error(const char *category, const String &message);
  static void _error(const char *category, const char *message);
  static void _errorf(const char *category, const char *format, ...);

  /**
   * @brief Logs a debug message. Called internally by the LOG_DEBUG macro.
   * @param category Subsystem or tag.
   * @param message Message content.
   */
  static void _debug(const char *category, const String &message);
  static void _debug(const char *category, const char *message);
  static void _debugf(const char *category, const char *format, ...);

  /**
   * @brief Logs a verbose message. Called internally by the LOG_VERBOSE macro.
   * @param category Subsystem or tag.
   * @param message Message content.
   */
  static void _verbose(const char *category, const String &message);
  static void _verbose(const char *category, const char *message);
  static void _verbosef(const char *category, const char *format, ...);

private:
  static std::vector<String> secrets; ///< Registry of sensitive strings
                                      /**
                                       * @brief Print redacted
                                       * @param icon
                                       * @param category
                                       * @param message
                                       */
  static void printRedacted(const String &icon, const char *category,
                            const String &message);
  static void printRedacted(const String &icon, const char *category,
                            const char *message);
  static String redact(const String &message);
  /**
   * @brief Scans a char buffer and replaces any registered secrets with
   * asterisks in-place.
   * @param buffer The character array to redact.
   * @param bufferSize The maximum size of the buffer to prevent overflow.
   */
  static void redactInPlace(char *buffer, size_t bufferSize);
};
