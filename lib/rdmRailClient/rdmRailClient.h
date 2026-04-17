/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * rdmRailClient Library
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */

#pragma once
#include "JsonListenerGS.h"
#include "JsonStreamingParserGS.h"
#include <sharedDataStructs.h>
#include <responseCodes.h>

#define MAXHOSTSIZE 48
#define MAXAPIURLSIZE 48
#define MAXPLATFORMFILTERSIZE 25


class rdmRailClient: public JsonListenerGS {

    private:

        struct rdiLocation {
          char location[MAXLOCATIONSIZE];
          char scheduledTime[6];
          char actualTime[6];
        };

        const char* rdmHost = "api1.raildata.org.uk";
        const char* rdmDeparturesApi = "/1010-live-departure-board-dep1_2/LDBWS/api/20220120/GetDepBoardWithDetails/";
        const char* rdmServiceDetailApi = "/1010-service-details1_2/LDBWS/api/20220120/GetServiceDetails/";

        int inCallingArray = 0;
        int arrayNestLevel = 0;
        bool fetchingDepartures;
        rdiStation* xStation = nullptr;
        stnMessages* xMessages = nullptr;
        sharedBufferSpace* js = nullptr;

        rdiLocation thisLocation;
        rdiLocation lastLocation;

        bool addedStopLocation = false;
        int id=0;
        int coaches=0;

        bool firstDataLoad;
        bool endXml;

        char platformFilter[MAXPLATFORMFILTERSIZE];
        bool filterPlatforms = false;
        bool keepRoute = false;

        static bool compareTimes(const rdiService& a, const rdiService& b);
        void removeHtmlTags(char* input);
        void replaceWord(char* input, const char* target, const char* replacement);
        void pruneFromPhrase(char* input, const char* target);
        void fixFullStop(char* input);
        int timeDiff(const char *scheduled, const char *actual);
        void sanitiseData();
        void deleteService(int x);
        void trim(char* &start, char* &end);
        bool equalsIgnoreCase(const char* a, int a_len, const char* b);
        bool serviceMatchesFilter(const char* filter, const char* serviceId);
        void trimSpaces(char *text);
        int getServiceDetails(const char *serviceID, String apiToken);

        virtual void whitespace(char c);
        virtual void startDocument();
        virtual void key(const char *key);
        virtual void value(const char *value);
        virtual void endArray();
        virtual void endObject();
        virtual void endDocument();
        virtual void startArray();
        virtual void startObject();

    public:
        rdmRailClient(rdiStation *station, stnMessages *messages, sharedBufferSpace *sharedBuffer);
        void cleanFilter(const char* rawFilter, char* cleanedFilter, size_t maxLen);
        int fetchDepartures(rdStation *station, stnMessages *messages, const char *crsCode, String departuresApiKey, String serviceApiKey, int numRows, bool includeBusServices, const char *callingCrsCode, const char *platforms, int timeOffset, bool fetchLastSeen, bool includeServiceMessages);
        void loadDepartures(rdStation *station, stnMessages *messages);
};