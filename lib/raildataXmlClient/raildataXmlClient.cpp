/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * raildataXmlClient Library
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 */

#include <raildataXmlClient.h>
#include <xmlListener.h>
#include <WiFiClientSecure.h>
#include <stationData.h>

raildataXmlClient::raildataXmlClient() {
    firstDataLoad=true;
}

// Custom comparator function to compare time strings
bool raildataXmlClient::compareTimes(const rdiService& a, const rdiService& b) {
    // Convert time strings to integers for comparison
    int hour1, minute1, hour2, minute2;
    sscanf(a.sTime, "%d:%d", &hour1, &minute1);
    sscanf(b.sTime, "%d:%d", &hour2, &minute2);

    // Compare hours first
    if (hour1 != hour2) {
        // Fudge for rollover at midnight
        if (hour1 < 2 && hour2 > 20) return false;
        if (hour2 < 2 && hour1 > 20) return true;
        else return hour1 < hour2;
    }
    // If hours are equal, compare minutes
    return minute1 < minute2;
}

//
// This function obtains the SOAP host and api url from the given wsdlHost and wsdlAPI
//
int raildataXmlClient::init(const char *wsdlHost, const char *wsdlAPI, rdCallback RDcb)
{
    Xcb = RDcb;

    WiFiClientSecure httpsClient;
    httpsClient.setInsecure();
    httpsClient.setTimeout(5000);
    httpsClient.setConnectionTimeout(3000);

    int retryCounter=0; //retry counter
    while((!httpsClient.connect(wsdlHost, 443)) && (retryCounter < 10)){
        delay(100);
        retryCounter++;
    }
    if(retryCounter>=10) {
      return UPD_NO_RESPONSE;   // No response within 3s
    }

    httpsClient.print("GET " + String(wsdlAPI) + F(" HTTP/1.0\r\n") +
      F("Host: ") + String(wsdlHost) + F("\r\n") +
      F("Connection: close\r\n\r\n"));

    retryCounter = 0;
    while(!httpsClient.available()) {
        delay(100);
        retryCounter++;
        if (retryCounter > 100) {
            return UPD_TIMEOUT;     // Timeout after 10s
        }
    }

    while (httpsClient.connected() || httpsClient.available()) {
      String line = httpsClient.readStringUntil('\n');
      // check for success code...
      if (line.startsWith(F("HTTP"))) {
        if (line.indexOf(F("200 OK")) == -1) {
          httpsClient.stop();
          if (line.indexOf(F("401")) > 0) {
            return UPD_UNAUTHORISED;
          } else if (line.indexOf(F("500")) > 0) {
            return UPD_DATA_ERROR;
          } else {
            return UPD_HTTP_ERROR;
          }
        }
      }
      if (line == F("\r")) {
        // Headers received
        break;
      }
    }

    char c;
    unsigned long dataSendTimeout = millis() + 8000UL;
    loadingWDSL = true;
    xmlStreamingParser parser;
    parser.setListener(this);
    parser.reset();
    grandParentTagName = "";
    parentTagName = "";
    tagName = "";
    tagLevel = 0;

    while((httpsClient.available() || httpsClient.connected()) && (millis() < dataSendTimeout)) {
      while (httpsClient.available()) {
        c = httpsClient.read();
        parser.parse(c);
      }
    }

    httpsClient.stop();
    loadingWDSL = false;

    if (soapURL.startsWith(F("https://"))) {
      int delim = soapURL.indexOf(F("/"),8);
      if (delim>0) {
        soapURL.substring(8,delim).toCharArray(soapHost,sizeof(soapHost));
        soapURL.substring(delim).toCharArray(soapAPI,sizeof(soapAPI));
        return UPD_SUCCESS;
      }
    }
    return UPD_DATA_ERROR;
}

//
// Function to remove HTML tags from a character array
//
void raildataXmlClient::removeHtmlTags(char* input) {
    bool inTag = false;
    char* output = input; // Output pointer

    for (char* p = input; *p; ++p) {
        if (*p == '<') {
            inTag = true;
        } else if (*p == '>') {
            inTag = false;
        } else if (!inTag) {
            *output++ = *p; // Copy non-tag characters
        }
    }

    *output = '\0'; // Null-terminate the output
}

//
// Function to replace occurrences of a word or phrase in a character array
//
void raildataXmlClient::replaceWord(char* input, const char* target, const char* replacement) {
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
// Function to prune messages from the point at which a word or phrase is found
//
void raildataXmlClient::pruneFromPhrase(char* input, const char* target) {
    // Find the first occurance of the target word or phrase
    char* pos = strstr(input,target);
    // If found, prune from here
    if (pos) input[pos - input] = '\0';
}

//
// Function to ensure there's one and only one fullstop at the end of messages.
//
void raildataXmlClient::fixFullStop(char *input) {
    if (input[0]) {
        while (input[0] && (input[strlen(input)-1] == '.' || input[strlen(input)-1] == ' ')) input[strlen(input)-1] = '\0'; // Remove all trailing full stops
        if (strlen(input) < MAXMESSAGESIZE-1) strcat(input,".");  // Add a single fullstop
    }
}

// Trim leading and trailing spaces in-place
void raildataXmlClient::trim(char* &start, char* &end) {
  while (start <= end && isspace(*start)) start++;
  while (end >= start && isspace(*end)) end--;
}

// Compare strings case-insensitively
bool raildataXmlClient::equalsIgnoreCase(const char* a, int a_len, const char* b) {
  for (int i = 0; i < a_len; i++) {
    if (tolower(a[i]) != tolower(b[i])) return false;
  }
  return b[a_len] == '\0';
}

// Check if the service is in the filter list (if there is one)
bool raildataXmlClient::serviceMatchesFilter(const char* filter, const char* serviceId) {
  if (filter == nullptr || filter[0] == '\0') return true; // empty filter = match all

  const char* start = filter;
  const char* ptr = filter;

  while (true) {
    if (*ptr == ',' || *ptr == '\0') {
      const char* end = ptr - 1;
      char* trimStart = const_cast<char*>(start);
      char* trimEnd   = const_cast<char*>(end);
      trim(trimStart, trimEnd);
      int len = trimEnd - trimStart + 1;
      if (len > 0 && equalsIgnoreCase(trimStart, len, serviceId)) {
        return true;
      }
      if (*ptr == '\0') break;
      ptr++;
      start = ptr;
    } else {
      ptr++;
    }
  }

  return false;
}

void raildataXmlClient::cleanFilter(const char* rawFilter, char* cleanedFilter, size_t maxLen) {
    if (!rawFilter || rawFilter[0] == '\0') {
        if (maxLen > 0) cleanedFilter[0] = '\0';
        return;
    }

    size_t j = 0;
    const char* ptr = rawFilter;

    while (*ptr != '\0' && j < maxLen - 1) {
        if (*ptr == ',') {
            cleanedFilter[j++] = ',';
            ptr++;
            continue;
        }
        if (!isspace(*ptr)) {
            cleanedFilter[j++] = tolower(*ptr);
        }
        ptr++;
    }

    cleanedFilter[j] = '\0';
    return;
}


//
// Updates the Departure Board data from the SOAP API
//
int raildataXmlClient::updateDepartures(rdStation *station, stnMessages *messages, const char *crsCode, const char *customToken, int numRows, bool includeBusServices, const char *callingCrsCode, const char *platforms, int timeOffset) {

    unsigned long perfTimer=millis();
    bool bChunked = false;
    lastErrorMessage = "";

    // Reset the counters
    xStation.numServices=0;
    xMessages.numMessages=0;
    xStation.platformAvailable=false;
    addedStopLocation = false;
    strcpy(xStation.location,"");

    for (int i=0;i<MAXBOARDSERVICES;i++) {
      strcpy(xStation.service[i].sTime,"");
      strcpy(xStation.service[i].destination,"");
      strcpy(xStation.service[i].via,"");
      strcpy(xStation.service[i].origin,"");
      strcpy(xStation.service[i].etd,"");
      strcpy(xStation.service[i].platform,"");
      strcpy(xStation.service[i].opco,"");
      strcpy(xStation.service[i].calling,"");
      strcpy(xStation.service[i].serviceMessage,"");
      xStation.service[i].trainLength=0;
      xStation.service[i].classesAvailable=0;
      xStation.service[i].serviceType=0;
      xStation.service[i].isCancelled=false;
      xStation.service[i].isDelayed=false;
    }
    for (int i=0;i<MAXBOARDMESSAGES;i++) strcpy(xMessages.messages[i],"");
    id=-1;
    coaches=0;

    WiFiClientSecure httpsClient;
    httpsClient.setInsecure();
    httpsClient.setTimeout(5000);
    httpsClient.setConnectionTimeout(5000);
    httpsClient.setNoDelay(false);

    int retryCounter=0; //retry counter
    while((!httpsClient.connect(soapHost, 443)) && (retryCounter < 10)) {
        delay(100);
        retryCounter++;
    }
    if(retryCounter>=10) {
        lastErrorMessage = F("Timed out, no response from connect");    // No response within 3s
        return UPD_NO_RESPONSE;
    }

    int reqRows = MAXBOARDSERVICES;
    if (platforms[0]) reqRows = 10;   // Request maximum services if we're filtering platforms
    String data = F("<soap-env:Envelope xmlns:soap-env=\"http://schemas.xmlsoap.org/soap/envelope/\"><soap-env:Header><ns0:AccessToken xmlns:ns0=\"http://thalesgroup.com/RTTI/2013-11-28/Token/types\"><ns0:TokenValue>");
    data += String(customToken) + F("</ns0:TokenValue></ns0:AccessToken></soap-env:Header><soap-env:Body><ns0:GetDepBoardWithDetailsRequest xmlns:ns0=\"http://thalesgroup.com/RTTI/2021-11-01/ldb/\"><ns0:numRows>") + String(reqRows) + F("</ns0:numRows><ns0:crs>");
    data += String(crsCode) + F("</ns0:crs>");
    if (callingCrsCode[0]) {
        data += "<ns0:filterCrs>" + String(callingCrsCode) + F("</ns0:filterCrs><ns0:filterType>to</ns0:filterType>");
    }
    if (timeOffset) data += "<ns0:timeOffset>" + String(timeOffset) + F("</ns0:timeOffset>");
    data += F("</ns0:GetDepBoardWithDetailsRequest></soap-env:Body></soap-env:Envelope>");

    httpsClient.print("POST " + String(soapAPI) + F(" HTTP/1.1\r\n") +
      F("Host: ") + String(soapHost) + F("\r\n") +
      F("Content-Type: text/xml;charset=UTF-8\r\n") +
      F("Connection: close\r\n") +
      F("Content-Length: ") + String(data.length()) + F("\r\n\r\n") +
      data + F("\r\n\r\n"));

    Xcb(1,0);   // progress callback
    unsigned long ticker = millis()+800;
    retryCounter = 0;
    while(!httpsClient.available()) {
        delay(100);
        retryCounter++;
        if (retryCounter >= 30) {
            lastErrorMessage = F("Timed out (GET)");
            return UPD_TIMEOUT;     // No response within 3s
        }
    }

    unsigned long dataSendTimeout = millis() + 1000UL;
    while((httpsClient.available() || httpsClient.connected()) && (millis() < dataSendTimeout)) {
        String line = httpsClient.readStringUntil('\n');
        // check for success code...
        if (line.startsWith(F("HTTP"))) {
            if (line.indexOf(F("200 OK")) == -1) {
                httpsClient.stop();
                if (line.indexOf(F("401")) > 0) {
                    lastErrorMessage = line;
                    return UPD_UNAUTHORISED;
                } else if (line.indexOf(F("500")) > 0) {
                    lastErrorMessage = line;
                    return UPD_DATA_ERROR;
                } else {
                    lastErrorMessage = line;
                    return UPD_HTTP_ERROR;
                }
            }
        } else if (line.startsWith(F("Transfer-Encoding:")) && line.indexOf(F("chunked")) >= 0) bChunked=true;
        if (line == F("\r")) {
            // Headers received
            break;
        }
        delay(1);
    }

    xmlStreamingParser parser;
    parser.setListener(this);
    parser.reset();
    grandParentTagName = "";
    parentTagName = "";
    tagName = "";
    tagLevel = 0;
    loadingWDSL=false;
    long dataReceived = 0;
    if (platforms[0]) {
        filterPlatforms = true;
        strcpy(platformFilter,platforms);
    } else {
        filterPlatforms = false;
        strcpy(platformFilter,"");
    }
    keepRoute=false;

    char c;
    dataSendTimeout = millis() + 12000UL;
    perfTimer=millis(); // Reset the data load timer
    while((httpsClient.available() || httpsClient.connected()) && (millis() < dataSendTimeout)) {
        while (httpsClient.available()) {
            c = httpsClient.read();
            parser.parse(c);
            dataReceived++;
            if (millis()>ticker) {
                Xcb(2,xStation.numServices);    // Callback progress
                ticker = millis()+800;
            }
        }
        if (millis()>ticker) {
            Xcb(2,id);      // Callback with progress
            ticker = millis()+800;
        }
        delay(5);
    }

    httpsClient.stop();
    if (bChunked) lastErrorMessage = F("WARNING: Chunked response! ");
    if (millis() >= dataSendTimeout) {
        lastErrorMessage += F("Timed out during data receive operation - ");
        lastErrorMessage += String(dataReceived) + F(" bytes received");
        return UPD_TIMEOUT;
    }

    if (!xStation.location[0]) {
        // We didn't get a location back so probably failed
        lastErrorMessage += F("Data incomplete - no location in response");
        return UPD_DATA_ERROR;
    }
    if (filterPlatforms && !keepRoute && xStation.numServices) xStation.numServices--;   // Last route added needs filtering out

    sanitiseData();
    if (includeBusServices) {
        // Look for any included bus services, and sort if found
        for (int i=0;i<xStation.numServices;i++) {
            if (xStation.service[i].serviceType == BUS) {
                size_t arraySize = xStation.numServices;
                std::sort(xStation.service, xStation.service+arraySize,compareTimes);
                break;
            }
        }
    } else {
        // Go through and delete any bus services
        int i=0;
        while (i<xStation.numServices) {
            // Remove any bus services
            if (xStation.service[i].serviceType == BUS) deleteService(i);
            else i++;
        }
    }

    bool noUpdate = true;
    if (!firstDataLoad) {
        // Check for any changes
        if (messages->numMessages != xMessages.numMessages || station->numServices != xStation.numServices || station->platformAvailable != xStation.platformAvailable || strcmp(station->location,xStation.location)) noUpdate=false;
        else {
            for (int i=0;i<xMessages.numMessages;i++) {
                if (strcmp(messages->messages[i],xMessages.messages[i])) {
                    noUpdate=false;
                    break;
                }
            }
            if (noUpdate) {
                for (int i=0;i<xStation.numServices;i++) {
                    if (strcmp(station->service[i].sTime, xStation.service[i].sTime) || strcmp(station->service[i].destination, xStation.service[i].destination) || strcmp(station->service[i].via, xStation.service[i].via) || strcmp(station->service[i].etd, xStation.service[i].etd) || strcmp(station->service[i].platform, xStation.service[i].platform)) {
                        noUpdate=false;
                        break;
                    }
                    if (station->service[i].isCancelled != xStation.service[i].isCancelled || station->service[i].isDelayed != xStation.service[i].isDelayed || station->service[i].trainLength != xStation.service[i].trainLength || station->service[i].classesAvailable != xStation.service[i].classesAvailable || station->service[i].serviceType != xStation.service[i].serviceType) {
                        noUpdate=false;
                        break;
                    }
                }
            }
        }
    } else {
        firstDataLoad=false;
        noUpdate=false;
    }

    if (!noUpdate) {
        // copy everything back to the caller's structure
        messages->numMessages = xMessages.numMessages;
        station->numServices = xStation.numServices;
        strcpy(station->location,xStation.location);
        station->platformAvailable = xStation.platformAvailable;
        for (int i=0;i<xMessages.numMessages;i++) strcpy(messages->messages[i],xMessages.messages[i]);
        for (int i=0;i<xStation.numServices;i++) {
            strcpy(station->service[i].sTime, xStation.service[i].sTime);
            strcpy(station->service[i].destination, xStation.service[i].destination);
            strcpy(station->service[i].via, xStation.service[i].via);
            strcpy(station->service[i].etd, xStation.service[i].etd);
            strcpy(station->service[i].platform, xStation.service[i].platform);
            station->service[i].isCancelled = xStation.service[i].isCancelled;
            station->service[i].isDelayed = xStation.service[i].isDelayed;
            station->service[i].trainLength = xStation.service[i].trainLength;
            station->service[i].classesAvailable = xStation.service[i].classesAvailable;
            strcpy(station->service[i].opco, xStation.service[i].opco);
            station->service[i].serviceType = xStation.service[i].serviceType;
        }
        if (xStation.numServices) {
            strcpy(station->calling,xStation.service[0].calling);
            strcpy(station->origin,xStation.service[0].origin);
            strcpy(station->serviceMessage,xStation.service[0].serviceMessage);
        }
    }

    Xcb(3,xStation.numServices);
    if (noUpdate) {
        lastErrorMessage += "Success (No Changes) - data [" + String(dataReceived) + F("] load took ") + String(millis()-perfTimer) + F("ms");
        return UPD_NO_CHANGE;
    } else {
        lastErrorMessage += "Success - data [" + String(dataReceived) + F("] took ") + String(millis()-perfTimer) + F("ms");
        return UPD_SUCCESS;
    }
}

String raildataXmlClient::getLastError() {
    return lastErrorMessage;
}

void raildataXmlClient::deleteService(int x) {

  if (x==xStation.numServices-1) {
    // it's the last one so just reduce the count by one
    xStation.numServices--;
    return;
  }
  // shuffle the other services down
  for (int i=x;i<xStation.numServices-1;i++) {
    xStation.service[i] = xStation.service[i+1];
  }
  xStation.numServices--;
}

void raildataXmlClient::sanitiseData() {

  int i=0;
  while (i<xStation.numServices) {
    // Remove any services that are missing destinations/std/etd
    if (!xStation.service[i].destination[0] || !xStation.service[i].etd[0] || !xStation.service[i].sTime[0]) deleteService(i);
    else i++;
  }

  // Issue #5 - Ampersands in Station Location
  removeHtmlTags(xStation.location);
  replaceWord(xStation.location,"&amp;","&");

  for (int i=0;i<xStation.numServices;i++) {
    // first change any &lt; &gt;
    removeHtmlTags(xStation.service[i].destination);
    replaceWord(xStation.service[i].destination,"&amp;","&");
    removeHtmlTags(xStation.service[i].calling);
    replaceWord(xStation.service[i].calling,"&amp;","&");
    removeHtmlTags(xStation.service[i].via);
    replaceWord(xStation.service[i].via,"&amp;","&");
    removeHtmlTags(xStation.service[i].serviceMessage);
    replaceWord(xStation.service[i].serviceMessage,"&amp;","&");
    replaceWord(xStation.service[i].serviceMessage,"&quot;","\"");
    fixFullStop(xStation.service[i].serviceMessage);
  }

  for (int i=0;i<xMessages.numMessages;i++) {
    // Remove all non printing characters from messages...
    int j = 0; // Index for the modified array
    for (int x=0; xMessages.messages[i][x] != '\0'; ++x) {
        if (isprint(xMessages.messages[i][x])) {
            xMessages.messages[i][j] = xMessages.messages[i][x];
            ++j;
        }
    }
    xMessages.messages[i][j] = '\0'; // Null-terminate the modified array
    replaceWord(xMessages.messages[i],"&lt;","<");
    replaceWord(xMessages.messages[i],"&gt;",">");
    replaceWord(xMessages.messages[i],"<p>","");
    replaceWord(xMessages.messages[i],"</p>","");
    replaceWord(xMessages.messages[i],"<br>"," ");

    removeHtmlTags(xMessages.messages[i]);
    replaceWord(xMessages.messages[i],"&amp;","&");
    replaceWord(xMessages.messages[i],"&quot;","\"");
    // Remove unwanted text at the end of service messages...
    pruneFromPhrase(xMessages.messages[i]," More details ");
    pruneFromPhrase(xMessages.messages[i]," Latest information ");
    pruneFromPhrase(xMessages.messages[i]," Further information ");
    pruneFromPhrase(xMessages.messages[i]," More information can ");

    fixFullStop(xMessages.messages[i]);
  }
}

void raildataXmlClient::startTag(const char *tag)
{
    tagLevel++;
    grandParentTagName = parentTagName;
    parentTagName = tagName;
    tagName = String(tag);
    tagPath = grandParentTagName + "/" + parentTagName + "/" + tagName;
}

void raildataXmlClient::endTag(const char *tag)
{
    tagLevel--;
    tagName = parentTagName;
    parentTagName=grandParentTagName;
    grandParentTagName="??";
}

void raildataXmlClient::parameter(const char *param)
{
}

void raildataXmlClient::value(const char *value)
{
    if (loadingWDSL) return;

    if (tagLevel<6 || tagLevel==9 || tagLevel>11) return;

    if (tagLevel == 11 && tagPath.endsWith(F("callingPoint/lt8:locationName"))) {
        if ((strlen(xStation.service[id].calling) + strlen(value) + 13) < sizeof(xStation.service[0].calling)) {
            // Add the calling point, add a comma prefix if this isn't the first one
            if (xStation.service[id].calling[0]) strcat(xStation.service[id].calling,", ");
            strcat(xStation.service[id].calling,value);
            addedStopLocation = true;
        }
        return;
    } else if (tagLevel == 11 && tagPath.endsWith(F("callingPoint/lt8:st")) && addedStopLocation) {
        // check there's still room to add the eta of the calling point
        if ((strlen(xStation.service[id].calling) + strlen(value) + 4) < sizeof(xStation.service[0].calling)) {
            strcat(xStation.service[id].calling," (");
            strcat(xStation.service[id].calling,value);
            strcat(xStation.service[id].calling,")");
        }
        addedStopLocation = false;
        return;
    } else if (tagLevel == 11 && tagName == F("lt7:coachClass")) {
        if (strcmp(value,"First")==0) xStation.service[id].classesAvailable = xStation.service[id].classesAvailable | 1;
        else if (strcmp(value,"Standard")==0) xStation.service[id].classesAvailable = xStation.service[id].classesAvailable | 2;
        coaches++;
        return;
    } else if (tagLevel == 8 && tagName == F("lt4:length")) {
        xStation.service[id].trainLength = String(value).toInt();
        return;
    } else if (tagLevel == 8 && tagName == F("lt4:operator")) {
        strncpy(xStation.service[id].opco,value,sizeof(xStation.service[0].opco)-1);
        xStation.service[id].opco[sizeof(xStation.service[0].opco)-1] = '\0';
        return;
    } else if (tagLevel == 10 && tagPath.startsWith(F("lt5:origin/lt4:location/lt4:loc"))) {
        strncpy(xStation.service[id].origin,value,sizeof(xStation.service[0].origin)-1);
        xStation.service[id].origin[sizeof(xStation.service[0].origin)-1] = '\0';
        return;
    } else if (tagLevel == 8 && tagName == F("lt4:serviceType")) {
        if (strcmp(value,"train")==0) xStation.service[id].serviceType = TRAIN;
        else if (strcmp(value,"bus")==0) xStation.service[id].serviceType = BUS;
        return;
    } else if (tagLevel == 8 && tagName == F("lt4:std")) {
        // Starting a new service
        // If we're filtering on platform numbers, check if we need to keep the previous service (if there was one)
        if (filterPlatforms && !keepRoute && id>=0) {
            // We don't want this service, so clear it
            strcpy(xStation.service[id].sTime,"");
            strcpy(xStation.service[id].destination,"");
            strcpy(xStation.service[id].via,"");
            strcpy(xStation.service[id].origin,"");
            strcpy(xStation.service[id].etd,"");
            strcpy(xStation.service[id].platform,"");
            strcpy(xStation.service[id].opco,"");
            strcpy(xStation.service[id].calling,"");
            strcpy(xStation.service[id].serviceMessage,"");
            xStation.service[id].trainLength=0;
            xStation.service[id].classesAvailable=0;
            xStation.service[id].serviceType=0;
            xStation.service[id].isCancelled=false;
            xStation.service[id].isDelayed=false;
            xStation.numServices--;
            id--;
        }
        keepRoute = false;  // reset for next route
        if (id>=0) {
            if (xStation.service[id].trainLength == 0) xStation.service[id].trainLength = coaches;
        }
        coaches=0;
        if (id < MAXBOARDSERVICES-1) {
            id++;
            xStation.numServices++;
        }
        strncpy(xStation.service[id].sTime,value,sizeof(xStation.service[0].sTime));
        xStation.service[id].sTime[sizeof(xStation.service[0].sTime)-1] = '\0';
        return;
    } else if (tagLevel == 8 && tagName == F("lt4:etd")) {
        strncpy(xStation.service[id].etd,value,sizeof(xStation.service[0].etd));
        xStation.service[id].etd[sizeof(xStation.service[0].etd)-1] = '\0';
        return;
    } else if (tagLevel == 10 && tagPath.startsWith(F("lt5:destination/lt4:location/lt4:lo"))) {
        strncpy(xStation.service[id].destination,value,sizeof(xStation.service[0].destination)-1);
        xStation.service[id].destination[sizeof(xStation.service[0].destination)-1] = '\0';
        return;
    } else if (tagLevel == 10 && tagPath == F("lt5:destination/lt4:location/lt4:via")) {
        strncpy(xStation.service[id].via,value,sizeof(xStation.service[0].via)-1);
        xStation.service[id].via[sizeof(xStation.service[0].via)-1] = '\0';
        return;
    } else if (tagLevel == 8 && tagName == F("lt4:delayReason")) {
        strncpy(xStation.service[id].serviceMessage,value,sizeof(xStation.service[0].serviceMessage)-1);
        xStation.service[id].serviceMessage[sizeof(xStation.service[0].serviceMessage)-1] = '\0';
        xStation.service[id].isDelayed = true;
        return;
    } else if (tagLevel == 8 && tagName == F("lt4:cancelReason")) {
        strncpy(xStation.service[id].serviceMessage,value,sizeof(xStation.service[0].serviceMessage)-1);
        xStation.service[id].serviceMessage[sizeof(xStation.service[0].serviceMessage)-1] = '\0';
        xStation.service[id].isCancelled = true;
        return;
    } else if (tagLevel == 8 && tagName == (F("lt4:platform"))) {
        strncpy(xStation.service[id].platform,value,sizeof(xStation.service[0].platform)-1);
        xStation.service[id].platform[sizeof(xStation.service[0].platform)-1] = '\0';
        if (filterPlatforms && serviceMatchesFilter(platformFilter,xStation.service[id].platform)) keepRoute=true;
        return;
    } else if (tagLevel == 6 && tagName == F("lt4:locationName")) {
        strncpy(xStation.location,value,sizeof(xStation.location)-1);
        xStation.location[sizeof(xStation.location)-1] = '\0';
        return;
    } else if (tagLevel == 6 && tagName == F("lt4:platformAvailable")) {
        if (strcmp(value,"true")==0) xStation.platformAvailable = true;
        return;
    } else if (tagPath.endsWith(F("nrccMessages/lt:message"))) {    // tagLevel 7
        if (xMessages.numMessages < MAXBOARDMESSAGES) {
            xMessages.numMessages++;
            strncpy(xMessages.messages[xMessages.numMessages-1],value,sizeof(xMessages.messages[0])-1);
            xMessages.messages[xMessages.numMessages-1][sizeof(xMessages.messages[0])-1] = '\0';
        }
        return;
    }
}

void raildataXmlClient::attribute(const char *attr)
{
    if (loadingWDSL) {
        if (tagName == F("soap:address")) {
            String myURL = String(attr);
            if (myURL.startsWith(F("location=\"")) && myURL.endsWith(F("\""))) {
                soapURL = myURL.substring(10,myURL.length()-1);
            }
        }
    }
}
