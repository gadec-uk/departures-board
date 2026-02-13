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

#define UPD_SUCCESS 0
#define UPD_INCOMPLETE 1
#define UPD_UNAUTHORISED 2
#define UPD_HTTP_ERROR 3
#define UPD_TIMEOUT 4
#define UPD_NO_RESPONSE 5
#define UPD_DATA_ERROR 6
#define UPD_NO_CHANGE 7

#define MAX_RSS_TITLES 5
#define MAX_RSS_TITLE_SIZE 140

class rssClient: public xmlListener {

    private:

        String grandParentTagName = "";
        String parentTagName = "";
        String tagName = "";
        String tagPath = "";
        int tagLevel = 0;
        String currentPath = "";
        String lastErrorMessage = "";

        void trim(char* str);

        virtual void startTag(const char *tagName);
        virtual void endTag(const char *tagName);
        virtual void parameter(const char *param);
        virtual void value(const char *value);
        virtual void attribute(const char *attribute);

    public:

        rssClient();
        int loadFeed(String url);
        String getLastError();
        char rssTitle[MAX_RSS_TITLES][MAX_RSS_TITLE_SIZE];
        int numRssTitles = 0;
};