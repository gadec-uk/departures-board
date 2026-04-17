/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * GitHub Client Library - enables checking for latest release and downloading assets to file system
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */
#pragma once
#include <JsonListenerGS.h>
#include <JsonStreamingParserGS.h>
#include <md5Utils.h>
#include <sharedDataStructs.h>
#include <responseCodes.h>

#define MAX_RELEASE_ASSETS 16   //  The maximum number of release asset details that will be read and stored
#define RELEASEIDSIZE
class github: public JsonListenerGS {

    private:

        sharedBufferSpace* js = nullptr;
        String assetURL;
        String assetName;
        md5Utils md5;

    public:
        String releaseId="";
        String releaseDescription="";
        String firmwareURL="";

        github(sharedBufferSpace *sharedBuffer);

        int getLatestRelease();

        String getLastError();

        virtual void whitespace(char c);
        virtual void startDocument();
        virtual void key(const char *key);
        virtual void value(const char *value);
        virtual void endArray();
        virtual void endObject();
        virtual void endDocument();
        virtual void startArray();
        virtual void startObject();
};
