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
#include <JsonListener.h>
#include <JsonStreamingParser.h>
#include <md5Utils.h>

#define MAX_RELEASE_ASSETS 16   //  The maximum number of release asset details that will be read and stored

class github: public JsonListener {

    private:
        const char* apiHost = "api.github.com";
        String apiGetLatestRelease = "/repos/gadec-uk/departures-board/releases/latest";
        String currentKey = "";
        String currentArray = "";
        String currentObject = "";
        String previousObject = "";

        String lastErrorMsg = "";

        String assetURL;
        String assetName;
        md5Utils md5;

    public:
        String accessToken;
        String releaseId;
        String releaseDescription;
        int releaseAssets;
        String releaseAssetURL[MAX_RELEASE_ASSETS];
        String releaseAssetName[MAX_RELEASE_ASSETS];

        github(String repository, String token);

        bool getLatestRelease();
        //bool downloadAssetToLittleFS(String url, String filename);

        String getLastError();

        virtual void whitespace(char c);
        virtual void startDocument();
        virtual void key(String key);
        virtual void value(String value);
        virtual void endArray();
        virtual void endObject();
        virtual void endDocument();
        virtual void startArray();
        virtual void startObject();
};
