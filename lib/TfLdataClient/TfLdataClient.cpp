/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * TfL London Underground Client Library
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */

#include <TfLdataClient.h>
#include <JsonListenerGS.h>
#include <WiFiClientSecure.h>
#include <logger.hpp>

TfLdataClient::TfLdataClient(busTubeStation *station, stnMessages *messages,  sharedBufferSpace *sharedBuffer) : xStation(station), xMessages(messages), js(sharedBuffer) {}

int TfLdataClient::fetchArrivals(rdStation *station, stnMessages *messages, const char *locationId, const char *lineId, const char *lineDirection, bool noMessages, const char *apiKey) {

    unsigned long perfTimer=millis();
    long dataReceived = 0;
    bool bChunked = false;
    js->lastResultMessage[0] = '\0';

    JsonStreamingParserGS parser;
    parser.setListener(this);
    WiFiClientSecure httpsClient;
    httpsClient.setInsecure();
    httpsClient.setTimeout(8000);
    httpsClient.setConnectionTimeout(8000);

    boardChanged = false;

    int retryCounter=0;
    while (!httpsClient.connect(apiHost,443) && (retryCounter++ < 15)){
        delay(200);
    }
    if (retryCounter>=15) {
        LOG_ERROR("DATA", "TfL API Connect Timeout");
        strcpy(js->lastResultMessage,"Error: Connect timed out");
        return UPD_NO_RESPONSE;
    }
    String request;
    if (strcmp(lineId,"all")) {
        request="GET /Line/" + String(lineId) + "/Arrivals/" + String(locationId);
        if (lineDirection[0]) request+="?direction=" + String(lineDirection) + "&app_key=" + String(apiKey) + " HTTP/1.0\r\nHost: " + String(apiHost) + "\r\nConnection: close\r\n\r\n";
        else request+="?app_key=" + String(apiKey) + " HTTP/1.0\r\nHost: " + String(apiHost) + "\r\nConnection: close\r\n\r\n";
    } else {
        request="GET /StopPoint/" + String(locationId) + "/Arrivals?app_key=" + apiKey + " HTTP/1.0\r\nHost: " + String(apiHost) + "\r\nConnection: close\r\n\r\n";
    }
    LOG_INFOf("DATA", "Fetching TfL Arrivals for %s...", locationId);
    LOG_DEBUGf("DATA", "Request: %s", request.c_str());
    httpsClient.print(request);
    retryCounter=0;
    while(!httpsClient.available() && retryCounter++ < 40) {
        delay(200);
    }

    if (!httpsClient.available()) {
        // no response within 8 seconds so exit
        httpsClient.stop();
        LOG_ERROR("DATA", "TfL API GET Timeout");
        strcpy(js->lastResultMessage,"Error: GET timed out");
        return UPD_TIMEOUT;
    }

    // Parse status code
    String statusLine = httpsClient.readStringUntil('\n');
    LOG_DEBUGf("DATA", "Response Status: %s", statusLine.c_str());
    if (!statusLine.startsWith("HTTP/") || statusLine.indexOf("200 OK") == -1) {
        httpsClient.stop();
        strlcpy(js->lastResultMessage,statusLine.c_str(),sizeof(js->lastResultMessage));
        if (statusLine.indexOf("401") > 0 || statusLine.indexOf("429") > 0) {
            LOG_WARNf("DATA", "TfL API Unauthorized/Rate Limited: %s", statusLine.c_str());
            return UPD_UNAUTHORISED;
        } else if (statusLine.indexOf("500") > 0) {
            LOG_ERRORf("DATA", "TfL API Data Error: %s", statusLine.c_str());
            return UPD_DATA_ERROR;
        } else {
            LOG_ERRORf("DATA", "TfL API HTTP Error: %s", statusLine.c_str());
            return UPD_HTTP_ERROR;
        }
    }
    LOG_INFO("DATA", "TfL API fetch successful (HTTP 200 OK)");

    // Skip the remaining headers
    while (httpsClient.connected() || httpsClient.available()) {
        statusLine = httpsClient.readStringUntil('\n');
        if (statusLine == "\r") break;
        if (statusLine.startsWith("Transfer-Encoding:") && statusLine.indexOf("chunked") >= 0) bChunked=true;
    }

    bool isBody = false;
    char c;
    id=0;
    maxServicesRead = false;
    fetchingArrivals = true;
    xStation->numServices = 0;
    xMessages->numMessages = 0;
    for (int i=0;i<MAXBOARDMESSAGES;i++) strcpy(xMessages->messages[i],"");
    for (int i=0;i<MAXTUBEBUSREADSERVICES;i++) strcpy(xStation->service[i].destinationName,"Check front of Train");

    unsigned long dataSendTimeout = millis() + 10000UL;
    
#if APP_DEBUG_LEVEL >= APP_LOG_LEVEL_DEBUG
    String debugPayload = "";
#endif

    while((httpsClient.available() || httpsClient.connected()) && (millis() < dataSendTimeout) && (!maxServicesRead)) {
        while(httpsClient.available() && !maxServicesRead) {
            c = httpsClient.read();
            dataReceived++;
            if (c == '{' || c == '[') isBody = true;
            if (isBody) {
#if APP_DEBUG_LEVEL >= APP_LOG_LEVEL_DEBUG
                debugPayload += c;
                if (debugPayload.length() >= 512) {
                    LOG_DEBUG("DATA", debugPayload.c_str());
                    debugPayload = "";
                }
#endif
                parser.parse(c);
            }
        }
        delay(5);
    }
#if APP_DEBUG_LEVEL >= APP_LOG_LEVEL_DEBUG
    if (debugPayload.length() > 0) {
        LOG_DEBUG("DATA", debugPayload.c_str());
    }
#endif
    httpsClient.stop();
    if (millis() >= dataSendTimeout) {
        LOG_ERRORf("DATA", "TfL API Receive Timeout after %d bytes", dataReceived);
        sprintf(js->lastResultMessage,"Error: Timeout after %d bytes",dataReceived);
        return UPD_TIMEOUT;
    }

    if (!noMessages) {
        // Update the distruption messages
        retryCounter=0;
        while (!httpsClient.connect(apiHost, 443) && (retryCounter++ < 15)){
            delay(200);
        }
        if (retryCounter>=15) {
            LOG_ERROR("DATA", "TfL API Disruption Connect Timeout");
            strcpy(js->lastResultMessage,"Error: Connect timed out [Msgs]");
            return UPD_NO_RESPONSE;
        }
        request = "GET /StopPoint/" + String(locationId) + "/Disruption?getFamily=true&flattenResponse=true&app_key=" + String(apiKey) + " HTTP/1.0\r\nHost: " + String(apiHost) + "\r\nConnection: close\r\n\r\n";
        LOG_INFOf("DATA", "Fetching TfL Disruption Messages for %s...", locationId);
        LOG_DEBUGf("DATA", "Request: %s", request.c_str());
        httpsClient.print(request);
        retryCounter=0;
        while(!httpsClient.available() && retryCounter++ < 40) {
            delay(200);
        }

        if (!httpsClient.available()) {
            // no response within 8 seconds so exit
            httpsClient.stop();
            LOG_ERROR("DATA", "TfL API Disruption GET Timeout");
            strcpy(js->lastResultMessage,"Error: GET timed out [Msgs]");
            return UPD_TIMEOUT;
        }

        // Parse status code
        statusLine = httpsClient.readStringUntil('\n');
        LOG_DEBUGf("DATA", "Response Status: %s", statusLine.c_str());
        if (!statusLine.startsWith("HTTP/") || statusLine.indexOf("200 OK") == -1) {
            httpsClient.stop();
            strlcpy(js->lastResultMessage,statusLine.c_str(),sizeof(js->lastResultMessage));
            if (statusLine.indexOf("401") > 0) {
                LOG_WARNf("DATA", "TfL API Unauthorized: %s", statusLine.c_str());
                return UPD_UNAUTHORISED;
            } else if (statusLine.indexOf("500") > 0) {
                LOG_ERRORf("DATA", "TfL API Data Error: %s", statusLine.c_str());
                return UPD_DATA_ERROR;
            } else {
                LOG_ERRORf("DATA", "TfL API HTTP Error: %s", statusLine.c_str());
                return UPD_HTTP_ERROR;
            }
        }
        LOG_INFO("DATA", "TfL API Disruption fetch successful (HTTP 200 OK)");

        // Skip the remaining headers
        while (httpsClient.connected() || httpsClient.available()) {
            statusLine = httpsClient.readStringUntil('\n');
            if (statusLine == "\r") break;
            if (statusLine.startsWith("Transfer-Encoding:") && statusLine.indexOf("chunked") >= 0) bChunked=true;
        }

        isBody = false;
        id=0;
        maxServicesRead = false;
        fetchingArrivals = false;
        parser.reset();

        dataSendTimeout = millis() + 10000UL;
        
#if APP_DEBUG_LEVEL >= APP_LOG_LEVEL_DEBUG
        String debugPayload = "";
#endif

        while((httpsClient.available() || httpsClient.connected()) && (millis() < dataSendTimeout) && (!maxServicesRead)) {
            while(httpsClient.available() && !maxServicesRead) {
                c = httpsClient.read();
                dataReceived++;
                if (c == '{' || c == '[') isBody = true;
                if (isBody) {
#if APP_DEBUG_LEVEL >= APP_LOG_LEVEL_DEBUG
                    debugPayload += c;
                    if (debugPayload.length() >= 512) {
                        LOG_DEBUG("DATA", debugPayload.c_str());
                        debugPayload = "";
                    }
#endif
                    parser.parse(c);
                }
            }
            delay(5);
        }
#if APP_DEBUG_LEVEL >= APP_LOG_LEVEL_DEBUG
        if (debugPayload.length() > 0) {
            LOG_DEBUG("DATA", debugPayload.c_str());
        }
#endif
        httpsClient.stop();
        if (millis() >= dataSendTimeout) {
            LOG_ERRORf("DATA", "TfL API Disruption Receive Timeout after %d bytes", dataReceived);
            sprintf(js->lastResultMessage,"Error: Timeout after %d bytes [Msgs]",dataReceived);
            return UPD_TIMEOUT;
        }
    }

    // Sort the services by arrival time
    size_t arraySize = xStation->numServices;
    std::sort(xStation->service, xStation->service+arraySize,compareTimes);

    // Limit results to the nearest MAXBOARDSERVICES services
    if (xStation->numServices > MAXBOARDSERVICES) xStation->numServices = MAXBOARDSERVICES;

    // Add the attribution message
    if (xMessages->numMessages < MAXBOARDMESSAGES) strcpy(xMessages->messages[xMessages->numMessages++],tflAttribution);

    // Clean up the destinations
    for (int i=0;i<xStation->numServices;++i) {
        pruneFromPhrase(xStation->service[i].destinationName," Underground Station");
        pruneFromPhrase(xStation->service[i].destinationName," DLR Station");
        pruneFromPhrase(xStation->service[i].destinationName," (H&C Line)");
        pruneFromPhrase(xStation->service[i].currentLocation," Platform ");
    }

    // Check if any of the services have changed
    if (xStation->numServices != station->numServices) boardChanged=true;
    else if (xStation->numServices && strcmp(xStation->service[0].destinationName,station->service[0].destination)) boardChanged = true;
    if (!noMessages) {
        if (messages->numMessages != xMessages->numMessages) boardChanged = true;
    }

    // Remove line break and excess spaces from messages
    for (int i=0;i<xMessages->numMessages;i++) {
        replaceWord(xMessages->messages[i],"\\n"," ");
        removeExcessSpaces(xMessages->messages[i]);
        if (i<xMessages->numMessages-1) fixFullStop(xMessages->messages[i]);
    }

    UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    if (boardChanged) {
        sprintf(js->lastResultMessage+strlen(js->lastResultMessage),"OK: UP D:%d T:%d S:%d %s",dataReceived,millis()-perfTimer,uxHighWaterMark,bChunked?"C!":"");
        return UPD_SUCCESS;
    } else {
        sprintf(js->lastResultMessage+strlen(js->lastResultMessage),"OK: NC D:%d T:%d S:%d %s",dataReceived,millis()-perfTimer,uxHighWaterMark,bChunked?"C!":"");
        return UPD_NO_CHANGE;
    }
}

void TfLdataClient::loadArrivals(rdStation *station, stnMessages *messages) {
    // Update the callers data with the new data
    station->boardChanged = boardChanged;
    station->numServices = xStation->numServices;
    for (int i=0;i<xStation->numServices;i++) {
        strcpy(station->service[i].destination,xStation->service[i].destinationName);
        strcpy(station->service[i].via,xStation->service[i].lineName);
        station->service[i].timeToStation = xStation->service[i].timeToStation;
    }
    strcpy(station->origin,xStation->service[0].currentLocation);
    messages->numMessages = xMessages->numMessages;
    for (int i=0;i<xMessages->numMessages;i++) strcpy(messages->messages[i],xMessages->messages[i]);
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

//
// Function to remove excess spaces in a character array
//
void TfLdataClient::removeExcessSpaces(char *input) {
    if (input == nullptr || input[0] == '\0') return;

    int i = 0;
    int j = 0;
    bool lastWasSpace = false;

    while (input[i] != '\0') {
        if (input[i] != ' ') {
            input[j++] = input[i];
            lastWasSpace = false;
        } else {
            if (!lastWasSpace) {
                input[j++] = ' ';
                lastWasSpace = true;
            }
        }
        i++;
    }

    input[j] = '\0';
}

//
// Function to ensure there's one and only one fullstop at the end of messages.
//
void TfLdataClient::fixFullStop(char *input) {
    if (input[0]) {
        while (input[0] && (input[strlen(input)-1] == '.' || input[strlen(input)-1] == ' ')) input[strlen(input)-1] = '\0'; // Remove all trailing full stops
        if (strlen(input) < MAXMESSAGESIZE-1) strcat(input,".");  // Add a single fullstop
    }
}

// Custom comparator function to compare time to station
bool TfLdataClient::compareTimes(const busTubeService& a, const busTubeService& b) {
    return a.timeToStation < b.timeToStation;
}

void TfLdataClient::whitespace(char c) {}

void TfLdataClient::startDocument() {
    js->currentPath[0] = '\0';
    js->arrayName[0] = '\0';
    js->objectCurrentKey[0] = '\0';
}

void TfLdataClient::key(const char *key) {
    strlcpy(js->currentKey,key,MAXKEYNAMESIZE);
    if (strcmp(js->currentKey, "id")==0 && fetchingArrivals) {
        // Next entry
        if (xStation->numServices<MAXTUBEBUSREADSERVICES) {
            xStation->numServices++;
            id = xStation->numServices-1;
        } else {
            // We've read all we need to
            maxServicesRead = true;
        }
    } else if (strcmp(js->currentKey, "description")==0 && !fetchingArrivals) {
        // Next service message
        if (xMessages->numMessages<MAXBOARDMESSAGES) {
            id = xMessages->numMessages;
            xMessages->numMessages++;
        } else {
            // We've read all we need to
            maxServicesRead = true;
        }
    }
}

void TfLdataClient::value(const char *value) {
    if (fetchingArrivals) {
        if (strcmp(js->currentKey, "destinationName")==0) strlcpy(xStation->service[id].destinationName,value,MAXBUSTUBELOCATIONSIZE);
        else if (strcmp(js->currentKey, "currentLocation")==0) strlcpy(xStation->service[id].currentLocation,value,MAXBUSTUBELOCATIONSIZE);
        else if (strcmp(js->currentKey, "timeToStation")==0) xStation->service[id].timeToStation = atoi(value);
        else if (strcmp(js->currentKey, "lineName")==0) {
            strlcpy(xStation->service[id].lineName,value,MAXLINESIZE);
        }
    } else {
        // Fetching messages
        if (strcmp(js->currentKey, "description")==0) {
            // Disruption message, check for duplicates before adding
            for (int i=0;i<id;i++) {
                if (strcmp(xMessages->messages[i],value)==0) {
                    // Duplicate, don't add it
                    xMessages->numMessages--;
                    return;
                }
            }
            strlcpy(xMessages->messages[id],value,MAXMESSAGESIZE);
        }
    }
}

void TfLdataClient::endArray() {}

void TfLdataClient::endObject() {}

void TfLdataClient::endDocument() {}

void TfLdataClient::startArray() {}

void TfLdataClient::startObject() {}
