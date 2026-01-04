/*
 * XML Streaming Parser Library
 *  - based on the structure of samxl embedded XML parser by Zorxx Software at https://github.com/zorxx/saxml
 *
 * MIT License
 *
 * Copyright (c) 2025-2026 Gadec Software
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#pragma once

#include <Arduino.h>
#include <xmlListener.h>

#define XML_BUFFER_MAX_LENGTH 450

#define STATE_NULL 0
#define STATE_BEGIN 1
#define STATE_STARTTAG 2
#define STATE_TAGNAME 3
#define STATE_TAGCONTENTS 4
#define STATE_ENDTAG 5
#define STATE_EMPTYTAG 6
#define STATE_ATTRIBUTE 7

class xmlStreamingParser {
  private:

    int state;
    int nextState;
    xmlListener* myListener;

    char buffer[XML_BUFFER_MAX_LENGTH];
    bool bInitialize;   // True for the first call into a state
    bool inAttrQuote = false; // true if we're inside a quoted attribute string
    uint32_t length;

    void state_Begin(const char character);
    void state_StartTag(const char character);
    void state_TagName(const char character);
    void state_EmptyTag(const char character);
    void state_TagContents(const char character);
    void state_Attribute(const char character);
    void state_EndTag(const char character);
    void ContextBufferAddChar(const char character);
    void ChangeState(int newState);

  public:
    xmlStreamingParser();
    void parse(const char character);
    void setListener(xmlListener* listener);
    void reset();

};
