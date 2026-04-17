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
#include <sharedDataStructs.h>
#include <responseCodes.h>

#define MAXBUSFILTERSIZE 25

#define PBT_START 0
#define PBT_HEADER 1
#define PBT_SERVICE 2
#define PBT_DESTINATION 3
#define PBT_SCHEDULED 4
#define PBT_EXPECTED 5

class busDataClient {

    private:

        const char* apiHost = "bustimes.org";

        int id=0;
        bool maxServicesRead = false;
        bool boardChanged = false;
        busTubeStation* xBusStop = nullptr;
        sharedBufferSpace* js = nullptr;

        String stripTag(String html);
        void replaceWord(char* input, const char* target, const char* replacement);
        void trim(char* &start, char* &end);
        bool equalsIgnoreCase(const char* a, int a_len, const char* b);
        bool serviceMatchesFilter(const char* filter, const char* serviceId);

    public:

        busDataClient(busTubeStation *station, sharedBufferSpace *sharedBuffer);
        void cleanFilter(const char* rawFilter, char* cleanedFilter, size_t maxLen);
        int fetchDepartures(rdStation *station, const char *locationId, const char *filter);
        void loadDepartures(rdStation *station);
};