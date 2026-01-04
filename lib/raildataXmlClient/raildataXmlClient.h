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
#include <stationData.h>

typedef void (*rdCallback) (int state, int id);

#define MAXHOSTSIZE 48
#define MAXAPIURLSIZE 48


class raildataXmlClient: public xmlListener {

    private:

        struct rdiService {
          char sTime[6];
          char destination[MAXLOCATIONSIZE];
          char via[MAXLOCATIONSIZE];
          char origin[MAXLOCATIONSIZE];
          char etd[11];
          char platform[4];
          bool isCancelled;
          bool isDelayed;
          int trainLength;
          byte classesAvailable;
          char opco[50];
          char calling[MAXCALLINGSIZE];
          char serviceMessage[MAXMESSAGESIZE];
          int serviceType;
        };

        struct rdiStation {
          char location[MAXLOCATIONSIZE];
          bool platformAvailable;
          int numServices;
          rdiService service[MAXBOARDSERVICES];
        };

        String grandParentTagName = "";
        String parentTagName = "";
        String tagName = "";
        String tagPath = "";
        int tagLevel = 0;
        bool loadingWDSL=false;
        String soapURL = "";
        char soapHost[MAXHOSTSIZE];
        char soapAPI[MAXAPIURLSIZE];

        String currentPath = "";
        rdiStation xStation;
        stnMessages xMessages;

        bool addedStopLocation = false;
        int id=0;
        int coaches=0;

        String lastErrorMessage = "";
        bool firstDataLoad;
        bool endXml;

        char filterCrs[4];
        bool filter = false;
        bool keepRoute = false;

        rdCallback Xcb;
        static bool compareTimes(const rdiService& a, const rdiService& b);
        void removeHtmlTags(char* input);
        void replaceWord(char* input, const char* target, const char* replacement);
        void pruneFromPhrase(char* input, const char* target);
        void fixFullStop(char* input);
        void sanitiseData();
        void deleteService(int x);

        virtual void startTag(const char *tagName);
        virtual void endTag(const char *tagName);
        virtual void parameter(const char *param);
        virtual void value(const char *value);
        virtual void attribute(const char *attribute);

    public:
        raildataXmlClient();
        int init(const char *wsdlHost, const char *wsdlAPI, rdCallback RDcb);
        int updateDepartures(rdStation *station, stnMessages *messages, const char *crsCode, const char *customToken, int numRows, bool includeBusServices, const char *callingCrsCode);
        String getLastError();
};