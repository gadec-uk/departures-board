/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * GitHub Client Library - enables checking for latest release and downloading assets to file system
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */

#include <githubClient.h>
#include <JsonListener.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <md5Utils.h>

github::github(String repository, String token) {
    if (repository!="") apiGetLatestRelease = "/repos/" + repository + "/releases/latest";
    accessToken = token; // Initialise with a GitHub token if the repository is private
}

bool github::getLatestRelease() {

    lastErrorMsg = "";
    JsonStreamingParser parser;
    parser.setListener(this);
    WiFiClientSecure httpsClient;

    httpsClient.setInsecure();
    httpsClient.setTimeout(5000);
    httpsClient.setConnectionTimeout(5000);

    int retryCounter=0; //retry counter
    while((!httpsClient.connect(apiHost, 443)) && (retryCounter < 10)){
        delay(200);
        retryCounter++;
    }
    if(retryCounter>=10) {
        lastErrorMsg += F("Connection timeout");
        return false;
    }

    String request = "GET " + apiGetLatestRelease + F(" HTTP/1.0\r\nHost: ") + String(apiHost) + F("\r\nuser-agent: esp32/1.0\r\nX-GitHub-Api-Version: 2022-11-28\r\nAccept: application/vnd.github+json\r\n");
    if (accessToken.length()) request += "Authorization: Bearer " + String(accessToken) + F("\r\n");
    request += F("Connection: close\r\n\r\n");

    httpsClient.print(request);
    retryCounter=0;
    while(!httpsClient.available()) {
        delay(200);
        retryCounter++;
        if (retryCounter > 25) {
            // no response within 5 seconds so quit
            httpsClient.stop();
            lastErrorMsg += F("Response timeout");
            return false;
        }
    }

    while (httpsClient.connected()) {
        String line = httpsClient.readStringUntil('\n');
        // check for success code...
        if (line.startsWith("HTTP")) {
            if (line.indexOf("200 OK") == -1) {
            httpsClient.stop();
            lastErrorMsg += line;
            return false;
            }
        }
        if (line == "\r") {
            // Headers received
            break;
        }
    }

    bool isBody = false;
    char c;
    releaseId="";
    releaseDescription="";
    releaseAssets=0;
    unsigned long dataReceived = 0;

    unsigned long dataSendTimeout = millis() + 12000UL;
    while((httpsClient.available() || httpsClient.connected()) && (millis() < dataSendTimeout)) {
        while(httpsClient.available()) {
            c = httpsClient.read();
            dataReceived++;
            if (c == '{' || c == '[') isBody = true;
            if (isBody) parser.parse(c);
        }
        delay(5);
    }
    httpsClient.stop();
    if (millis() >= dataSendTimeout) {
        lastErrorMsg += "Data timeout (" + String(dataReceived) + F(" bytes)");
        return false;
    }

    lastErrorMsg=F("SUCCESS");

    return true;
}

String github::getLastError() {
    return lastErrorMsg;
}

void github::whitespace(char c) {}

void github::startDocument() {
    currentArray = "";
    currentObject = "";
}

void github::key(String key) {
    currentKey = key;
}

void github::value(String value) {
    if (currentKey == "tag_name") releaseId = value;
    else if ((currentKey == "name") && (currentArray=="")) releaseDescription = value;
    else if ((currentKey == "url") && (currentArray=="assets") && (currentObject!="uploader")) assetURL = value;
    else if ((currentKey == "name") && (currentArray=="assets") && (currentObject!="uploader")) assetName = value;

    if (assetURL.length() && assetName.length() && releaseAssets<MAX_RELEASE_ASSETS) {
        // Save the full asset url to the list
        releaseAssetURL[releaseAssets] = assetURL;
        releaseAssetName[releaseAssets++] = assetName;
        assetURL="";
        assetName="";
    }
}

void github::endArray() {
    currentArray = "";
}

void github::endObject() {
    currentObject = "";
}

void github::endDocument() {}

void github::startArray() {
    currentArray = currentKey;
}

void github::startObject() {
    currentObject = currentKey;
}
