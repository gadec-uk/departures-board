/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * raildataXmlClient Library
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

#define MAXHOSTSIZE 48
#define MAXAPIURLSIZE 48
#define MAXPLATFORMFILTERSIZE 25


class raildataXmlClient: public xmlListener {

    private:

        struct rdiLocation {
          char location[MAXLOCATIONSIZE];
          char scheduledTime[6];
          char actualTime[6];
        };

        String greatGrandParentTagName = "";
        String grandParentTagName = "";
        String parentTagName = "";
        String tagName = "";
        String tagPath = "";
        int tagLevel = 0;
        bool loadingWDSL=false;
        bool fetchingDepartures;
        bool WDSLok=false;
        String soapURL = "";
        char soapHost[MAXHOSTSIZE];
        char soapAPI[MAXAPIURLSIZE];

        String currentPath = "";

        rdiStation* xStation = nullptr;
        stnMessages* xMessages = nullptr;
        sharedBufferSpace* js = nullptr;

        rdiLocation thisLocation;
        rdiLocation lastLocation;

        bool addedStopLocation = false;
        int id=0;
        int coaches=0;
        int numStoppingPoints=0;
        bool serviceArrived = false;
        bool addCallingTimes;

        bool firstDataLoad;
        bool endXml;
        char stcp[6];
        char *endCAL;
        int catPos;

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
        void trimSpaces(char *text);
        bool serviceMatchesFilter(const char* filter, const char* serviceId);
        int getServiceDetails(const char *serviceID, const char *customToken);

        virtual void startTag(const char *tagName);
        virtual void endTag(const char *tagName);
        virtual void parameter(const char *param);
        virtual void value(const char *value);
        virtual void attribute(const char *attribute);

    public:
        raildataXmlClient(rdiStation *station, stnMessages *messages, sharedBufferSpace *sharedBuffer);
        int init(const char *wsdlHost, const char *wsdlAPI);
        void cleanFilter(const char* rawFilter, char* cleanedFilter, size_t maxLen);
        int fetchDepartures(rdStation *station, stnMessages *messages, const char *crsCode, const char *customToken, int numRows, bool includeBusServices, const char *callingCrsCode, const char *platforms, int timeOffset, bool fetchLastSeen, bool includeServiceMessages, bool includeCallingTimes);
        void loadDepartures(rdStation *station, stnMessages *messages);
};