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
#include <JsonListener.h>
#include <JsonStreamingParser.h>

class weatherClient: public JsonListener {

    private:
        const char* apiHost = "api.openweathermap.org";
        String currentKey = "";
        String currentObject = "";
        int weatherItem = 0;

        String description;
        float temperature;
        float windSpeed;

    public:
        String currentWeather = "";
        String lastErrorMsg = "";

        weatherClient();

        bool updateWeather(String apiKey, String lat, String lon);

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