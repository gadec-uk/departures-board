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

github::github(String token) {
    accessToken = token;            // Initialise with a GitHub token if the repository is private
}

bool github::getLatestRelease() {

    lastErrorMsg = "";
    JsonStreamingParser parser;
    parser.setListener(this);
    WiFiClientSecure httpsClient;

    httpsClient.setInsecure();
    httpsClient.setTimeout(15000);

    int retryCounter=0; //retry counter
    while((!httpsClient.connect(apiHost, 443)) && (retryCounter < 30)){
        delay(200);
        retryCounter++;
    }
    if(retryCounter>=30) {
        lastErrorMsg += F("Connection timeout");
        return false;
    }

    String request = "GET "+ String(apiGetLatestRelease) + F(" HTTP/1.0\r\nHost: ") + String(apiHost) + F("\r\nuser-agent: esp32/1.0\r\nX-GitHub-Api-Version: 2022-11-28\r\nAccept: application/vnd.github+json\r\n");
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
        delay(50);
    }
    httpsClient.stop();
    if (millis() >= dataSendTimeout) {
        lastErrorMsg += "Data timeout (" + String(dataReceived) + F(" bytes)");
        return false;
    }

    lastErrorMsg=F("SUCCESS");

    return true;
}

/*
bool github::downloadAssetToLittleFS(String url, String filename) {

    HTTPClient http;
    WiFiClientSecure client;
    bool result = true;
    int redirectCount = 0;
    const int maxRedirects = 5;

    lastErrorMsg = "";
    LittleFS.remove(F("/tempfile"));  // delete any leftover temp file

    client.setInsecure();

    File f = LittleFS.open(F("/tempfile"), "w");
    if (!f) {
        lastErrorMsg = F("Failed to create temp file");
        return false;
    }

    while (redirectCount < maxRedirects) {
        http.begin(client, url);
        http.addHeader(F("Accept"), F("application/octet-stream"));
        if (accessToken.length()) http.addHeader(F("Authorization"), "Bearer " + accessToken);
        http.addHeader(F("X-GitHub-Api-Version"), F("2022-11-28"));
        http.addHeader(F("user-agent"), F("esp32/1.0"));
        const char * headerkeys[] = { "x-ms-blob-content-md5" };    // GitHub uses x-ms-blob-content-m5d, not x-md5
        size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
        // track the MD5 hash
        http.collectHeaders(headerkeys, headerkeyssize);

        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            int res = http.writeToStream(&f);
            if (res <= 0) {
                lastErrorMsg = "WriteToStream failed. Error: " + http.errorToString(res);
                result = false;
            }
            http.end();
            break;
        } else if (httpCode == HTTP_CODE_MOVED_PERMANENTLY ||
                   httpCode == HTTP_CODE_FOUND ||
                   httpCode == HTTP_CODE_TEMPORARY_REDIRECT ||
                   httpCode == HTTP_CODE_PERMANENT_REDIRECT) {
            // Handle redirect
            String newUrl = http.getLocation();
            http.end();  // End current request before retrying
            if (newUrl.length() == 0) {
                lastErrorMsg = F("HTTP Redirect without Location header!");
                result = false;
                break;
            }
            url = newUrl;
            redirectCount++;
        } else {
            lastErrorMsg = "GET failed, error: " + String(httpCode) + " " + http.errorToString(httpCode);
            result = false;
            http.end();
            break;
        }
    }

    f.close();

    if (result) {
        // File downloaded, let's check the MD5 if provided
        if (http.hasHeader("x-ms-blob-content-md5")) {
            // Convert the base64 encoded MD5 back to a hex string
            String md5Server = md5.base64ToHex(String(http.header("x-ms-blob-content-md5")));
            // Calculate the MD5 of the downloaded file
            String md5Download = md5.calculateFileMD5("/tempfile");
            if (md5Download != md5Server) {
                lastErrorMsg = "\"" + filename.substring(1) + F("\" MD5 mismatch (corruption)");
                return false;
            }
        }

        // File downloaded so delete the old one
        if (LittleFS.exists(filename)) {
            if (!LittleFS.remove(filename)) {
                lastErrorMsg = "Could not delete existing " + filename;
                return false;
            }
        }

        // Rename temp file
        if (!LittleFS.rename("/tempfile", filename)) {
            lastErrorMsg = F("Could not rename temp file");
            return false;
        }
        LittleFS.remove(F("/tempfile"));
        lastErrorMsg = F("Success");
    }

    return result;
}
*/

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
