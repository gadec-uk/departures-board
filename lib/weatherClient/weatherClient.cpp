/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * Open-Meteo / OpenWeather Map Weather Client Library
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */

#include <weatherClient.h>
#include <JsonListenerGS.h>
#include <WiFiClientSecure.h>

const char* const weatherClient::apiHosts[] = {
    "api.openweathermap.org",
    "api.open-meteo.com"
};

weatherClient::weatherClient(sharedBufferSpace *sharedBuffer) : js(sharedBuffer) {}

int weatherClient::updateWeather(const char *apiKey, float lat, float lon) {

    currentWeatherMessage[0] = '\0';

    JsonStreamingParserGS parser;
    parser.setListener(this);
    WiFiClientSecure httpsClient;
    httpsClient.setInsecure();
    httpsClient.setTimeout(8000);
    httpsClient.setConnectionTimeout(8000);
    httpsClient.setNoDelay(false);

    // Which client are we using
    weatherSource = (apiKey[0]?OPENWEATHERMAP:OPENMETEO);

    int retryCounter=0;
    while (!httpsClient.connect(apiHosts[weatherSource], 443) && (retryCounter++ < 15)) {
        delay(200);
    }
    if (retryCounter>=15) {
        return UPD_NO_RESPONSE;
    }

    String request;
    if (weatherSource == OPENWEATHERMAP) {
        request = "GET /data/2.5/weather?units=metric&lang=en&lat=" + String(lat) + "&lon=" + String(lon) + "&appid=" + String(apiKey) + " HTTP/1.0\r\nHost: " + String(apiHosts[weatherSource]) + "\r\nConnection: close\r\n\r\n";
    } else {
        request = "GET /v1/forecast?latitude=" + String(lat) + "&longitude=" + String(lon) + "&current=temperature_2m,weather_code,wind_speed_10m&past_days=0&forecast_days=0&wind_speed_unit=mph HTTP/1.0\r\nHost: " + String(apiHosts[weatherSource]) + "\r\nConnection: close\r\n\r\n";
    }
    httpsClient.print(request);
    retryCounter=0;
    while(!httpsClient.available() && retryCounter++ < 40) {
        delay(200);
    }

    if (!httpsClient.available()) {
        // no response within 8 seconds so exit
        httpsClient.stop();
        return UPD_TIMEOUT;
    }

    // Parse status code
    String statusLine = httpsClient.readStringUntil('\n');
    if (!statusLine.startsWith("HTTP/") || statusLine.indexOf("200 OK") == -1) {
        httpsClient.stop();

        if (statusLine.indexOf("401") > 0) {
            return UPD_UNAUTHORISED;
        } else if (statusLine.indexOf("500") > 0) {
            return UPD_DATA_ERROR;
        } else {
            return UPD_HTTP_ERROR;
        }
    }

    // Skip the remaining headers
    while (httpsClient.connected() || httpsClient.available()) {
        String line = httpsClient.readStringUntil('\n');
        if (line == "\r") break;
    }

    bool isBody = false;
    char c;
    weatherItem=0;
    temperature=0;
    windSpeed=0;
    weatherCode=-1;

    unsigned long dataSendTimeout = millis() + 10000UL;
    while((httpsClient.available() || httpsClient.connected()) && (millis() < dataSendTimeout)) {
        while(httpsClient.available()) {
            c = httpsClient.read();
            if (c == '{' || c == '[') isBody = true;
            if (isBody) parser.parse(c);
        }
        delay(5);
    }
    httpsClient.stop();
    if (millis() >= dataSendTimeout) {
        return UPD_TIMEOUT;
    }

    if (weatherSource == OPENWEATHERMAP) {
        if (currentWeatherMessage[0]) {
            sprintf(currentWeatherMessage + strlen(currentWeatherMessage)," %.0f\xB0 Wind: %.0fmph",temperature,windSpeed);
            currentWeatherMessage[0] = toUpperCase(currentWeatherMessage[0]);
        }
    } else {
        if (weatherCode >= 0) {
            const char* weatherDesc = getWeatherDescription(weatherCode);
            snprintf(currentWeatherMessage, MAXWEATHERSIZE, "%s %.0f\xB0 Wind: %.0fmph", weatherDesc, temperature, windSpeed);
        }
    }
    return UPD_SUCCESS;
}

const char* weatherClient::getWeatherDescription(int code) {
    switch (code) {
        case 0: return "Clear sky";
        case 1: return "Mainly clear";
        case 2: return "Partly cloudy";
        case 3: return "Overcast";
        case 45: return "Fog";
        case 48: return "Depositing rime fog";
        case 51: return "Light drizzle";
        case 53: return "Moderate drizzle";
        case 55: return "Dense drizzle";
        case 56: return "Light freezing drizzle";
        case 57: return "Dense freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Moderate rain";
        case 65: return "Heavy rain";
        case 66: return "Light freezing rain";
        case 67: return "Heavy freezing rain";
        case 71: return "Slight snow fall";
        case 73: return "Moderate snow fall";
        case 75: return "Heavy snow fall";
        case 77: return "Snow grains";
        case 80: return "Slight rain showers";
        case 81: return "Moderate rain showers";
        case 82: return "Violent rain showers";
        case 85: return "Slight snow showers";
        case 86: return "Heavy snow showers";
        case 95: return "Thunderstorm";
        case 96: return "Thunderstorm with slight hail";
        case 99: return "Thunderstorm with heavy hail";
        default: return "Weather";
    }
}

void weatherClient::whitespace(char c) {}

void weatherClient::startDocument() {
    js->currentPath[0] = '\0';
    js->arrayName[0] = '\0';
    js->objectCurrentKey[0] = '\0';
}

void weatherClient::key(const char *key) {
    strlcpy(js->currentKey,key,MAXKEYNAMESIZE);
}

void weatherClient::value(const char *value) {
    if (weatherSource == OPENWEATHERMAP) {
        if (strcmp(js->objectCurrentKey, "weather")==0 && weatherItem==0) {
            // Only read the first weather entry in the array
            if (strcmp(js->currentKey, "description")==0) strlcpy(currentWeatherMessage,value,32);
        }
        else if (strcmp(js->currentKey, "temp")==0) temperature = atof(value);
        // Windspeed reported in mps, converting to mph
        else if (strcmp(js->currentKey, "speed")==0) windSpeed = atof(value) * 2.23694;
    } else {
        if (strcmp(js->objectCurrentKey, "current")==0) {
            if (strcmp(js->currentKey, "temperature_2m")==0) temperature = atof(value);
            else if (strcmp(js->currentKey, "wind_speed_10m")==0) windSpeed = atof(value);
            else if (strcmp(js->currentKey, "weather_code")==0) weatherCode = atoi(value);
        }
    }
}

void weatherClient::endArray() {}

void weatherClient::endObject() {
    if (weatherSource == OPENWEATHERMAP && strcmp(js->objectCurrentKey, "weather")==0) weatherItem++;
    js->objectCurrentKey[0] = '\0';
}

void weatherClient::endDocument() {}

void weatherClient::startArray() {}

void weatherClient::startObject() {
    strcpy(js->objectCurrentKey,js->currentKey);
}
