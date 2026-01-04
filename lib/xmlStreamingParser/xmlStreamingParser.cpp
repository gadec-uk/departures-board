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
#include <xmlStreamingParser.h>

#define fallthrough  __attribute__((__fallthrough__))

xmlStreamingParser::xmlStreamingParser() {
    reset();
}

void xmlStreamingParser::setListener(xmlListener* listener) {
    myListener = listener;
}

void xmlStreamingParser::reset() {
    inAttrQuote=false;
    ChangeState(STATE_BEGIN);
}

void xmlStreamingParser::parse(const char character) {
    switch (state) {
        case STATE_BEGIN:
            state_Begin(character);
            break;
        case STATE_STARTTAG:
            state_StartTag(character);
            break;
        case STATE_TAGNAME:
            state_TagName(character);
            break;
        case STATE_TAGCONTENTS:
            state_TagContents(character);
            break;
        case STATE_ENDTAG:
            state_EndTag(character);
            break;
        case STATE_EMPTYTAG:
            state_EmptyTag(character);
            break;
        case STATE_ATTRIBUTE:
            state_Attribute(character);
            break;
        default:
            break;
    }
}

/* Wait for a tag start character */
void  xmlStreamingParser::state_Begin(const char character) {

    if (bInitialize) {
        length=0;
        buffer[length] = '\0';
        bInitialize=false;
    }

    switch (character)
    {
        case '<':
            ChangeState(STATE_STARTTAG);
            break;
        default:
            break;
    }
}

/* We've already found a tag start character, determine if this is start or end tag,
 *  and parse the tag name */
void xmlStreamingParser::state_StartTag(const char character) {

    if (bInitialize) bInitialize=false;

    switch(character)
    {
        case '<': case '>':
            /* Syntax error! */
            break;
        case ' ': case '\r': case '\n': case '\t':
            /* Ignore whitespace */
            break;
        case '/':
            ChangeState(STATE_ENDTAG);
            break;
        default:
            buffer[0] = character;
            length = 1;
            buffer[length] = '\0';
            ChangeState(STATE_TAGNAME);
            break;
    }
}

void xmlStreamingParser::state_TagName(const char character) {

    nextState = STATE_NULL;
    if(bInitialize)
    {
        /* Expect one character in the buffer; the start of the tag name from the previous state*/
        bInitialize = false;
    }

    switch(character)
    {
        case ' ': case '\r': case '\n': case '\t':
            /* Tag name complete, whitespace indicates tag attribute */
            nextState = STATE_ATTRIBUTE;
            break;
        case '/':
            nextState = STATE_EMPTYTAG;    // workaround for urls
            break;
        case '>':
            nextState = STATE_TAGCONTENTS;
            /* Done with tag, contents may follow */
            break;
        default:
            ContextBufferAddChar(character);
            break;
    }

    if(nextState != STATE_NULL)
    {
        if (length>0) { length++; myListener->startTag(buffer);}
        ChangeState(nextState);
    }
}

void xmlStreamingParser::state_EmptyTag(const char character) {
    nextState = STATE_NULL;

    if(bInitialize)
    {
        /* We need to keep the buffer as-is, since it contains the tag name */
        bInitialize = false;
    }

    switch(character)
    {
        case '>':
            nextState = STATE_TAGCONTENTS;
            break;
        default:
            break;
    }

    if(nextState != STATE_NULL)
    {
        if (length>0) { length++; myListener->endTag(buffer); }
        ChangeState(nextState);
    }
}

void xmlStreamingParser::state_TagContents(const char character) {
    nextState = STATE_NULL;

    if(bInitialize)
    {
        length = 0;
        buffer[length] = '\0';
        bInitialize = false;
    }

    switch(character)
    {
        case '<':
            nextState = STATE_STARTTAG;
            break;
        case ' ': case '\r': case '\n': case '\t':
            if(length == 0)
                break; /* Ignore leading whitespace */
            else
            {
                // Fallthrough
                fallthrough;
            }
        default:
            ContextBufferAddChar(character);
            break;
    }

    if(nextState != STATE_NULL)
    {
        if (length>0) { length++; myListener->value(buffer); }
        ChangeState(nextState);
    }
}

void xmlStreamingParser::state_Attribute(const char character) {
    nextState = STATE_NULL;

    if(bInitialize)
    {
        length = 0;
        buffer[length] = '\0';
        bInitialize = false;
    }

    switch(character)
    {
        case ' ': case '\r': case '\n': case '\t':
            if(length == 0)
                break;
            else
                nextState = STATE_ATTRIBUTE;
            break;
        case '\"':
            inAttrQuote = !inAttrQuote;
            ContextBufferAddChar('\"');
            break;
        case '/':
            if (inAttrQuote) {
                ContextBufferAddChar('/');
            } else {
                /* Handle the case where an attribute is included in an empty tag,
                and the attribute name/value has no trailing whitespace
                prior to the empty tag terminator. */
                if (length > 0) {
                    myListener->attribute(buffer);
                    length = 0;
                    buffer[length] = '\0';
                }
                /* We've found an empty tag that contains at least one attribute.
                Since the buffer containing the tag name is long-gone (the attribute
                is now in the parser's string buffer), we don't have a way to get it
                back. In order to generate a "tagEnd" event, store a dummy string
                containing a single space character (which isn't a valid tag name),
                which will be provided to the tagEndHandler callback. */
                ContextBufferAddChar(' ');
                nextState = STATE_EMPTYTAG;
            }
            break;
        case '>':
            if (inAttrQuote) ContextBufferAddChar(character);
            else nextState = STATE_TAGCONTENTS; /* Done with tag, contents may follow */
            break;
        default:
            ContextBufferAddChar(character);
            break;
    }

    if(nextState != STATE_NULL)
    {
        if(nextState != STATE_EMPTYTAG)
        {
            if (length>0) { inAttrQuote=false; length++; myListener->attribute(buffer); }
        }
        ChangeState(nextState);
    }
}

void xmlStreamingParser::state_EndTag(const char character) {

    nextState=STATE_NULL;

    if(bInitialize)
    {
        length = 0;
        buffer[length] = '\0';
        bInitialize = false;
    }

    switch(character)
    {
        case '<':
            /* Syntax error! */
            break;
        case ' ': case '\r': case '\n': case '\t':
            /* Ignore whitespace */
            break;
        case '>':
            nextState = STATE_TAGCONTENTS;
            break;
        default:
            ContextBufferAddChar(character);
            break;
    }

    if(nextState != STATE_NULL)
    {
        if (length>0) { length++; myListener->endTag(buffer); }
        ChangeState(nextState);
    }
}

void xmlStreamingParser::ContextBufferAddChar(const char character) {
    if (length < sizeof(buffer)-2) {
        buffer[length] = character;
        length++;
        buffer[length] = '\0';
    }
}

void xmlStreamingParser::ChangeState(int newState) {
    state = newState;
    bInitialize=true;
}