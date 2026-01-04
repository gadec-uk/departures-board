/*
 * HTTPUpdateGitHub Library (c) 2025-2026 Gadec Software
 *  - based on orignal HTTPUpdate for ESP32 by Markus Sattler
 *  - Added ability to use GitHub token for updating from private repositories
 *  - Added correct handling of redirects
 *  - Added correct checking of MD5 hash from GitHub server
 *  - Removed redundant code
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

 #include <HTTPUpdateGitHub.h>
 #include <StreamString.h>

 #include <esp_partition.h>
 #include <esp_ota_ops.h>                // get running partition
 #include <md5Utils.h>

 HTTPUpdate::HTTPUpdate(void)
         : _httpClientTimeout(8000)
 {
 }

 HTTPUpdate::HTTPUpdate(int httpClientTimeout)
         : _httpClientTimeout(httpClientTimeout)
 {
 }

 HTTPUpdate::~HTTPUpdate(void)
 {
 }

 /**
  * return error code as int
  */
 int HTTPUpdate::getLastError(void)
 {
     return _lastError;
 }

 /**
  * return error code as String
  */
 String HTTPUpdate::getLastErrorString(void)
 {

     if(_lastError == 0) {
         return String(); // no error
     }

     // error from Update class
     if(_lastError > 0) {
         StreamString error;
         Update.printError(error);
         error.trim(); // remove line ending
         return String("Update error: ") + error;
     }

     // error from http client
     if(_lastError > -100) {
         return String("HTTP error: ") + HTTPClient::errorToString(_lastError);
     }

     switch(_lastError) {
     case HTTP_UE_TOO_LESS_SPACE:
         return "Not Enough space";
     case HTTP_UE_SERVER_NOT_REPORT_SIZE:
         return "Server Did Not Report Size";
     case HTTP_UE_SERVER_FILE_NOT_FOUND:
         return "File Not Found (404)";
     case HTTP_UE_SERVER_FORBIDDEN:
         return "Forbidden (403)";
     case HTTP_UE_SERVER_WRONG_HTTP_CODE:
         return "Wrong HTTP Code";
     case HTTP_UE_SERVER_FAULTY_MD5:
         return "Wrong MD5";
     case HTTP_UE_BIN_VERIFY_HEADER_FAILED:
         return "Verify Bin Header Failed";
     case HTTP_UE_BIN_FOR_WRONG_FLASH:
         return "New Binary Does Not Fit Flash Size";
     case HTTP_UE_NO_PARTITION:
         return "Partition Could Not be Found";
     }

     return String();
 }

 /*
  * HTTPUpdate is now exposed directly from the library.
  *  - optionally pass GitHub token for downloads from private repository.
  *  - handles redirects correctly.
  */
  HTTPUpdateResult HTTPUpdate::handleUpdate(WiFiClient& client, const String& uri = "/", const String& token = "") {

     HTTPUpdateResult ret = HTTP_UPDATE_FAILED;

     HTTPClient http;
     int redirectCount = 0;
     const int maxRedirects = 5;
     String url = uri;
     String md5Hex = "";

     while (redirectCount < maxRedirects) {

        log_d("URL: %s\n",url.c_str());
        if(!http.begin(client, url))
        {
            return HTTP_UPDATE_FAILED;
        }

        // use HTTP/1.0 for update since the update handler does not support any transfer Encoding
        http.useHTTP10(true);
        http.setTimeout(_httpClientTimeout);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);    // Disable redirects, they don't work anyway
        http.setUserAgent("ESP32-http-Update");
        http.addHeader("Cache-Control", "no-cache");
        // Additional headers for GitHub
        http.addHeader("Accept", "application/octet-stream");
        if (token.length()) http.addHeader("Authorization", "Bearer " + token);
        http.addHeader("X-GitHub-Api-Version", "2022-11-28");

        const char * headerkeys[] = { "x-ms-blob-content-md5" };    // GitHub uses x-ms-blob-content-m5d, not x-md5
        size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);

        // track the MD5 hash
        http.collectHeaders(headerkeys, headerkeyssize);

        int code = http.GET();
        int len = http.getSize();
        log_d("HTTP GET result %d\n",code);

        if (code == HTTP_CODE_MOVED_PERMANENTLY ||
            code == HTTP_CODE_FOUND ||
            code == HTTP_CODE_TEMPORARY_REDIRECT ||
            code == HTTP_CODE_PERMANENT_REDIRECT) {
            // Handle redirect
            String newUrl = http.getLocation();
            log_d("[HTTP] Redirected to: %s\n", newUrl.c_str());
            http.end();  // End current request before retrying
            if (newUrl.length() == 0) {
                log_e("[HTTP] Redirect without Location header!");
                return HTTP_UPDATE_FAILED;
            }
            url = newUrl;
            redirectCount++;
        } else {
            if(code <= 0) {
                log_e("HTTP error: %s\n", http.errorToString(code).c_str());
                _lastError = code;
                http.end();
                return HTTP_UPDATE_FAILED;
            }

            log_d("Header read fin.\n");
            log_d("Server header:\n");
            log_d(" - code: %d\n", code);
            log_d(" - len: %d\n", len);

            if(http.hasHeader("x-ms-blob-content-md5")) {
                log_d(" - MD5: %s\n", http.header("x-ms-blob-content-md5").c_str());
                // Convert the base64 encoded MD5 back to a hex string
                md5Hex = md5.base64ToHex(String(http.header("x-ms-blob-content-md5")));
                log_d(" - MD5 Hex: %s\n", md5Hex.c_str());
            }

            log_d("ESP32 info:\n");
            log_d(" - free Space: %d\n", ESP.getFreeSketchSpace());
            log_d(" - current Sketch Size: %d\n", ESP.getSketchSize());

            switch(code) {
            case HTTP_CODE_OK:  ///< OK (Start Update)
                if(len > 0) {
                    bool startUpdate = true;
                    int sketchFreeSpace = ESP.getFreeSketchSpace();
                    if(!sketchFreeSpace){
                        _lastError = HTTP_UE_NO_PARTITION;
                        return HTTP_UPDATE_FAILED;
                    }

                    if(len > sketchFreeSpace) {
                        log_e("FreeSketchSpace to low (%d) needed: %d\n", sketchFreeSpace, len);
                        startUpdate = false;
                    }

                    if(!startUpdate) {
                        _lastError = HTTP_UE_TOO_LESS_SPACE;
                        ret = HTTP_UPDATE_FAILED;
                    } else {
                        // Warn main app we're starting up...
                        if (_cbStart) {
                            _cbStart();
                        }

                        WiFiClient * tcp = http.getStreamPtr();
                        delay(200);

                        int command;
                        command = U_FLASH;
                        log_d("runUpdate flash...\n");

                        // check for valid first magic byte
                        if(tcp->peek() != 0xE9) {
                            log_e("Magic header does not start with 0xE9, starts with %d\n",tcp->peek());
                            _lastError = HTTP_UE_BIN_VERIFY_HEADER_FAILED;
                            http.end();
                            return HTTP_UPDATE_FAILED;
                        }

                        if(runUpdate(*tcp, len, md5Hex, command)) {
                            ret = HTTP_UPDATE_OK;
                            log_d("Update ok\n");
                            http.end();
                            // Warn main app we're all done
                            if (_cbEnd) {
                                _cbEnd();
                            }

                            if(_rebootOnUpdate) {
                                ESP.restart();
                            }

                        } else {
                            ret = HTTP_UPDATE_FAILED;
                            log_e("Update failed\n");
                        }
                    }
                } else {
                    _lastError = HTTP_UE_SERVER_NOT_REPORT_SIZE;
                    ret = HTTP_UPDATE_FAILED;
                    log_e("Content-Length was 0 or wasn't set by Server?!\n");
                }
                break;
            case HTTP_CODE_NOT_MODIFIED:
                ///< Not Modified (No updates)
                ret = HTTP_UPDATE_NO_UPDATES;
                break;
            case HTTP_CODE_NOT_FOUND:
                _lastError = HTTP_UE_SERVER_FILE_NOT_FOUND;
                ret = HTTP_UPDATE_FAILED;
                break;
            case HTTP_CODE_FORBIDDEN:
                _lastError = HTTP_UE_SERVER_FORBIDDEN;
                ret = HTTP_UPDATE_FAILED;
                break;
            default:
                _lastError = HTTP_UE_SERVER_WRONG_HTTP_CODE;
                ret = HTTP_UPDATE_FAILED;
                log_e("HTTP Code is (%d)\n", code);
                break;
            }

            http.end();
            return ret;
        }
    }
    log_e("Too many redirects!");
    return HTTP_UPDATE_FAILED;
}

 /**
  * write Update to flash
  * @param in Stream&
  * @param size uint32_t
  * @param md5 String
  * @return true if Update ok
  */
 bool HTTPUpdate::runUpdate(Stream& in, uint32_t size, String md5, int command)
 {
      StreamString error;

     if (_cbProgress) {
         Update.onProgress(_cbProgress);
     }

     if(!Update.begin(size, command)) {
         _lastError = Update.getError();
         Update.printError(error);
         error.trim(); // remove line ending
         log_e("Update.begin failed! (%s)\n", error.c_str());
         return false;
     }

     if (_cbProgress) {
         _cbProgress(0, size);
     }

     if(md5.length()) {
         if(!Update.setMD5(md5.c_str())) {
             _lastError = HTTP_UE_SERVER_FAULTY_MD5;
             log_e("Update.setMD5 failed! (%s)\n", md5.c_str());
             return false;
         }
     }

     if(Update.writeStream(in) != size) {
         _lastError = Update.getError();
         Update.printError(error);
         error.trim(); // remove line ending
         log_e("Update.writeStream failed! (%s)\n", error.c_str());
         return false;
     }

     if (_cbProgress) {
         _cbProgress(size, size);
     }

     if(!Update.end()) {
         _lastError = Update.getError();
         Update.printError(error);
         error.trim(); // remove line ending
         log_e("Update.end failed! (%s)\n", error.c_str());
         return false;
     }

     return true;
 }

 #if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_HTTPUPDATE)
 HTTPUpdate httpUpdate;
 #endif
