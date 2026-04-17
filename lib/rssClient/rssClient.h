/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * rssClient Library
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */
#pragma once
#include <xmlListener.h>
#include <xmlStreamingParser.h>
#include <sharedDataStructs.h>
#include <responseCodes.h>

#define MAX_RSS_TITLES 5
#define MAX_RSS_TITLE_SIZE 140

class rssClient: public xmlListener {

    private:

        int tagLevel = 0;
        sharedBufferSpace* js = nullptr;

        void trim(char* str);

        virtual void startTag(const char *tagName);
        virtual void endTag(const char *tagName);
        virtual void parameter(const char *param);
        virtual void value(const char *value);
        virtual void attribute(const char *attribute);

    public:

        rssClient(sharedBufferSpace *sharedBuffer);
        int loadFeed(String url);
        char rssTitle[MAX_RSS_TITLES][MAX_RSS_TITLE_SIZE];
        int numRssTitles = 0;
};