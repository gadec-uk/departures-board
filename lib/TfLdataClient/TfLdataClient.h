/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * TfL London Underground Client Library
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */
#pragma once
#include <JsonListenerGS.h>
#include <JsonStreamingParserGS.h>
#include <sharedDataStructs.h>
#include <responseCodes.h>

class TfLdataClient: public JsonListenerGS {

    private:

        const char* apiHost = "api.tfl.gov.uk";
        const char* tflAttribution = "Powered by TfL Open Data";

        int id=0;
        bool maxServicesRead = false;
        bool boardChanged = false;
        bool fetchingArrivals = false;

        busTubeStation *xStation = nullptr;
        stnMessages *xMessages = nullptr;
        sharedBufferSpace* js = nullptr;

        bool pruneFromPhrase(char* input, const char* target);
        void replaceWord(char* input, const char* target, const char* replacement);
        void removeExcessSpaces(char *input);
        void fixFullStop(char *input);
        static bool compareTimes(const busTubeService& a, const busTubeService& b);

    public:

        TfLdataClient(busTubeStation *station, stnMessages *messages, sharedBufferSpace *sharedBuffer);
        int fetchArrivals(rdStation *station, stnMessages *messages, const char *locationId, const char *lineId, const char *lineDirection, bool noMessages, const char *apiKey);
        void loadArrivals(rdStation *station, stnMessages *messages);

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