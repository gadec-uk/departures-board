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

#include <weatherClient.h>
#include <JsonListenerGS.h>
#include <WiFiClient.h>

weatherClient::weatherClient(sharedBufferSpace *sharedBuffer) : js(sharedBuffer) {}

int weatherClient::updateWeather(String apiKey, String lat, String lon) {

    unsigned long perfTimer = millis();
    currentWeatherMessage[0] = '\0';

    JsonStreamingParserGS parser;
    parser.setListener(this);
    WiFiClient httpClient;

    int retryCounter=0;
    while (!httpClient.connect(apiHost, 80) && (retryCounter++ < 15)){
        delay(200);
    }
    if (retryCounter>=15) {
        return UPD_NO_RESPONSE;
    }

    String request = "GET /data/2.5/weather?units=metric&lang=en&lat=" + lat + "&lon=" + lon + "&appid=" + apiKey + " HTTP/1.0\r\nHost: " + String(apiHost) + "\r\nConnection: close\r\n\r\n";
    httpClient.print(request);
    retryCounter=0;
    while(!httpClient.available() && retryCounter++ < 40) {
        delay(200);
    }

    if (!httpClient.available()) {
        // no response within 8 seconds so exit
        httpClient.stop();
        return UPD_TIMEOUT;
    }

    // Parse status code
    String statusLine = httpClient.readStringUntil('\n');
    if (!statusLine.startsWith("HTTP/") || statusLine.indexOf("200 OK") == -1) {
        httpClient.stop();

        if (statusLine.indexOf("401") > 0) {
            return UPD_UNAUTHORISED;
        } else if (statusLine.indexOf("500") > 0) {
            return UPD_DATA_ERROR;
        } else {
            return UPD_HTTP_ERROR;
        }
    }

    // Skip the remaining headers
    while (httpClient.connected() || httpClient.available()) {
        String line = httpClient.readStringUntil('\n');
        if (line == "\r") break;
    }

    bool isBody = false;
    char c;
    weatherItem=0;

    unsigned long dataSendTimeout = millis() + 10000UL;
    while((httpClient.available() || httpClient.connected()) && (millis() < dataSendTimeout)) {
        while(httpClient.available()) {
            c = httpClient.read();
            if (c == '{' || c == '[') isBody = true;
            if (isBody) parser.parse(c);
        }
        delay(5);
    }
    httpClient.stop();
    if (millis() >= dataSendTimeout) {
        return UPD_TIMEOUT;
    }

    if (currentWeatherMessage[0]) {
        sprintf(currentWeatherMessage + strlen(currentWeatherMessage)," %d\xB0 Wind: %dmph",(int)round(temperature),(int)round(windSpeed));
        currentWeatherMessage[0] = toUpperCase(currentWeatherMessage[0]);
    }
    return UPD_SUCCESS;
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
    if (strcmp(js->objectCurrentKey, "weather")==0 && weatherItem==0) {
        // Only read the first weather entry in the array
        if (strcmp(js->currentKey, "description")==0) strlcpy(currentWeatherMessage,value,32);
    }
    else if (strcmp(js->currentKey, "temp")==0) temperature = atof(value);
    // Windspeed reported in mps, converting to mph
    else if (strcmp(js->currentKey, "speed")==0) windSpeed = atof(value) * 2.23694;
}

void weatherClient::endArray() {}

void weatherClient::endObject() {
    if (strcmp(js->objectCurrentKey, "weather")==0) weatherItem++;
    js->objectCurrentKey[0] = '\0';
}

void weatherClient::endDocument() {}

void weatherClient::startArray() {}

void weatherClient::startObject() {
    strcpy(js->objectCurrentKey,js->currentKey);
}
