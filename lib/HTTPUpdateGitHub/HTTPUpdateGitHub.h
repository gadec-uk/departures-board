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

 #ifndef ___HTTP_UPDATE_H___
 #define ___HTTP_UPDATE_H___

 #include <Arduino.h>
 #include <WiFi.h>
 #include <WiFiClient.h>
 #include <WiFiUdp.h>
 #include <HTTPClient.h>
 #include <Update.h>
 #include <md5Utils.h>

 /// note we use HTTP client errors too so we start at 100
 #define HTTP_UE_TOO_LESS_SPACE              (-100)
 #define HTTP_UE_SERVER_NOT_REPORT_SIZE      (-101)
 #define HTTP_UE_SERVER_FILE_NOT_FOUND       (-102)
 #define HTTP_UE_SERVER_FORBIDDEN            (-103)
 #define HTTP_UE_SERVER_WRONG_HTTP_CODE      (-104)
 #define HTTP_UE_SERVER_FAULTY_MD5           (-105)
 #define HTTP_UE_BIN_VERIFY_HEADER_FAILED    (-106)
 #define HTTP_UE_BIN_FOR_WRONG_FLASH         (-107)
 #define HTTP_UE_NO_PARTITION                (-108)

 enum HTTPUpdateResult {
     HTTP_UPDATE_FAILED,
     HTTP_UPDATE_NO_UPDATES,
     HTTP_UPDATE_OK
 };

 typedef HTTPUpdateResult t_httpUpdate_return; // backward compatibility

 using HTTPUpdateStartCB = std::function<void()>;
 using HTTPUpdateRequestCB = std::function<void(HTTPClient*)>;
 using HTTPUpdateEndCB = std::function<void()>;
 using HTTPUpdateErrorCB = std::function<void(int)>;
 using HTTPUpdateProgressCB = std::function<void(int, int)>;

 class HTTPUpdate
 {
 public:
     HTTPUpdate(void);
     HTTPUpdate(int httpClientTimeout);
     ~HTTPUpdate(void);

     void rebootOnUpdate(bool reboot)
     {
         _rebootOnUpdate = reboot;
     }

     HTTPUpdateResult handleUpdate(WiFiClient& client, const String& uri, const String& token);

     // Notification callbacks
     void onStart(HTTPUpdateStartCB cbOnStart)          { _cbStart = cbOnStart; }
     void onEnd(HTTPUpdateEndCB cbOnEnd)                { _cbEnd = cbOnEnd; }
     void onError(HTTPUpdateErrorCB cbOnError)          { _cbError = cbOnError; }
     void onProgress(HTTPUpdateProgressCB cbOnProgress) { _cbProgress = cbOnProgress; }

     int getLastError(void);
     String getLastErrorString(void);

 protected:

     bool runUpdate(Stream& in, uint32_t size, String md5, int command = U_FLASH);

     // Set the error and potentially use a CB to notify the application
     void _setLastError(int err) {
         _lastError = err;
         if (_cbError) {
             _cbError(err);
         }
     }
     int _lastError;
     bool _rebootOnUpdate = true;

 private:
     int _httpClientTimeout;
     md5Utils md5;

     // Callbacks
     HTTPUpdateStartCB    _cbStart;
     HTTPUpdateEndCB      _cbEnd;
     HTTPUpdateErrorCB    _cbError;
     HTTPUpdateProgressCB _cbProgress;
 };

 #if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_HTTPUPDATE)
 extern HTTPUpdate httpUpdate;
 #endif

 #endif /* ___HTTP_UPDATE_H___ */
