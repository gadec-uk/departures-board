/*
 * Departures Board (c) 2025 Gadec Software
 *
 * TfL London Underground Client Library
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */

#include <TfLdataClient.h>
#include <JsonListener.h>
#include <WiFiClientSecure.h>
#include <stationData.h>

TfLdataClient::TfLdataClient() {}

int TfLdataClient::updateArrivals(rdStation *station, stnMessages *messages, const char *locationId, String apiKey, tflClientCallback Xcb) {

    unsigned long perfTimer=millis();
    long dataReceived = 0;
    bool bChunked = false;
    lastErrorMsg = "";

    JsonStreamingParser parser;
    parser.setListener(this);
    WiFiClientSecure httpsClient;
    httpsClient.setInsecure();
    httpsClient.setTimeout(15000);

    station->boardChanged=false;

    int retryCounter=0;
    while (!httpsClient.connect(apiHost,443) && (retryCounter++ < 15)){
        delay(200);
    }
    if (retryCounter>=15) {
        lastErrorMsg = F("Connection timeout");
        return UPD_NO_RESPONSE;
    }
    String request = "GET /StopPoint/" + String(locationId) + F("/Arrivals?app_key=") + apiKey + F(" HTTP/1.0\r\nHost: ") + String(apiHost) + F("\r\nConnection: close\r\n\r\n");
    httpsClient.print(request);
    Xcb();
    unsigned long ticker = millis()+800;
    retryCounter=0;
    while(!httpsClient.available() && retryCounter++ < 40) {
        delay(200);
    }

    if (!httpsClient.available()) {
        // no response within 8 seconds so exit
        httpsClient.stop();
        lastErrorMsg = F("Response timeout");
        return UPD_TIMEOUT;
    }

    // Parse status code
    String statusLine = httpsClient.readStringUntil('\n');
    if (!statusLine.startsWith(F("HTTP/")) || statusLine.indexOf(F("200 OK")) == -1) {
        httpsClient.stop();

        if (statusLine.indexOf(F("401")) > 0 || statusLine.indexOf(F("429")) > 0) {
            lastErrorMsg = F("Not Authorized");
            return UPD_UNAUTHORISED;
        } else if (statusLine.indexOf(F("500")) > 0) {
            lastErrorMsg = statusLine;
            return UPD_DATA_ERROR;
        } else {
            lastErrorMsg = statusLine;
            return UPD_HTTP_ERROR;
        }
    }

    // Skip the remaining headers
    while (httpsClient.connected() || httpsClient.available()) {
        String line = httpsClient.readStringUntil('\n');
        if (line == F("\r")) break;
        if (line.startsWith(F("Transfer-Encoding:")) && line.indexOf(F("chunked")) >= 0) bChunked=true;
    }

    bool isBody = false;
    char c;
    id=0;
    maxServicesRead = false;
    xStation.numServices = 0;
    xMessages.numMessages = 0;
    for (int i=0;i<MAXBOARDMESSAGES;i++) strcpy(xMessages.messages[i],"");
    for (int i=0;i<MAXBOARDSERVICES;i++) strcpy(xStation.service[i].destinationName,"Check front of train");

    unsigned long dataSendTimeout = millis() + 10000UL;
    while((httpsClient.available() || httpsClient.connected()) && (millis() < dataSendTimeout) && (!maxServicesRead)) {
        while(httpsClient.available() && !maxServicesRead) {
            c = httpsClient.read();
            dataReceived++;
            if (c == '{' || c == '[') isBody = true;
            if (isBody) parser.parse(c);
            if (millis()>ticker) {
                Xcb();
                ticker = millis()+800;
            }
        }
        delay(25);
    }
    httpsClient.stop();
    if (millis() >= dataSendTimeout) {
        lastErrorMsg = F("Timed out during data receive operation - ");
        lastErrorMsg += String(dataReceived) + F(" bytes received");
        return UPD_TIMEOUT;
    }

    // Update the distruption messages
    while (!httpsClient.connect(apiHost, 443) && (retryCounter++ < 15)){
        delay(200);
    }
    if (retryCounter>=15) {
        lastErrorMsg = F("Connection timeout [msgs]");
        return UPD_NO_RESPONSE;
    }
    request = "GET /StopPoint/" + String(locationId) + F("/Disruption?getFamily=true&flattenResponse=true&app_key=") + apiKey + F(" HTTP/1.0\r\nHost: ") + String(apiHost) + F("\r\nConnection: close\r\n\r\n");
    httpsClient.print(request);
    retryCounter=0;
    while(!httpsClient.available() && retryCounter++ < 40) {
        delay(200);
    }

    if (!httpsClient.available()) {
        // no response within 8 seconds so exit
        httpsClient.stop();
        lastErrorMsg = F("Response timeout [msgs]");
        return UPD_TIMEOUT;
    }

    // Parse status code
    statusLine = httpsClient.readStringUntil('\n');
    if (!statusLine.startsWith(F("HTTP/")) || statusLine.indexOf(F("200 OK")) == -1) {
        httpsClient.stop();

        if (statusLine.indexOf(F("401")) > 0) {
            lastErrorMsg = F("Not Authorized [msgs]");
            return UPD_UNAUTHORISED;
        } else if (statusLine.indexOf(F("500")) > 0) {
            lastErrorMsg = F("Server Error [msgs]");
            return UPD_DATA_ERROR;
        } else {
            lastErrorMsg = statusLine;
            return UPD_HTTP_ERROR;
        }
    }

    // Skip the remaining headers
    while (httpsClient.connected() || httpsClient.available()) {
        String line = httpsClient.readStringUntil('\n');
        if (line == F("\r")) break;
        if (line.startsWith(F("Transfer-Encoding:")) && line.indexOf(F("chunked")) >= 0) bChunked=true;
    }

    isBody = false;
    id=0;
    maxServicesRead = false;
    parser.reset();

    dataSendTimeout = millis() + 10000UL;
    while((httpsClient.available() || httpsClient.connected()) && (millis() < dataSendTimeout) && (!maxServicesRead)) {
        while(httpsClient.available() && !maxServicesRead) {
            c = httpsClient.read();
            dataReceived++;
            if (c == '{' || c == '[') isBody = true;
            if (isBody) parser.parse(c);
            if (millis()>ticker) {
                Xcb();
                ticker = millis()+800;
            }
        }
        delay(25);
    }
    httpsClient.stop();
    if (millis() >= dataSendTimeout) {
        lastErrorMsg = F("Timed out during msgs data receive operation - ");
        lastErrorMsg += String(dataReceived) + F(" bytes received");
        return UPD_TIMEOUT;
    }

    // Sort the services by arrival time
    size_t arraySize = xStation.numServices;
    std::sort(xStation.service, xStation.service+arraySize,compareTimes);

    // Limit results to the nearest UGMAXSERVICES services
    if (xStation.numServices > MAXBOARDSERVICES) xStation.numServices = MAXBOARDSERVICES;

    // Check if any of the services have changed
    if (xStation.numServices != station->numServices) station->boardChanged=true;
    else {
        for (int i=0;i<xStation.numServices;i++) {
            if (i>1) break; // Only check first two services
            if (strcmp(xStation.service[i].destinationName,station->service[i].destination)) {
                station->boardChanged=true;
                break;
            }
        }
    }

    // Remove line break formatting from messages
    for (int i=0;i<xMessages.numMessages;i++) {
        replaceWord(xMessages.messages[i],"\\n","");
    }

    // Update the callers data with the new data
    station->numServices = xStation.numServices;
    for (int i=0;i<xStation.numServices;i++) {
        strcpy(station->service[i].destination,xStation.service[i].destinationName);
        strcpy(station->service[i].via,xStation.service[i].lineName);
        station->service[i].timeToStation = xStation.service[i].timeToStation;
    }
    messages->numMessages = xMessages.numMessages;
    for (int i=0;i<xMessages.numMessages;i++) strcpy(messages->messages[i],xMessages.messages[i]);

    if (bChunked) lastErrorMsg = F("WARNING: Chunked response! ");
    if (station->boardChanged) {
        lastErrorMsg += F("SUCCESS [Primary Service Changed] Update took: ");
        lastErrorMsg += String(millis() - perfTimer) + F("ms [") + String(dataReceived) + F("]");
        return UPD_SUCCESS;
    } else {
        lastErrorMsg += F("SUCCESS Update took: ");
        lastErrorMsg += String(millis() - perfTimer) + F("ms [") + String(dataReceived) + F("]");
        return UPD_NO_CHANGE;
    }
}

//
// Function to prune messages from the point at which a word or phrase is found
//
bool TfLdataClient::pruneFromPhrase(char* input, const char* target) {
    // Find the first occurance of the target word or phrase
    char* pos = strstr(input,target);
    // If found, prune from here
    if (pos) {
        input[pos - input] = '\0';
        return true;
    }
    return false;
}

//
// Function to replace occurrences of a word or phrase in a character array
//
void TfLdataClient::replaceWord(char* input, const char* target, const char* replacement) {
    // Find the first occurrence of the target word
    char* pos = strstr(input, target);
    while (pos) {
        // Calculate the length of the target word
        size_t targetLen = strlen(target);
        // Calculate the length difference between target and replacement
        int diff = strlen(replacement) - targetLen;

        // Shift the remaining characters to accommodate the replacement
        memmove(pos + strlen(replacement), pos + targetLen, strlen(pos + targetLen) + 1);

        // Copy the replacement word into the position
        memcpy(pos, replacement, strlen(replacement));

        // Find the next occurrence of the target word
        pos = strstr(pos + strlen(replacement), target);
    }
}

// Custom comparator function to compare time to station
bool TfLdataClient::compareTimes(const ugService& a, const ugService& b) {
    return a.timeToStation < b.timeToStation;
}

void TfLdataClient::whitespace(char c) {}

void TfLdataClient::startDocument() {}

void TfLdataClient::key(String key) {
    currentKey = key;
    if (currentKey == F("id")) {
        // Next entry
        if (xStation.numServices<UGMAXREADSERVICES) {
            xStation.numServices++;
            id = xStation.numServices-1;
        } else {
            // We've read all we need to
            maxServicesRead = true;
        }
    } else if (currentKey == F("description")) {
        // Next service message
        if (xMessages.numMessages<MAXBOARDMESSAGES) {
            xMessages.numMessages++;
            id = xMessages.numMessages-1;
        } else {
            // We've read all we need to
            maxServicesRead = true;
        }
    }
}

void TfLdataClient::value(String value) {
    if (currentKey == F("destinationName")) {
        String cleanLocation;
        if (value.endsWith(F(" Underground Station"))) {
            strncpy(xStation.service[id].destinationName,value.substring(0,value.length()-20).c_str(),MAXLOCATIONSIZE-1);
        } else if (value.endsWith(F(" DLR Station"))) {
            strncpy(xStation.service[id].destinationName,value.substring(0,value.length()-12).c_str(),MAXLOCATIONSIZE-1);
        } else if (value.endsWith(F(" (H&C Line)"))) {
            strncpy(xStation.service[id].destinationName,value.substring(0,value.length()-11).c_str(),MAXLOCATIONSIZE-1);
        } else {
            strncpy(xStation.service[id].destinationName,value.c_str(),MAXLOCATIONSIZE-1);
        }
        xStation.service[id].destinationName[MAXLOCATIONSIZE-1] = '\0';
    } else if (currentKey == F("timeToStation")) xStation.service[id].timeToStation = value.toInt();
    else if (currentKey == F("lineName")) {
        strncpy(xStation.service[id].lineName,value.c_str(),MAXLINESIZE-1);
        xStation.service[id].lineName[MAXLINESIZE-1] = '\0';
    } else if (currentKey == F("description")) {
        // Disruption message
        strncpy(xMessages.messages[id],value.c_str(),MAXMESSAGESIZE-1);
        xMessages.messages[id][MAXMESSAGESIZE-1] = '\0';
    }
}

void TfLdataClient::endArray() {}

void TfLdataClient::endObject() {}

void TfLdataClient::endDocument() {}

void TfLdataClient::startArray() {}

void TfLdataClient::startObject() {}
