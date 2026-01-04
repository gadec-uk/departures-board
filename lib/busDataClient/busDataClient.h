/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * bustimes.org Client Library
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */
#pragma once
#include <JsonListener.h>
#include <JsonStreamingParser.h>
#include <stationData.h>

typedef void (*busClientCallback) ();

#define MAXBUSLINESIZE 9
#define BUSMAXREADSERVICES 20

#define PBT_START 0
#define PBT_HEADER 1
#define PBT_SERVICE 2
#define PBT_DESTINATION 3
#define PBT_SCHEDULED 4
#define PBT_EXPECTED 5

class busDataClient: public JsonListener {

    private:

        struct busService {
            char destinationName[MAXLOCATIONSIZE];
            char lineName[MAXBUSLINESIZE];
            char scheduled[6];
            char expected[6];
        };

        struct busStop {
            int numServices;
            busService service[BUSMAXREADSERVICES];
        };

        const char* apiHost = "bustimes.org";
        String currentKey = "";
        String currentObject = "";

        int id=0;
        String longName;
        bool maxServicesRead = false;
        busStop xBusStop;

        String stripTag(String html);
        void replaceWord(char* input, const char* target, const char* replacement);

    public:
        String lastErrorMsg = "";

        busDataClient();
        int getStopLongName(const char *locationId, char *locationName);
        int updateDepartures(rdStation *station, const char *locationId, busClientCallback Xcb);

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