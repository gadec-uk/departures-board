/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * OpenWeatherMap Weather Client Library
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

class weatherClient: public JsonListenerGS {

    private:
        const char* apiHost = "api.openweathermap.org";
        sharedBufferSpace* js = nullptr;
        int weatherItem = 0;

        float temperature;
        float windSpeed;

    public:
        char currentWeatherMessage[MAXWEATHERSIZE];

        weatherClient(sharedBufferSpace *sharedBuffer);
        int updateWeather(String apiKey, String lat, String lon);

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