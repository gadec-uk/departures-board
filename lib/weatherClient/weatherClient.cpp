/*
 * Departures Board (c) 2025 Gadec Software
 *
 * OpenWeatherMap Weather Client Library
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */

#include <weatherClient.h>
#include <JsonListener.h>
#include <WiFiClient.h>

weatherClient::weatherClient() {}

bool weatherClient::updateWeather(String apiKey, String lat, String lon) {

    lastErrorMsg = "";

    JsonStreamingParser parser;
    parser.setListener(this);
    WiFiClient httpClient;

    int retryCounter=0;
    while (!httpClient.connect(apiHost, 80) && (retryCounter++ < 15)){
        delay(200);
    }
    if (retryCounter>=15) {
        lastErrorMsg += F("Connection timeout");
        return false;
    }

    String request = "GET /data/2.5/weather?units=metric&lang=en&lat=" + lat + F("&lon=") + lon + F("&appid=") + apiKey + F(" HTTP/1.0\r\nHost: ") + String(apiHost) + F("\r\nConnection: close\r\n\r\n");
    httpClient.print(request);
    retryCounter=0;
    while(!httpClient.available() && retryCounter++ < 40) {
        delay(200);
    }

    if (!httpClient.available()) {
        // no response within 8 seconds so exit
        httpClient.stop();
        lastErrorMsg += F("Response timeout");
        return false;
    }

    // Parse status code
    String statusLine = httpClient.readStringUntil('\n');
    if (!statusLine.startsWith(F("HTTP/")) || statusLine.indexOf(F("200 OK")) == -1) {
        httpClient.stop();

        if (statusLine.indexOf(F("401")) > 0) {
            lastErrorMsg = F("Not Authorized");
        } else if (statusLine.indexOf(F("500")) > 0) {
            lastErrorMsg = F("Server Error");
        } else {
            lastErrorMsg = statusLine;
        }
        return false;
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
        lastErrorMsg += F("Data timeout");
        return false;
    }

    lastErrorMsg="";

    currentWeather = description + " " + String((int)round(temperature)) + "\x80 Wind: " + String((int)round(windSpeed)) + "mph";
    return true;
}

void weatherClient::whitespace(char c) {}

void weatherClient::startDocument() {}

void weatherClient::key(String key) {
    currentKey = key;
}

void weatherClient::value(String value) {
    if (currentObject == "weather" && weatherItem==0) {
        // Only read the first weather entry in the array
        if (currentKey == "description") description = value;
    }
    else if (currentKey == "temp") temperature = value.toFloat();
    // Windspeed reported in mps, converting to mph
    else if (currentKey == "speed") windSpeed = value.toFloat() * 2.23694;
}

void weatherClient::endArray() {}

void weatherClient::endObject() {
    if (currentObject == "weather") weatherItem++;
    currentObject = "";
}

void weatherClient::endDocument() {}

void weatherClient::startArray() {}

void weatherClient::startObject() {
    currentObject = currentKey;
}
