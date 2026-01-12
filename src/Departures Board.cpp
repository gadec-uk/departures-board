/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 *
 * ESP32 "Mini" Board with 3.12" 256x64 OLED Display Panel with SSD1322 controller on-board.
 *
 * OLED PANEL     ESP32 MINI
 * 1 VSS          GND
 * 2 VCC_IN       3.3V
 * 4 D0/CLK       IO18
 * 5 D1/DIN       IO23
 * 14 D/C#        IO5
 * 16 CS#         IO26
 *
 */

// Release version number
#define VERSION_MAJOR 1
#define VERSION_MINOR 9

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <HTTPUpdateGitHub.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <weatherClient.h>
#include <stationData.h>
#include <raildataXmlClient.h>
#include <TfLdataClient.h>
#include <busDataClient.h>
#include <githubClient.h>
#include <webgui/webgraphics.h>
#include <webgui/index.h>
#include <webgui/keys.h>
#include <gfx/xbmgfx.h>

#include <time.h>

#include <SPI.h>
#include <U8g2lib.h>

#define msDay 86400000 // 86400000 milliseconds in a day
#define msHour 3600000 // 3600000 milliseconds in an hour
#define msMin 60000 // 60000 milliseconds in a second

WebServer server(80);     // Hosting the Web GUI
File fsUploadFile;        // File uploads

// Shorthand for response formats
static const char contentTypeJson[] PROGMEM = "application/json";
static const char contentTypeText[] PROGMEM = "text/plain";
static const char contentTypeHtml[] PROGMEM = "text/html";

// Using NTP to set and maintain the clock
static const char ntpServer[] PROGMEM = "europe.pool.ntp.org";
static struct tm timeinfo;

// Local firmware updates via /update Web GUI
static const char updatePage[] PROGMEM =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<html><body><h2>Departures Board Manual Update</h2><p>Upload a <b>firmware.bin</b> file.</p>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('Progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')"
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script></body></html>";

// /upload page
static const char uploadPage[] PROGMEM =
"<html><body>"
"<h2>Upload a file to the file system</h2><form method='post' enctype='multipart/form-data'><input type='file' name='name'>"
"<input class='button' type='submit' value='Upload'></form></body></html>";

// /success page
static const char successPage[] PROGMEM =
"<html><body><h3>Upload completed successfully.</h3>\n"
"<p><a href=\"/dir\">List file system directory</a></p>\n"
"<h2>Upload another file</h2><form method=\"post\" action=\"/upload\" enctype=\"multipart/form-data\"><input type=\"file\" name=\"name\"><input class=\"button\" type=\"submit\" value=\"Upload\"></form>\n"
"</body></html>";

#define SCREEN_WIDTH 256 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define DIMMED_BRIGHTNESS 1 // OLED display brightness level when in sleep/screensaver mode

U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 26, /* dc=*/ 5, /* reset=*/ U8X8_PIN_NONE);

// Vertical line positions on the OLED display (National Rail)
#define LINE0 0
#define LINE1 13
#define LINE2 28
#define LINE3 41
#define LINE4 55

// Vertical line positions on the OLED display (Underground)
#define ULINE0 0
#define ULINE1 15
#define ULINE2 28
#define ULINE3 41
#define ULINE4 56

//
// Custom fonts - replicas of those used on the real display boards
//
static const uint8_t NatRailSmall9[985] U8G2_FONT_SECTION("NatRailSmall9") =
  "b\0\3\2\3\4\2\5\5\7\11\0\0\11\0\11\2\1\71\2r\3\300 \5\0\63\5!\7\71\245"
  "\304\240\4\42\7\33-Eb\11#\16=\245M)I\6\245\62(\245$\1$\14=\245U\266\324\266"
  "$\331\42\0%\14<eE\244H\221\24)R\0&\15=\245\215T\222\222DJ\242H\11'\6\31"
  "\255\304\0(\10;%UR\252\25)\11;%EV\252\224\0*\12-\247ERY,K\3+\12"
  "-\247U\30\15R\30\1,\7\32\341L\242\0-\6\13+\305\0.\6\11\245D\0/\13<e]"
  "$ER$e\0\60\12=\245\315\222yK\26\0\61\10\273\245M\42u\31\62\12=\245\315\222\205Y"
  "\333 \63\14=\245\315\222\205\221\252%\13\0\64\14=\245]&%\245d\320\302\4\65\13=\245\305q"
  "HC-Y\0\66\14=\245\315\222\211C\222i\311\2\67\12=\245\305 f\305&\0\70\14=\245\315"
  "\222i\311\222i\311\2\71\14=\245\315\222i\311\20j\311\2:\6!\247D\24;\7*\345L\252\0"
  "<\10<e]\324\330\0=\10\34i\305\20\16\1>\10<eE\330\324\6?\14=\245\315\222\205\221"
  "\226C\21\0@\14=\245\315\222Y\22eH\27\0A\13=\245\315\222i\303\220\331\2B\15=\245\305"
  "\220d\332\240d\332\240\0C\12=\245\315\222\211m\311\2D\12=\245\305\220d\336\6\5E\13=\245"
  "\305\61\34\222\60\34\4F\12=\245\305\61\34\222\260\10G\14=\245\315\222\211\311\220i\311\2H\12="
  "\245Ef\33\206\314\26I\10;%\305\22u\31J\11=\245eG-Y\0K\15=\245E&%%"
  "-\211*Y\0L\10=\245E\330\343 M\12=\245E\266,\211\346\26N\13=\245E\66)\211\264"
  "\331\2O\12=\245\315\222yK\26\0P\14=\245\305\220d\332\240\204E\0Q\12M\241\315\222yK"
  "\306\64R\15=\245\305\220d\332\240\224*Y\0S\13=\245\315\222\251\253\226,\0T\11=\245\305 "
  "\205=\1U\11=\245E\346[\262\0V\12=\245E\346-\251E\0W\12=\245E\346\222(\311-"
  "X\13=\245E\246%\265JM\13Y\12=\245E\246%\265\260\11Z\12=\245\305 f\35\7\1["
  "\7:\345\304\322E\134\12<eE\246eZ\246\5]\7:e\206\322e^\6\23/M\3_\6\14"
  "c\305\20`\6\22\357D\24a\12-\245\315\232\14Z\62\4b\13=\245EX\61i\332\240\0c\11"
  "-\245\315 V\207\0d\12=\245e\305\264i\311\20e\12-\245\315\222\15C:\4f\12<eU"
  "\245\64e%\0g\14=\241\315\240\331\222!\34\24\0h\12=\245EX\61i\266\0i\10;%M"
  "(\265\14j\13La]\16dmR\242\0k\13<eEVR\22))\5l\10;%\205\324\313"
  "\0m\12-\245M\27%\321\264\0n\11-\245Eb\322l\1o\11-\245\315\222\331\222\5p\14="
  "\241\305\220d\266A\11C\0q\12=\241\315\240\331\222!,r\11-\245Eb\22\213\0s\11-\245"
  "\315\240\36\24\0t\12<eM\26\15IV\24u\11-\245E\346\244(\1v\12-\245EfKj"
  "\21\0w\13-\245E\246$J\242t\1x\12-\245E\226\324*\265\0y\13=\241E\346\226\14\341"
  "\240\0z\11-\245\305\240\265\15\2{\17G#\206\266,=E\26\65\311\222d\11|\6\71\245\304!"
  "}\6\15\253\305 ~\15>\345\315\220\204\306\341!\211\22\0\15=\245E\222U\212Q\22%J\1"
  "\200\11$k\215\22I\211\2\201\14\265%^\62hQ\66(\31\0\0\0\0";

static uint8_t NatRailTall12[1064] U8G2_FONT_SECTION("NatRailTall12") =
  "a\0\3\2\4\4\2\5\5\11\14\0\375\11\375\11\0\1Q\2\235\4\17 \5\0\346\12!\7\221B"
  "\211C\22\42\7#^\212D\11#\21\225B\233R\222\14J)\211\222dPJI\2$\17\225B\253"
  "l)%\331\226DI\262E\0%\12\225B\313i\312:\35\1&\20\225B\33\251\22%Q$%\211"
  "\224D\221\22'\6!^\11\1(\11\223B\252\244\324\255\0)\11\223B\212\254\324\245\4*\14uF"
  "\253Je\261,M\21\0+\12UJ\253\60\32\244\60\2,\7\62\272\231D\1-\6\23R\212\1."
  "\6!B\11\1/\11\225B\313\266\216E\0\60\12\225B\233%\363[\262\0\61\11\223C\233D\352\313"
  "\0\62\13\225B\233%\13k\35\7\1\63\15\225B\233%\13Kj\250%\13\0\64\16\225B\273LJ"
  "JI\224\14ZX\1\65\15\225B\213c\70\244a\250%\13\0\66\15\225B\233%\23\303!\311l\311"
  "\2\67\15\225B\213A\254\205Y\230\205\31\0\70\15\225B\233%\263%KfK\26\0\71\15\225B\233"
  "%\263%C\30j\311\2:\6QJ\11E;\10b\306\231Z\242\0<\10t\306\272\250\261\1=\10"
  "\64\316\212!\34\2>\10t\306\212\260\251\15?\14\225B\233%\13k\305\34\212\0@\16\225B\233%"
  "\263$J\242\14a\272\0A\13\225B\233%\263\15C\346\26B\15\225B\213!\311l\203\222\331\6\5"
  "C\13\225B\233%\23{K\26\0D\13\225B\213!\311\374\66(\0E\13\225B\213cqH\302\342"
  " F\13\225B\213cqH\302F\0G\14\225B\233%\23K\233-Y\0H\13\225B\213\314m\30"
  "\62\267\0I\6\221B\211\7J\11\225B\313>j\311\2K\16\225B\213\60\223\222\222\246%Q%\13"
  "L\11\225B\213\260\37\7\1M\14\225B\213lY\22%\321\274\5N\15\225B\213l\232\224DI\244"
  "\233\26O\12\225B\233%\363[\262\0P\14\225B\213!\311l\203\22\66\2Q\13\265:\233%\363["
  "\62\246\1R\16\225B\213!\311l\203R\252dZ\0S\14\225B\233%S\325\242\226,\0T\11\225"
  "B\213A\12\373\11U\11\225B\213\314\337\222\5V\12\225B\213\314oI-\2W\13\225B\213\314\227"
  "DIn\1X\16\225B\213LKJIV\211\222\232\26Y\14\225B\213LKJI\26v\2Z\13"
  "\225B\213A\254u\14\7\1[\10\222\302\211\245/\2\134\11\225B\213\260\332\261\0]\10\222\302\11\245"
  "/\3^\6#^\232\6_\6\25>\213A`\6\42\336\211(a\13eB\233\65\31\64-\31\2b"
  "\14\225B\213\260\305\244i\223\242\0c\11eB\233Al\35\2d\13\225B\313\26\323fR\224\0e"
  "\14eB\233%\33\206\60K\26\0f\13\224\302*%\213\206$\353\4g\15\225\66\33\323fR\224P"
  "K\26\0h\12\225B\213\260\305\244\271\5i\7\201B\211d\30j\13\264\266\272\34\310z\223\22\5k"
  "\15\225B\213\260))iIT\311\2l\6\221B\211\7m\15eB\13\245EI\224DI\224\2n"
  "\11eB\213\304\244\271\5o\12eB\233%sK\26\0p\15\225\66\213\304\244i\223\242\204E\0q"
  "\13\225\66\33\323fR\224\260\1r\11eB\213\304$\66\2s\12eB\233%]\265d\1t\12\204"
  "\302\232,\32\222\254Qu\11eB\213\314\223\242\4v\14eB\213LKJI\26F\0w\16eB"
  "\213$Q\22%Q\22\245\13\0x\13eB\213,\251\205YR\13y\14\225\66\213\314\223\242\204Z\262"
  "\0z\12eB\213A\314\332\6\1{\21\227B\234,\321\226\245\247\310\242&Y\222,\1|\6\221B"
  "\213\7}\17\266\66\273\70\31\306:\26\206\303\22g\0~\21\226\302\233!\31\206P\34\36\24e\30\222"
  "(\1\16\226\302\213$\314\224(\315\222,\351?\200\24\231B\255AK\223L\222B)\224BMJ"
  "\322l\220\0\0\0\0";

static const uint8_t NatRailClockSmall7[137] U8G2_FONT_SECTION("NatRailClockSmall7") =
  "\12\0\3\3\3\3\3\1\5\7\7\0\0\7\0\7\0\0\0\0\0\0p\60\12?\343Td\274I*"
  "\0\61\10\274\343HF\272\20\62\13?\343TdRIE*=\63\14?\343TdR\331\230&\251\0"
  "\64\14?\343\315H\22%\311Q*\1\65\13?c\34\244f)MR\1\66\14?\343TdT\213\214"
  "&\251\0\67\11?c\134\205Z\325\0\70\15?\343Td\64IEF\223T\0\71\14?\343Td\64"
  "\211\225&\251\0\0\0\0";

static const uint8_t NatRailClockLarge9[177] U8G2_FONT_SECTION("NatRailClockLarge9") =
  "\13\0\4\3\4\4\3\2\5\11\11\0\0\11\0\11\0\0\0\0\0\0\230\60\13\231T\307\205\24\277\222"
  "\270\0\61\11\224W\207\304\210\276 \62\16\231T\307\205\224\234\212\13\71u\7\2\63\17\231T\307\205\224"
  "\234\232B\71*\211\13\0\64\23\231T\327\24\221\204\214\210\32\11!\211\3\61\71\11\0\65\20\231T\307"
  "\301\234\334A\240\34\25\225\304\5\0\66\20\231T\307\205\24\235\334A\204\24+\211\13\0\67\14\231T\303"
  "\201\234\252Ur\32\1\70\17\231T\307\205\24+\211\13)V\22\27\0\71\20\231T\307\205\24+\211\203"
  "\70\71*\211\13\0:\7r\235\2\31\1\0\0\0";

static const uint8_t Underground10[1072] U8G2_FONT_SECTION("Underground10") =
"`\0\3\2\3\4\3\5\5\7\12\0\377\11\377\11\0\1Y\2\300\4\27 \5\0\346\12!\7IB"
"\211C\22\42\7\23^\212D\11#\21MB\233R\222\14J)\211\222dPJI\2$\17MB\253"
"l)%\331\226DI\262E\0%\12MB\313i\312:\35\1&\17F\302\33-j\323\242DK\242"
"$\221\2'\10\42\326\211!Q\0(\10J\302\31\245O\1)\11J\302\211(\351E\1*\14=F"
"\253J\345\240,M\21\0+\12-J\253\60\32\244\60\2,\10\42\276\211!Q\0-\6\15R\213A"
".\6\22\302\211!/\10?\306\353\264\317\0\60\14N\302\233!\11\375\230\14\11\0\61\7J\303\233\245"
"\37\62\13N\302\233!\11\323b\307a\63\16N\302\233!\11\323\322\234\212\311\220\0\64\16N\302\313P"
"K\242J\226\14cZ\1\65\16N\302\213C\232\16r\232\212\311\220\0\66\17N\302\233!\11\325tP"
"Bc\62$\0\67\11N\302\213k\261\327\24\70\17N\302\233!\11\215\311\220\204\306dH\0\71\17N"
"\302\233!\11\215\311\240\246b\62$\0:\10\62\306\211!\34\2;\11:\302\211!T\24\0<\10<"
"\306\272\250\261\1=\10\34\316\212!\34\2>\10<\306\212\260\251\15?\15MB\233%\323\302\254\230C"
"\21\0@\16MB\233%\263$J\242\14a\272\0A\14N\302\233!\11\215\303 :\6B\15N\302"
"\213A\11\215\303\22\32\207\5C\14N\302\233!\11\325\36\223!\1D\13N\302\213A\11\375\70,\0"
"E\14N\302\213CZ\35\206\264:\14F\13N\302\213CZ\35\206\264\25G\16N\302\233!\11\325\312"
" \32\223!\1H\13N\302\213\320q\30D\307\0I\11KB\212%\352\313\0J\12MB\313\36\65"
"-Y\0K\20N\302\213PK\242J&&YTK\302\0L\11N\302\213\264_\207\1M\16OB"
"\214t[*R$E\252k\0N\15N\302\213P\334\224HJ\264\321\30O\14N\302\233!\11\375\230"
"\14\11\0P\14N\302\213A\11\215\303\222\266\2Q\14V\276\233!\11\375\224$C\34R\16N\302\213"
"A\11\215\303R\213jI\30S\17N\302\233!\11\325x\210S\61\31\22\0T\12OB\214C\26\367"
"\33\0U\12N\302\213\320\37\223!\1V\14OB\214\324\327$\253\244\31\0W\16OB\214\324S$"
"ER\244tK\0X\16OB\214TM\262JZ\311*\251\32Y\14OB\214\64\311*i\334\33\0"
"Z\13OB\214C\234\366y\30\2[\10J\302\211\245/\2\134\11MB\213\260\332\261\0]\10J\302"
"\11\245/\3^\6\23^\232\6_\6\15>\213A`\7\42\326\211A\12a\13\65F\233\65\31\64-"
"\31\2b\14EF\213\60\34\222\314mP\0c\12\65F\233%\23k\311\2d\13EF\313\312\240\271"
"%C\0e\14\65F\233%\33\206\60K\26\0f\13L\302*%\213\246\254\23\0g\14E>\233%"
"sK\206\60Y\0h\13MB\213\60\34\222\314\267\0i\7AF\211d\30j\11S>\252\64\352i"
"\1k\15EF\213\260\224\224\264$\252d\1l\10CF\212\250o\2m\16\67F\214E\211\42)\222"
"\42)\222\12n\11\65F\213!\311\274\5o\12\65F\233%sK\26\0p\14E>\213!\311\334\6"
"%\14\1q\13E>\233%sK\206\260\0r\12\64\306\212d\210\262\66\0s\12\65F\233%]\265"
"d\1t\12D\306\232,\32\222\254Qu\11\65F\213\314[\62\4v\12\65F\213\314-\251E\0w"
"\13\67F\214\324)R\272%\0x\13\65F\213,\251\205YR\13y\13E>\213\314[\62\204\203\2"
"z\12\65F\213A+f\331 {\21OB\234,\321\226\245\247\310\242&Y\222,\1|\6IB\213"
"\7}\14\265\312\273d\320\242lP\62\0~\21N\302\233!\31\206P\34\36\24e\30\222(\1\16"
"N\302\213$\314\224(\315\222,\351?\0\0\0";

static const uint8_t UndergroundClock8[150] U8G2_FONT_SECTION("UndergroundClock8") =
"\13\0\3\3\3\4\2\2\5\7\10\0\0\10\0\10\0\0\0\0\0\0}\60\12G\305\251\310\370&\251"
"\0\61\10\304\305\222\234\364\0\62\15G\305\251\310\244B\331L(;\10\63\15G\305\251\310\244\42\62\215"
"&\251\0\64\15G\305\24\316H\22%\311Q*\1\65\14G\305\70\310\250f\32MR\1\66\14G\305"
"\251\310\250\26\31\233\244\2\67\12G\305\70\310\204z\225\2\70\15G\305\251\310h\222\212\214MR\1\71"
"\15G\305\251\310h\22+\215&\251\0:\6\262\257 \22\0\0\0";

// Service attribution texts
const char nrAttributionn[] = "Powered by National Rail Enquiries";
const char tflAttribution[] = "Powered by TfL Open Data";
const char btAttribution[] = "Powered by bustimes.org";

//
// GitHub Client for firmware updates
//  - Pass a GitHub token if updates are to be loaded from a private repository
//
github ghUpdate("");

#define SCREENSAVERINTERVAL 10000     // How often the screen is changed in sleep mode (ms - 10 seconds)
#define DATAUPDATEINTERVAL 150000     // How often we fetch data from National Rail (ms - 2.5 mins) - "default" option
#define FASTDATAUPDATEINTERVAL 45000  // How often we fetch data from National Rail (ms - 45 secs) - "fast" option
#define UGDATAUPDATEINTERVAL 30000    // How often we fetch data from TfL (ms - 30 secs)
#define BUSDATAUPDATEINTERVAL 45000   // How often we fetch data from bustimes.org (ms - 45 secs)

// Bit and bobs
unsigned long timer = 0;
bool isSleeping = false;            // Is the screen sleeping (showing the "screensaver")
bool sleepEnabled = false;          // Is overnight sleep enabled?
bool dateEnabled = false;           // Showing the date on screen?
bool weatherEnabled = false;        // Showing weather at station location. Requires an OpenWeatherMap API key.
bool enableBus = false;             // Include Bus services on the board?
bool firmwareUpdates = true;        // Check for and install firmware updates automatically at boot?
byte sleepStarts = 0;               // Hour at which the overnight sleep (screensaver) begins
byte sleepEnds = 6;                 // Hour at which the overnight sleep (screensaver) ends
int brightness = 50;                // Initial brightness level of the OLED screen
unsigned long lastWiFiReconnect=0;  // Last WiFi reconnection time (millis)
bool firstLoad = true;              // Are we loading for the first time (no station config)?
int prevProgressBarPosition=0;      // Used for progress bar smooth animation
int startupProgressPercent;         // Initialisation progress
bool wifiConnected = false;         // Connected to WiFi?
unsigned long nextDataUpdate = 0;   // Next National Rail update time (millis)
int dataLoadSuccess = 0;            // Count of successful data downloads
int dataLoadFailure = 0;            // Count of failed data downloads
unsigned long lastLoadFailure = 0;  // When the last failure occurred
int dateWidth;                      // Width of the displayed date in pixels
int dateDay;                        // Day of the month of displayed date
bool altStationEnabled = false;     // Switch between stations based on time of day
bool altStationActive = false;      // Is the alternate station currently shown
byte altStarts = 12;                // Hour at which to switch to the alternate station
byte altEnds = 23;                  // Hour at which to switch back to the default station
bool noScrolling = false;           // Suppress all horizontal scrolling

char hostname[20] = "DeparturesBoard"; // Default network hostname (WiFi and mDNS - can be manually overridden in config.json)
char myUrl[24];                     // Stores the board's own url

// WiFi Manager status
bool wifiConfigured = false;        // Is WiFi configured successfully?

// Station Board Data
char nrToken[37] = "";              // National Rail Darwin Lite Tokens are in the format nnnnnnnn-nnnn-nnnn-nnnn-nnnnnnnnnnnn, where each 'n' represents a hexadecimal character (0-9 or a-f).
char crsCode[4] = "";               // Station code (3 character)
float stationLat=0;                 // Selected station Latitude/Longitude (used to get weather for the location)
float stationLon=0;
char callingCrsCode[4] = "";        // Station code to filter routes on
char callingStation[45] = "";       // Calling filter station friendly name
char filterPlatform[4] = "";        // Platform filter
char altCrsCode[4] = "";            // Station code of alternate station
float altLat=0;                     // Alternate station Latitude/Longitude (used to get weather for the location)
float altLon=0;
char altCallingCrsCode[4] = "";     // Calling filter for alternate station
char altCallingStation[45] = "";    // Calling filter station friendly name for alternate station
char altFilterPlatform[4] = "";     // Platform filter for alternate station
String tflAppkey = "";              // TfL API Key
char tubeId[13] = "";               // Underground station naptan id
String tubeName="";                 // Underground Station Name
char busAtco[13]="";                // Bus Stop ATCO location
String busName="";                  // Bus Stop long name
int busDestX;                       // Variable margin for bus destination
float busLat=0;                     // Bus stop Latitude/Longitude (used to get weather for the location)
float busLon=0;

// board has three possible modes.
enum boardModes {
  MODE_RAIL = 0,
  MODE_TUBE = 1,
  MODE_BUS = 2
};
boardModes boardMode = MODE_RAIL;

// Coach class availability
static const char firstClassSeating[] PROGMEM = " First class seating only.";
static const char standardClassSeating[] PROGMEM = " Standard class seating only.";
static const char dualClassSeating[] PROGMEM = " First and Standard class seating available.";

// Animation vars
int numMessages=0;
int scrollStopsXpos = 0;
int scrollStopsYpos = 0;
int scrollStopsLength = 0;
bool isScrollingStops = false;
int currentMessage = 0;
int prevMessage = 0;
int prevScrollStopsLength = 0;
char line2[4+MAXBOARDMESSAGES][MAXCALLINGSIZE+12];

// Line 3 (additional services)
int line3Service = 0;
int scrollServiceYpos = 0;
bool isScrollingService = false;
int prevService = 0;
bool isShowingVia=false;
unsigned long serviceTimer=0;
unsigned long viaTimer=0;
bool showingMessage = false;
// TfL/bus specific animation
int scrollPrimaryYpos = 0;
bool isScrollingPrimary = false;
bool attributionScrolled = false;

char displayedTime[29] = "";        // The currently displayed time
unsigned long nextClockUpdate = 0;  // Next time we need to check/update the clock display
int fpsDelay=25;                    // Total ms between text movement (for smooth animation)
unsigned long refreshTimer = 0;

// Weather Stuff
char weatherMsg[46];                            // Current weather at station location
unsigned long nextWeatherUpdate = 0;            // When the next weather update is due
String openWeatherMapApiKey = "";               // The API key to use
weatherClient currentWeather;                   // Create a weather client

bool noDataLoaded = true;                       // True if no data received for the station
int lastUpdateResult = 0;                       // Result of last data refresh
unsigned long lastDataLoadTime = 0;             // Timestamp of last data load
long apiRefreshRate = DATAUPDATEINTERVAL;       // User selected refresh rate for National Rail API

#define MAXHOSTSIZE 48                          // Maximum size of the wsdl Host
#define MAXAPIURLSIZE 48                        // Maximum size of the wsdl url

char wsdlHost[MAXHOSTSIZE];                     // wsdl Host name
char wsdlAPI[MAXAPIURLSIZE];                    // wsdl API url

// RailData XML Client
raildataXmlClient* raildata = nullptr;
// TfL Client
TfLdataClient* tfldata = nullptr;
// Bus Client
busDataClient* busdata = nullptr;
// Station Data (shared)
rdStation station;
// Station Messages (shared)
stnMessages messages;

/*
 * Graphics helper functions for OLED panel
*/
void blankArea(int x, int y, int w, int h) {
  u8g2.setDrawColor(0);
  u8g2.drawBox(x,y,w,h);
  u8g2.setDrawColor(1);
}

int getStringWidth(const char *message) {
  return u8g2.getStrWidth(message);
}

int getStringWidth(const __FlashStringHelper *message) {
  String temp = String(message);
  char buff[temp.length()+1];
  temp.toCharArray(buff,sizeof(buff));
  return u8g2.getStrWidth(buff);
}

void drawTruncatedText(const char *message, int line) {
  char buff[strlen(message)+4];
  int maxWidth = SCREEN_WIDTH - 6;
  strcpy(buff,message);
  int i = strlen(buff);
  while (u8g2.getStrWidth(buff)>maxWidth && i) buff[i--] = '\0';
  strcat(buff,"...");
  u8g2.drawStr(0,line,buff);
}

void centreText(const char *message, int line) {
  int width = u8g2.getStrWidth(message);
  if (width<=SCREEN_WIDTH) u8g2.drawStr((SCREEN_WIDTH-width)/2,line,message);
  else drawTruncatedText(message,line);
}

void centreText(const __FlashStringHelper *message, int line) {
  String temp = String(message);
  char buff[temp.length()+1];
  temp.toCharArray(buff,sizeof(buff));
  int width = u8g2.getStrWidth(buff);
  if (width<=SCREEN_WIDTH) u8g2.drawStr((SCREEN_WIDTH-width)/2,line,buff);
  else drawTruncatedText(buff,line);
}

void drawProgressBar(int percent) {
  int newPosition = (percent*190)/100;
  u8g2.drawFrame(32,36,192,12);
  if (prevProgressBarPosition>newPosition) {
    for (int i=prevProgressBarPosition;i>=newPosition;i--) {
      u8g2.setDrawColor(0);
      u8g2.drawBox(33,37,190,10);
      u8g2.setDrawColor(1);
      u8g2.drawBox(33,37,i,10);
      u8g2.updateDisplayArea(0,3,32,3);
      delay(5);
    }
  } else {
    for (int i=prevProgressBarPosition;i<=newPosition;i++) {
      u8g2.setDrawColor(0);
      u8g2.drawBox(33,37,190,10);
      u8g2.setDrawColor(1);
      u8g2.drawBox(33,37,i,10);
      u8g2.updateDisplayArea(0,3,32,3);
      delay(5);
    }
  }
  prevProgressBarPosition=newPosition;
}

void progressBar(const char *text, int percent) {
  u8g2.setFont(NatRailSmall9);
  blankArea(0,24,256,25);
  centreText(text,24);
  drawProgressBar(percent);
}

void progressBar(const __FlashStringHelper *text, int percent) {
  u8g2.setFont(NatRailSmall9);
  blankArea(0,24,256,25);
  centreText(text,24);
  drawProgressBar(percent);
}

void drawBuildTime() {
  char timestamp[22];
  char buildtime[20];
  struct tm tm = {};

  sprintf(timestamp,"%s %s",__DATE__,__TIME__);
  strptime(timestamp,"%b %d %Y %H:%M:%S",&tm);
  sprintf(buildtime,"v%d.%d-%02d%02d%02d%02d%02d",VERSION_MAJOR,VERSION_MINOR,tm.tm_year-100,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min);
  u8g2.drawStr(0,53,buildtime);
}

void drawStartupHeading() {
  u8g2.setFont(NatRailTall12);
  centreText(F("Departures Board"),0);
  u8g2.setFont(NatRailSmall9);
  drawBuildTime();
}

void drawStationHeader(const char *stopName, const char *callingStopName, const char *platformFilter) {

  // Clear the top line
  if (boardMode == MODE_TUBE || boardMode == MODE_BUS) {
    blankArea(0,ULINE0,256,ULINE1-1);
  } else {
    blankArea(0,LINE0,256,LINE1-1);
  }

  u8g2.setFont(NatRailSmall9);
  char boardTitle[95];
  
  // Build the title with platform filter and calling station
  if (platformFilter[0] && callingStopName[0]) {
    snprintf(boardTitle,sizeof(boardTitle),"%s (%s) (%c%s)",stopName,platformFilter,129,callingStopName);
  } else if (platformFilter[0]) {
    snprintf(boardTitle,sizeof(boardTitle),"%s (%s)",stopName,platformFilter);
  } else if (callingStopName[0]) {
    snprintf(boardTitle,sizeof(boardTitle),"%s  (%c%s)",stopName,129,callingStopName);
  } else {
    strncpy(boardTitle,stopName,sizeof(boardTitle));
  }
  
  int boardTitleWidth = getStringWidth(boardTitle);
  int line4Y = (boardMode == MODE_RAIL) ? LINE4 : ULINE4;

  if (dateEnabled) {
    // Get the date
    char sysTime[29];
    getLocalTime(&timeinfo);
    strftime(sysTime,29,"%a %d %b",&timeinfo);
    dateWidth = getStringWidth(sysTime);
    dateDay = timeinfo.tm_mday;
    if (callingStopName[0] || boardTitleWidth+dateWidth+10>=SCREEN_WIDTH) {
      blankArea(SCREEN_WIDTH-70,line4Y,70,SCREEN_HEIGHT-line4Y);
      u8g2.drawStr(SCREEN_WIDTH-dateWidth,line4Y-1,sysTime); // Date bottom right
      centreText(boardTitle,LINE0-1);
    } else {
      u8g2.drawStr(SCREEN_WIDTH-dateWidth,ULINE0-1,sysTime); // right-aligned date top
      if ((SCREEN_WIDTH-boardTitleWidth)/2 < dateWidth+8) {
        // station name left aligned
        u8g2.drawStr(0,ULINE0-1,boardTitle);
      } else {
        centreText(boardTitle,ULINE0-1);
      }
    }
  } else {
    centreText(boardTitle,ULINE0-1);
  }
}

// Draw the NR clock (if the time has changed)
void drawCurrentTime(bool update) {
  char sysTime[29];
  getLocalTime(&timeinfo);

  sprintf(sysTime,"%02d:%02d:",timeinfo.tm_hour,timeinfo.tm_min);
  if (strcmp(displayedTime,sysTime)) {
    u8g2.setFont(NatRailClockLarge9);
    blankArea(96,LINE4,64,SCREEN_HEIGHT-LINE4);
    u8g2.drawStr(96,LINE4-1,sysTime);
    u8g2.setFont(NatRailClockSmall7);
    sprintf(sysTime,"%02d",timeinfo.tm_sec);
    u8g2.drawStr(144,LINE4+1,sysTime);
    u8g2.setFont(NatRailSmall9);
    if (update) u8g2.updateDisplayArea(12,6,8,2);
    strcpy(displayedTime,sysTime);
    if (dateEnabled && timeinfo.tm_mday!=dateDay) {
      // Need to update the date on screen
      const char* activeCallingStation = (altStationActive && altStationEnabled) ? altCallingStation : callingStation;
      const char* activePlatformFilter = (altStationActive && altStationEnabled) ? altFilterPlatform : filterPlatform;
      drawStationHeader(station.location,activeCallingStation,activePlatformFilter);
      if (update) u8g2.sendBuffer();  // Just refresh on new date
    }
  }
}

// Screensaver Screen
void drawSleepingScreen() {
  char sysTime[8];
  char sysDate[29];

  u8g2.setContrast(DIMMED_BRIGHTNESS);
  u8g2.clearBuffer();
  getLocalTime(&timeinfo);
  sprintf(sysTime,"%02d:%02d",timeinfo.tm_hour,timeinfo.tm_min);
  strftime(sysDate,29,"%d %B %Y",&timeinfo);

  int offset = (getStringWidth(sysDate)-getStringWidth(sysTime))/2;
  u8g2.setFont(NatRailTall12);
  int y = random(39);
  int x = random(SCREEN_WIDTH-getStringWidth(sysDate));
  u8g2.drawStr(x+offset,y,sysTime);
  u8g2.setFont(NatRailSmall9);
  u8g2.drawStr(x,y+13,sysDate);
  u8g2.sendBuffer();
  firstLoad=true;
}

void showUpdateIcon(bool show) {
  if (show) {
    u8g2.setFont(NatRailTall12);
    u8g2.drawStr(0,50,"}");
    u8g2.setFont(NatRailSmall9);
  } else {
    blankArea(0,50,6,13);
  }
  u8g2.updateDisplayArea(0,6,1,2);
}

/*
 * Setup / Notification Screen Layouts
*/
void showSetupScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText(F("Departures Board first-time setup"),0);
  u8g2.setFont(NatRailSmall9);
  centreText(F("To configure Wi-Fi, please connect to the"),18);
  centreText(F("the \"Departures Board\" network and go to"),32);
  centreText(F("http://192.168.4.1 in a web browser."),46);
  u8g2.sendBuffer();
}

void showNoDataScreen() {
  u8g2.clearBuffer();
  char msg[60];
  u8g2.setFont(NatRailTall12);
  if (boardMode!=MODE_RAIL) strcpy(msg,"No data available for the selected station.");
  else sprintf(msg,"No data available for station code \"%s\".",crsCode);
  centreText(msg,-1);
  u8g2.setFont(NatRailSmall9);
  centreText(F("Please ensure you have selected a valid station"),14);
  centreText(F("Go to the URL below to choose a station..."),26);
  centreText(myUrl,40);
  u8g2.sendBuffer();
}

void showSetupKeysHelpScreen() {
  u8g2.clearBuffer();
  char msg[60];
  u8g2.setFont(NatRailTall12);
  centreText(F("Departures Board Setup"),-1);
  u8g2.setFont(NatRailSmall9);
  centreText(F("Next, you need to enter your API keys."),16);
  centreText(F("Please go to the URL below to start..."),28);
  u8g2.setFont(NatRailTall12);
  centreText(myUrl,50);
  u8g2.sendBuffer();
}

void showSetupCrsHelpScreen() {
  u8g2.clearBuffer();
  char msg[60];
  u8g2.setFont(NatRailTall12);
  centreText(F("Departures Board Setup"),-1);
  u8g2.setFont(NatRailSmall9);
  centreText(F("Next, you need to choose a location. Please"),16);
  centreText(F("go to the URL below to choose a station..."),28);
  u8g2.setFont(NatRailTall12);
  centreText(myUrl,50);
  u8g2.sendBuffer();
}

void showWsdlFailureScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText(F("The National Rail data feed is unavailable."),-1);
  u8g2.setFont(NatRailSmall9);
  centreText(F("WDSL entry point could not be accessed, so the"),14);
  centreText(F("Departures Board cannot be loaded."),26);
  centreText(F("Please try again later. :("),40);
  u8g2.sendBuffer();
}

void showTokenErrorScreen() {
  char msg[60];
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  switch (boardMode) {
    case MODE_RAIL:
      centreText(F("Access to the National Rail database denied."),-1);
      strcpy(nrToken,"");
      break;
    case MODE_TUBE:
      centreText(F("Access to the TfL database denied."),-1);
      break;
    case MODE_BUS:
      centreText(F("Access to the bustimes database denied."),-1);
      break;
  }
  u8g2.setFont(NatRailSmall9);
  centreText(F("You must enter a valid auth token, please"),14);
  centreText(F("check you have entered it correctly below:"),26);
  sprintf(msg,"%s/keys.htm",myUrl);
  centreText(msg,40);
  u8g2.sendBuffer();
}

void showCRSErrorScreen() {
  u8g2.clearBuffer();
  char msg[60];
  u8g2.setFont(NatRailTall12);
  switch (boardMode) {
    case MODE_RAIL:
      sprintf(msg,"The station code \"%s\" is not valid.",crsCode);
      break;
    case MODE_TUBE:
      strcpy(msg,"The Underground station is not valid");
      break;
    case MODE_BUS:
      sprintf(msg,"The atco code \"%s\" is not valid.",busAtco);
      break;
  }
  centreText(msg,-1);
  u8g2.setFont(NatRailSmall9);
  centreText(F("Please ensure you have selected a valid station."),14);
  centreText(F("Go to the URL below to choose a station..."),26);
  centreText(myUrl,40);
  u8g2.sendBuffer();
}

void showFirmwareUpdateWarningScreen(const char *msg, int secs) {
  char countdown[60];
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText(F("Firmware Update Available"),-1);
  u8g2.setFont(NatRailSmall9);
  centreText(F("A new version of the Departures Board firmware"),14);
  sprintf(countdown,"will be installed in %d seconds. This provides:",secs);
  centreText(countdown,26);
  sprintf(countdown,"\"%s\"",msg);
  centreText(countdown,40);
  centreText(F("* DO NOT REMOVE THE POWER DURING THE UPDATE *"),54);
  u8g2.sendBuffer();
}

void showFirmwareUpdateProgress(int percent) {
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText(F("Firmware Update in Progress"),-1);
  u8g2.setFont(NatRailSmall9);
  progressBar(F("Updating Firmware"),percent);
  centreText(F("* DO NOT REMOVE THE POWER DURING THE UPDATE *"),54);
  u8g2.sendBuffer();
}

void showUpdateCompleteScreen(const char *title, const char *msg1, const char *msg2, const char *msg3, int secs, bool showReboot) {
  char countdown[60];
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText(title,-1);
  u8g2.setFont(NatRailSmall9);
  centreText(msg1,14);
  centreText(msg2,26);
  centreText(msg3,40);
  if (showReboot) sprintf(countdown,"The system will restart in %d seconds.",secs);
  else sprintf(countdown,"The system will continue in %d seconds.",secs);
  centreText(countdown,54);
  u8g2.sendBuffer();
}

/*
 * Utility functions
*/

// Saves a file (string) to the FFS
bool saveFile(String fName, String fData) {
  File f = LittleFS.open(fName,"w");
  if (f) {
    f.println(fData);
    f.close();
    return true;
  } else return false;
}

// Loads a file (string) from the FFS
String loadFile(String fName) {
  File f = LittleFS.open(fName,"r");
  if (f) {
    String result = f.readString();
    f.close();
    return result;
  } else return "";
}

// Get the Build Timestamp of the running firmware
String getBuildTime() {
  char timestamp[22];
  char buildtime[11];
  struct tm tm = {};

  sprintf(timestamp,"%s %s",__DATE__,__TIME__);
  strptime(timestamp,"%b %d %Y %H:%M:%S",&tm);
  sprintf(buildtime,"%02d%02d%02d%02d%02d",tm.tm_year-100,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min);
  return String(buildtime);
}

void checkPostWebUpgrade() {
  String prevGUI = loadFile(F("/webver"));
  prevGUI.trim();
  String currentGUI = String(WEBAPPVER_MAJOR) + F(".") + String(WEBAPPVER_MINOR);
  if (prevGUI != currentGUI) {
    // clean up old/dev files
    progressBar(F("Cleaning up following upgrade"),45);
    LittleFS.remove(F("/index_d.htm"));
    LittleFS.remove(F("/index.htm"));
    LittleFS.remove(F("/keys.htm"));
    LittleFS.remove(F("/nrelogo.webp"));
    LittleFS.remove(F("/tfllogo.webp"));
    LittleFS.remove(F("/btlogo.webp"));
    LittleFS.remove(F("/tube.webp"));
    LittleFS.remove(F("/nr.webp"));
    LittleFS.remove(F("/favicon.svg"));
    LittleFS.remove(F("/favicon.png"));
    saveFile(F("/webver"),currentGUI);
  }
}

// Check if the NR clock needs to be updated
void doClockCheck() {
  if (!firstLoad) {
    if (millis()>nextClockUpdate) {
      drawCurrentTime(true);
      nextClockUpdate=millis()+500;
    }
  }
}

// Returns true if sleep mode is enabled and we're within the sleep period
bool isSnoozing() {
  if (!sleepEnabled) return false;
  getLocalTime(&timeinfo);
  byte myHour = timeinfo.tm_hour;
  if (sleepStarts > sleepEnds) {
    if ((myHour >= sleepStarts) || (myHour < sleepEnds)) return true; else return false;
  } else {
    if ((myHour >= sleepStarts) && (myHour < sleepEnds)) return true; else return false;
  }
}

// Returns true if an alternate station is enabled and we're within the activation period
bool isAltActive() {
  if (!altStationEnabled) return false;
  getLocalTime(&timeinfo);
  byte myHour = timeinfo.tm_hour;
  if (altStarts > altEnds) {
    if ((myHour >= altStarts) || (myHour < altEnds)) return true; else return false;
  } else {
    if ((myHour >= altStarts) && (myHour < altEnds)) return true; else return false;
  }
}

// Callback from the raildataXMLclient library when processing data. As this can take some time, this callback is used to keep the clock working
// and to provide progress on the initial load at boot
void raildataCallback(int stage, int nServices) {
  if (firstLoad) {
    int percent = ((nServices*20)/MAXBOARDSERVICES)+80;
    progressBar(F("Initialising National Rail interface"),percent);
  } else doClockCheck();
}

// Stores/updates the url of our Web GUI
void updateMyUrl() {
  IPAddress ip = WiFi.localIP();
  snprintf(myUrl,sizeof(myUrl),"http://%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);
}

/*
 * Start-up configuration functions
 */

// Load the API keys from the file system (if they exist)
void loadApiKeys() {
  JsonDocument doc;

  if (LittleFS.exists(F("/apikeys.json"))) {
    File file = LittleFS.open(F("/apikeys.json"), "r");
    if (file) {
      DeserializationError error = deserializeJson(doc, file);
      if (!error) {
        JsonObject settings = doc.as<JsonObject>();

        if (settings[F("nrToken")].is<const char*>()) {
          strlcpy(nrToken, settings[F("nrToken")], sizeof(nrToken));
        }

        if (settings[F("owmToken")].is<const char*>()) {
          openWeatherMapApiKey = settings[F("owmToken")].as<String>();
        }

        if (settings[F("appKey")].is<const char*>()) {
          tflAppkey = settings[F("appKey")].as<String>();
        }

      } else {
        // JSON deserialization failed - TODO
      }
      file.close();
    }
  }
}

// Write a default config file so that the Web GUI works initially (force Tube mode if no NR token)
void writeDefaultConfig() {
    String defaultConfig = "{\"crs\":\"\",\"station\":\"\",\"lat\":0,\"lon\":0,\"weather\":" + String((openWeatherMapApiKey.length())?"true":"false") + F(",\"sleep\":false,\"showDate\":false,\"showBus\":false,\"update\":true,\"sleepStarts\":23,\"sleepEnds\":8,\"brightness\":20,\"tubeId\":\"\",\"tubeName\":\"\",\"mode\":") + String((!nrToken[0])?"1":"0") + "}";
    saveFile(F("/config.json"),defaultConfig);
    strcpy(crsCode,"");
    strcpy(tubeId,"");
}

// Load the configuration settings (if they exist, if not create a default set for the Web GUI page to read)
void loadConfig() {
  JsonDocument doc;

  if (LittleFS.exists(F("/config.json"))) {
    File file = LittleFS.open(F("/config.json"), "r");
    if (file) {
      DeserializationError error = deserializeJson(doc, file);
      if (!error) {
        JsonObject settings = doc.as<JsonObject>();

        if (settings[F("crs")].is<const char*>())        strlcpy(crsCode, settings[F("crs")], sizeof(crsCode));
        if (settings[F("callingCrs")].is<const char*>()) strlcpy(callingCrsCode, settings[F("callingCrs")], sizeof(callingCrsCode));
        if (settings[F("callingStation")].is<const char*>()) strlcpy(callingStation, settings[F("callingStation")], sizeof(callingStation));
        if (settings[F("filterPlatform")].is<const char*>()) strlcpy(filterPlatform, settings[F("filterPlatform")], sizeof(filterPlatform));
        if (settings[F("hostname")].is<const char*>())   strlcpy(hostname, settings[F("hostname")], sizeof(hostname));
        if (settings[F("wsdlHost")].is<const char*>())   strlcpy(wsdlHost, settings[F("wsdlHost")], sizeof(wsdlHost));
        if (settings[F("wsdlAPI")].is<const char*>())    strlcpy(wsdlAPI, settings[F("wsdlAPI")], sizeof(wsdlAPI));
        if (settings[F("showDate")].is<bool>())          dateEnabled = settings[F("showDate")];
        if (settings[F("showBus")].is<bool>())           enableBus = settings[F("showBus")];
        if (settings[F("sleep")].is<bool>())             sleepEnabled = settings[F("sleep")];
        if (settings[F("fastRefresh")].is<bool>())       apiRefreshRate = settings[F("fastRefresh")] ? FASTDATAUPDATEINTERVAL : DATAUPDATEINTERVAL;
        if (settings[F("weather")].is<bool>() && openWeatherMapApiKey.length())
                                                    weatherEnabled = settings[F("weather")];
        if (settings[F("update")].is<bool>())            firmwareUpdates = settings[F("update")];
        if (settings[F("sleepStarts")].is<int>())        sleepStarts = settings[F("sleepStarts")];
        if (settings[F("sleepEnds")].is<int>())          sleepEnds = settings[F("sleepEnds")];
        if (settings[F("brightness")].is<int>())         brightness = settings[F("brightness")];
        if (settings[F("lat")].is<float>())              stationLat = settings[F("lat")];
        if (settings[F("lon")].is<float>())              stationLon = settings[F("lon")];

        if (settings[F("mode")].is<int>())               boardMode = settings[F("mode")];
        else if (settings[F("tube")].is<bool>())         boardMode = settings[F("tube")] ? MODE_TUBE : MODE_RAIL; // handle legacy v1.x config
        if (settings[F("tubeId")].is<const char*>())     strlcpy(tubeId, settings[F("tubeId")], sizeof(tubeId));
        if (settings[F("tubeName")].is<const char*>())   tubeName = settings[F("tubeName")].as<String>();

        // Clean up the underground station name
        if (tubeName.endsWith(F(" Underground Station"))) tubeName.remove(tubeName.length()-20);
        else if (tubeName.endsWith(F(" DLR Station"))) tubeName.remove(tubeName.length()-12);
        else if (tubeName.endsWith(F(" (H&C Line)"))) tubeName.remove(tubeName.length()-11);

        if (settings[F("altCrs")].is<const char*>())     strlcpy(altCrsCode, settings[F("altCrs")], sizeof(altCrsCode));
        if (altCrsCode[0]) altStationEnabled = true; else altStationEnabled = false;
        if (settings[F("altStarts")].is<int>())          altStarts = settings[F("altStarts")];
        if (settings[F("altEnds")].is<int>())            altEnds = settings[F("altEnds")];
        if (settings[F("altLat")].is<float>())           altLat = settings[F("altLat")];
        if (settings[F("altLon")].is<float>())           altLon = settings[F("altLon")];
        if (settings[F("altCallingCrs")].is<const char*>()) strlcpy(altCallingCrsCode, settings[F("altCallingCrs")], sizeof(altCallingCrsCode));
        if (settings[F("altCallingStation")].is<const char*>()) strlcpy(altCallingStation, settings[F("altCallingStation")], sizeof(altCallingStation));
        if (settings[F("altFilterPlatform")].is<const char*>()) strlcpy(altFilterPlatform, settings[F("altFilterPlatform")], sizeof(altFilterPlatform));

        if (settings[F("busId")].is<const char*>())      strlcpy(busAtco, settings[F("busId")], sizeof(busAtco));
        if (settings[F("busName")].is<const char*>())    busName = String(settings[F("busName")]);
        if (settings[F("busLat")].is<float>())           busLat = settings[F("busLat")];
        if (settings[F("busLon")].is<float>())           busLon = settings[F("busLon")];

        if (settings[F("noScroll")].is<bool>())          noScrolling = settings[F("noScroll")];
      } else {
        // JSON deserialization failed - TODO
      }
      file.close();
    }
  } else if (nrToken[0] || tflAppkey.length()) writeDefaultConfig();
}

// Switch to the alternate station settings if appropriate
bool setAlternateStation() {
  if (boardMode==MODE_RAIL && altStationEnabled && isAltActive()) {
    // Switch to alternate station
    strcpy(crsCode,altCrsCode);
    stationLat = altLat;
    stationLon = altLon;
    return true;
  } else {
    return false;
  }
}

// Soft reset/reload
void softResetBoard() {
  int previousMode = boardMode;

  // Reload the settings
  loadConfig();
  u8g2.clearBuffer();
  drawStartupHeading();
  u8g2.updateDisplay();

  // Force an update asap
  nextDataUpdate = 0;
  nextWeatherUpdate = 0;
  isScrollingService = false;
  isScrollingStops = false;
  isSleeping=false;
  firstLoad=true;
  noDataLoaded=true;
  viaTimer=0;
  timer=0;
  prevProgressBarPosition=70;
  startupProgressPercent=70;
  currentMessage=0;
  prevMessage=0;
  prevScrollStopsLength=0;
  isShowingVia=false;
  line3Service=0;
  prevService=0;
  if (!weatherEnabled) strcpy(weatherMsg,"");
  if (previousMode!=boardMode) {
    // Board mode has changed!
    switch (previousMode) {
      case MODE_RAIL:
        // Delete the NR client from memory
        delete raildata;
        raildata = nullptr;
        break;

      case MODE_TUBE:
        // Delete the tfl client from memory
        delete tfldata;
        tfldata = nullptr;
        break;

      case MODE_BUS:
        // Delete the Bus client from memory
        delete busdata;
        busdata = nullptr;
        break;
    }

    switch (boardMode) {
      case MODE_RAIL:
        // Create the NR client
        raildata = new raildataXmlClient();
        if (boardMode == MODE_RAIL) {
          int res = raildata->init(wsdlHost, wsdlAPI, &raildataCallback);
          if (res != UPD_SUCCESS) {
            showWsdlFailureScreen();
             while (true) { server.handleClient(); yield();}
          }
        }
        break;

      case MODE_TUBE:
        // Create the TfL client
        tfldata = new TfLdataClient();
        break;

      case MODE_BUS:
        // Create the Bus client
        busdata = new busDataClient();
        break;
    }
  }

  switch (boardMode) {
    case MODE_RAIL:
      altStationActive = setAlternateStation();
      break;

    case MODE_TUBE:
      progressBar(F("Initialising TfL interface"),70);
      break;

    case MODE_BUS:
      progressBar(F("Initialising BusTimes interface"),70);
      break;
  }
  station.numServices=0;
  messages.numMessages=0;
}

// WiFiManager callback, entered config mode
void wmConfigModeCallback (WiFiManager *myWiFiManager) {
  showSetupScreen();
  wifiConfigured = true;
}

/*
 * Firmware / Web GUI Update functions
*/
bool isFirmwareUpdateAvailable() {
  int releaseMajor = ghUpdate.releaseId.substring(1,ghUpdate.releaseId.indexOf(".")).toInt();
  int releaseMinor = ghUpdate.releaseId.substring(ghUpdate.releaseId.indexOf(".")+1,ghUpdate.releaseId.indexOf("-")).toInt();
  if (VERSION_MAJOR > releaseMajor) return false;
  if ((VERSION_MAJOR == releaseMajor) && (VERSION_MINOR >= releaseMinor)) return false;
  return true;
}

// Callback function for displaying firmware update progress
void update_progress(int cur, int total) {
  int percent = ((cur * 100)/total);
  showFirmwareUpdateProgress(percent);
}

// Attempts to install newer firmware if available
bool checkForFirmwareUpdate() {
  bool result = true;

  if (!isFirmwareUpdateAvailable()) return result;

  // Find the firmware binary in the release assets
  String updatePath="";
  for (int i=0;i<ghUpdate.releaseAssets;i++){
    if (ghUpdate.releaseAssetName[i] == "firmware.bin") {
      updatePath = ghUpdate.releaseAssetURL[i];
      break;
    }
  }
  if (updatePath.length()==0) {
    //  No firmware binary in release assets
    return result;
  }

  unsigned long tmr=millis()+1000;
  for (int i=30;i>=0;i--) {
    showFirmwareUpdateWarningScreen(ghUpdate.releaseDescription.c_str(),i);
    while (tmr>millis()) {
      yield();
      server.handleClient();
    }
    tmr=millis()+1000;
  }
  u8g2.clearDisplay();
  prevProgressBarPosition=0;
  showFirmwareUpdateProgress(0);  // So we don't have a blank screen
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.onProgress(update_progress);
  httpUpdate.rebootOnUpdate(false); // Don't auto reboot, we'll handle it

  HTTPUpdateResult ret = httpUpdate.handleUpdate(client, updatePath, ghUpdate.accessToken);
  const char* msgTitle = "Firmware Update";
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      char msg[60];
      sprintf(msg,"The update failed with error %d.",httpUpdate.getLastError());
      result=false;
      for (int i=20;i>=0;i--) {
        showUpdateCompleteScreen(msgTitle,msg,httpUpdate.getLastErrorString().c_str(),"",i,false);
        delay(1000);
      }
      break;

    case HTTP_UPDATE_NO_UPDATES:
      for (int i=10;i>=0;i--) {
        showUpdateCompleteScreen(msgTitle,"","No firmware updates were available.","",i,false);
        delay(1000);
      }
      break;

    case HTTP_UPDATE_OK:
      for (int i=20;i>=0;i--) {
        showUpdateCompleteScreen(msgTitle,"The firmware update has completed successfully.","For more information visit the URL below:","github.com/gadec-uk/departures-board/releases",i,true);
        delay(1000);
      }
      ESP.restart();
      break;
  }
  u8g2.clearDisplay();
  drawStartupHeading();
  u8g2.sendBuffer();
  return result;
}

/*
 * Station Board functions - pulling updates and animating the Departures Board main display
 */

// Request a data update via the raildataClient
bool getStationBoard() {
  if (!firstLoad) showUpdateIcon(true);
  
  const char* activeCallingCrs = (altStationActive && altStationEnabled) ? altCallingCrsCode : callingCrsCode;
  const char* activePlatformFilter = (altStationActive && altStationEnabled) ? altFilterPlatform : filterPlatform;
  
  lastUpdateResult = raildata->updateDepartures(&station,&messages,crsCode,nrToken,MAXBOARDSERVICES,enableBus,activeCallingCrs,activePlatformFilter);
  nextDataUpdate = millis()+apiRefreshRate;
  if (lastUpdateResult == UPD_SUCCESS || lastUpdateResult == UPD_NO_CHANGE) {
    showUpdateIcon(false);
    lastDataLoadTime=millis();
    noDataLoaded=false;
    dataLoadSuccess++;
    return true;
  } else if (lastUpdateResult == UPD_DATA_ERROR || lastUpdateResult == UPD_TIMEOUT) {
    lastLoadFailure=millis();
    dataLoadFailure++;
    nextDataUpdate = millis() + 30000; // 30 secs
    showUpdateIcon(false);
    return false;
  } else if (lastUpdateResult == UPD_UNAUTHORISED) {
    showTokenErrorScreen();
    while (true) { server.handleClient(); yield();}
  } else {
    showUpdateIcon(false);
    dataLoadFailure++;
    return false;
  }
}

// Draw the primary service line
void drawPrimaryService(bool showVia) {
  int destPos;
  char clipDestination[MAXLOCATIONSIZE];
  char etd[16];

  u8g2.setFont(NatRailTall12);
  blankArea(0,LINE1,256,LINE2-LINE1);
  destPos = u8g2.drawStr(0,LINE1-1,station.service[0].sTime) + 6;
  if (station.service[0].platform[0] && strlen(station.service[0].platform)<3 && station.service[0].serviceType == TRAIN) {
    destPos += u8g2.drawStr(destPos,LINE1-1,station.service[0].platform) + 6;
  } else if (station.service[0].serviceType == BUS) {
    destPos += u8g2.drawStr(destPos,LINE1-1,"~") + 6; // Bus icon
  }
  if (isDigit(station.service[0].etd[0])) sprintf(etd,"Exp %s",station.service[0].etd);
  else strcpy(etd,station.service[0].etd);
  int etdWidth = getStringWidth(etd);
  u8g2.drawStr(SCREEN_WIDTH - etdWidth,LINE1-1,etd);
  // Space available for destination name
  int spaceAvailable = SCREEN_WIDTH - destPos - etdWidth - 6;
  if (showVia) strcpy(clipDestination,station.service[0].via);
  else strcpy(clipDestination,station.service[0].destination);
  if (getStringWidth(clipDestination) > spaceAvailable) {
    while (getStringWidth(clipDestination) > (spaceAvailable - 8)) {
      clipDestination[strlen(clipDestination)-1] = '\0';
    }
    // check if there's a trailing space left
    if (clipDestination[strlen(clipDestination)-1] == ' ') clipDestination[strlen(clipDestination)-1] = '\0';
    strcat(clipDestination,"...");
  }
  u8g2.drawStr(destPos,LINE1-1,clipDestination);
  // Set font back to standard
  u8g2.setFont(NatRailSmall9);
}

// Draw the secondary service line
void drawServiceLine(int line, int y) {
  char clipDestination[30];
  char ordinal[5];

  switch (line) {
    case 1:
      strcpy(ordinal,"2nd ");
      break;
    case 2:
      strcpy(ordinal,"3rd ");
      break;
    default:
      sprintf(ordinal,"%dth ",line+1);
      break;
  }

  u8g2.setFont(NatRailSmall9);
  blankArea(0,y,256,9);

  if (line<station.numServices) {
    u8g2.drawStr(0,y-1,ordinal);
    int destPos = u8g2.drawStr(23,y-1,station.service[line].sTime) + 27;
    char plat[3];
    if (station.platformAvailable) {
      if (station.service[line].platform[0] && strlen(station.service[line].platform)<3 && station.service[line].serviceType == TRAIN) {
        strncpy(plat,station.service[line].platform,3);
        plat[2]='\0';
      } else {
        if (station.service[line].serviceType == BUS) strcpy(plat,"~");  // Bus icon
        else strcpy(plat,"}}");
      }
      u8g2.drawStr(destPos+11-getStringWidth(plat),y-1,plat);
      destPos+=16;
    }
    char etd[16];
    if (isDigit(station.service[line].etd[0])) sprintf(etd,"Exp %s",station.service[line].etd);
    else strcpy(etd,station.service[line].etd);
    int etdWidth = getStringWidth(etd);
    u8g2.drawStr(SCREEN_WIDTH - etdWidth,y-1,etd);
    // work out if we need to clip the destination
    strcpy(clipDestination,station.service[line].destination);

    int spaceAvailable = SCREEN_WIDTH - destPos - etdWidth - 6;

    if (getStringWidth(clipDestination) > spaceAvailable) {
      while (getStringWidth(clipDestination) > spaceAvailable - 17) {
        clipDestination[strlen(clipDestination)-1] = '\0';
      }
      // check if there's a trailing space left
      if (clipDestination[strlen(clipDestination)-1] == ' ') clipDestination[strlen(clipDestination)-1] = '\0';
      strcat(clipDestination,"...");
    }
    u8g2.drawStr(destPos,y-1,clipDestination);
  } else {
    if (weatherMsg[0] && line==station.numServices) {
      // We're showing the weather
      centreText(weatherMsg,y-1);
    } else {
      // We're showing the mandatory attribution
      centreText(nrAttributionn,y-1);
    }
  }
}

// Draw the initial Departures Board
void drawStationBoard() {
  numMessages=0;
  if (firstLoad) {
    // Clear the entire screen for the first load since boot up/wake from sleep
    u8g2.clearBuffer();
    u8g2.setContrast(brightness);
    firstLoad=false;
    line3Service = noScrolling ? 1 : 0;
  } else {
    // Clear the top two lines
    blankArea(0,LINE0,256,LINE2-1);
  }
  const char* activeCallingStation = (altStationActive && altStationEnabled) ? altCallingStation : callingStation;
  const char* activePlatformFilter = (altStationActive && altStationEnabled) ? altFilterPlatform : filterPlatform;
  drawStationHeader(station.location,activeCallingStation,activePlatformFilter);

  // Draw the primary service line
  isShowingVia=false;
  viaTimer=millis()+300000;  // effectively don't check for via
  if (station.numServices) {
    drawPrimaryService(false);
    if (station.service[0].via[0]) viaTimer=millis()+4000;
    if (station.service[0].isCancelled) {
      // This train is cancelled
      if (station.serviceMessage[0]) {
        strcpy(line2[0],station.serviceMessage);
        numMessages=1;
      }
    } else {
      // The train is not cancelled
      if (station.service[0].isDelayed && station.serviceMessage[0]) {
        // The train is delayed and there's a reason
        strcpy(line2[0],station.serviceMessage);
        numMessages++;
      }
      if (station.calling[0]) {
        // Add the calling stops message
        sprintf(line2[numMessages],"Calling at: %s",station.calling);
        numMessages++;
      }
      if (strcmp(station.origin, station.location)==0) {
        // Service originates at this station
        if (station.service[0].opco[0]) {
          sprintf(line2[numMessages],"This %s service starts here.",station.service[0].opco);
        } else {
          strcpy(line2[numMessages],"This service starts here.");
        }
        // Add the seating if available
        switch (station.service[0].classesAvailable) {
          case 1:
            strcat(line2[numMessages],firstClassSeating);
            break;
          case 2:
            strcat(line2[numMessages],standardClassSeating);
            break;
          case 3:
            strcat(line2[numMessages],dualClassSeating);
            break;
        }
        numMessages++;
      } else {
        // Service originates elsewhere
        strcpy(line2[numMessages],"");
        if (station.service[0].opco[0]) {
          if (station.origin[0]) {
            sprintf(line2[numMessages],"This is the %s service from %s.",station.service[0].opco,station.origin);
          } else {
            sprintf(line2[numMessages],"This is the %s service.",station.service[0].opco);
          }
        } else {
          if (station.origin[0]) {
            sprintf(line2[numMessages],"This service originated at %s.",station.origin);
          }
        }
        // Add the seating if available
        switch (station.service[0].classesAvailable) {
          case 1:
            strcat(line2[numMessages],firstClassSeating);
            break;
          case 2:
            strcat(line2[numMessages],standardClassSeating);
            break;
          case 3:
            strcat(line2[numMessages],dualClassSeating);
            break;
        }
        if (line2[numMessages][0]) numMessages++;
      }
      if (station.service[0].trainLength) {
        // Add the number of carriages message
        sprintf(line2[numMessages],"This train is formed of %d coaches.",station.service[0].trainLength);
        numMessages++;
      }
    }
    // Add any nrcc messages
    for (int i=0;i<messages.numMessages;i++) {
      strcpy(line2[numMessages],messages.messages[i]);
      numMessages++;
    }
    // Setup for the first message to rollover to
    isScrollingStops=false;
    currentMessage=numMessages-1;
    if (noScrolling && station.numServices>1) {
      drawServiceLine(1,LINE2);
    }
  } else {
    blankArea(0,LINE2,256,LINE4-LINE2);
    u8g2.setFont(NatRailTall12);
    centreText(F("There are no scheduled services at this station."),LINE1-1);
    numMessages = messages.numMessages;
    for (int i=0;i<messages.numMessages;i++) {
      strcpy(line2[i],messages.messages[i]);
    }
    // Setup for the first message to rollover to
    isScrollingStops=false;
    currentMessage=numMessages-1;
  }
  u8g2.setFont(NatRailSmall9);
  u8g2.sendBuffer();
}

/*
 *
 * London Underground Board
 *
 */

// Draw the TfL clock (if the time has changed)
void drawCurrentTimeUG(bool update) {
  char sysTime[29];
  getLocalTime(&timeinfo);

  sprintf(sysTime,"%02d:%02d:%02d",timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);
  if (strcmp(displayedTime,sysTime)) {
    u8g2.setFont(UndergroundClock8);
    blankArea(99,ULINE4,58,8);
    u8g2.drawStr(99,ULINE4-1,sysTime);
    if (update) u8g2.updateDisplayArea(12,7,8,1);
    strcpy(displayedTime,sysTime);
    u8g2.setFont(Underground10);

    if (dateEnabled && timeinfo.tm_mday!=dateDay) {
      if (boardMode == MODE_TUBE) drawStationHeader(tubeName.c_str(),"","");
      else drawStationHeader(busName.c_str(),"","");
      if (update) u8g2.sendBuffer();  // Just refresh on new date
      u8g2.setFont(Underground10);
    }
  }
}

// Callback from the TfLdataClient/busDataClient library when processing data. Shows progress at startup and keeps clock running
void tflCallback() {
  if (firstLoad) {
    if (startupProgressPercent<95) {
      startupProgressPercent+=5;
      if (boardMode == MODE_TUBE) progressBar(F("Initialising TfL interface"),startupProgressPercent);
      else progressBar(F("Initialising BusTimes interface"),startupProgressPercent);
    }
  } else if (millis()>nextClockUpdate) {
    nextClockUpdate = millis()+500;
    drawCurrentTimeUG(true);
  }
}

bool getUndergroundBoard() {
  if (!firstLoad) showUpdateIcon(true);
  lastUpdateResult = tfldata->updateArrivals(&station,&messages,tubeId,tflAppkey,&tflCallback);
  nextDataUpdate = millis()+UGDATAUPDATEINTERVAL; // default update freq
  if (lastUpdateResult == UPD_SUCCESS || lastUpdateResult == UPD_NO_CHANGE) {
    showUpdateIcon(false);
    lastDataLoadTime=millis();
    noDataLoaded=false;
    dataLoadSuccess++;
    if (noScrolling) messages.numMessages = 0;
    return true;
  } else if (lastUpdateResult == UPD_DATA_ERROR || lastUpdateResult == UPD_TIMEOUT) {
    lastLoadFailure=millis();
    dataLoadFailure++;
    nextDataUpdate = millis() + 30000; // 30 secs
    showUpdateIcon(false);
    return false;
  } else if (lastUpdateResult == UPD_UNAUTHORISED) {
    showTokenErrorScreen();
    while (true) { server.handleClient(); yield();}
  } else {
    showUpdateIcon(false);
    dataLoadFailure++;
    return false;
  }
}

void drawUndergroundService(int serviceId, int y) {
  char serviceData[8+MAXLINESIZE+MAXLOCATIONSIZE];

  u8g2.setFont(Underground10);
  blankArea(0,y,256,10);

  if (serviceId < station.numServices) {
    sprintf(serviceData,"%d %s",serviceId+1,station.service[serviceId].destination);
    u8g2.drawStr(0,y-1,serviceData);
    if (serviceId || station.service[serviceId].timeToStation > 30) {
      if (station.service[serviceId].timeToStation <= 60) u8g2.drawStr(SCREEN_WIDTH-19,y-1,"Due");
      else {
        int mins = (station.service[serviceId].timeToStation + 30) / 60; // Round to nearest minute
        sprintf(serviceData,"%d",mins);
        if (mins==1) u8g2.drawStr(SCREEN_WIDTH-22,y-1,"min"); else u8g2.drawStr(SCREEN_WIDTH-22,y-1,"mins");
        u8g2.drawStr(SCREEN_WIDTH-27-(strlen(serviceData)*7),y-1,serviceData);
      }
    }
  }
}

// Draw/update the Underground Arrivals Board
void drawUndergroundBoard() {
  numMessages = messages.numMessages;
  if (line3Service==0) line3Service=1;
  attributionScrolled=false;
  if (firstLoad) {
    // Clear the entire screen for the first load since boot up/wake from sleep
    u8g2.clearBuffer();
    u8g2.setContrast(brightness);
    firstLoad=false;
  } else {
      // Clear the top three lines
      blankArea(0,ULINE0,256,ULINE3-1);
  }
  drawStationHeader(tubeName.c_str(),"","");

  if (station.boardChanged) {
    // prepare to scroll up primary services
    scrollPrimaryYpos = 11;
    isScrollingPrimary = true;
    // reset line3
    line3Service = 99;
    prevScrollStopsLength = 0;
    currentMessage=99;
    blankArea(0,ULINE3,256,11);
    serviceTimer=0;
  } else {
    // Draw the primary service line(s)
    if (station.numServices) {
      drawUndergroundService(0,ULINE1);
      if (station.numServices>1) drawUndergroundService(1,ULINE2);
    } else {
      u8g2.setFont(Underground10);
      centreText(F("There are no scheduled arrivals at this station."),ULINE1-1);
    }
  }
  for (int i=0;i<messages.numMessages;i++) {
    strcpy(line2[i],messages.messages[i]);
  }
  // Add attribution msg
  strcpy(line2[messages.numMessages],tflAttribution);
  messages.numMessages++;

  u8g2.sendBuffer();
}

/*
 *
 * Bus Departures Board
 *
 */
bool getBusDeparturesBoard() {
  if (!firstLoad) showUpdateIcon(true);
  lastUpdateResult = busdata->updateDepartures(&station,busAtco,&tflCallback);
  nextDataUpdate = millis()+BUSDATAUPDATEINTERVAL; // default update freq
  if (lastUpdateResult == UPD_SUCCESS || lastUpdateResult == UPD_NO_CHANGE) {
    showUpdateIcon(false);
    lastDataLoadTime=millis();
    noDataLoaded=false;
    dataLoadSuccess++;
    // Work out the max column size for service numbers
    busDestX=0;
    u8g2.setFont(NatRailSmall9);
    for (int i=0;i<station.numServices;i++) {
      int svcWidth = getStringWidth(station.service[i].via);
      busDestX = (busDestX > svcWidth) ? busDestX : svcWidth;
    }
    busDestX+=5;
    return true;
  } else if (lastUpdateResult == UPD_DATA_ERROR || lastUpdateResult == UPD_TIMEOUT) {
    lastLoadFailure=millis();
    dataLoadFailure++;
    nextDataUpdate = millis() + 30000; // 30 secs
    showUpdateIcon(false);
    return false;
  } else if (lastUpdateResult == UPD_UNAUTHORISED) {
    showTokenErrorScreen();
    while (true) { server.handleClient(); yield();}
  } else {
    showUpdateIcon(false);
    dataLoadFailure++;
    return false;
  }
}

void drawBusService(int serviceId, int y, int destPos) {
  char clipDestination[MAXLOCATIONSIZE];
  char etd[16];

  if (serviceId < station.numServices) {
    u8g2.setFont(NatRailSmall9);
    blankArea(0,y,256,9);

    u8g2.drawStr(0,y-1,station.service[serviceId].via);
    int etdWidth = 25;
    if (isDigit(station.service[serviceId].etd[0])) {
      sprintf(etd,"Exp %s",station.service[serviceId].etd);
      etdWidth = 47;
    } else strcpy(etd,station.service[serviceId].sTime);
    u8g2.drawStr(SCREEN_WIDTH - etdWidth,y-1,etd);

    // work out if we need to clip the destination
    strcpy(clipDestination,station.service[serviceId].destination);
    int spaceAvailable = SCREEN_WIDTH - destPos - etdWidth - 6;
    if (getStringWidth(clipDestination) > spaceAvailable) {
      while (getStringWidth(clipDestination) > spaceAvailable - 17) {
        clipDestination[strlen(clipDestination)-1] = '\0';
      }
      // check if there's a trailing space left
      if (clipDestination[strlen(clipDestination)-1] == ' ') clipDestination[strlen(clipDestination)-1] = '\0';
      strcat(clipDestination,"...");
    }
    u8g2.drawStr(destPos,y-1,clipDestination);
  }
}

// Draw/update the Bus Departures Board
void drawBusDeparturesBoard() {

  if (line3Service==0) line3Service=1;
  if (firstLoad) {
    // Clear the entire screen for the first load since boot up/wake from sleep
    u8g2.clearBuffer();
    u8g2.setContrast(brightness);
    firstLoad=false;
  } else {
      // Clear the top three lines
      blankArea(0,ULINE0,256,ULINE3-1);
  }
  drawStationHeader(busName.c_str(),"","");

  if (station.boardChanged) {
    // prepare to scroll up primary services
    scrollPrimaryYpos = 11;
    isScrollingPrimary = true;
    // reset line3
    if (station.numServices>2) {
      line3Service=2;
    } else {
      line3Service=99;
    }
    currentMessage = -1;
    blankArea(0,ULINE3,256,11);
    serviceTimer=0;
  } else {
    // Draw the primary service line(s)
    if (station.numServices) {
      drawBusService(0,ULINE1,busDestX);
      if (station.numServices>1) drawBusService(1,ULINE2,busDestX);
    } else {
      u8g2.setFont(NatRailSmall9);
      centreText(F("There are no scheduled services at this stop."),ULINE1-1);
    }
  }
  messages.numMessages=0;
  if (weatherEnabled && weatherMsg[0]) {
    strcpy(line2[messages.numMessages++],weatherMsg);
  }
  strcpy(line2[messages.numMessages++],btAttribution);
  u8g2.sendBuffer();
}

/*
 * Web GUI functions
 */

// Helper function for returning text status messages
void sendResponse(int code, const __FlashStringHelper* msg) {
  server.send(code, contentTypeText, msg);
}

void sendResponse(int code, String msg) {
  server.send(code, contentTypeText, msg);
}

// Return the correct MIME type for a file name
String getContentType(String filename) {
  if (server.hasArg(F("download"))) {
    return F("application/octet-stream");
  } else if (filename.endsWith(F(".htm"))) {
    return F("text/html");
  } else if (filename.endsWith(F(".html"))) {
    return F("text/html");
  } else if (filename.endsWith(F(".css"))) {
    return F("text/css");
  } else if (filename.endsWith(F(".js"))) {
    return F("application/javascript");
  } else if (filename.endsWith(F(".png"))) {
    return F("image/png");
  } else if (filename.endsWith(F(".gif"))) {
    return F("image/gif");
  } else if (filename.endsWith(F(".jpg"))) {
    return F("image/jpeg");
  } else if (filename.endsWith(F(".ico"))) {
    return F("image/x-icon");
  } else if (filename.endsWith(F(".xml"))) {
    return F("text/xml");
  } else if (filename.endsWith(F(".pdf"))) {
    return F("application/x-pdf");
  } else if (filename.endsWith(F(".zip"))) {
    return F("application/x-zip");
  } else if (filename.endsWith(F(".json"))) {
    return F("application/json");
  } else if (filename.endsWith(F(".gz"))) {
    return F("application/x-gzip");
  } else if (filename.endsWith(F(".svg"))) {
    return F("image/svg+xml");
  } else if (filename.endsWith(F(".webp"))) {
    return F("image/webp");
  }
  return F("text/plain");
}

// Stream a file from the file system
bool handleStreamFile(String filename) {
  if (LittleFS.exists(filename)) {
    File file = LittleFS.open(filename,"r");
    String contentType = getContentType(filename);
    server.streamFile(file, contentType);
    file.close();
    return true;
  } else return false;
}

// Stream a file stored in PROGMEM flash (default graphics are now included in the firmware image)
void handleStreamFlashFile(String filename, const uint8_t *filedata, size_t contentLength) {

  String contentType = getContentType(filename);
  WiFiClient client = server.client();
  // Send the headers
  client.println(F("HTTP/1.1 200 OK"));
  client.print(F("Content-Type: "));
  client.println(contentType);
  client.print(F("Content-Length: "));
  client.println(contentLength);
  client.println(F("Connection: close"));
  client.println(); // End of headers

  const size_t chunkSize = 512;
  uint8_t buffer[chunkSize];
  size_t sent = 0;

  while (sent < contentLength) {
    size_t toSend = min(chunkSize, contentLength - sent);
    // Copy from PROGMEM to buffer
    for (size_t i=0;i<toSend;i++) {
      buffer[i] = pgm_read_byte(&filedata[sent + i]);
    }
    client.write(buffer, toSend);
    sent += toSend;
  }
}

// Save the API keys POSTed from the keys.htm page
// If an OWM key is passed, this is tested before being committed to the file system. It's not possible
// to check the National Rail or TfL tokens at this point.
//
void handleSaveKeys() {
  String newJSON, owmToken;
  JsonDocument doc;
  bool result = true;
  String msg = F("API keys saved successfully!");

  if ((server.method() == HTTP_POST) && (server.hasArg("plain"))) {
    newJSON = server.arg("plain");
    // Deserialise to get the OWM API key
    DeserializationError error = deserializeJson(doc, newJSON);
    if (!error) {
      JsonObject settings = doc.as<JsonObject>();
      if (settings[F("owmToken")].is<const char*>()) {
        owmToken = settings[F("owmToken")].as<String>();
        if (owmToken.length()) {
          // Check if this is a valid token...
          if (!currentWeather.updateWeather(owmToken, "51.52", "-0.13")) {
            msg = F("OpenWeather Map API key is not valid. It may not yet be active if you have just created it. No changes have been saved.");
            result = false;
          }
        }
      }
      if (result) {
        if (!saveFile(F("/apikeys.json"),newJSON)) {
          msg = F("Failed to save the API keys to the file system (file system corrupt or full?)");
          result = false;
        }
      }
    } else {
      msg = F("Invalid JSON format. No changes have been saved.");
      result = false;
    }
    if (result) {
      sendResponse(200,msg);
      // Load/Update the API Keys in memory
      loadApiKeys();
      // If both the station codes are blank, we're in the setup process. If not, the keys have been changed so just reboot.
      if (!crsCode[0] && !tubeId[0]) {
        writeDefaultConfig();
        showSetupCrsHelpScreen();
      } else {
        delay(500);
        ESP.restart();
      }
    } else {
      sendResponse(400,msg);
    }
  } else {
    sendResponse(400,F("Invalid"));
  }
}

// Save configuration setting POSTed from index.htm
void handleSaveSettings() {
  String newJSON;

  if ((server.method() == HTTP_POST) && (server.hasArg("plain"))) {
    newJSON = server.arg("plain");
    saveFile(F("/config.json"),newJSON);
    if (!crsCode[0] && !tubeId[0]) {
      // First time setup, we need a full reboot
      sendResponse(200,F("Configuration saved. The system will now restart."));
      delay(1000);
      ESP.restart();
    } else {
      sendResponse(200,F("Configuration updated. The system will update shortly."));
      softResetBoard();
    }
  } else {
    // Something went wrong saving the config file
    sendResponse(400,F("The configuration could not be updated. The system will restart."));
    delay(1000);
    ESP.restart();
  }
}

/*
 * Expose the file system via the Web GUI with some basic functions for directory browsing, file reading and deletion.
 */

 // Return storage information
String getFSInfo() {
  char info[70];

  sprintf(info,"Total: %d bytes, Used: %d bytes\n",LittleFS.totalBytes(), LittleFS.usedBytes());
  return String(info);
}

// Send a basic directory listing to the browser
void handleFileList() {
  String path;
  if (!server.hasArg("dir")) path="/"; else path = server.arg("dir");
  File root = LittleFS.open(path);

  String output=F("<html><body>");
  if (!root) {
    output+=F("<p>Failed to open directory</p>");
  } else if (!root.isDirectory()) {
    output+=F("<p>Not a directory</p>");
  } else {
    output+=F("<table>");
    File file = root.openNextFile();
    while (file) {
      output+=F("<tr><td>");
      if (file.isDirectory()) {
        output+="[DIR]</td><td><a href=\"/rmdir?f=" + String(file.path()) + F("\">X</a></td><td><a href=\"/dir?dir=") + String(file.path()) + F("\">") + String(file.name()) + F("</a></td></tr>");
      } else {
        output+=String(file.size()) + F("</td><td><a href=\"/del?f=")+ String(file.path()) + F("\">X</a></td><td><a href=\"/cat?f=") + String(file.path()) + F("\">") + String(file.name()) + F("</a></td></tr>");
      }
      file = root.openNextFile();
    }
  }

  output += F("</table><br>");
  output += getFSInfo() + F("<p><a href=\"/upload\">Upload</a> a file</p></body></html>");
  server.send(200,contentTypeHtml,output);
}

// Stream a file to the browser
void handleCat() {
  String filename;

  if (server.hasArg(F("f"))) {
    handleStreamFile(server.arg("f"));
  } else sendResponse(404,F("Not found"));
}

// Delete a file from the file system
void handleDelete() {
  String filename;

  if (server.hasArg(F("f"))) {
    if (LittleFS.remove(server.arg(F("f")))) {
      // Successfully removed go back to directory listing
      server.sendHeader(F("Location"),F("/dir"));
      server.send(303);
    } else sendResponse(400,F("Failed to delete file"));
  } else sendResponse(404,F("Not found"));

}

// Format the file system
void handleFormatFFS() {
  String message;

  if (LittleFS.format()) {
    message=F("File System was successfully formatted\n\n");
    message+=getFSInfo();
  } else message=F("File System could not be formatted!");
  sendResponse(200,message);
}

// Upload a file from the browser
void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith(F("/"))) {
      filename = "/" + filename;
    }
    fsUploadFile = LittleFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    WiFiClient client = server.client();
    if (fsUploadFile) {
      fsUploadFile.close();
      client.println(F("HTTP/1.1 302 Found"));
      client.println(F("Location: /success"));
      client.println(F("Connection: close"));
    } else {
      client.println(F("HTTP/1.1 500 Could not create file"));
      client.println(F("Connection: close"));
    }
    client.println();
  }
}

/*
 * Web GUI handlers
 */

// Fallback function for browser requests
void handleNotFound() {
  if ((LittleFS.exists(server.uri())) && (server.method() == HTTP_GET)) handleStreamFile(server.uri());
  else if (server.uri() == F("/keys.htm")) handleStreamFlashFile(server.uri(), keyshtm, sizeof(keyshtm));
  else if (server.uri() == F("/index.htm")) handleStreamFlashFile(server.uri(), indexhtm, sizeof(indexhtm));
  else if (server.uri() == F("/nrelogo.webp")) handleStreamFlashFile(server.uri(), nrelogo, sizeof(nrelogo));
  else if (server.uri() == F("/tfllogo.webp")) handleStreamFlashFile(server.uri(), tfllogo, sizeof(tfllogo));
  else if (server.uri() == F("/btlogo.webp")) handleStreamFlashFile(server.uri(), btlogo, sizeof(btlogo));
  else if (server.uri() == F("/tube.webp")) handleStreamFlashFile(server.uri(), tubeicon, sizeof(tubeicon));
  else if (server.uri() == F("/nr.webp")) handleStreamFlashFile(server.uri(), nricon, sizeof(nricon));
  else if (server.uri() == F("/favicon.svg")) handleStreamFlashFile(server.uri(), favicon, sizeof(favicon));
  else if (server.uri() == F("/favicon.png")) handleStreamFlashFile(server.uri(), faviconpng, sizeof(faviconpng));
  else sendResponse(404,F("Not Found"));
}

// Send some useful system & station information to the browser
void handleInfo() {
  unsigned long uptime = millis();
  char sysUptime[30];
  int days = uptime / msDay ;
  int hours = (uptime % msDay) / msHour;
  int minutes = ((uptime % msDay) % msHour) / msMin;

  sprintf(sysUptime,"%d days, %d hrs, %d min", days,hours,minutes);

  String message = "Hostname: " + String(hostname) + F("\nFirmware version: v") + String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + " " + getBuildTime() + F("\nSystem uptime: ") + String(sysUptime) + F("\nFree Heap: ") + String(ESP.getFreeHeap()) + F("\nFree LittleFS space: ") + String(LittleFS.totalBytes() - LittleFS.usedBytes());
  message+="\nCore Plaform: " + String(ESP.getCoreVersion()) + F("\nCPU speed: ") + String(ESP.getCpuFreqMHz()) + F("MHz\nCPU Temperature: ") + String(temperatureRead()) + F("\nWiFi network: ") + String(WiFi.SSID()) + F("\nWiFi signal strength: ") + String(WiFi.RSSI()) + F("dB");
  getLocalTime(&timeinfo);

  sprintf(sysUptime,"%02d:%02d:%02d %02d/%02d/%04d",timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec,timeinfo.tm_mday,timeinfo.tm_mon+1,timeinfo.tm_year+1900);
  message+="\nSystem clock: " + String(sysUptime);
  message+="\nCRS station code: " + String(crsCode) + F("\nNaptan station code: ") + String(tubeId) + F("\nSuccessful: ") + String(dataLoadSuccess) + F("\nFailures: ") + String(dataLoadFailure) + F("\nTime since last data load: ") + String((int)((millis()-lastDataLoadTime)/1000)) + F(" seconds");
  if (dataLoadFailure) message+="\nTime since last failure: " + String((int)((millis()-lastLoadFailure)/1000)) + F(" seconds");
  message+=F("\nLast Result: ");
  switch (boardMode) {
    case MODE_RAIL:
      message+=raildata->getLastError();
      break;

    case MODE_TUBE:
      message+=tfldata->lastErrorMsg;
      break;

    case MODE_BUS:
      message+=busdata->lastErrorMsg;
      break;
  }
  message+="\nServices: " + String(station.numServices) + F("\nMessages: ");
  if (boardMode == MODE_TUBE) message+=String(messages.numMessages-1); else message+=String(messages.numMessages);
  message+=F("\n");
  if (boardMode != MODE_BUS) for (int i=0;i<messages.numMessages;i++) message+=String(messages.messages[i]) + "\n";
  message+=F("\nUpdate result code: ");
  switch (lastUpdateResult) {
    case UPD_SUCCESS:
      message+=F("SUCCESS");
      break;
    case UPD_NO_CHANGE:
      message+=F("SUCCESS (NO CHANGES)");
      break;
    case UPD_DATA_ERROR:
      message+=F("DATA ERROR");
      break;
    case UPD_UNAUTHORISED:
      message+=F("UNAUTHORISED");
      break;
    case UPD_HTTP_ERROR:
      message+=F("HTTP ERROR");
      break;
    case UPD_INCOMPLETE:
      message+=F("INCOMPLETE JSON RECEIVED");
      break;
    case UPD_NO_RESPONSE:
      message+=F("NO RESPONSE FROM SERVER");
      break;
    case UPD_TIMEOUT:
      message+=F("TIMEOUT WAITING FOR SERVER");
      break;
    default:
      message+="ERROR CODE (" + String(lastUpdateResult) + F(")");
      break;
  }
  sendResponse(200,message);
}

// Stream the index.htm page unless we're in first time setup and need the api keys
void handleRoot() {
  if (!nrToken[0] && tflAppkey=="") {
    if (LittleFS.exists(F("/keys.htm"))) handleStreamFile(F("/keys.htm")); else handleStreamFlashFile(F("/keys.htm"),keyshtm,sizeof(keyshtm));
  } else {
    if (LittleFS.exists(F("/index_d.htm"))) handleStreamFile(F("/index_d.htm")); else handleStreamFlashFile(F("/index.htm"),indexhtm,sizeof(indexhtm));
  }
}

// Send the firmware version to the client (called from index.htm)
void handleFirmwareInfo() {
  String response = "{\"firmware\":\"B" + String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + "-W" + String(WEBAPPVER_MAJOR) + "." + String(WEBAPPVER_MINOR) + F(" Build:") + getBuildTime() + F("\"}");
  server.send(200,contentTypeJson,response);
}

// Force a reboot of the ESP32
void handleReboot() {
  sendResponse(200,F("The Departures Board is restarting..."));
  delay(1000);
  ESP.restart();
}

// Erase the stored WiFiManager credentials
void handleEraseWiFi() {
  sendResponse(200,F("Erasing stored WiFi. You will need to connect to the \"Departures Board\" network and use WiFi Manager to reconfigure."));
  delay(1000);
  WiFiManager wm;
  wm.resetSettings();
  delay(500);
  ESP.restart();
}

// "Factory reset" the app - delete WiFi, format file system and reboot
void handleFactoryReset() {
  sendResponse(200,F("Factory reseting the Departures Board..."));
  delay(1000);
  WiFiManager wm;
  wm.resetSettings();
  delay(500);
  LittleFS.format();
  delay(500);
  ESP.restart();
}

// Interactively change the brightness of the OLED panel (called from index.htm)
void handleBrightness() {
  if (server.hasArg(F("b"))) {
    int level = server.arg(F("b")).toInt();
    if (level>0 && level<256) {
      u8g2.setContrast(level);
      brightness = level;
      sendResponse(200,F("OK"));
      return;
    }
  }
  sendResponse(200,F("invalid request"));
}

// Web GUI has requested updates be installed
void handleOtaUpdate() {
  sendResponse(200,F("Update initiated - check Departure Board display for progress"));
  delay(500);
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText(F("Getting latest firmware details from GitHub..."),26);
  u8g2.sendBuffer();

  if (ghUpdate.getLatestRelease()) {
    checkForFirmwareUpdate();
  } else {
    for (int i=15;i>=0;i--) {
      showUpdateCompleteScreen("Firmware Update Check Failed","Unable to retrieve latest release information.",ghUpdate.getLastError().c_str(),"",i,false);
      delay(1000);
    }
    log_e("FW Update failed: %s\n",ghUpdate.getLastError().c_str());
  }
  // Always restart
  ESP.restart();
}

/*
 * External data functions - weather, stationpicker, firmware updates
 */

// Call the National Rail Station Picker (called from index.htm)
void handleStationPicker() {
  if (!server.hasArg(F("q"))) {
    sendResponse(400, F("Missing Query"));
    return;
  }

  String query = server.arg(F("q"));
  if (query.length() <= 2) {
    sendResponse(400, F("Query Too Short"));
    return;
  }

  const char* host = "stationpicker.nationalrail.co.uk";
  WiFiClientSecure httpsClient;
  httpsClient.setInsecure();
  httpsClient.setTimeout(10000);

  int retryCounter = 0;
  while (!httpsClient.connect(host, 443) && retryCounter++ < 20) {
    delay(50);
  }

  if (retryCounter >= 20) {
    sendResponse(408, F("NR Timeout"));
    return;
  }

  httpsClient.print(String("GET /stationPicker/") + query + F(" HTTP/1.0\r\n") +
                    F("Host: stationpicker.nationalrail.co.uk\r\n") +
                    F("Referer: https://www.nationalrail.co.uk\r\n") +
                    F("Origin: https://www.nationalrail.co.uk\r\n") +
                    F("Connection: close\r\n\r\n"));

  // Wait for response header
  retryCounter = 0;
  while (!httpsClient.available() && retryCounter++ < 15) {
    delay(100);
  }

  if (!httpsClient.available()) {
    httpsClient.stop();
    sendResponse(408, F("NRQ Timeout"));
    return;
  }

  // Parse status code
  String statusLine = httpsClient.readStringUntil('\n');
  if (!statusLine.startsWith(F("HTTP/")) || statusLine.indexOf(F("200 OK")) == -1) {
    httpsClient.stop();

    if (statusLine.indexOf(F("401")) > 0) {
      sendResponse(401, F("Not Authorized"));
    } else if (statusLine.indexOf(F("500")) > 0) {
      sendResponse(500, F("Server Error"));
    } else {
      sendResponse(503, statusLine.c_str());
    }
    return;
  }

  // Skip the remaining headers
  while (httpsClient.connected() || httpsClient.available()) {
    String line = httpsClient.readStringUntil('\n');
    if (line == "\r") break;
  }

  // Start sending response
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, contentTypeJson, "");

  String buffer;
  unsigned long timeout = millis() + 5000UL;

  while ((httpsClient.connected() || httpsClient.available()) && millis() < timeout) {
    while (httpsClient.available()) {
      char c = httpsClient.read();
      if (c <= 128) buffer += c;
      if (buffer.length() >= 1024) {
        server.sendContent(buffer);
        buffer = "";
        yield();
      }
    }
  }

  // Flush remaining buffer
  if (buffer.length()) {
    server.sendContent(buffer);
  }

  httpsClient.stop();
  server.sendContent("");
  server.client().stop();
}

// Update the current weather message if weather updates are enabled and we have a lat/lon for the selected location
void updateCurrentWeather(float latitude, float longitude) {
  nextWeatherUpdate = millis() + 1200000; // update every 20 mins
  if (!latitude || !longitude) return; // No location co-ordinates
  strcpy(weatherMsg,"");
  bool currentWeatherValid = currentWeather.updateWeather(openWeatherMapApiKey, String(latitude), String(longitude));
  if (currentWeatherValid) {
    currentWeather.currentWeather.toCharArray(weatherMsg,sizeof(weatherMsg));
    weatherMsg[0] = toUpperCase(weatherMsg[0]);
    weatherMsg[sizeof(weatherMsg)-1] = '\0';
  } else {
    nextWeatherUpdate = millis() + 30000; // Try again in 30s
  }
}

/*
 * Setup / Loop functions
*/

//
// The main processing cycle for the National Rail Departures Board
//
void departureBoardLoop() {

  if (altStationEnabled && !isSleeping && altStationActive != isAltActive()) softResetBoard(); // Switch between station views

  if ((millis() > nextDataUpdate) && (!isScrollingStops) && (!isScrollingService) && (lastUpdateResult != UPD_UNAUTHORISED) && (!isSleeping) && (wifiConnected)) {
    timer = millis() + 2000;
    if (getStationBoard()) {
      if ((lastUpdateResult == UPD_SUCCESS) || (lastUpdateResult == UPD_NO_CHANGE && firstLoad)) drawStationBoard(); // Something changed so redraw the board.
    } else if (lastUpdateResult == UPD_UNAUTHORISED) showTokenErrorScreen();
	  else if (lastUpdateResult == UPD_DATA_ERROR) {
	    if (noDataLoaded) showNoDataScreen();
	    else drawStationBoard();
	  } else if (noDataLoaded) showNoDataScreen();
  } else if (weatherEnabled && (millis()>nextWeatherUpdate) && (!noDataLoaded) && (!isScrollingStops) && (!isScrollingService) && (!isSleeping) && (wifiConnected)) {
    updateCurrentWeather(stationLat,stationLon);
  }

  if (millis()>timer && numMessages && !isScrollingStops && !isSleeping && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR && !noScrolling) {
    // Need to start a new scrolling line 2
    prevMessage = currentMessage;
    prevScrollStopsLength = scrollStopsLength;
    currentMessage++;
    if (currentMessage>=numMessages) currentMessage=0;
    scrollStopsXpos=0;
    scrollStopsYpos=10;
    scrollStopsLength = getStringWidth(line2[currentMessage]);
    isScrollingStops=true;
  }

  // Check if there's a via destination
  if (millis()>viaTimer) {
    if (station.numServices && station.service[0].via[0] && !isSleeping && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
      isShowingVia = !isShowingVia;
      drawPrimaryService(isShowingVia);
      u8g2.updateDisplayArea(0,1,32,3);
      if (isShowingVia) viaTimer = millis()+3000; else viaTimer = millis()+4000;
    }
  }

  if (millis()>serviceTimer && !isScrollingService && !isSleeping && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
    // Need to change to the next service if there is one
    if (station.numServices <= 1 && !weatherMsg[0]) {
      // There's no other services and no weather so just so static attribution.
      drawServiceLine(1,LINE3); //TODO?
      serviceTimer = millis() + 30000;
      isScrollingService = false;
    } else {
      prevService = line3Service;
      line3Service++;
      if (station.numServices) {
        if ((line3Service>station.numServices && !weatherMsg[0]) || (line3Service>station.numServices+1 && weatherMsg[0])) line3Service=(noScrolling && station.numServices>1) ? 2:1;  // First 'other' service
      } else {
        if (weatherMsg[0] && line3Service>1) line3Service=0;
      }
      scrollServiceYpos=10;
      isScrollingService = true;
    }
  }

  if (isScrollingStops && millis()>timer && !isSleeping && !noScrolling) {
    blankArea(0,LINE2,256,9);
    if (scrollStopsYpos) {
      // we're scrolling up the message initially
      u8g2.setClipWindow(0,LINE2,256,LINE2+9);
      // if the previous message didn't scroll then we need to scroll it up off the screen
      if (prevScrollStopsLength && prevScrollStopsLength<256 && strncmp("Calling",line2[prevMessage],7)) centreText(line2[prevMessage],scrollStopsYpos+LINE2-12);
      if (scrollStopsLength<256 && strncmp("Calling",line2[currentMessage],7)) centreText(line2[currentMessage],scrollStopsYpos+LINE2-2); // Centre text if it fits
      else u8g2.drawStr(0,scrollStopsYpos+LINE2-2,line2[currentMessage]);
      u8g2.setMaxClipWindow();
      scrollStopsYpos--;
      if (scrollStopsYpos==0) timer=millis()+1500;
    } else {
      // we're scrolling left
      if (scrollStopsLength<256 && strncmp("Calling",line2[currentMessage],7)) centreText(line2[currentMessage],LINE2-1); // Centre text if it fits
      else u8g2.drawStr(scrollStopsXpos,LINE2-1,line2[currentMessage]);
      if (scrollStopsLength < 256) {
        // we don't need to scroll this message, it fits so just set a longer timer
        timer=millis()+6000;
        isScrollingStops=false;
      } else {
        scrollStopsXpos--;
        if (scrollStopsXpos < -scrollStopsLength) {
          isScrollingStops=false;
          timer=millis()+500;  // pause before next message
        }
      }
    }
  }

  if (isScrollingService && millis()>serviceTimer && !isSleeping) {
    blankArea(0,LINE3,256,9);
    if (scrollServiceYpos) {
      // we're scrolling the service into view
      u8g2.setClipWindow(0,LINE3,256,LINE3+9);
      // if the prev service is showing, we need to scroll it up off
      if (prevService>0) drawServiceLine(prevService,scrollServiceYpos+LINE3-12);
      drawServiceLine(line3Service,scrollServiceYpos+LINE3-1);
      u8g2.setMaxClipWindow();
      scrollServiceYpos--;
      if (scrollServiceYpos==0) {
        serviceTimer=millis()+5000;
        isScrollingService=false;
      }
    }
  }

  if (!isSleeping) {
    // Check if the clock should be updated
    doClockCheck();

    // To ensure a consistent refresh rate (for smooth text scrolling), we update the screen every 25ms (around 40fps)
    // so we need to wait any additional ms not used by processing so far before sending the frame to the display controller
    long delayMs = fpsDelay - (millis()-refreshTimer);
    if (delayMs>0) delay(delayMs);
    u8g2.updateDisplayArea(0,3,32,4);
    refreshTimer=millis();
  }
}

//
// Processing loop for London Underground Arrivals board
//
void undergroundArrivalsLoop() {
  char serviceData[8+MAXLINESIZE+MAXLOCATIONSIZE];
  bool fullRefresh = false;

  if (millis()>nextDataUpdate && !isScrollingService && !isScrollingPrimary && !isSleeping && wifiConnected) {
    if (getUndergroundBoard()) {
      if (lastUpdateResult == UPD_SUCCESS || lastUpdateResult == UPD_NO_CHANGE) drawUndergroundBoard();
    } else if (lastUpdateResult == UPD_UNAUTHORISED) showTokenErrorScreen();
	  else if (lastUpdateResult == UPD_DATA_ERROR) {
	    if (noDataLoaded) showNoDataScreen();
	    else drawUndergroundBoard();
	  } else if (noDataLoaded) showNoDataScreen();
  }

    // Scrolling the additional services
  if (millis()>serviceTimer && !isScrollingService && !isSleeping && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
    if (station.numServices<=2 && messages.numMessages==1 && attributionScrolled) {
      // There are no additional services to scroll in so static attribution.
      serviceTimer = millis() + 30000;
    } else {
      // Need to change to the next service or message if there is one
      attributionScrolled = true;
      prevService = line3Service;
      line3Service++;
      scrollServiceYpos=11;
      scrollStopsXpos=0;
      isScrollingService = true;
      if (line3Service>=station.numServices) {
        // Showing the messages
        prevMessage = currentMessage;
        prevScrollStopsLength = scrollStopsLength;  // Save the length of the previous message
        currentMessage++;
        if (currentMessage>=messages.numMessages) {
          if (station.numServices>2) {
            line3Service=2;
            currentMessage=-1; // Rollover back to services
          } else {
            line3Service = station.numServices;
            currentMessage=0;
          }
        }
        scrollStopsLength = getStringWidth(line2[currentMessage]);
      } else {
        scrollStopsLength=SCREEN_WIDTH;
      }
    }
  }

  if (isScrollingService && millis()>serviceTimer && !isSleeping) {
    blankArea(0,ULINE3,256,10);
    if (scrollServiceYpos) {
      // we're scrolling up the message initially
      u8g2.setClipWindow(0,ULINE3,256,ULINE3+10);
      // Was the previous display a service?
      if (prevService<station.numServices) {
        drawUndergroundService(prevService,scrollServiceYpos+ULINE3-13);
      } else {
        // if the previous message didn't scroll then we need to scroll it up off the screen
        if (prevScrollStopsLength && prevScrollStopsLength<256) centreText(line2[prevMessage],scrollServiceYpos+ULINE3-13);
      }
      // Is this entry a service?
      if (line3Service<station.numServices) {
        drawUndergroundService(line3Service,scrollServiceYpos+ULINE3-1);
      } else {
        if (scrollStopsLength<256) centreText(line2[currentMessage],scrollServiceYpos+ULINE3-2); // Centre text if it fits
        else u8g2.drawStr(0,scrollServiceYpos+ULINE3-2,line2[currentMessage]);
      }
      u8g2.setMaxClipWindow();
      scrollServiceYpos--;
      if (scrollServiceYpos==0) {
        if (line3Service<station.numServices) {
          serviceTimer=millis()+3500;
          isScrollingService=false;
        } else {
          serviceTimer=millis()+500;
        }
      }
    } else {
      // we're scrolling left
      if (scrollStopsLength<256) centreText(line2[currentMessage],ULINE3-1); // Centre text if it fits
      else u8g2.drawStr(scrollStopsXpos,ULINE3-1,line2[currentMessage]);
      if (scrollStopsLength < 256) {
        // we don't need to scroll this message, it fits so just set a longer timer
        serviceTimer=millis()+3000;
        isScrollingService=false;
      } else {
        scrollStopsXpos--;
        if (scrollStopsXpos < -scrollStopsLength) {
          isScrollingService=false;
          serviceTimer=millis()+500;  // pause before next message
        }
      }
    }
  }

  if (isScrollingPrimary && !isSleeping) {
    blankArea(0,ULINE1,256,ULINE3-ULINE1);
    fullRefresh = true;
    // we're scrolling the primary service(s) into view
    u8g2.setClipWindow(0,ULINE1,256,ULINE1+10);
    if (station.numServices) drawUndergroundService(0,scrollPrimaryYpos+ULINE1-1);
    else centreText(F("There are no scheduled arrivals at this station."),scrollPrimaryYpos+ULINE1-1);
    if (station.numServices>1) {
      u8g2.setClipWindow(0,ULINE2,256,ULINE2+10);
      drawUndergroundService(1,scrollPrimaryYpos+ULINE2-1);
    }
    u8g2.setMaxClipWindow();
    scrollPrimaryYpos--;
    if (scrollPrimaryYpos==0) {
      isScrollingPrimary=false;
    }
  }

  if (!isSleeping) {
    // Check if the clock should be updated
    if (millis()>nextClockUpdate) {
      nextClockUpdate = millis()+500;
      drawCurrentTimeUG(true);
    }

    long delayMs = 18 - (millis()-refreshTimer);
    if (delayMs>0) delay(delayMs);
    if (fullRefresh) u8g2.updateDisplayArea(0,1,32,6); else u8g2.updateDisplayArea(0,5,32,2);
    refreshTimer=millis();
  }
}

//
// Processing loop for Bus Departures board
//
void busDeparturesLoop() {
  char serviceData[8+MAXLINESIZE+MAXLOCATIONSIZE];
  bool fullRefresh = false;

  if (millis()>nextDataUpdate && !isScrollingService && !isScrollingPrimary && !isSleeping && wifiConnected) {
    if (getBusDeparturesBoard()) {
      if (lastUpdateResult == UPD_SUCCESS || lastUpdateResult == UPD_NO_CHANGE) drawBusDeparturesBoard(); // Something changed so redraw the board.
    } else if (lastUpdateResult == UPD_UNAUTHORISED) showTokenErrorScreen();
	  else if (lastUpdateResult == UPD_DATA_ERROR) {
	    if (noDataLoaded) showNoDataScreen();
	    else drawBusDeparturesBoard();
	  } else if (noDataLoaded) showNoDataScreen();
  } else if (weatherEnabled && millis()>nextWeatherUpdate && !noDataLoaded && !isScrollingService && !isScrollingPrimary && !isSleeping && wifiConnected) {
    updateCurrentWeather(busLat,busLon);
    // Update the weather text immediately
    if (weatherMsg[0]) {
      strcpy(line2[1],btAttribution);
      strcpy(line2[0],weatherMsg);
      messages.numMessages=2;
    }
  }

  // Scrolling the additional services
  if (millis()>serviceTimer && !isScrollingPrimary && !isScrollingService && !isSleeping && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
    // Need to change to the next service if there is one
    if (station.numServices<=2 && messages.numMessages==1) {
      // There are no additional services or weather to scroll in so static attribution.
      serviceTimer = millis() + 10000;
      line3Service=station.numServices;
    } else {
      // Need to change to the next service or message
      prevService = line3Service;
      line3Service++;
      scrollServiceYpos=11;
      isScrollingService = true;
      if (line3Service>=station.numServices) {
        // Showing the messages
        prevMessage = currentMessage;
        currentMessage++;
        if (currentMessage>=messages.numMessages) {
          if (station.numServices>2) {
            line3Service = 2;
            currentMessage=-1; // Rollover back to services
          } else {
            line3Service = station.numServices;
            currentMessage=0;
          }
        }
      }
    }
  }

  if (isScrollingService && millis()>serviceTimer && !isSleeping) {
    if (scrollServiceYpos) {
      blankArea(0,ULINE3,256,10);
      // we're scrolling up the message
      u8g2.setClipWindow(0,ULINE3,256,ULINE3+10);
      // Was the previous display a service?
      if (prevService<station.numServices) {
        drawBusService(prevService,scrollServiceYpos+ULINE3-13,busDestX);
      } else {
        // Scrolling up the previous message
        centreText(line2[prevMessage],scrollServiceYpos+ULINE3-13);
      }
      // Is this entry a service?
      if (line3Service<station.numServices) {
        drawBusService(line3Service,scrollServiceYpos+ULINE3-1,busDestX);
      } else {
        centreText(line2[currentMessage],scrollServiceYpos+ULINE3-2);
      }
      u8g2.setMaxClipWindow();
      scrollServiceYpos--;
      if (scrollServiceYpos==0) {
        serviceTimer = millis()+2800;
        if (station.numServices<=2) serviceTimer+=3000;
      }
    } else isScrollingService=false;
  }

  if (isScrollingPrimary && !isSleeping) {
    blankArea(0,ULINE1,256,ULINE3-ULINE1+10);
    fullRefresh = true;
    // we're scrolling the primary service(s) into view
    u8g2.setClipWindow(0,ULINE1,256,ULINE1+10);
    if (station.numServices) drawBusService(0,scrollPrimaryYpos+ULINE1-1,busDestX);
    else centreText(F("There are no scheduled services at this stop."),scrollPrimaryYpos+ULINE1-1);
    if (station.numServices>1) {
      u8g2.setClipWindow(0,ULINE2,256,ULINE2+10);
      drawBusService(1,scrollPrimaryYpos+ULINE2-1,busDestX);
    }
    if (station.numServices>2) {
      u8g2.setClipWindow(0,ULINE3,256,ULINE3+10);
      drawBusService(2,scrollPrimaryYpos+ULINE3-1,busDestX);
    } else if (station.numServices<3 && messages.numMessages==1) {
      // scroll up the attribution once...
      u8g2.setClipWindow(0,ULINE3,256,ULINE3+10);
      centreText(btAttribution,scrollPrimaryYpos+ULINE3-1);
    }
    u8g2.setMaxClipWindow();
    scrollPrimaryYpos--;
    if (scrollPrimaryYpos==0) {
      isScrollingPrimary=false;
      serviceTimer = millis()+2800;
    }
  }

  if (!isSleeping) {
    // Check if the clock should be updated
    if (millis()>nextClockUpdate) {
      nextClockUpdate = millis()+500;
      drawCurrentTimeUG(true);    // just use the Tube clock for bus mode
      u8g2.setFont(NatRailSmall9);
    }

    long delayMs = 40 - (millis()-refreshTimer);
    if (delayMs>0) delay(delayMs);
    if (fullRefresh) u8g2.updateDisplayArea(0,1,32,6); else u8g2.updateDisplayArea(0,5,32,2);
    refreshTimer=millis();
  }
}

//
// Setup code
//
void setup(void) {
  // These are the default wsdl XML SOAP entry points. They can be overridden in the config.json file if necessary
  strncpy(wsdlHost,"lite.realtime.nationalrail.co.uk",sizeof(wsdlHost));
  strncpy(wsdlAPI,"/OpenLDBWS/wsdl.aspx?ver=2021-11-01",sizeof(wsdlAPI));
  u8g2.begin();                       // Start the OLED panel
  u8g2.setContrast(brightness);       // Initial brightness
  u8g2.setDrawColor(1);               // Only a monochrome display, so set the colour to "on"
  u8g2.setFontMode(1);                // Transparent fonts
  u8g2.setFontRefHeightAll();         // Count entire font height
  u8g2.setFontPosTop();               // Reference from top

  u8g2.drawXBM(81,0,gadeclogo_width,gadeclogo_height,gadeclogo_bits);
  u8g2.setFont(NatRailTall12);
  String buildDate = String(__DATE__);
  String notice = "\x80 " + buildDate.substring(buildDate.length()-4) + F(" Gadec Software (github.com/gadec-uk)");
  centreText(notice.c_str(),48);
  u8g2.sendBuffer();                  // Send to OLED display

  bool isFSMounted = LittleFS.begin(true);    // Start the File System, format if necessary
  strcpy(station.location,"");                // No default location
  strcpy(weatherMsg,"");                      // No weather message
  strcpy(nrToken,"");                         // No default National Rail token
  tflAppkey="";                               // No default TfL AppKey
  loadApiKeys();                              // Load the API keys from the apiKeys.json
  loadConfig();                               // Load the configuration settings from config.json
  u8g2.setContrast(brightness);               // Set the panel brightness to the user saved level
  delay(5000);

  u8g2.clearBuffer();
  drawStartupHeading();
  u8g2.sendBuffer();
  progressBar(F("Connecting to Wi-Fi"),20);
  WiFi.mode(WIFI_MODE_NULL);        // Reset the WiFi
  WiFi.setSleep(WIFI_PS_NONE);      // Turn off WiFi Powersaving
  WiFi.hostname(hostname);          // Set the hostname ("Departures Board")
  WiFi.mode(WIFI_STA);              // Enter WiFi station mode
  WiFiManager wm;                   // Start WiFiManager
  wm.setAPCallback(wmConfigModeCallback);     // Set the callback for config mode notification
  wm.setWiFiAutoReconnect(true);              // Attempt to auto-reconnect WiFi
  wm.setConnectTimeout(8);
  wm.setConnectRetries(2);

  bool result = wm.autoConnect("Departures Board");    // Attempt to connect to WiFi (or enter interactive configuration mode)
  if (!result) {
      // Failed to connect/configure
      ESP.restart();
  }

  // Wait for WiFi connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }

  // Get our IP address and store
  updateMyUrl();
  if (MDNS.begin(hostname)) {
    MDNS.addService("http","tcp",80);
  }

  wifiConnected=true;
  WiFi.setAutoReconnect(true);
  u8g2.clearBuffer();                                             // Clear the display
  drawStartupHeading();                                           // Draw the startup heading
  char ipBuff[17];
  WiFi.localIP().toString().toCharArray(ipBuff,sizeof(ipBuff));   // Get the IP address of the ESP32
  centreText(ipBuff,53);                                          // Display the IP address
  progressBar(F("Wi-Fi Connected"),30);
  u8g2.sendBuffer();                                              // Send to OLED panel

  // Configure the local webserver paths
  server.on(F("/"),handleRoot);
  server.on(F("/erasewifi"),handleEraseWiFi);
  server.on(F("/factoryreset"),handleFactoryReset);
  server.on(F("/info"),handleInfo);
  server.on(F("/formatffs"),handleFormatFFS);
  server.on(F("/dir"),handleFileList);
  server.onNotFound(handleNotFound);
  server.on(F("/cat"),handleCat);
  server.on(F("/del"),handleDelete);
  server.on(F("/reboot"),handleReboot);
  server.on(F("/stationpicker"),handleStationPicker);           // Used by the Web GUI to lookup station codes interactively
  server.on(F("/firmware"),handleFirmwareInfo);                 // Used by the Web GUI to display the running firmware version
  server.on(F("/savesettings"),HTTP_POST,handleSaveSettings);   // Used by the Web GUI to save updated configuration settings
  server.on(F("/savekeys"),HTTP_POST,handleSaveKeys);           // Used by the Web GUI to verify/save API keys
  server.on(F("/brightness"),handleBrightness);                 // Used by the Web GUI to interactively set the panel brightness
  server.on(F("/ota"),handleOtaUpdate);                         // Used by the Web GUI to initiate a manual firmware/WebApp update

  server.on(F("/update"), HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, contentTypeHtml, updatePage);
  });
  /*handling uploading firmware file */
  server.on(F("/update"), HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    sendResponse(200,(Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        //Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        //Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
      } else {
        //Update.printError(Serial);
      }
    }
  });

  server.on(F("/upload"), HTTP_GET, []() {
      server.send(200, contentTypeHtml, uploadPage);
  });
  server.on(F("/upload"), HTTP_POST, []() {
  }, handleFileUpload);

  server.on(F("/success"), HTTP_GET, []() {
    server.send(200, contentTypeHtml, successPage);
  });

  server.begin();     // Start the local web server

  // Check for Firmware/GUI updates?
  if (firmwareUpdates) {
    progressBar(F("Checking for firmware updates"),40);
    if (ghUpdate.getLatestRelease()) {
      checkForFirmwareUpdate();
    } else {
      for (int i=15;i>=0;i--) {
        showUpdateCompleteScreen("Firmware Update Check Failed","Unable to retrieve latest release information.",ghUpdate.getLastError().c_str(),"",i,false);
        delay(1000);
      }
      u8g2.clearDisplay();
      drawStartupHeading();
      u8g2.sendBuffer();
    }
  }

  // First time configuration?
  if ((!crsCode[0] && !tubeId[0]) || (!nrToken[0] && tflAppkey=="")) {
    if (!nrToken[0] && tflAppkey=="") showSetupKeysHelpScreen();
    else showSetupCrsHelpScreen();
    // First time setup mode will exit with a reboot, so just loop here forever servicing web requests
    while (true) {
      yield();
      server.handleClient();
    }
  }

  checkPostWebUpgrade();

  configTime(0,0, ntpServer);                 // Configure NTP server for setting the clock
  setenv("TZ","GMT0BST,M3.5.0/1,M10.5.0",1);  // Configure UK TimeZone
  tzset();                                    // Set the TimeZone

  // Check the clock has been set successfully before continuing
  int p=50;
  int ntpAttempts=0;
  bool ntpResult=true;
  progressBar(F("Setting the system clock..."),50);
  if(!getLocalTime(&timeinfo)) {              // attempt to set the clock from NTP
    do {
      delay(500);                             // If no NTP response, wait 500ms and retry
      ntpResult = getLocalTime(&timeinfo);
      ntpAttempts++;
      p+=5;
      progressBar(F("Setting the system clock..."),p);
      if (p>80) p=45;
    } while ((!ntpResult) && (ntpAttempts<10));
  }
  if (!ntpResult) {
    // Sometimes NTP/UDP fails. A reboot usually fixes it.
    progressBar(F("Failed to set the clock. Rebooting in 5 sec."),0);
    delay(5000);
    ESP.restart();
  }

  station.numServices=0;
  if (boardMode == MODE_RAIL) {
      progressBar(F("Initialising National Rail interface"),60);
      altStationActive = setAlternateStation();  // Check & set the alternate station if appropriate
      raildata = new raildataXmlClient();
      int res = raildata->init(wsdlHost, wsdlAPI, &raildataCallback);
      if (res != UPD_SUCCESS) {
        showWsdlFailureScreen();
        while (true) { server.handleClient(); yield();}
      }
      progressBar(F("Initialising National Rail interface"),70);
  } else if (boardMode == MODE_TUBE) {
      tfldata = new TfLdataClient();
      progressBar(F("Initialising TfL interface"),70);
      startupProgressPercent=70;
  } else if (boardMode == MODE_BUS) {
      busdata = new busDataClient();
      progressBar(F("Initialising BusTimes interface"),70);
      startupProgressPercent=70;
  }
}


void loop(void) {

  isSleeping = isSnoozing();

  if (isSleeping && millis()>timer) {       // If the "screensaver" is active, change the screen every 8 seconds
    drawSleepingScreen();
    timer=millis()+8000;
  }

  // WiFi Status icon
  if (WiFi.status() != WL_CONNECTED && wifiConnected) {
    wifiConnected=false;
    u8g2.setFont(NatRailSmall9);
    u8g2.drawStr(0,56,"\x7F");  // No Wifi Icon
    u8g2.updateDisplayArea(0,7,1,1);
  } else if (WiFi.status() == WL_CONNECTED && !wifiConnected) {
    wifiConnected=true;
    blankArea(0,57,5,7);
    u8g2.updateDisplayArea(0,7,1,1);
    updateMyUrl();  // in case our IP changed
  }

  // Force a manual reset if we've been disconnected for more than 10 secs
  if (WiFi.status() != WL_CONNECTED && millis() > lastWiFiReconnect+10000) {
    WiFi.disconnect();
    delay(100);
    WiFi.reconnect();
    lastWiFiReconnect=millis();
  }

  switch (boardMode) {
    case MODE_RAIL:
      departureBoardLoop();
      break;

    case MODE_TUBE:
      undergroundArrivalsLoop();
      break;

    case MODE_BUS:
      busDeparturesLoop();
      break;
  }

  server.handleClient();
}
