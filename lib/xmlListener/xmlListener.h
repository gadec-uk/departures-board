/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * xmlListener Library
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */

#pragma once
#include <Arduino.h>

class xmlListener {
  private:

  public:

    virtual void startTag(const char *tagName) = 0;
    virtual void endTag(const char *tagName) = 0;
    virtual void parameter(const char *param) = 0;
    virtual void value(const char *value) = 0;
    virtual void attribute(const char *attribute) = 0;
};
