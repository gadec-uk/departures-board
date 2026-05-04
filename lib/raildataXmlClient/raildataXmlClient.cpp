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

raildataXmlClient::raildataXmlClient(rdiStation *station, stnMessages *messages, sharedBufferSpace *sharedBuffer) : xStation(station), xMessages(messages), js(sharedBuffer) {
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
int raildataXmlClient::init(const char *wsdlHost, const char *wsdlAPI)
{
    WiFiClientSecure httpsClient;
    httpsClient.setInsecure();
    httpsClient.setTimeout(10000);
    httpsClient.setConnectionTimeout(10000);

    int retryCounter=0; //retry counter
    while((!httpsClient.connect(wsdlHost, 443)) && (retryCounter < 10)){
        delay(100);
        retryCounter++;
    }
    if(retryCounter>=10) {
      return UPD_NO_RESPONSE;   // No response within 3s
    }

    httpsClient.print("GET " + String(wsdlAPI) + " HTTP/1.0\r\n" +
      "Host: " + String(wsdlHost) + "\r\n" +
      "Connection: close\r\n\r\n");

    retryCounter = 0;
    while(!httpsClient.available()) {
        delay(100);
        retryCounter++;
        if (retryCounter > 100) {
            httpsClient.stop();
            return UPD_TIMEOUT;     // Timeout after 10s
        }
    }

    while (httpsClient.connected() || httpsClient.available()) {
      String line = httpsClient.readStringUntil('\n');
      // check for success code...
      if (line.startsWith("HTTP")) {
        if (line.indexOf("200 OK") == -1) {
          httpsClient.stop();
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

    char c;
    unsigned long dataSendTimeout = millis() + 8000UL;
    loadingWDSL = true;
    xmlStreamingParser parser;
    parser.setListener(this);
    parser.reset();
    greatGrandParentTagName = "";
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

    if (soapURL.startsWith("https://")) {
      int delim = soapURL.indexOf("/",8);
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

    *output = '\0';
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
  for (int i=0;i<a_len;++i) {
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
// Fetches the Departure Board data from the SOAP API
//
int raildataXmlClient::fetchDepartures(rdStation *station, stnMessages *messages, const char *crsCode, const char *customToken, int numRows, bool includeBusServices, const char *callingCrsCode, const char *platforms, int timeOffset, bool fetchLastSeen, bool includeServiceMessages) {

    unsigned long perfTimer=millis();
    bool bChunked = false;
    js->lastResultMessage[0] = '\0';

    // Reset the counters
    xStation->numServices=0;
    xMessages->numMessages=0;
    xStation->platformAvailable=false;
    addedStopLocation = false;
    strcpy(xStation->location,"");

    for (int i=0;i<MAXBOARDSERVICES;++i) {
      strcpy(xStation->service[i].sTime,"");
      strcpy(xStation->service[i].destination,"");
      strcpy(xStation->service[i].via,"");
      strcpy(xStation->service[i].origin,"");
      strcpy(xStation->service[i].etd,"");
      strcpy(xStation->service[i].platform,"");
      strcpy(xStation->service[i].opco,"");
      strcpy(xStation->service[i].calling,"");
      strcpy(xStation->service[i].serviceMessage,"");
      strcpy(xStation->service[i].serviceID,"");
      xStation->service[i].trainLength=0;
      xStation->service[i].classesAvailable=0;
      xStation->service[i].serviceType=0;
      xStation->service[i].isCancelled=false;
      xStation->service[i].isDelayed=false;
    }
    for (int i=0;i<MAXBOARDMESSAGES;++i) strcpy(xMessages->messages[i],"");
    id=-1;
    coaches=0;

    WiFiClientSecure httpsClient;
    httpsClient.setInsecure();
    httpsClient.setTimeout(8000);
    httpsClient.setConnectionTimeout(8000);
    httpsClient.setNoDelay(false);

    int retryCounter=0; //retry counter
    while((!httpsClient.connect(soapHost, 443)) && (retryCounter < 10)) {
        delay(100);
        retryCounter++;
    }
    if(retryCounter>=10) {
        strcpy(js->lastResultMessage,"Error: Connect timed out");
        return UPD_NO_RESPONSE;
    }

    int reqRows = MAXBOARDSERVICES;
    if (platforms[0]) reqRows = 10;   // Request maximum services if we're filtering platforms
    String data = "<soap-env:Envelope xmlns:soap-env=\"http://schemas.xmlsoap.org/soap/envelope/\"><soap-env:Header><ns0:AccessToken xmlns:ns0=\"http://thalesgroup.com/RTTI/2013-11-28/Token/types\"><ns0:TokenValue>";
    data += String(customToken) + "</ns0:TokenValue></ns0:AccessToken></soap-env:Header><soap-env:Body><ns0:GetDepBoardWithDetailsRequest xmlns:ns0=\"http://thalesgroup.com/RTTI/2021-11-01/ldb/\"><ns0:numRows>" + String(reqRows) + "</ns0:numRows><ns0:crs>";
    data += String(crsCode) + "</ns0:crs>";
    if (callingCrsCode[0]) {
        data += "<ns0:filterCrs>" + String(callingCrsCode) + "</ns0:filterCrs><ns0:filterType>to</ns0:filterType>";
    }
    if (timeOffset) data += "<ns0:timeOffset>" + String(timeOffset) + "</ns0:timeOffset>";
    data += "</ns0:GetDepBoardWithDetailsRequest></soap-env:Body></soap-env:Envelope>";

    httpsClient.print("POST " + String(soapAPI) + " HTTP/1.1\r\n" +
      "Host: " + String(soapHost) + "\r\n" +
      "Content-Type: text/xml;charset=UTF-8\r\n" +
      "Connection: close\r\n" +
      "Content-Length: " + String(data.length()) + "\r\n\r\n" +
      data + "\r\n\r\n");

    retryCounter = 0;
    while(!httpsClient.available()) {
        delay(100);
        retryCounter++;
        if (retryCounter >= 80) {
            httpsClient.stop();
            strcpy(js->lastResultMessage,"Error: GET timed out");
            return UPD_TIMEOUT;     // No response within 8s
        }
    }

    unsigned long dataSendTimeout = millis() + 1000UL;
    while((httpsClient.available() || httpsClient.connected()) && (millis() < dataSendTimeout)) {
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
        } else if (line.startsWith("Transfer-Encoding:") && line.indexOf("chunked") >= 0) bChunked=true;
        if (line == "\r") {
            // Headers received
            break;
        }
        delay(1);
    }

    xmlStreamingParser parser;
    parser.setListener(this);
    parser.reset();
    greatGrandParentTagName = "";
    grandParentTagName = "";
    parentTagName = "";
    tagName = "";
    tagLevel = 0;
    loadingWDSL = false;
    fetchingDepartures = true;
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
        }
        delay(5);
    }

    httpsClient.stop();
    if (millis() >= dataSendTimeout) {
        sprintf(js->lastResultMessage,"Error: Timeout after %d bytes",dataReceived);
        return UPD_TIMEOUT;
    }

    if (!xStation->location[0]) {
        // We didn't get a location back so probably failed
        strcpy(js->lastResultMessage,"Error: Incomplete data (no location)");
        return UPD_DATA_ERROR;
    }
    if (filterPlatforms && !keepRoute && xStation->numServices) xStation->numServices--;   // Last route added needs filtering out

    if (!includeBusServices) {
        // Go through and delete any bus services
        int i=0;
        while (i<xStation->numServices) {
            // Remove any bus services
            if (xStation->service[i].serviceType == BUS) deleteService(i);
            else i++;
        }
    }

    // Get the sort times (scheduled or estimated if present)
    for (int i=0;i<xStation->numServices;++i) {
        if (isdigit(xStation->service[i].etd[0]) && strlen(xStation->service[i].etd) == 5) {
            strcpy(xStation->service[i].sortTime,xStation->service[i].etd);
        } else {
            strcpy(xStation->service[i].sortTime,xStation->service[i].sTime);
        }
    }
    // Sort the services by actual departure time
    size_t arraySize = xStation->numServices;
    std::sort(xStation->service, xStation->service+arraySize,compareTimes);

    if (xStation->numServices && (xStation->service[0].isCancelled || strcmp(xStation->service[0].etd,"Delayed")==0)) {
        // First service is cancelled or delayed (without estimate), check if it should be dropped
        struct tm nowtime;
        getLocalTime(&nowtime);
        char timenow[6];
        sprintf(timenow,"%02d:%02d",nowtime.tm_hour,nowtime.tm_min);
        if (timeDiff(xStation->service[0].sortTime,timenow) < -1) deleteService(0);
    }

    // Handle getting last seen location from GetServiceDetails api
    if (fetchLastSeen && xStation->numServices && xStation->service[0].serviceID[0] && strcmp(xStation->location,xStation->service[0].origin)) {
        getServiceDetails(xStation->service[0].serviceID, customToken);
    }

    // Do we want service messages?
    if (!includeServiceMessages) xMessages->numMessages=0;

    sanitiseData();
    bool noUpdate = true;
    bool secondaryChange = true;
    if (!firstDataLoad) {
        // Check for any changes
        if (messages->numMessages != xMessages->numMessages || station->numServices != xStation->numServices || station->platformAvailable != xStation->platformAvailable || strcmp(station->location,xStation->location)) {
            noUpdate=false;
            secondaryChange = false;
        }
        else {
            for (int i=0;i<xMessages->numMessages;++i) {
                if (strcmp(messages->messages[i],xMessages->messages[i])) {
                    noUpdate=false;
                    break;
                }
            }
            if (strcmp(station->calling,xStation->service[0].calling)) noUpdate=false;
            for (int i=0;i<xStation->numServices;++i) {
                if (strcmp(station->service[i].sTime, xStation->service[i].sTime) || strcmp(station->service[i].destination, xStation->service[i].destination) || strcmp(station->service[i].via, xStation->service[i].via) || strcmp(station->service[i].etd, xStation->service[i].etd) || strcmp(station->service[i].platform, xStation->service[i].platform)) {
                    noUpdate=false;
                    if (i==0 && (strcmp(station->service[i].destination, xStation->service[i].destination) || strcmp(station->service[i].via, xStation->service[i].via))) secondaryChange = false;
                    break;
                }
                if (station->service[i].isCancelled != xStation->service[i].isCancelled || station->service[i].isDelayed != xStation->service[i].isDelayed || station->service[i].trainLength != xStation->service[i].trainLength || station->service[i].classesAvailable != xStation->service[i].classesAvailable || station->service[i].serviceType != xStation->service[i].serviceType) {
                    noUpdate=false;
                    if (i==0) secondaryChange = false;
                    break;
                }
            }
        }
    } else {
        firstDataLoad=false;
        noUpdate=false;
        secondaryChange=false;
    }

    UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    if (noUpdate) {
        sprintf(js->lastResultMessage+strlen(js->lastResultMessage),"[DB] OK: NC D:%d T:%d S:%d %s",dataReceived,millis()-perfTimer,uxHighWaterMark,bChunked?"C!":"");
        return UPD_NO_CHANGE;
    } else {
        if (secondaryChange) {
            sprintf(js->lastResultMessage+strlen(js->lastResultMessage),"[DB] OK: SC D:%d T:%d S:%d %s",dataReceived,millis()-perfTimer,uxHighWaterMark,bChunked?"C!":"");
            return UPD_SEC_CHANGE;
        } else {
            sprintf(js->lastResultMessage+strlen(js->lastResultMessage),"[DB] OK: UP D:%d T:%d S:%d %s",dataReceived,millis()-perfTimer,uxHighWaterMark,bChunked?"C!":"");
            return UPD_SUCCESS;
        }
    }
}

void raildataXmlClient::loadDepartures(rdStation *station, stnMessages *messages) {
    // copy everything back to the caller's structure
    messages->numMessages = xMessages->numMessages;
    station->numServices = xStation->numServices;
    strcpy(station->location,xStation->location);
    station->platformAvailable = xStation->platformAvailable;
    for (int i=0;i<xMessages->numMessages;++i) strcpy(messages->messages[i],xMessages->messages[i]);
    for (int i=0;i<xStation->numServices;++i) {
        strcpy(station->service[i].sTime, xStation->service[i].sTime);
        strcpy(station->service[i].destination, xStation->service[i].destination);
        strcpy(station->service[i].via, xStation->service[i].via);
        strcpy(station->service[i].etd, xStation->service[i].etd);
        strcpy(station->service[i].platform, xStation->service[i].platform);
        station->service[i].isCancelled = xStation->service[i].isCancelled;
        station->service[i].isDelayed = xStation->service[i].isDelayed;
        station->service[i].trainLength = xStation->service[i].trainLength;
        station->service[i].classesAvailable = xStation->service[i].classesAvailable;
        strcpy(station->service[i].opco, xStation->service[i].opco);
        station->service[i].serviceType = xStation->service[i].serviceType;
    }
    if (xStation->numServices) {
        strcpy(station->calling,xStation->service[0].calling);
        strcpy(station->origin,xStation->service[0].origin);
        strcpy(station->serviceMessage,xStation->service[0].serviceMessage);
    }
}

int raildataXmlClient::getServiceDetails(const char *serviceID, const char *customToken) {

    unsigned long perfTimer=millis();
    bool bChunked = false;
    js->lastResultMessage[0] = '\0';
    // Use a spare char buffer space for the last report temporary text
    xStation->service[1].calling[0] = '\0';

    // Reset the counters
    WiFiClientSecure httpsClient;
    httpsClient.setInsecure();
    httpsClient.setTimeout(8000);
    httpsClient.setConnectionTimeout(8000);
    httpsClient.setNoDelay(false);

    int retryCounter=0; //retry counter
    while((!httpsClient.connect(soapHost, 443)) && (retryCounter < 10)) {
        delay(100);
        retryCounter++;
    }
    if(retryCounter>=10) {
        strcpy(js->lastResultMessage,"[SD] Connect Timeout");
        return UPD_NO_RESPONSE;
    }

    String data = "<soap-env:Envelope xmlns:soap-env=\"http://schemas.xmlsoap.org/soap/envelope/\"><soap-env:Header><ns0:AccessToken xmlns:ns0=\"http://thalesgroup.com/RTTI/2013-11-28/Token/types\"><ns0:TokenValue>";
    data += String(customToken) + "</ns0:TokenValue></ns0:AccessToken></soap-env:Header><soap-env:Body><ns0:GetServiceDetailsRequest xmlns:ns0=\"http://thalesgroup.com/RTTI/2021-11-01/ldb/\"><ns0:serviceID>";
    data += String(serviceID) + "</ns0:serviceID></ns0:GetServiceDetailsRequest></soap-env:Body></soap-env:Envelope>";

    httpsClient.print("POST " + String(soapAPI) + " HTTP/1.1\r\n" +
      "Host: " + String(soapHost) + "\r\n" +
      "Content-Type: text/xml;charset=UTF-8\r\n" +
      "Connection: close\r\n" +
      "Content-Length: " + String(data.length()) + "\r\n\r\n" +
      data + "\r\n\r\n");

    retryCounter = 0;
    while(!httpsClient.available()) {
        delay(100);
        retryCounter++;
        if (retryCounter >= 80) {
            httpsClient.stop();
            strcpy(js->lastResultMessage,"[SD] GET Timeout");
            return UPD_TIMEOUT;     // No response within 8s
        }
    }

    unsigned long dataSendTimeout = millis() + 1000UL;
    while((httpsClient.available() || httpsClient.connected()) && (millis() < dataSendTimeout)) {
        String line = httpsClient.readStringUntil('\n');
        // check for success code...
        if (line.startsWith("HTTP")) {
            if (line.indexOf("200 OK") == -1) {
                httpsClient.stop();
                if (line.indexOf("401") > 0) {
                    strcpy(js->lastResultMessage,"[SD] 401 Unauthorised ");
                    return UPD_UNAUTHORISED;
                } else if (line.indexOf("500") > 0) {
                    strcpy(js->lastResultMessage,"[SD] 500 Data Error ");
                    return UPD_DATA_ERROR;
                } else {
                    sprintf(js->lastResultMessage,"[SD] HTTP Error %.3s ",line);
                    return UPD_HTTP_ERROR;
                }
            }
        } else if (line.startsWith("Transfer-Encoding:") && line.indexOf("chunked") >= 0) bChunked=true;
        if (line == "\r") {
            // Headers received
            break;
        }
        delay(1);
    }

    xmlStreamingParser parser;
    parser.setListener(this);
    parser.reset();
    greatGrandParentTagName = "";
    grandParentTagName = "";
    parentTagName = "";
    tagName = "";
    tagLevel = 0;
    loadingWDSL = false;
    fetchingDepartures = false;
    long dataReceived = 0;
    lastLocation.actualTime[0]='\0';
    lastLocation.location[0]='\0';
    lastLocation.scheduledTime[0]='\0';
    thisLocation.actualTime[0]='\0';
    thisLocation.location[0]='\0';
    thisLocation.scheduledTime[0]='\0';

    char c;
    dataSendTimeout = millis() + 12000UL;
    perfTimer=millis(); // Reset the data load timer
    while((httpsClient.available() || httpsClient.connected()) && (millis() < dataSendTimeout)) {
        while (httpsClient.available()) {
            c = httpsClient.read();
            parser.parse(c);
            dataReceived++;
        }
        delay(5);
    }

    httpsClient.stop();

    if (millis() >= dataSendTimeout) {
        sprintf(js->lastResultMessage,"[SD] Data timeout %d ",dataReceived);
        return UPD_TIMEOUT;
    }

    // Handle possible last location
    if (thisLocation.actualTime[0]) {
        strcpy(lastLocation.location,thisLocation.location);
        strcpy(lastLocation.actualTime,thisLocation.actualTime);
        strcpy(lastLocation.scheduledTime,thisLocation.scheduledTime);
    }

    if (lastLocation.location[0] && lastLocation.actualTime[0] && lastLocation.scheduledTime[0]) {
        sprintf(xStation->service[1].calling,".  Last seen at %s",lastLocation.location);
        if (strcmp(lastLocation.actualTime,"On ti")==0) {
            sprintf(xStation->service[1].calling + strlen(xStation->service[1].calling)," (%s), on time.",lastLocation.scheduledTime);
        } else if (isdigit(lastLocation.actualTime[0]) && isdigit(lastLocation.scheduledTime[0])) {
            int offMins = timeDiff(lastLocation.scheduledTime,lastLocation.actualTime);
            sprintf(xStation->service[1].calling + strlen(xStation->service[1].calling)," (%s), %d %s %s.",lastLocation.actualTime, abs(offMins), abs(offMins)==1?"min":"mins", offMins>0?"early":"late");
        }
        if ((strlen(xStation->service[0].calling) + strlen(xStation->service[1].calling)) < MAXCALLINGSIZE) {
            strcat(xStation->service[0].calling, xStation->service[1].calling);
        }
    }

    sprintf(js->lastResultMessage,"[SD] OK: D:%d T:%d ",dataReceived,millis()-perfTimer);
    return UPD_SUCCESS;
}

void raildataXmlClient::deleteService(int x) {

  if (x==xStation->numServices-1) {
    // it's the last one so just reduce the count by one
    xStation->numServices--;
    return;
  }
  // shuffle the other services down
  for (int i=x;i<xStation->numServices-1;++i) {
    xStation->service[i] = xStation->service[i+1];
  }
  xStation->numServices--;
}

int raildataXmlClient::timeDiff(const char *scheduled, const char *actual) {
    int h_scheduled, m_scheduled;
    int h_actual, m_actual;

    sscanf(scheduled, "%d:%d", &h_scheduled, &m_scheduled);
    sscanf(actual, "%d:%d", &h_actual, &m_actual);

    int mins_scheduled = (h_scheduled * 60) + m_scheduled;
    int mins_actual = (h_actual * 60) + m_actual;
    int diff = mins_scheduled - mins_actual;

    // Handle midnight wrap-around
    if (diff > 720) diff -= 1440;
    else if (diff < -720) diff += 1440;

    return diff;
}

void raildataXmlClient::sanitiseData() {

  int i=0;
  while (i<xStation->numServices) {
    // Remove any services that are missing destinations/std/etd
    if (!xStation->service[i].destination[0] || !xStation->service[i].etd[0] || !xStation->service[i].sTime[0]) deleteService(i);
    else i++;
  }

  // Issue #5 - Ampersands in Station Location
  removeHtmlTags(xStation->location);
  replaceWord(xStation->location,"&amp;","&");

  for (int i=0;i<xStation->numServices;++i) {
    // first change any &lt; &gt;
    removeHtmlTags(xStation->service[i].destination);
    replaceWord(xStation->service[i].destination,"&amp;","&");
    removeHtmlTags(xStation->service[i].via);
    replaceWord(xStation->service[i].via,"&amp;","&");
    if (i==0) {
        removeHtmlTags(xStation->service[i].calling);
        replaceWord(xStation->service[i].calling,"&amp;","&");
        removeHtmlTags(xStation->service[i].opco);
        replaceWord(xStation->service[i].opco,"&amp;","&");
        removeHtmlTags(xStation->service[i].origin);
        replaceWord(xStation->service[i].origin,"&amp;","&");
        removeHtmlTags(xStation->service[i].serviceMessage);
        replaceWord(xStation->service[i].serviceMessage,"&amp;","&");
        replaceWord(xStation->service[i].serviceMessage,"&quot;","\"");
        fixFullStop(xStation->service[i].serviceMessage);
    }
  }

  for (int i=0;i<xMessages->numMessages;++i) {
    // Remove all non printing characters from messages...
    int j=0;
    for (int x=0; xMessages->messages[i][x] != '\0'; ++x) {
        if (isprint(xMessages->messages[i][x])) {
            xMessages->messages[i][j] = xMessages->messages[i][x];
            ++j;
        } else if (xMessages->messages[i][x] == '\n') {
            xMessages->messages[i][j] = ' ';
            ++j;
        }
    }
    xMessages->messages[i][j] = '\0';
    replaceWord(xMessages->messages[i],"&lt;","<");
    replaceWord(xMessages->messages[i],"&gt;",">");
    replaceWord(xMessages->messages[i],"<p>","");
    replaceWord(xMessages->messages[i],"</p>"," ");
    replaceWord(xMessages->messages[i],"<br>"," ");

    removeHtmlTags(xMessages->messages[i]);
    replaceWord(xMessages->messages[i],"&amp;","&");
    replaceWord(xMessages->messages[i],"&quot;","\"");
    // Remove unwanted text at the end of service messages...
    pruneFromPhrase(xMessages->messages[i]," More details ");
    pruneFromPhrase(xMessages->messages[i]," Latest information ");
    pruneFromPhrase(xMessages->messages[i]," Further information ");
    pruneFromPhrase(xMessages->messages[i]," More information can ");
    trimSpaces(xMessages->messages[i]);

    fixFullStop(xMessages->messages[i]);
  }
}

void raildataXmlClient::trimSpaces(char *text) {
    if (!text) return;

    char *read = text;
    char *write = text;
    char *last_non_space = nullptr;

    // 1. Skip leading whitespace
    while (std::isspace(static_cast<unsigned char>(*read))) {
        read++;
    }

    // 2. Shift characters to the left and track the last non-space character
    while (*read != '\0') {
        *write = *read;
        if (!std::isspace(static_cast<unsigned char>(*write))) {
            last_non_space = write;
        }
        write++;
        read++;
    }

    // 3. Null-terminate the string
    if (last_non_space) {
        *(last_non_space + 1) = '\0';
    } else {
        // The string was entirely whitespace or empty
        *text = '\0';
    }
}

void raildataXmlClient::startTag(const char *tag)
{
    tagLevel++;
    greatGrandParentTagName = grandParentTagName;
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
    grandParentTagName=greatGrandParentTagName;
    greatGrandParentTagName="??";
}

void raildataXmlClient::parameter(const char *param)
{
}

void raildataXmlClient::value(const char *value)
{
    if (loadingWDSL) return;

    if (fetchingDepartures) {

        if (tagLevel<6 || tagLevel==9 || tagLevel>11) return;

        if (tagLevel == 11 && tagPath.endsWith("callingPoint/lt8:locationName")) {
            if ((strlen(xStation->service[id].calling) + strlen(value) + 13) < sizeof(xStation->service[0].calling)) {
                // Add the calling point, add a comma prefix if this isn't the first one
                if (xStation->service[id].calling[0]) strcat(xStation->service[id].calling,", ");
                strcat(xStation->service[id].calling,value);
                addedStopLocation = true;
            }
            return;
        } else if (tagLevel == 11 && tagPath.endsWith("callingPoint/lt8:st") && addedStopLocation) {
            // check there's still room to add the eta of the calling point
            if ((strlen(xStation->service[id].calling) + strlen(value) + 4) < sizeof(xStation->service[0].calling)) {
                strcat(xStation->service[id].calling," (");
                strcat(xStation->service[id].calling,value);
                strcat(xStation->service[id].calling,")");
            }
            addedStopLocation = false;
            return;
        } else if (tagLevel == 11 && tagName == "lt7:coachClass") {
            if (strcmp(value,"First")==0) xStation->service[id].classesAvailable = xStation->service[id].classesAvailable | 1;
            else if (strcmp(value,"Standard")==0) xStation->service[id].classesAvailable = xStation->service[id].classesAvailable | 2;
            coaches++;
            return;
        } else if (tagLevel == 8 && tagName == "lt4:length") {
            xStation->service[id].trainLength = String(value).toInt();
            return;
        } else if (tagLevel == 8 && tagName == "lt4:operator") {
            strlcpy(xStation->service[id].opco,value,sizeof(xStation->service[0].opco));
            return;
        } else if (tagLevel == 8 && tagName == "lt4:serviceID") {
            strlcpy(xStation->service[id].serviceID,value,sizeof(xStation->service[0].serviceID));
            return;
        } else if (tagLevel == 10 && tagPath.startsWith("lt5:origin/lt4:location/lt4:loc")) {
            strlcpy(xStation->service[id].origin,value,sizeof(xStation->service[0].origin));
            return;
        } else if (tagLevel == 8 && tagName == "lt4:serviceType") {
            if (strcmp(value,"train")==0) xStation->service[id].serviceType = TRAIN;
            else if (strcmp(value,"bus")==0) xStation->service[id].serviceType = BUS;
            return;
        } else if (tagLevel == 8 && tagName == "lt4:std") {
            // Starting a new service
            // If we're filtering on platform numbers, check if we need to keep the previous service (if there was one)
            if (filterPlatforms && !keepRoute && id>=0) {
                // We don't want this service, so clear it
                strcpy(xStation->service[id].sTime,"");
                strcpy(xStation->service[id].destination,"");
                strcpy(xStation->service[id].via,"");
                strcpy(xStation->service[id].origin,"");
                strcpy(xStation->service[id].etd,"");
                strcpy(xStation->service[id].platform,"");
                strcpy(xStation->service[id].opco,"");
                strcpy(xStation->service[id].calling,"");
                strcpy(xStation->service[id].serviceMessage,"");
                xStation->service[id].trainLength=0;
                xStation->service[id].classesAvailable=0;
                xStation->service[id].serviceType=0;
                xStation->service[id].isCancelled=false;
                xStation->service[id].isDelayed=false;
                xStation->numServices--;
                id--;
            }
            keepRoute = false;  // reset for next route
            if (id>=0) {
                if (xStation->service[id].trainLength == 0) xStation->service[id].trainLength = coaches;
            }
            coaches=0;
            if (id < MAXBOARDSERVICES-1) {
                id++;
                xStation->numServices++;
            }
            strlcpy(xStation->service[id].sTime,value,sizeof(xStation->service[0].sTime));
            return;
        } else if (tagLevel == 8 && tagName == "lt4:etd") {
            strlcpy(xStation->service[id].etd,value,sizeof(xStation->service[0].etd));
            return;
        } else if (tagLevel == 10 && tagPath.startsWith("lt5:destination/lt4:location/lt4:lo")) {
            strlcpy(xStation->service[id].destination,value,sizeof(xStation->service[0].destination));
            return;
        } else if (tagLevel == 10 && tagPath == "lt5:destination/lt4:location/lt4:via") {
            strlcpy(xStation->service[id].via,value,sizeof(xStation->service[0].via));
            return;
        } else if (tagLevel == 8 && tagName == "lt4:delayReason") {
            strlcpy(xStation->service[id].serviceMessage,value,sizeof(xStation->service[0].serviceMessage));
            xStation->service[id].isDelayed = true;
            return;
        } else if (tagLevel == 8 && tagName == "lt4:cancelReason") {
            strlcpy(xStation->service[id].serviceMessage,value,sizeof(xStation->service[0].serviceMessage));
            xStation->service[id].isCancelled = true;
            return;
        } else if (tagLevel == 8 && tagName == ("lt4:platform")) {
            strlcpy(xStation->service[id].platform,value,sizeof(xStation->service[0].platform));
            if (filterPlatforms && serviceMatchesFilter(platformFilter,xStation->service[id].platform)) keepRoute=true;
            return;
        } else if (tagLevel == 6 && tagName == "lt4:locationName") {
            strlcpy(xStation->location,value,sizeof(xStation->location));
            return;
        } else if (tagLevel == 6 && tagName == "lt4:platformAvailable") {
            if (strcmp(value,"true")==0) xStation->platformAvailable = true;
            return;
        } else if (tagPath.endsWith("nrccMessages/lt:message")) {    // tagLevel 7
            if (xMessages->numMessages < MAXBOARDMESSAGES) {
                xMessages->numMessages++;
                strlcpy(xMessages->messages[xMessages->numMessages-1],value,sizeof(xMessages->messages[0]));
            }
            return;
        }
    } else {
        // Loading service details
        if (tagLevel == 9 && greatGrandParentTagName.endsWith("previousCallingPoints")) {
            if (tagPath.endsWith("callingPoint/lt8:locationName")) {
                // Next location, save the previous one if it has an actual time
                if (thisLocation.actualTime[0]) {
                    strcpy(lastLocation.location,thisLocation.location);
                    strcpy(lastLocation.actualTime,thisLocation.actualTime);
                    strcpy(lastLocation.scheduledTime,thisLocation.scheduledTime);
                }
                strlcpy(thisLocation.location,value,MAXLOCATIONSIZE);
                thisLocation.actualTime[0]='\0';
                thisLocation.scheduledTime[0]='\0';
                return;
            } else if (tagPath.endsWith("callingPoint/lt8:st")) {
                strlcpy(thisLocation.scheduledTime,value,sizeof(thisLocation.scheduledTime));
                return;
            } else if (tagPath.endsWith("callingPoint/lt8:at")) {
                strlcpy(thisLocation.actualTime,value,sizeof(thisLocation.actualTime));
                return;
            }
        }
    }
}

void raildataXmlClient::attribute(const char *attr)
{
    if (loadingWDSL) {
        if (tagName == "soap:address") {
            String myURL = String(attr);
            if (myURL.startsWith("location=\"") && myURL.endsWith("\"")) {
                soapURL = myURL.substring(10,myURL.length()-1);
            }
        }
    }
}
