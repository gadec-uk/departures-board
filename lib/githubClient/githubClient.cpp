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
#include <JsonListenerGS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <md5Utils.h>

github::github(sharedBufferSpace *sharedBuffer) : js(sharedBuffer) {}

int github::getLatestRelease() {

    js->lastResultMessage[0] = '\0';
    JsonStreamingParserGS parser;
    parser.setListener(this);
    WiFiClientSecure httpsClient;

    httpsClient.setInsecure();
    httpsClient.setTimeout(5000);
    httpsClient.setConnectionTimeout(5000);

    int retryCounter=0; //retry counter
    while((!httpsClient.connect(GITHUBAPIHOST, 443)) && (retryCounter < 10)) {
        delay(200);
        retryCounter++;
    }
    if(retryCounter>=10) {
        strcpy(js->lastResultMessage,"Error: GH Connect timed out");
        return UPD_NO_RESPONSE;
    }

    String request = "GET " GITHUBREPOPATH " HTTP/1.0\r\nHost: " GITHUBAPIHOST "\r\nuser-agent: esp32/1.0\r\nX-GitHub-Api-Version: 2022-11-28\r\nAccept: application/vnd.github+json\r\n";
    if (strlen(GITHUBTOKEN)) request += "Authorization: Bearer " GITHUBTOKEN "\r\nConnection: close\r\n\r\n";
    else request += "Connection: close\r\n\r\n";

    httpsClient.print(request);
    retryCounter=0;
    while(!httpsClient.available()) {
        delay(200);
        retryCounter++;
        if (retryCounter > 25) {
            // no response within 5 seconds so quit
            httpsClient.stop();
            strcpy(js->lastResultMessage,"Error: GH GET timed out");
            return UPD_TIMEOUT;
        }
    }

    while (httpsClient.connected()) {
        String line = httpsClient.readStringUntil('\n');
        // check for success code...
        if (line.startsWith("HTTP")) {
            if (line.indexOf("200 OK") == -1) {
                httpsClient.stop();
                strlcpy(js->lastResultMessage,line.c_str(),sizeof(js->lastResultMessage));
                if (line.indexOf("401") > 0) {
                    return UPD_UNAUTHORISED;
                } else if (line.indexOf("500") > 0) {
                    return UPD_DATA_ERROR;
                } else {
                    return UPD_HTTP_ERROR;
                }
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
    firmwareURL="";
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
        sprintf(js->lastResultMessage,"Error: GH Timeout after %d bytes",dataReceived);
        return UPD_TIMEOUT;
    }

    if (firmwareURL=="") {
        // Failed to find firmware.bin in the release assets
        strcpy(js->lastResultMessage,"No firmware.bin found in release assets");
        return UPD_INCOMPLETE;
    }
    sprintf(js->lastResultMessage+strlen(js->lastResultMessage),"[GH] OK: UP D:%d",dataReceived);
    return UPD_SUCCESS;
}

void github::whitespace(char c) {}

void github::startDocument() {
    js->currentPath[0] = '\0';
    js->arrayName[0] = '\0';
    js->objectCurrentKey[0] = '\0';
}

void github::key(const char *key) {
    strlcpy(js->currentKey,key,MAXKEYNAMESIZE);
}

void github::value(const char *value) {
    if (strcmp(js->currentKey, "tag_name")==0) releaseId = String(value);
    else if (strcmp(js->currentKey, "name")==0 && !js->arrayName[0]) releaseDescription = String(value);
    else if (strcmp(js->currentKey, "url")==0 && strcmp(js->arrayName, "assets")==0 && strcmp(js->objectCurrentKey, "uploader")) assetURL = String(value);
    else if (strcmp(js->currentKey, "name")==0 && strcmp(js->arrayName, "assets")==0 && strcmp(js->objectCurrentKey, "uploader")) assetName = String(value);

    if (assetURL.length() && assetName == "firmware.bin") {
        // found the firmware file, save the url
        firmwareURL = assetURL;
        assetURL="";
        assetName="";
    }
}

void github::endArray() {
    js->arrayName[0] = '\0';
}

void github::endObject() {
    js->objectCurrentKey[0] = '\0';
}

void github::endDocument() {}

void github::startArray() {
    strcpy(js->arrayName,js->currentKey);
}

void github::startObject() {
    strcpy(js->objectCurrentKey, js->currentKey);
}
