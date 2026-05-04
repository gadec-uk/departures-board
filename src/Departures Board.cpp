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
 * Optional TTP223 touch sensor connection:
 *
 * TTP223         ESP32 MINI
 * GND            GND
 * VCC            3.3V
 * I/O            IO34
 *
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <StreamString.h>
#include <Ticker.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <HTTPUpdateGitHub.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <weatherClient.h>
#include <sharedDataStructs.h>
#include <responseCodes.h>
#include <raildataXmlClient.h>
#include <rdmRailClient.h>
#include <TfLdataClient.h>
#include <busDataClient.h>
#include <githubClient.h>
#include <rssClient.h>
#include <touchSensor.h>
#include <webgui/webgraphics.h>
#include <webgui/index.h>
#include <webgui/keys.h>
#include <webgui/editrss.h>
#include <webgui/rss.h>
#include <gfx/xbmgfx.h>
#include <time.h>

#include <SPI.h>
#include <U8g2lib.h>

#define msDay 86400000 // 86400000 milliseconds in a day
#define msHour 3600000 // 3600000 milliseconds in an hour
#define msMin 60000 // 60000 milliseconds in a second

static AsyncWebServer server(80); // Hosting the Web GUI

// Shorthand for response formats
static const char contentTypeJson[] = "application/json";
static const char contentTypeText[] = "text/plain";
static const char contentTypeHtml[] = "text/html";

// Using NTP to set and maintain the clock
static struct tm timeinfo;
static const char ukTimezone[] = "GMT0BST,M3.5.0/1,M10.5.0";

// Default hostname
static const char defaultHostname[] = "DeparturesBoard";

// Local firmware updates via /update Web GUI
static const char updatePage[] =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<html><body style=\"font-family:Helvetica,Arial,sans-serif\"><h2>Departures Board Manual Update</h2><p>Upload a <b>firmware.bin</b> file.</p>"
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
static const char uploadPage[] =
"<html><body style=\"font-family:Helvetica,Arial,sans-serif\">"
"<h2>Upload a file to the file system</h2><form method='post' enctype='multipart/form-data'><input type='file' name='name'>"
"<input class='button' type='submit' value='Upload'></form></body></html>";

// /success page
static const char successPage[] =
"<html><body style=\"font-family:Helvetica,Arial,sans-serif\"><h3>Upload completed successfully.</h3>\n"
"<p><a href=\"/dir\">List file system directory</a></p>\n"
"<h2>Upload another file</h2><form method=\"post\" action=\"/upload\" enctype=\"multipart/form-data\"><input type=\"file\" name=\"name\"><input class=\"button\" type=\"submit\" value=\"Upload\"></form>\n"
"</body></html>";

#define SCREEN_WIDTH 256 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define DIMMED_BRIGHTNESS 1 // OLED display brightness level when in sleep/screensaver mode

U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ GPIO_NUM_26, /* dc=*/ GPIO_NUM_5, /* reset=*/ U8X8_PIN_NONE);

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

static Ticker restartTimer; // used to schedule reboots

//
// Custom fonts - replicas of those used on the real display boards
//
static const uint8_t NatRailSmall9[1329] U8G2_FONT_SECTION("NatRailSmall9") =
  "\221\0\3\2\4\4\4\5\5\11\11\0\0\11\0\11\2\1E\2\214\5\30 \5\0\230)!\7q("
  "%\6%\42\10\63h)\22K\0#\17u(mJI\62(\225A)%\11\0$\14u(\255\262"
  "\245\266%\311\26\1%\14t(+\42E\212\244H\221\2&\16u(m\244\222\224$R\22EJ\0"
  "'\6\61h%\6(\11s(\251\222R\255\0)\11s()\262R\245\4*\12U\70-\222\312b"
  "Y\32+\12U\70\255\302h\220\302\10,\7\62\10g\22\5-\6\23X)\6.\6\21(%\2/"
  "\13t(\353\42)\222\42)\3\60\12u(m\226\314[\262\0\61\11s)m\22\251\313\0\62\13u"
  "(m\226,\314\332\6\1\63\14u(m\226,\214T-Y\0\64\15u(\355\62))%\203\26&"
  "\0\65\13u(-\216C\32j\311\2\66\15u(m\226L\34\222LK\26\0\67\12u(-\6\61"
  "+\66\1\70\15u(m\226LK\226LK\26\0\71\15u(m\226LK\206PK\26\0:\7A"
  "\70%\242\0;\7R(gR\5<\10t(\353\242\306\6=\10\64H+\206p\10>\11t(+"
  "\302\246\66\0?\14u(m\226,\214\264\34\212\0@\14u(m\226\314\222(C\272\0A\13u("
  "m\226L\33\206\314\26B\15u(-\206$\323\6%\323\6\5C\13u(m\226LlK\26\0D"
  "\13u(-\206$\363\66(\0E\13u(-\216\341\220\204\341 F\13u(-\216\341\220\204E\0"
  "G\15u(m\226LL\206LK\26\0H\13u(-\62\333\60d\266\0I\7q(%\16\1J"
  "\11u(-;j\311\2K\15u(-\62))iIT\311\2L\11u(-\302\36\7\1M\13"
  "u(-\262eI\64\267\0N\13u(-\262II\244\315\26O\12u(m\226\314[\262\0P\14"
  "u(-\206$\323\6%,\2Q\13\225\10m\226\314[\62\246\1R\15u(-\206$\323\6\245T"
  "\311\2S\13u(m\226L]\265d\1T\11u(-\6)\354\11U\11u(-\62\337\222\5V"
  "\12u(-\62oI-\2W\13u(-\62\227DIn\1X\14u(-\62-\251UjZ\0"
  "Y\13u(-\62-\251\205M\0Z\12u(-\6\61\353\70\10[\10r('\226.\2\134\12t"
  "(+\62-\323\62-]\10r(\63\224.\3^\6#xi\32_\7\24\30+\206\0`\7\42x"
  "'\242\0a\12U(m\326d\320\222!b\13u(-\302\212I\323\6\5c\11U(m\6\261:"
  "\4d\13u(-+\246MK\206\0e\12U(m\226l\30\322!f\12t(\253*\245)+\1"
  "g\14u\10m\6\315\226\14\341\240\0h\12u(-\302\212I\263\5i\7q(%\222Aj\13\224"
  "\10\353r k\223\22\5k\13t(+\262\222\222HI)l\7q(%\16\1m\12U(m\272"
  "(\211\246\5n\11U(-\22\223f\13o\12U(m\226\314\226,\0p\14u\10-\206$\263\15"
  "J\30\2q\13u\10m\6\315\226\14a\1r\11U(-\22\223X\4s\11U(m\6\365\240\0"
  "t\13t(k\262hH\262\242\0u\11U(-\62'E\11v\12U(-\62[R\213\0w\14"
  "U(-\62%Q\22\245\13\0x\12U(-\262\244V\251\5y\13u\10-\62\267d\10\7\5z"
  "\11U(-\6\255m\20{\12s(\251\222(\311\242,|\7q(%\16\1}\13s()\262("
  "K\242$\2~\10%XmL\11\0\15u(-\222\254R\214\222(Q\12\200\5\0\10!\201\14"
  "e)\361\222A\213\262A\311\0\202\10B\10g*\12\0\203\5\0\10!\204\11D\10kzQ\22\0"
  "\205\7\25(-\222\2\206\15v(o\206$\64\16\17I\224\0\207\16w(\261\266\244)\262\250I\226"
  "M\0\210\21\210\30\63\323!\213\222d\213\262\245\224\15i\10\211\5\0\10!\212\5\0\10!\213\5\0"
  "\10!\214\5\0\10!\215\15u(/NK\242)C\62\14\2\216\16u(/\16\221\62(\203\42\15"
  "C\0\217\14w(\61\336\246\310\242n\303\1\220\6\63N?\36\221\7\62h'\222(\222\7\62hg"
  "\22\5\223\10\64h+\222\246$\224\10\64hkZ\224\4\225\6\63H)\36\226\7\25X-\6\1\227"
  "\5\0\10!\230\5\0\10!\231\5\0\10!\232\5\0\10!\233\5\0\10!\234\5\0\10!\235\5\0"
  "\10!\236\5\0\10!\237\5\0\10!\240\5\0\10!\241\5\0\10!\242\5\0\10!\243\14u(\255"
  "\244J\66Da\66\10\244\5\0\10!\245\5\0\10!\246\5\0\10!\247\5\0\10!\250\5\0\10!"
  "\251\5\0\10!\252\5\0\10!\253\5\0\10!\254\5\0\10!\255\5\0\10!\256\5\0\10!\257\5"
  "\0\10!\260\12DXk\224HJ\24\0\0\0\0";

static uint8_t NatRailTall12[1102] U8G2_FONT_SECTION("NatRailTall12") =
  "c\0\3\2\4\4\2\5\5\11\14\0\375\11\375\11\0\1Q\2\235\4\65 \5\0f\12!\7\221B"
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
  "\322l\220\0\201\24\231>\255AK\223,\323\62-\323\26\35H\322l\220\0\202\22\231>\335t\320*"
  "\311\30\245Q\270\324\262AM\1\0\0\0";

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

static const uint8_t Underground10[1335] U8G2_FONT_SECTION("Underground10") =
  "\221\0\3\2\3\4\4\5\5\11\12\0\377\11\377\11\0\1^\2\310\5\36 \5\0\314\25!\7I\204"
  "\22\207$\42\7\23\274\24\211\22#\21M\204\66\245$\31\224R\22%\311\240\224\222\4$\17M\204V"
  "\331RJ\262-\211\222d\213\0%\12M\204\226\323\224u:\2&\17F\204\67Z\324\246E\211\226D"
  "I\42\5'\10\42\254\23C\242\0(\10J\204\63J\237\2)\11J\204\23Q\322\213\2*\14=\214"
  "V\225\312AY\232\42\0+\12-\224Va\64Ha\4,\10\42|\23C\242\0-\7\15\244\26\203"
  "\0.\7\22\204\23C\0/\10?\214\327i\237\1\60\14N\204\67C\22\372\61\31\22\0\61\7J\205"
  "\67K?\62\14N\204\67C\22\246\305\216\303\0\63\16N\204\67C\22\246\245\71\25\223!\1\64\16N"
  "\204\227\241\226D\225,\31\306\264\2\65\16N\204\27\207\64\35\344\64\25\223!\1\66\17N\204\67C\22"
  "\252\351\240\204\306dH\0\67\12N\204\27\327b\257)\0\70\17N\204\67C\22\32\223!\11\215\311\220"
  "\0\71\17N\204\67C\22\32\223AM\305dH\0:\10\62\214\23C\70\4;\11:\204\23C\250("
  "\0<\10<\214uQc\3=\10\34\234\25C\70\4>\11<\214\25aS\33\0?\15M\204\66K"
  "\246\205Y\61\207\42\0@\16M\204\66KfI\224D\31\302t\1A\14N\204\67C\22\32\207At"
  "\14B\16N\204\27\203\22\32\207%\64\16\13\0C\14N\204\67C\22\252=&C\2D\13N\204\27"
  "\203\22\372qX\0E\14N\204\27\207\264:\14iu\30F\14N\204\27\207\264:\14i+\0G\16"
  "N\204\67C\22\252\225A\64&C\2H\13N\204\27\241\343\60\210\216\1I\11K\204\24K\324\227\1"
  "J\12M\204\226=jZ\262\0K\20N\204\27\241\226D\225LL\262\250\226\204\1L\11N\204\27i"
  "\277\16\3M\16O\204\30\351\266T\244H\212T\327\0N\15N\204\27\241\270)\221\224h\243\61O\14"
  "N\204\67C\22\372\61\31\22\0P\14N\204\27\203\22\32\207%m\5Q\14V|\67C\22\372)I"
  "\206\70R\16N\204\27\203\22\32\207\245\26\325\222\60S\17N\204\67C\22\252\361\20\247b\62$\0T"
  "\12O\204\30\207,\356\67\0U\12N\204\27\241?&C\2V\14O\204\30\251\257IVI\63\0W"
  "\16O\204\30\251\247H\212\244H\351\226\0X\16O\204\30\251\232d\225\264\222UR\65Y\14O\204\30"
  "i\222U\322\270\67\0Z\13O\204\30\207\70\355\363\60\4[\10J\204\23K_\4\134\11M\204\26a"
  "\265c\1]\10J\204\23J_\6^\6\23\274\64\15_\7\15|\26\203\0`\7\42\254\23\203\24a"
  "\13\65\214\66k\62hZ\62\4b\14E\214\26a\70$\231\333\240\0c\12\65\214\66K&\326\222\5"
  "d\13E\214\226\225AsK\206\0e\14\65\214\66K\66\14a\226,\0f\13L\204UJ\26MY"
  "'\0g\14E|\66K\346\226\14a\262\0h\13M\204\26a\70$\231o\1i\7A\214\22\311\60"
  "j\11S|Ti\324\323\2k\15E\214\26a))iIT\311\2l\10C\214\24Q\337\4m\16"
  "\67\214\30\213\22ER$ER$\25n\11\65\214\26C\222y\13o\12\65\214\66K\346\226,\0p"
  "\14E|\26C\222\271\15J\30\2q\13E|\66K\346\226\14a\1r\12\64\214\25\311\20em\0"
  "s\12\65\214\66K\272j\311\2t\13D\214\65Y\64$Y\243\0u\11\65\214\26\231\267d\10v\12"
  "\65\214\26\231[R\213\0w\13\67\214\30\251S\244tK\0x\13\65\214\26YR\13\263\244\26y\13"
  "E|\26\231\267d\10\7\5z\12\65\214\26\203V\314\262A{\13K\204TIT\311\242Z\0|\6"
  "I\204\26\17}\13K\204\24YTK\242J\4~\21N\204\67C\62\14\241\70<(\312\60$Q\2"
  "\16N\204\27I\230)Q\232%Y\322\200\5\0\204\20\201\10\25\204\26I\242\24\202\5\0\204\20"
  "\203\5\0\204\20\204\5\0\204\20\205\5\0\204\20\206\5\0\204\20\207\5\0\204\20\210\5\0\204\20\211\5"
  "\0\204\20\212\5\0\204\20\213\5\0\204\20\214\5\0\204\20\215\5\0\204\20\216\5\0\204\20\217\5\0\204"
  "\20\220\6\33\237\37\17\221\5\0\204\20\222\5\0\204\20\223\5\0\204\20\224\5\0\204\20\225\5\0\204\20"
  "\226\5\0\204\20\227\5\0\204\20\230\5\0\204\20\231\5\0\204\20\232\5\0\204\20\233\5\0\204\20\234\5"
  "\0\204\20\235\5\0\204\20\236\5\0\204\20\237\5\0\204\20\240\5\0\204\20\241\5\0\204\20\242\5\0\204"
  "\20\243\14=\214VR%\33\242\60\33\4\244\5\0\204\20\245\5\0\204\20\246\5\0\204\20\247\5\0\204"
  "\20\250\5\0\204\20\251\5\0\204\20\252\5\0\204\20\253\5\0\204\20\254\5\0\204\20\255\5\0\204\20\256"
  "\5\0\204\20\257\5\0\204\20\260\12-\244\66KfK\26\0\0\0\0";

static const uint8_t UndergroundClock8[150] U8G2_FONT_SECTION("UndergroundClock8") =
  "\13\0\3\3\3\4\2\2\5\7\10\0\0\10\0\10\0\0\0\0\0\0}\60\12G\305\251\310\370&\251"
  "\0\61\10\304\305\222\234\364\0\62\15G\305\251\310\244B\331L(;\10\63\15G\305\251\310\244\42\62\215"
  "&\251\0\64\15G\305\24\316H\22%\311Q*\1\65\14G\305\70\310\250f\32MR\1\66\14G\305"
  "\251\310\250\26\31\233\244\2\67\12G\305\70\310\204z\225\2\70\15G\305\251\310h\222\212\214MR\1\71"
  "\15G\305\251\310h\22+\215&\251\0:\6\262\257 \22\0\0\0";

// Service attribution texts
static const char nrAttributionn[] = "Powered by National Rail Enquiries";
static const char rdgAttribution[] = "Powered by Rail Delivery Group";
static const char btAttribution[] = "Powered by bustimes.org";

#define SCREENSAVERINTERVAL 8000      // How often the screen is changed in sleep mode (ms - 8 seconds)
#define DATAUPDATEINTERVAL 90000      // How often we fetch data from National Rail (ms - 1.5 mins) - "default" option
#define FASTDATAUPDATEINTERVAL 45000  // How often we fetch data from National Rail (ms - 45 secs) - "fast" option
#define UGDATAUPDATEINTERVAL 30000    // How often we fetch data from TfL (ms - 30 secs)
#define BUSDATAUPDATEINTERVAL 45000   // How often we fetch data from bustimes.org (ms - 45 secs)
#define RSSUPDATEINTERVAL 600000      // How often to refresh the RSS feed (ms - 10 mins)
#define WEATHERUPDATEINTERVAL 1200000 // How often to update the weather forecast (ms - 20 mins)

// Reusable data transfer structures
rdiStation xfrStation;
stnMessages xfrMessages;
busTubeStation xfrBusTubeStation;
sharedBufferSpace jsonKeyBuffer;

// Station Data (shared)
rdStation station;
// Station Messages (shared)
stnMessages messages;

// Data transfer clients
rdmRailClient rdmRailData(&xfrStation,&xfrMessages,&jsonKeyBuffer);
raildataXmlClient darwinRailData(&xfrStation,&xfrMessages,&jsonKeyBuffer);
TfLdataClient tfldata(&xfrBusTubeStation,&xfrMessages,&jsonKeyBuffer);
busDataClient busdata(&xfrBusTubeStation,&jsonKeyBuffer);
weatherClient currentWeather(&jsonKeyBuffer);
rssClient rss(&jsonKeyBuffer);
github ghUpdate(&jsonKeyBuffer);

static char weatherMsg[MAXWEATHERSIZE];

// Bit and bobs
static unsigned long timer = 0;
static bool isSleeping = false;            // Is the screen sleeping (showing the "screensaver")
static bool sleepEnabled = false;          // Is overnight sleep enabled?
static bool forcedSleep = false;           // Is the system in manual sleep mode?
static bool forcedAwake = false;           // Was the system woken by touch sensor?
static int stayAwakeSeconds = 300;         // How long to force stay awake since last tap
static bool sleepClock = true;             // Showing the clock in sleep mode?
static bool softResetNeeded = false;       // Is a soft reset pending?
static bool manualUpdateCheck = false;     // Has the GUI requested a firmware update check
static bool showDataIcon = false;          // Show the data transfer indicator?
static bool updateIconVisible = false;     // Is the data update icon visible?
static bool dateEnabled = false;           // Showing the date on screen?
static bool weatherEnabled = false;        // Showing weather at station location. Requires an OpenWeatherMap API key.
static bool enableBus = false;             // Include Bus services on the board?
static bool firmwareUpdates = true;        // Check for and install firmware updates automatically at boot?
static bool dailyUpdateCheck = false;      // Check for and install firmware updates at midnight?
static byte sleepStarts = 0;               // Hour at which the overnight sleep (screensaver) begins
static byte sleepEnds = 6;                 // Hour at which the overnight sleep (screensaver) ends
static int brightness = 50;                // Initial brightness level of the OLED screen
static unsigned long lastWiFiReconnect=0;  // Last WiFi reconnection time (millis)
static bool firstLoad = true;              // Are we loading for the first time (no station config)?
static int prevProgressBarPosition=0;      // Used for progress bar smooth animation
static int startupProgressPercent;         // Initialisation progress
static bool wifiConnected = false;         // Connected to WiFi?
volatile unsigned long nextDataUpdate = 0; // Next National Rail update time (millis)
static int dataLoadSuccess = 0;            // Count of successful data downloads
static int dataLoadFailure = 0;            // Count of failed data downloads
static unsigned long lastLoadFailure = 0;  // When the last failure occurred
static bool noDataLoaded = true;           // True if no data received for the location
static unsigned long lastDataLoadTime = 0; // Timestamp of last data load
static long apiRefreshRate = DATAUPDATEINTERVAL; // User selected refresh rate for National Rail API (90/45 secs)
static int dateWidth;                      // Width of the displayed date in pixels
static int dateDay;                        // Day of the month of displayed date
static bool noScrolling = false;           // Suppress all horizontal scrolling
static bool flipScreen = false;            // Rotate screen 180deg
static String timezone = "";               // custom (non UK) timezone for the clock
static bool hidePlatform = false;          // Hide platform numbers on Rail board?
static bool hideOrdinals = false;          // Hide service ordinals (2nd, 3rd, 4th etc.)
static bool showLastSeen = false;          // Include last reported arrival after the calling at list
static bool showFullCalling = true;        // Wait for the "Calling at" list to finish scrolling before changing the primary service
static bool showFullMsgs = true;           // Wait for the current service message or RSS feed to finish scrolling before changing primary service
static bool showServiceMsgs = true;        // Show station and service messages (rail/tube)
static bool showTubeCurrentLocation=false; // Show the current location of the primary tube service
static int nrTimeOffset = 0;               // Offset minutes for Rail departures display
static int prevUpdateCheckDay;             // Day of the month the last daily firmware update check was made
static unsigned long fwUpdateCheckTimer=0; // Next time to check if the day has rolled over for firmware update check
static bool apiKeys = false;               // Does apikeys.json exist?
static bool touchEnabled = false;          // TTP223 Touch Sensor installed?
static bool useRDMclient = false;          // Use the new Rail Data Marketplace API instead of Darwin Lite
static bool enableScheduler = false;
static bool enableCarousel = false;
static int numCarouselSlots = 0;
static int currentCarouselSlot = 0;
static int numScheduleSlots = 0;
static int currentScheduleSlot = 0;
static unsigned long nextSchedulerCheck = 0;
static char hostname[33];                  // Network hostname (mDNS)
static char myUrl[24];                     // Stores the board's own url

// WiFi Manager status
static bool wifiConfigured = false;        // Has WiFi Manager used the captive portal

// Station Board Data
static char locationCode[13];              // CRS, Naptan or Atco code of active location
static char locationName[MAXLOCATIONSIZE]; // Station/Bus stop long name
static char locationFilter[MAXFILTERSIZE];
static char locationCleanFilter[MAXFILTERSIZE];
static float locationLat=0;
static float locationLon=0;
static bool railIsSet = false;
static bool tubeIsSet = false;
static bool busIsSet = false;
static bool schedulerActive = false;
static bool carouselActive = false;
static int activeSlotEventTime;
static int nextSlotEventTime;

static char nrToken[37] = "";              // National Rail Darwin Lite Tokens are in the format nnnnnnnn-nnnn-nnnn-nnnn-nnnnnnnnnnnn, where each 'n' represents a hexadecimal character (0-9 or a-f).
static String rdmDeparturesApiKey = "";    // RDM Consumer key for DeparturesBoard API
static String rdmServiceApiKey = "";       // RDM Consumer key for ServiceDetails API
static char tflAppKey[33] = "";            // TfL app_key (not usually needed)
static char callingCrsCode[4] = "";        // Station code to filter routes on
static char callingStation[45] = "";       // Calling filter station friendly name
static char lineId[33];                    // Underground line to filter on
static char lineDirection[9];              // Underground direction filter
static int busDestX;                       // Variable margin for bus destination

enum boardModes {
  MODE_LOADCONFIG = -1,
  MODE_NEXTMODE = -2,
  MODE_NEXTSCHEDULE = -3,
  MODE_RAIL = 0,
  MODE_TUBE = 1,
  MODE_BUS = 2
};
boardModes boardMode = MODE_RAIL;

// National Rail entry point
#define MAXHOSTSIZE 48                     // Maximum size of the wsdl Host
#define MAXAPIURLSIZE 48                   // Maximum size of the wsdl url
static char wsdlHost[MAXHOSTSIZE];         // wsdl Host name
static char wsdlAPI[MAXAPIURLSIZE];        // wsdl API url

// Coach class availability
static const char firstClassSeating[] = " First class seating only.";
static const char standardClassSeating[] = " Standard class seating only.";
static const char dualClassSeating[] = " First and Standard class seating available.";

// Animation
#define frameTimeRail 25
#define frameTimeTube 18
#define frameTimeBus 40
static int numMessages=0;
static int scrollStopsXpos = 0;
static int scrollStopsYpos = 0;
static int scrollStopsLength = 0;
static bool isScrollingStops = false;
static bool isShowingCalling = false;
static int currentMessage = 0;
static int prevMessage = 0;
static int prevScrollStopsLength = 0;
static long delayMs;
static char line2[5+MAXBOARDMESSAGES][MAXCALLINGSIZE+12];

// Line 3 (additional services)
static int line3Service = 0;
static int scrollServiceYpos = 0;
static bool isScrollingService = false;
static int prevService = 0;
static bool isShowingVia=false;
static unsigned long serviceTimer=0;
static unsigned long viaTimer=0;
static bool showingMessage = false;

// TfL/bus specific animation
static int scrollPrimaryYpos = 0;
static bool isScrollingPrimary = false;
static bool attributionScrolled = false;

static char displayedTime[9] = "";        // The currently displayed time
static char currentTime[9] = "";          // The current time (keep updated in loop)
static unsigned long lastTimeUpdate = 0;
static unsigned long refreshTimer = 0;

// Weather Stuff
static unsigned long nextWeatherUpdate = 0;            // When the next weather update is due
static char openWeatherMapApiKey[33] = "";             // If no OWM API key is provided, we use Open-Meteo weather data

// RSS Client
static bool rssEnabled = false;                        // Add RSS feed to the messages
static bool rssPriority = false;                       // Prioritise RSS feed
static unsigned long nextRssUpdate = 0;                // When the next RSS update is due
static String rssURL;                                  // RSS URL to use
static String rssName;                                 // Name of feed for atrribution
static char rssMessage[MAXMESSAGESIZE] = "";           // Holds the current, formatted, RSS message


// Optional TTP223 touch sensor / push button
touchSensor button(GPIO_NUM_34);

// FreeRTOS Task Handle and Status Flags
TaskHandle_t fetchTaskHandle = NULL;
volatile bool fetchComplete = false;
volatile bool fetchInProgress = false;
volatile bool rssFetchComplete = false;
volatile bool weatherFetchComplete = false;
volatile int lastUpdateResult = UPD_SUCCESS;
volatile int lastWeatherUpdateResult = UPD_SUCCESS;
volatile int lastRssUpdateResult = UPD_SUCCESS;

enum fetchModes {
  FETCH_BOARD = 0,
  FETCH_WEATHER = 1,
  FETCH_RSS = 2
};
fetchModes fetchMode = FETCH_BOARD;

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

void drawTruncatedText(const char *message, int line, int x) {
  char buff[strlen(message)+4];
  int maxWidth = SCREEN_WIDTH - 6 - x;
  strcpy(buff,message);
  int i = strlen(buff);
  while (u8g2.getStrWidth(buff)>maxWidth && i) buff[i--] = '\0';
  strcat(buff,"...");
  u8g2.drawStr(x,line,buff);
}

void centreText(const char *message, int line) {
  int width = u8g2.getStrWidth(message);
  if (width<=SCREEN_WIDTH) u8g2.drawStr((SCREEN_WIDTH-width)/2,line,message);
  else drawTruncatedText(message,line,0);
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

void drawFirmware() {
  char firmware[16];
  sprintf(firmware,"B%d.%d-W%d.%d",VERSION_MAJOR,VERSION_MINOR,WEBAPPVER_MAJOR,WEBAPPVER_MINOR);
   u8g2.drawStr(0,53,firmware);
}

void drawStartupHeading() {
  u8g2.setFont(NatRailTall12);
  centreText("Departures Board",0);
  u8g2.setFont(NatRailSmall9);
  drawFirmware();
}

void drawStationHeader(const char *stopName, const char *callingStopName, const char *platFilter, const int timeOffset) {

  // Clear the top line
  if (boardMode == MODE_TUBE || boardMode == MODE_BUS) {
    blankArea(0,ULINE0,256,ULINE1-1);
  } else {
    blankArea(0,LINE0,256,LINE1-1);
  }

  u8g2.setFont(NatRailSmall9);
  char boardTitle[95];
  strlcpy(boardTitle,stopName,sizeof(boardTitle));
  if (timeOffset || platFilter[0] || callingStopName[0]) strlcat(boardTitle," ",sizeof(boardTitle));

  if (timeOffset) {
    char offset[9];
    sprintf(offset,"\x8F%+dm ",timeOffset);
    strlcat(boardTitle,offset,sizeof(boardTitle));
  }
  if (platFilter[0]) {
    strlcat(boardTitle,(boardMode == MODE_BUS)?"\x8E":"\x8D",sizeof(boardTitle));
    strlcat(boardTitle,platFilter,sizeof(boardTitle));
    strlcat(boardTitle," ",sizeof(boardTitle));
  }
  if (callingStopName[0]) {
    strlcat(boardTitle,"(\x81",sizeof(boardTitle));
    strlcat(boardTitle,callingStopName,sizeof(boardTitle));
    strlcat(boardTitle,")",sizeof(boardTitle));
  }

  int titleOffset = 0;
  if (schedulerActive || carouselActive) titleOffset = 13;
  int boardTitleWidth = getStringWidth(boardTitle);

  if (dateEnabled) {
    int const dateY=55;
    // Get the date
    char sysTime[29];
    strftime(sysTime,29,"%a %d %b",&timeinfo);
    dateWidth = getStringWidth(sysTime);
    dateDay = timeinfo.tm_mday;
    if (callingStopName[0] || boardTitleWidth+dateWidth+10+titleOffset>=SCREEN_WIDTH) {
      blankArea(SCREEN_WIDTH-70,dateY,70,SCREEN_HEIGHT-dateY);
      u8g2.drawStr(SCREEN_WIDTH-dateWidth,dateY-1,sysTime); // Date bottom right
      if (boardTitleWidth+titleOffset < SCREEN_WIDTH) centreText(boardTitle,LINE0-1);
      else drawTruncatedText(boardTitle,LINE0-1,titleOffset);
    } else {
      u8g2.drawStr(SCREEN_WIDTH-dateWidth,LINE0-1,sysTime); // right-aligned date top
      if ((SCREEN_WIDTH-boardTitleWidth)/2 < dateWidth+8) {
        // station name left aligned
        u8g2.drawStr(titleOffset,LINE0-1,boardTitle);
      } else {
        centreText(boardTitle,LINE0-1);
      }
    }
  } else {
    if (boardTitleWidth+titleOffset < SCREEN_WIDTH) centreText(boardTitle,LINE0-1);
    else drawTruncatedText(boardTitle,LINE0-1,titleOffset);
  }

  if (titleOffset) u8g2.drawStr(0,LINE0-1,schedulerActive?"\x87":"\x88");
}

// Draw the NR clock (if the time has changed)
void drawCurrentTime() {
  char timeSeg[7];

  if (strcmp(displayedTime,currentTime)) {
    u8g2.setFont(NatRailClockLarge9);
    blankArea(96,LINE4,64,SCREEN_HEIGHT-LINE4);
    strlcpy(timeSeg,currentTime,7);
    u8g2.drawStr(96,LINE4-1,timeSeg);
    u8g2.setFont(NatRailClockSmall7);
    strcpy(timeSeg,currentTime+6);
    u8g2.drawStr(144,LINE4+1,timeSeg);
    u8g2.setFont(NatRailSmall9);
    u8g2.updateDisplayArea(12,6,8,2);
    strcpy(displayedTime,currentTime);
    if (dateEnabled && timeinfo.tm_mday!=dateDay) {
      // Need to update the date on screen
      drawStationHeader(station.location,callingStation,locationFilter,nrTimeOffset);
      u8g2.sendBuffer();  // Just refresh on new date
    }
  }
}

// Screensaver Screen
void drawSleepingScreen() {
  char sysTime[8];
  char sysDate[29];

  u8g2.setContrast(DIMMED_BRIGHTNESS);
  u8g2.clearBuffer();
  if (sleepClock) {
    sprintf(sysTime,"%02d:%02d",timeinfo.tm_hour,timeinfo.tm_min);
    strftime(sysDate,29,"%d %B %Y",&timeinfo);

    int offset = (getStringWidth(sysDate)-44)/2;
    u8g2.setFont(NatRailClockLarge9);
    int y = random(39);
    int x = random(SCREEN_WIDTH-getStringWidth(sysDate));
    u8g2.drawStr(x+offset,y,sysTime);
    u8g2.setFont(NatRailSmall9);
    u8g2.drawStr(x,y+13,sysDate);
  }
  u8g2.sendBuffer();
}

void showUpdateIcon(bool show) {
  if (!showDataIcon) return;
  if (show) {
    u8g2.setFont(NatRailTall12);
    u8g2.drawStr(0,50,"}");
    if (boardMode == MODE_TUBE) u8g2.setFont(Underground10);
    else u8g2.setFont(NatRailSmall9);
    updateIconVisible = true;
  } else {
    blankArea(0,50,6,13);
    updateIconVisible = false;
  }
  u8g2.updateDisplayArea(0,6,1,2);
}

/*
 * Setup / Notification Screen Layouts
*/
void showSetupScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText("Departures Board first-time setup",0);
  u8g2.setFont(NatRailSmall9);
  centreText("To configure Wi-Fi, please connect to the",18);
  centreText("the \"Departures Board\" network and go to",32);
  centreText("http://192.168.4.1 in a web browser.",46);
  u8g2.sendBuffer();
}

void showNoDataScreen() {
  u8g2.clearBuffer();
  char msg[60];
  u8g2.setFont(NatRailTall12);
  switch (boardMode) {
    case MODE_RAIL:
      sprintf(msg,"No data available for station code \"%s\".",locationCode);
      break;
    case MODE_TUBE:
      strcpy(msg,"No data available for the selected station.");
      break;
    case MODE_BUS:
      strcpy(msg,"No data available for the selected bus stop.");
      break;
  }
  centreText(msg,-1);
  u8g2.setFont(NatRailSmall9);
  centreText("Please check you have selected a valid location",14);
  centreText("Go to the URL below to choose a location...",26);
  centreText(myUrl,40);
  u8g2.sendBuffer();
}

void showSetupKeysHelpScreen() {
  u8g2.clearBuffer();
  char msg[60];
  u8g2.setFont(NatRailTall12);
  centreText("Departures Board Setup",-1);
  u8g2.setFont(NatRailSmall9);
  centreText("Next, you need to enter your API keys.",16);
  centreText("Please go to the URL below to start...",28);
  u8g2.setFont(NatRailTall12);
  centreText(myUrl,50);
  u8g2.sendBuffer();
}

void showSetupCrsHelpScreen() {
  u8g2.clearBuffer();
  char msg[60];
  u8g2.setFont(NatRailTall12);
  centreText("Departures Board Setup",-1);
  u8g2.setFont(NatRailSmall9);
  centreText("Next, you need to choose a location. Please",16);
  centreText("go to the URL below to choose a station...",28);
  u8g2.setFont(NatRailTall12);
  centreText(myUrl,50);
  u8g2.sendBuffer();
}

void showWsdlFailureScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText("The National Rail data feed is unavailable.",-1);
  u8g2.setFont(NatRailSmall9);
  centreText("WDSL entry point could not be accessed, so the",14);
  centreText("Departures Board cannot be loaded.",26);
  centreText("Please try again later. :(",40);
  u8g2.sendBuffer();
}

void showTokenErrorScreen() {
  char msg[60];
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  switch (boardMode) {
    case MODE_RAIL:
      if (useRDMclient) {
        centreText("Access to the Rail Delivery Group api denied.",-1);
      } else {
        centreText("Access to the National Rail database denied.",-1);
        strcpy(nrToken,"");
      }
      break;
    case MODE_TUBE:
      centreText("Access to the TfL database denied.",-1);
      break;
    case MODE_BUS:
      centreText("Access to the bustimes database denied.",-1);
      break;
  }
  u8g2.setFont(NatRailSmall9);
  centreText("You must enter a valid api key, please",14);
  centreText("check you have entered it correctly below:",26);
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
      sprintf(msg,"The station code \"%s\" is not valid.",locationCode);
      break;
    case MODE_TUBE:
      strcpy(msg,"The Underground station is not valid");
      break;
    case MODE_BUS:
      sprintf(msg,"The atco code \"%s\" is not valid.",locationCode);
      break;
  }
  centreText(msg,-1);
  u8g2.setFont(NatRailSmall9);
  centreText("Please ensure you have selected a valid station.",14);
  centreText("Go to the URL below to choose a station...",26);
  centreText(myUrl,40);
  u8g2.sendBuffer();
}

void showFirmwareUpdateWarningScreen(const char *msg, int secs) {
  char countdown[60];
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText("Firmware Update Available",-1);
  u8g2.setFont(NatRailSmall9);
  centreText("A new version of the Departures Board firmware",14);
  sprintf(countdown,"will be installed in %d seconds. This provides:",secs);
  centreText(countdown,26);
  sprintf(countdown,"\"%s\"",msg);
  centreText(countdown,40);
  centreText("* DO NOT REMOVE THE POWER DURING THE UPDATE *",54);
  u8g2.sendBuffer();
}

void showFirmwareUpdateProgress(int percent) {
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText("Firmware Update in Progress",-1);
  u8g2.setFont(NatRailSmall9);
  progressBar("Updating Firmware",percent);
  centreText("* DO NOT REMOVE THE POWER DURING THE UPDATE *",54);
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

void showSwitchScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);

  if (carouselActive) centreText("Moving to next carousel slot",20);
  else if (schedulerActive) centreText("Moving to next scheduler slot",20);
  else centreText("Switching modes",20);
  u8g2.setFont(NatRailSmall9);
  centreText("Waiting for background process to complete...",42);
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
  JsonDocument doc;
  char prevFirmware[15] = "B0.0-W0.0";
  char prevGUI[8];
  char currentGUI[8];

  if (LittleFS.exists("/fw.json")) {
    File file = LittleFS.open("/fw.json", "r");
    if (file) {
      DeserializationError error = deserializeJson(doc, file);
      if (!error) {
        JsonObject settings = doc.as<JsonObject>();

        if (settings["fw"].is<const char*>()) {
          strlcpy(prevFirmware,settings["fw"],sizeof(prevFirmware));
        }
      }
      file.close();
    }
  }

  if (prevFirmware[0]) {
    sscanf(prevFirmware,"%*[^ -]-%s",prevGUI);
    sprintf(currentGUI,"W%d.%d",WEBAPPVER_MAJOR,WEBAPPVER_MINOR);
    if (strcmp(prevGUI,currentGUI)) {
      // clean up old/dev files
      progressBar("Cleaning up following upgrade",45);
      LittleFS.remove("/index_d.htm");
      LittleFS.remove("/index.htm");
      LittleFS.remove("/keys.htm");
      LittleFS.remove("/nrelogo.webp");
      LittleFS.remove("/rdglogo.webp");
      LittleFS.remove("/tfllogo.webp");
      LittleFS.remove("/btlogo.webp");
      LittleFS.remove("/tube.webp");
      LittleFS.remove("/nr.webp");
      LittleFS.remove("/favicon.svg");
      LittleFS.remove("/favicon.png");
      LittleFS.remove("/webver");
    }
  }
}

// Returns true if sleep mode is enabled and we're within the sleep period
bool isSnoozing() {
  if (forcedSleep) return true;
  if (forcedAwake) {
    if (button.secsSinceLastTap() >= stayAwakeSeconds) forcedAwake = false;
    else return false;
  }
  if (!sleepEnabled) return false;
  byte myHour = timeinfo.tm_hour;
  if (sleepStarts > sleepEnds) {
    if ((myHour >= sleepStarts) || (myHour < sleepEnds)) return true; else return false;
  } else {
    if ((myHour >= sleepStarts) && (myHour < sleepEnds)) return true; else return false;
  }
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

  if (LittleFS.exists("/apikeys.json")) {
    File file = LittleFS.open("/apikeys.json", "r");
    if (file) {
      DeserializationError error = deserializeJson(doc, file);
      if (!error) {
        JsonObject settings = doc.as<JsonObject>();

        if (settings["rdmDepKey"].is<const char*>()) {
          rdmDeparturesApiKey = settings["rdmDepKey"].as<String>();
        }

        if (settings["rdmSvcKey"].is<const char*>()) {
          rdmServiceApiKey = settings["rdmSvcKey"].as<String>();
        }

        if (settings["nrToken"].is<const char*>()) {
          strlcpy(nrToken, settings["nrToken"], sizeof(nrToken));
        }

        if (settings["owmToken"].is<const char*>()) {
          strlcpy(openWeatherMapApiKey, settings["owmToken"], sizeof(openWeatherMapApiKey));
        }

        if (settings["appKey"].is<const char*>()) {
          strlcpy(tflAppKey,settings["appKey"],sizeof(tflAppKey));
        }
        apiKeys = true;

      } else {
        // JSON deserialization failed - TODO
      }
      file.close();
    }
  }
}

void resetLocationIds() {
  strcpy(locationCode,"");
  railIsSet = false;
  tubeIsSet = false;
  busIsSet = false;
}

void saveFirmwareInfo() {
  String fw = "{\"fw\":\"B" + String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + "-W" + String(WEBAPPVER_MAJOR) + "." + String(WEBAPPVER_MINOR) + "\"}";
  saveFile("/fw.json",fw);
}

// Write a default config file so that the Web GUI works initially (force Tube mode if no NR token)
void writeDefaultConfig() {
  String defaultConfig = "{\"crs\":\"\",\"station\":\"\",\"lat\":0,\"lon\":0,\"weather\":true,\"sleep\":false,\"showDate\":false,\"showBus\":false,\"update\":true,\"sleepStarts\":23,\"sleepEnds\":8,\"brightness\":20,\"tubeId\":\"\",\"tubeName\":\"\",\"mode\":" + String((!nrToken[0] && rdmDeparturesApiKey=="")?"1":"0") + "}";
  saveFile("/config.json",defaultConfig);
  resetLocationIds();
  saveFirmwareInfo();
}

bool pruneFromPhrase(char* input, const char* target) {
  // Find the first occurance of the target word or phrase
  char* pos = strstr(input,target);
  // If found, prune from here
  if (pos) {
      input[pos - input] = '\0';
      return true;
  }
  return false;
}

int getTimeInMinutes() {
  return (timeinfo.tm_hour * 60 + timeinfo.tm_min);
}

void loadSlot(JsonObjectConst slot, bool isDefault, boardModes requestedMode) {
  if (requestedMode == MODE_NEXTMODE) {
    switch (boardMode) {
      case MODE_RAIL:
        if (tubeIsSet) boardMode = MODE_TUBE;
        else if (busIsSet) boardMode = MODE_BUS;
        break;
      case MODE_TUBE:
        if (busIsSet) boardMode = MODE_BUS;
        else if (railIsSet) boardMode = MODE_RAIL;
        break;
      case MODE_BUS:
        if (railIsSet) boardMode = MODE_RAIL;
        else if (tubeIsSet) boardMode = MODE_TUBE;
        break;
    }
  } else {
    if (slot["mode"].is<int>()) boardMode = slot["mode"];
    else if (slot["tube"].is<bool>()) boardMode = slot["tube"] ? MODE_TUBE : MODE_RAIL; // handle legacy v1.x config
  }

  switch (boardMode) {
    case MODE_RAIL:
      if (slot["crs"].is<const char*>())              strlcpy(locationCode, slot["crs"], sizeof(locationCode));
      if (slot["platformFilter"].is<const char*>())   strlcpy(locationFilter, slot["platformFilter"], sizeof(locationFilter));
      if (slot["callingCrs"].is<const char*>())       strlcpy(callingCrsCode, slot["callingCrs"], sizeof(callingCrsCode));
      if (slot["callingStation"].is<const char*>())   strlcpy(callingStation, slot["callingStation"], sizeof(callingStation));
      if (slot["lat"].is<float>())                    locationLat = slot["lat"];
      if (slot["lon"].is<float>())                    locationLon = slot["lon"];
      break;

    case MODE_TUBE:
      if (slot["tubeId"].is<const char*>())     strlcpy(locationCode, slot["tubeId"], sizeof(locationCode));
      if (slot["lineid"].is<const char*>())     strlcpy(lineId, slot["lineid"], sizeof(lineId));
      if (slot["direction"].is<const char*>())  strlcpy(lineDirection, slot["direction"], sizeof(lineDirection));
      if (isDefault) {
        if (slot["tubeName"].is<const char*>()) strlcpy(locationName, slot["tubeName"], sizeof(locationName));
        if (slot["tubeLat"].is<float>())      locationLat = slot["tubeLat"];
        if (slot["tubeLon"].is<float>())      locationLon = slot["tubeLon"];
        pruneFromPhrase(locationName," Underground Station");
        pruneFromPhrase(locationName," DLR Station");
        pruneFromPhrase(locationName," (H&C Line)");
      } else {
        if (slot["name"].is<const char*>()) strlcpy(locationName, slot["name"], sizeof(locationName));
        if (slot["lat"].is<float>())          locationLat = slot["lat"];
        if (slot["lon"].is<float>())          locationLon = slot["lon"];
      }
      break;

    case MODE_BUS:
      if (slot["busId"].is<const char*>())      strlcpy(locationCode, slot["busId"], sizeof(locationCode));
      if (slot["busFilter"].is<const char*>())  strlcpy(locationFilter, slot["busFilter"], sizeof(locationFilter));
      if (isDefault) {
        if (slot["busName"].is<const char*>())    strlcpy(locationName, slot["busName"], sizeof(locationName));
        if (slot["busLat"].is<float>())           locationLat = slot["busLat"];
        if (slot["busLon"].is<float>())           locationLon = slot["busLon"];
      } else {
        if (slot["name"].is<const char*>())   strlcpy(locationName, slot["name"], sizeof(locationName));
        if (slot["lat"].is<float>())          locationLat = slot["lat"];
        if (slot["lon"].is<float>())          locationLon = slot["lon"];
      }
      break;

  }
}

// Load the configuration settings (if they exist, if not create a default set for the Web GUI page to read)
void loadConfig(bool coldBoot = false, boardModes requestedMode = MODE_LOADCONFIG) {
  JsonDocument doc;

  // Set defaults
  strcpy(hostname,defaultHostname);
  strcpy(lineId,"all");
  strcpy(lineDirection,"");

  timezone = String(ukTimezone);
  resetLocationIds();

  schedulerActive = false;
  carouselActive = false;

  if (LittleFS.exists("/config.json")) {
    File file = LittleFS.open("/config.json", "r");
    if (file) {
      DeserializationError error = deserializeJson(doc, file);
      if (!error) {
        JsonObject settings = doc.as<JsonObject>();

        // Load common settings
        if (settings["crs"].is<const char*>() && strlen(settings["crs"])) railIsSet = true; else railIsSet = false;
        if (settings["tubeId"].is<const char*>() && strlen(settings["tubeId"])) tubeIsSet = true; else tubeIsSet = false;
        if (settings["busId"].is<const char*>() && strlen(settings["busId"])) busIsSet = true; else busIsSet = false;

        if (settings["hostname"].is<const char*>())   strlcpy(hostname, settings["hostname"], sizeof(hostname));
        if (settings["wsdlHost"].is<const char*>())   strlcpy(wsdlHost, settings["wsdlHost"], sizeof(wsdlHost));
        if (settings["wsdlAPI"].is<const char*>())    strlcpy(wsdlAPI, settings["wsdlAPI"], sizeof(wsdlAPI));
        if (settings["showDate"].is<bool>())          dateEnabled = settings["showDate"];
        if (settings["showBus"].is<bool>())           enableBus = settings["showBus"];
        if (settings["showFullCalling"].is<bool>())   showFullCalling = settings["showFullCalling"];
        if (settings["showFullMsgs"].is<bool>())      showFullMsgs = settings["showFullMsgs"];
        if (settings["sleep"].is<bool>())             sleepEnabled = settings["sleep"];
        if (settings["darkSleep"].is<bool>())         sleepClock = !settings["darkSleep"];
        if (settings["fastRefresh"].is<bool>())       apiRefreshRate = settings["fastRefresh"] ? FASTDATAUPDATEINTERVAL : DATAUPDATEINTERVAL;
        if (settings["weather"].is<bool>())           weatherEnabled = settings["weather"];
        if (settings["update"].is<bool>())            firmwareUpdates = settings["update"];
        if (settings["updateDaily"].is<bool>())       dailyUpdateCheck = settings["updateDaily"];
        if (settings["sleepStarts"].is<int>())        sleepStarts = settings["sleepStarts"];
        if (settings["sleepEnds"].is<int>())          sleepEnds = settings["sleepEnds"];
        if (settings["brightness"].is<int>())         brightness = settings["brightness"];

        if (settings["noScroll"].is<bool>())          noScrolling = settings["noScroll"];
        if (settings["flip"].is<bool>())              flipScreen = settings["flip"];
        if (settings["touch"].is<bool>())             touchEnabled = settings["touch"];
        if (settings["dataIcon"].is<bool>())          showDataIcon = settings["dataIcon"];
        if (settings["forceWakeTime"].is<int>())      stayAwakeSeconds = settings["forceWakeTime"];
        if (settings["TZ"].is<const char*>())         timezone = settings["TZ"].as<String>();
        if (settings["nrTimeOffset"].is<int>())       nrTimeOffset = settings["nrTimeOffset"];
        if (settings["hidePlatform"].is<bool>())      hidePlatform = settings["hidePlatform"];
        if (settings["hideOrdinals"].is<bool>())      hideOrdinals = settings["hideOrdinals"];
        if (settings["showLastSeen"].is<bool>())      showLastSeen = settings["showLastSeen"];
        if (settings["showTubeLocation"].is<bool>())  showTubeCurrentLocation = settings["showTubeLocation"];
        if (settings["showServiceMsgs"].is<bool>())   showServiceMsgs = settings["showServiceMsgs"];

        if (settings["enableScheduler"].is<bool>())   enableScheduler = settings["enableScheduler"];
        if (settings["enableCarousel"].is<bool>())    enableCarousel = settings["enableCarousel"];

        if (settings["rssUrl"].is<const char*>())     rssURL = settings["rssUrl"].as<String>();
        if (settings["rssName"].is<const char*>())    rssName = settings["rssName"].as<String>();
        if (rssURL != "") rssEnabled = true; else rssEnabled = false;
        if (settings["rssPriority"].is<bool>())       rssPriority = settings["rssPriority"];

        if (settings[F("mode")].is<int>())            boardMode = settings[F("mode")];
        else if (settings[F("tube")].is<bool>())      boardMode = settings[F("tube")] ? MODE_TUBE : MODE_RAIL; // handle legacy v1.x config

        if (settings["dataSource"].is<int>())         useRDMclient = (settings["dataSource"]?1:0);
        // validate the data source against which api keys are available
        if (nrToken[0] && rdmDeparturesApiKey=="") useRDMclient = false;
        else if (!nrToken[0] && rdmDeparturesApiKey!="") useRDMclient = true;

        if (coldBoot) {
          // Just load base parameters at boot, clock not set yet so exit
          file.close();
          return;
        }

        // Work out what board mode we're in
        JsonArray scheduler = settings["scheduler"].as<JsonArray>();
        JsonArray carousel = settings["carousel"].as<JsonArray>();

        if (enableScheduler && !scheduler.isNull() && scheduler.size() > 0 && (requestedMode==MODE_LOADCONFIG || requestedMode==MODE_NEXTSCHEDULE)) {
          if (requestedMode == MODE_LOADCONFIG) {
            int currentMins = getTimeInMinutes();
            int activeIndex = -1;

            // Iterate through the sorted schedule to find the last entry that has already started
            for (int i = 0; i < scheduler.size(); i++) {
              const char* timeStr = scheduler[i]["time"];
              if (timeStr) {
                int h, m;
                if (sscanf(timeStr, "%d:%d", &h, &m) == 2) {
                  int entryMins = h * 60 + m;
                  if (entryMins <= currentMins) {
                    activeIndex = i;
                  } else {
                    // Since the array is chronologically sorted, the first entry strictly
                    // greater than the current time implies we've passed the active one.
                    break;
                  }
                }
              }
            }

            // If activeIndex is still -1, it means the current time is before the first entry
            // of the day. Because of midnight wrap-around, the active entry is the LAST entry
            // from the previous day.
            if (activeIndex == -1) {
              activeIndex = scheduler.size() - 1;
            }

            // Save the active entry index (for touch)
            currentScheduleSlot = activeIndex;
            numScheduleSlots = scheduler.size();

            // The next entry simply follows the active one, wrapping back to 0 at the end of the array
            int nextIndex = (activeIndex + 1) % scheduler.size();

            JsonObject currentSchedule = scheduler[activeIndex];
            loadSlot(currentSchedule,false,requestedMode);
            schedulerActive = true;

            // Save the time of the active entry
            const char* timeStrActive = scheduler[activeIndex]["time"];
            if (timeStrActive) {
              int h, m;
              if (sscanf(timeStrActive, "%d:%d", &h, &m) == 2) {
                activeSlotEventTime = h * 60 + m; // The time (in minutes) of the active event
              }
            }

            // Save the time of the next entry (for the loop scheduler)
            const char* timeStr = scheduler[nextIndex]["time"];
            if (timeStr) {
              int h, m;
              if (sscanf(timeStr, "%d:%d", &h, &m) == 2) {
                nextSlotEventTime = h * 60 + m; // The time (in minutes) of the next change
              }
            }
          } else {
            // Push on to the next schedule slot via touch
            currentScheduleSlot = (currentScheduleSlot + 1) % numScheduleSlots;
            JsonObject currentSchedule = scheduler[currentScheduleSlot];
            loadSlot(currentSchedule,false,requestedMode);
            schedulerActive = true;
          }
        } else if (enableCarousel && !carousel.isNull() && carousel.size() > 0 && requestedMode == MODE_LOADCONFIG) {
          numCarouselSlots = carousel.size();
          if (currentCarouselSlot >= numCarouselSlots) currentCarouselSlot = 0;
          JsonObject currentCarousel = carousel[currentCarouselSlot];
          loadSlot(currentCarousel,false,requestedMode);
          carouselActive = true;

          // Work out when the next change occurs
          activeSlotEventTime = getTimeInMinutes();
          if (currentCarousel["duration"].is<int>()) nextSlotEventTime = currentCarousel["duration"];
          nextSlotEventTime = (activeSlotEventTime + nextSlotEventTime) % 1440;

        } else {
          // Plain board mode as defined by the user
          loadSlot(settings,true,requestedMode);
        }
      } else {
        // JSON deserialization failed - TODO
      }
      file.close();
    }
  } else if (apiKeys) writeDefaultConfig();
}

void buildRssMessage() {
  if (rss.numRssTitles>0) {
    sprintf(rssMessage,"%s: %s",rssName.c_str(),rss.rssTitle[0]);
    for (int i=1;i<rss.numRssTitles;i++) {
      if (strlen(rssMessage) + strlen(rss.rssTitle[i]) + 1 < MAXMESSAGESIZE) {
        strcat(rssMessage,"\x90");
        strcat(rssMessage,rss.rssTitle[i]);
      } else {
        break;
      }
    }
  } else {
    rssMessage[0] = '\0';
  }
}

void updateRssFeed() {
  if (lastRssUpdateResult=rss.loadFeed(rssURL); lastRssUpdateResult == UPD_SUCCESS) {
    nextRssUpdate = millis() + RSSUPDATEINTERVAL; // update every ten minutes
    buildRssMessage();
  }
  else nextRssUpdate = millis() + (RSSUPDATEINTERVAL/2); // Failed so try again in 5 minutes
}

// Update the current weather message if weather updates are enabled and we have a lat/lon for the selected location
void updateCurrentWeather(float latitude, float longitude) {
  nextWeatherUpdate = millis() + WEATHERUPDATEINTERVAL;
  if (!latitude || !longitude) return; // No location co-ordinates
  weatherMsg[0]='\0';
  lastWeatherUpdateResult = currentWeather.updateWeather(openWeatherMapApiKey, latitude, longitude);
  if (lastWeatherUpdateResult == UPD_SUCCESS) strlcpy(weatherMsg,currentWeather.currentWeatherMessage,MAXWEATHERSIZE);
}

void checkWeatherUpdate(float prevLat, float prevLon) {
  if (weatherEnabled && (prevLat!=locationLat || prevLon!=locationLon)) {
    prevProgressBarPosition = 114;
    progressBar("Getting weather conditions",60);
    updateCurrentWeather(locationLat,locationLon);
  }
}

// Soft reset/reload the board.
void softResetBoard(boardModes requestedMode) {
  boardModes previousMode = boardMode;
  String prevRssUrl = rssURL;
  float prevLat = locationLat;
  float prevLon = locationLon;
  bool prevWeatherEnabled = weatherEnabled;

  // Reload the settings
  loadConfig(false,requestedMode);
  if (flipScreen) u8g2.setFlipMode(1); else u8g2.setFlipMode(0);
  if (timezone!="") {
    setenv("TZ",timezone.c_str(),1);
  } else {
    setenv("TZ",ukTimezone,1);
  }
  tzset();
  u8g2.clearBuffer();
  drawStartupHeading();
  if (requestedMode==MODE_NEXTMODE) centreText("Switching modes...",53);
  u8g2.updateDisplay();

  // Force an update asap
  nextDataUpdate = 0;
  nextWeatherUpdate = millis()+60000; // Ensure the weather is updated after the data feed
  nextRssUpdate += 30000;
  isScrollingService = false;
  isScrollingStops = false;
  isScrollingPrimary = false;
  isSleeping=false;
  forcedSleep=false;
  firstLoad=true;
  noDataLoaded=true;
  viaTimer=0;
  timer=0;
  serviceTimer=0;
  prevProgressBarPosition=133;
  startupProgressPercent=70;
  currentMessage=0;
  prevMessage=0;
  prevScrollStopsLength=0;
  isShowingVia=false;
  line3Service=0;
  prevService=0;
  fetchComplete=false;
  nextSchedulerCheck=millis()+10000;
  if (!weatherEnabled) weatherMsg[0]='\0';
  else if (!prevWeatherEnabled) {
    // force a weather update, even if the location hasn't changed
    prevLat=0;
    prevLon=0;
  }

  if (rssEnabled && prevRssUrl != rssURL) {
    rssMessage[0] = '\0';
    if (boardMode == MODE_RAIL || boardMode == MODE_TUBE) {
      prevProgressBarPosition=95;
      progressBar("Updating RSS headlines feed",50);
      updateRssFeed();
    }
  } else if (rssEnabled && previousMode!=boardMode && boardMode!=MODE_BUS) {
    buildRssMessage();
  }

  switch (boardMode) {
    case MODE_RAIL:
      checkWeatherUpdate(prevLat,prevLon);
      // Create a cleaned platform filter (if any)
      rdmRailData.cleanFilter(locationFilter,locationCleanFilter,sizeof(locationFilter));
      progressBar("Initialising National Rail interface",70);
      if (!useRDMclient) {
        // Using legacy XML client
        int res = darwinRailData.init(wsdlHost, wsdlAPI);
        if (res != UPD_SUCCESS) {
          showWsdlFailureScreen();
          while (true) { delay(1);}
        }
      }
      break;

    case MODE_TUBE:
      checkWeatherUpdate(prevLat,prevLon);
      progressBar("Initialising TfL interface",70);
      break;

    case MODE_BUS:
      checkWeatherUpdate(prevLat,prevLon);
      progressBar("Initialising BusTimes interface",70);
      // Create a cleaned filter
      busdata.cleanFilter(locationFilter,locationCleanFilter,sizeof(locationFilter));
      break;
  }
  station.numServices=0;
  messages.numMessages=0;
}

// Handle switching to next board mode or carousel/scheduler slot (touch sensor)
void switchToNextMode() {
  if ((carouselActive && numCarouselSlots<2) || (schedulerActive && numScheduleSlots<2)) return;  // Nothing to switch to

  if (fetchInProgress) {
    // Wait for the background fetch to finish before we soft reset
    showSwitchScreen();
    while (fetchInProgress) delay(50);
  }

  if (carouselActive) {
    currentCarouselSlot = (currentCarouselSlot + 1) % numCarouselSlots;
    softResetBoard(MODE_LOADCONFIG);
  }
  else if (schedulerActive) softResetBoard(MODE_NEXTSCHEDULE);
  else if (railIsSet+tubeIsSet+busIsSet > 1) softResetBoard(MODE_NEXTMODE); // Check there's at least two configured modes
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

  // Check that we found the firmware.bin file in the release assets
  if (ghUpdate.firmwareURL.length()==0) return result;

  for (int i=30;i>=0;i--) {
    showFirmwareUpdateWarningScreen(ghUpdate.releaseDescription.c_str(),i);
    delay(1000);
  }
  u8g2.clearDisplay();
  prevProgressBarPosition=0;
  showFirmwareUpdateProgress(0);  // So we don't have a blank screen
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.onProgress(update_progress);
  httpUpdate.rebootOnUpdate(false); // Don't auto reboot, we'll handle it

  HTTPUpdateResult ret = httpUpdate.handleUpdate(client, ghUpdate.firmwareURL);
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

// Draw the primary service line
void drawPrimaryService(bool showVia) {
  int destPos;
  char clipDestination[MAXLOCATIONSIZE];
  char etd[16];
  char plat[9];

  u8g2.setFont(NatRailTall12);
  blankArea(0,LINE1,256,LINE2-LINE1);
  destPos = u8g2.drawStr(0,LINE1-1,station.service[0].sTime) + 6;
  if (isDigit(station.service[0].etd[0])) sprintf(etd,"Exp %s",station.service[0].etd);
  else strcpy(etd,station.service[0].etd);
  int etdWidth = getStringWidth(etd) + (etd[strlen(etd)-1]=='1'?1:0);
  u8g2.drawStr(SCREEN_WIDTH - etdWidth,LINE1-1,etd);
  int spaceAvailable = SCREEN_WIDTH - destPos - etdWidth - 6;

  if (station.platformAvailable && station.service[0].platform[0] && station.service[0].serviceType == TRAIN && !hidePlatform) {
    sprintf(plat,"Plat %.3s",station.service[0].platform);
    int platWidth = getStringWidth(plat) + (plat[strlen(plat)-1]=='1'?1:0);;
    u8g2.drawStr(SCREEN_WIDTH - etdWidth - platWidth - 7,LINE1-1,plat);
    spaceAvailable-=(platWidth+7);
  }

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
  char clipDestination[MAXLOCATIONSIZE];
  char ordinal[5];
  char plat[9];
  int destPos;

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
    if (hideOrdinals) {
      destPos = u8g2.drawStr(0,y-1,station.service[line].sTime) + 6;
    } else {
      u8g2.drawStr(0,y-1,ordinal);
      destPos = u8g2.drawStr(21,y-1,station.service[line].sTime) + 25;
    }
    char etd[16];
    if (isDigit(station.service[line].etd[0])) sprintf(etd,"Exp %s",station.service[line].etd);
    else strcpy(etd,station.service[line].etd);
    int etdWidth = getStringWidth(etd) + (etd[strlen(etd)-1]=='1'?1:0);
    u8g2.drawStr(SCREEN_WIDTH - etdWidth,y-1,etd);
    int spaceAvailable = SCREEN_WIDTH - destPos - etdWidth - 6;

    if (station.platformAvailable && !hidePlatform && station.service[line].platform[0] && station.service[line].serviceType == TRAIN) {
      sprintf(plat,"Plat %.3s",station.service[line].platform);
      int platWidth = getStringWidth(plat) + (plat[strlen(plat)-1]=='1'?1:0);
      u8g2.drawStr(SCREEN_WIDTH - etdWidth - platWidth - 7,y-1,plat);
      spaceAvailable-=(platWidth+7);
    }
    // work out if we need to clip the destination
    strcpy(clipDestination,station.service[line].destination);
    if (getStringWidth(clipDestination) > spaceAvailable) {
      while (getStringWidth(clipDestination) > spaceAvailable - 5) {
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
      centreText(useRDMclient?rdgAttribution:nrAttributionn,y-1);
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
  drawStationHeader(station.location,callingStation,locationFilter,nrTimeOffset);

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

    if (noScrolling && station.numServices>1) {
      drawServiceLine(1,LINE2);
    }
  } else {
    blankArea(0,LINE2,256,LINE4-LINE2);
    u8g2.setFont(NatRailTall12);
    centreText("There are no scheduled services at this station.",LINE1-1);
  }

  // Check if RSS should be inserted before nrcc messages
  if (rssEnabled && rssPriority && rssMessage[0]) {
    strcpy(line2[numMessages++],rssMessage);
  }

  // Add any nrcc messages
  for (int i=0;i<messages.numMessages;i++) {
    strcpy(line2[numMessages],messages.messages[i]);
    numMessages++;
  }

  // Check if RSS should be added after nrcc messages
  if (rssEnabled && !rssPriority && rssMessage[0]) {
    strcpy(line2[numMessages++],rssMessage);
  }

  // Setup for the first message to rollover to
  isScrollingStops=false;
  currentMessage=numMessages-1;

  u8g2.setFont(NatRailSmall9);
  u8g2.sendBuffer();
}

void updateRailDepartures() {
  if (useRDMclient) rdmRailData.loadDepartures(&station,&messages);
  else darwinRailData.loadDepartures(&station,&messages);
  lastDataLoadTime = millis();
  noDataLoaded = false;
  dataLoadSuccess++;
}

void waitForFirstLoad() {
  // Wait for the first data load
  while (!fetchComplete) {
    delay(250);
    if (startupProgressPercent<95) {
      startupProgressPercent+=5;
      drawProgressBar(startupProgressPercent);
    }
  }
  drawProgressBar(100);
}

/*
 *
 * London Underground Board
 *
 */

// Draw the TfL clock (if the time has changed)
bool drawCurrentTimeUG() {
  if (strcmp(displayedTime,currentTime)) {
    u8g2.setFont(UndergroundClock8);
    blankArea(99,ULINE4,58,8);
    u8g2.drawStr(99,ULINE4-1,currentTime);
    u8g2.updateDisplayArea(12,7,8,1);
    strcpy(displayedTime,currentTime);
    u8g2.setFont(Underground10);

    if (dateEnabled && timeinfo.tm_mday!=dateDay) {
      if (boardMode == MODE_TUBE) drawStationHeader(locationName,"","",0);
      else drawStationHeader(locationName,"",locationFilter,0);
      u8g2.sendBuffer();  // Just refresh on new date
      u8g2.setFont(Underground10);
    }
    return true;
  } else {
    return false;
  }
}

void updateArrivals() {
  tfldata.loadArrivals(&station,&messages);
  lastDataLoadTime = millis();
  noDataLoaded = false;
  dataLoadSuccess++;
}

void drawUndergroundService(int serviceId, int y, bool isShowingCurrentLocation = false) {
  char serviceData[4+MAXLOCATIONSIZE];
  int usedSpace = 4;

  u8g2.setFont(Underground10);
  blankArea(0,y,256,10);

  if (serviceId < station.numServices) {
    if (serviceId || (strcmp(station.origin,"At Platform") && station.service[0].timeToStation>10)) {
      if (station.service[serviceId].timeToStation <= 40) {
        usedSpace += u8g2.drawStr(SCREEN_WIDTH-19,y-1,"Due");
      } else {
        int mins = (station.service[serviceId].timeToStation + 30) / 60; // Round to nearest minute
        sprintf(serviceData,"%d",mins);
        if (mins==1) u8g2.drawStr(SCREEN_WIDTH-22,y-1,"min"); else u8g2.drawStr(SCREEN_WIDTH-22,y-1,"mins");
        usedSpace += u8g2.drawStr(SCREEN_WIDTH-27-(strlen(serviceData)*7),y-1,serviceData) + 22;
      }
    }

    if (isShowingCurrentLocation) sprintf(serviceData,"%d %s",serviceId+1,station.origin);
    else sprintf(serviceData,"%d %s",serviceId+1,station.service[serviceId].destination);
    if (getStringWidth(serviceData) > SCREEN_WIDTH-usedSpace) {
      while (getStringWidth(serviceData) > SCREEN_WIDTH-usedSpace-6) {
        serviceData[strlen(serviceData)-1] = '\0';
      }
      if (serviceData[strlen(serviceData)-1] == ' ') serviceData[strlen(serviceData)-1] = '\0'; // remove any trailing space
      strcat(serviceData,"\x81");
    }
    u8g2.drawStr(0,y-1,serviceData);
  }
}

// Draw/update the Underground Arrivals Board
void drawUndergroundBoard() {
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
  drawStationHeader(locationName,"","",0);

  if (station.boardChanged) {
    isShowingVia = false;
    if (station.origin[0]) viaTimer=millis()+6000; else viaTimer=millis()+300000;
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
      centreText("There are no scheduled arrivals at this station.",ULINE1-1);
    }
  }

  numMessages = 0;

  // Add weather message if enabled and available
  if (weatherEnabled && weatherMsg[0]) {
    strcpy(line2[numMessages],weatherMsg);
    numMessages++;
  }

  // Check if RSS should be inserted before TfL messages
  if (rssEnabled && rssPriority && rssMessage[0] && !noScrolling) {
    strcpy(line2[numMessages],rssMessage);
    numMessages++;
  }

  // Add any TfL messages
  for (int i=0;i<messages.numMessages;i++) {
    strcpy(line2[numMessages],messages.messages[i]);
    numMessages++;
  }

  // Check if RSS should be added after TfL messages
  if (rssEnabled && !rssPriority && rssMessage[0] && !noScrolling) {
    strcpy(line2[numMessages],rssMessage);
    numMessages++;
  }

  u8g2.sendBuffer();
}

/*
 *
 * Bus Departures Board
 *
 */
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
  drawStationHeader(locationName,"",locationFilter,0);

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
      centreText("There are no scheduled services at this stop.",ULINE1-1);
    }
  }
  u8g2.sendBuffer();
}

void updateBusDepartures() {
  busdata.loadDepartures(&station);
  lastDataLoadTime = millis();
  noDataLoaded = false;
  dataLoadSuccess++;
  // Work out the max column size for service numbers
  busDestX=0;
  u8g2.setFont(NatRailSmall9);
  for (int i=0;i<station.numServices;i++) {
    int svcWidth = getStringWidth(station.service[i].via);
    busDestX = (busDestX > svcWidth) ? busDestX : svcWidth;
  }
  busDestX+=5;
  if (weatherEnabled && weatherMsg[0]) {
    strcpy(line2[0],weatherMsg);
    strcpy(line2[1],btAttribution);
    messages.numMessages=2;
  } else{
    strcpy(line2[0],btAttribution);
    messages.numMessages=1;
  }
}

/*
 * Web GUI functions
 */

// Helper function for returning text status messages
void sendResponse(int code, String msg, AsyncWebServerRequest *request) {
  request->send(code,contentTypeText,msg);
}

// Return the correct MIME type for a file name
String getContentType(String filename) {
  if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".json")) {
    return "application/json";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  } else if (filename.endsWith(".svg")) {
    return "image/svg+xml";
  } else if (filename.endsWith(".webp")) {
    return "image/webp";
  }
  return "text/plain";
}

// Stream a file from the file system
bool handleStreamFile(String filename, AsyncWebServerRequest *request) {
  if (LittleFS.exists(filename)) {
    String contentType = getContentType(filename);
    request->send(LittleFS,filename,contentType);
    return true;
  } else return false;
}

// Stream a file stored in flash (default graphics are now included in the firmware image)
void handleStreamFlashFile(String filename, const uint8_t *filedata, size_t contentLength, AsyncWebServerRequest *request) {
  String contentType = getContentType(filename);
  AsyncWebServerResponse *response = request->beginResponse(200, contentType, filedata, contentLength);
  response->addHeader("Cache-Control", "public,max-age=3600,s-maxage=3600");
  request->send(response);
}

void handleStreamGzipFlashFile(String filename, const uint8_t *filedata, size_t contentLength, AsyncWebServerRequest *request) {
  String contentType = getContentType(filename);
  AsyncWebServerResponse *response = request->beginResponse(200, contentType, filedata, contentLength);
  response->addHeader("Content-Encoding", "gzip");
  request->send(response);
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
void handleFileList(AsyncWebServerRequest *request) {
  String path;
  if (!request->hasParam("dir")) path="/"; else path = request->getParam("dir")->value();
  File root = LittleFS.open(path);

  String output="<html><body style=\"font-family:Helvetica,Arial,sans-serif\"><h2>Departures Board File System</h2>";
  if (!root) {
    output+="<p>Failed to open directory</p>";
  } else if (!root.isDirectory()) {
    output+="<p>Not a directory</p>";
  } else {
    output+="<table>";
    File file = root.openNextFile();
    while (file) {
      output+="<tr><td>";
      if (file.isDirectory()) {
        output+="[DIR]</td><td><a href=\"/rmdir?f=" + String(file.path()) + "\" title=\"Delete\">X</a></td><td><a href=\"/dir?dir=" + String(file.path()) + "\">" + String(file.name()) + "</a></td></tr>";
      } else {
        output+=String(file.size()) + "</td><td><a href=\"/del?f="+ String(file.path()) + "\" title=\"Delete\">X</a></td><td><a href=\"/cat?f=" + String(file.path()) + "\">" + String(file.name()) + "</a></td></tr>";
      }
      file = root.openNextFile();
    }
  }

  output += "</table><br>";
  output += getFSInfo() + "<p><a href=\"/upload\">Upload</a> a file</p></body></html>";
  request->send(200,contentTypeHtml,output);
}

// Stream a file to the browser
void handleCat(AsyncWebServerRequest *request) {
  if (request->hasParam("f")) {
    String filename = request->getParam("f")->value();
    handleStreamFile(filename,request);
  } else sendResponse(404,"Not found",request);
}

// Delete a file from the file system
void handleDelete(AsyncWebServerRequest *request) {
  if (request->hasParam("f")) {
    String filename = request->getParam("f")->value();
    if (LittleFS.remove(filename)) {
      // Successfully removed go back to directory listing
      request->redirect("/dir");
    } else sendResponse(400,"Failed to delete file",request);
  } else sendResponse(404,"Not found",request);
}

// Format the file system
void handleFormatFFS(AsyncWebServerRequest *request) {
  String message;

  if (LittleFS.format()) {
    message="File System was successfully formatted\n\n";
    message+=getFSInfo();
  } else message="File System could not be formatted!";
  sendResponse(200,message,request);
}

/*
 * Web GUI handlers
 */

// Fallback function for browser requests
void handleNotFound(AsyncWebServerRequest *request) {
  if ((LittleFS.exists(request->url())) && (request->method() == HTTP_GET)) handleStreamFile(request->url(),request);
  else if (request->url() == "/keys.htm") handleStreamGzipFlashFile(request->url(), keyshtm, sizeof(keyshtm),request);
  else if (request->url() == "/index.htm") handleStreamGzipFlashFile(request->url(), indexhtm, sizeof(indexhtm),request);
  else if (request->url() == "/editrss.htm") handleStreamGzipFlashFile(request->url(), editrsshtm, sizeof(editrsshtm),request);
  else if (request->url() == "/nrelogo.webp") handleStreamFlashFile(request->url(), nrelogo, sizeof(nrelogo),request);
  else if (request->url() == "/rdglogo.webp") handleStreamFlashFile(request->url(), rdglogo, sizeof(nrelogo),request);
  else if (request->url() == "/tfllogo.webp") handleStreamFlashFile(request->url(), tfllogo, sizeof(tfllogo),request);
  else if (request->url() == "/btlogo.webp") handleStreamFlashFile(request->url(), btlogo, sizeof(btlogo),request);
  else if (request->url() == "/tube.webp") handleStreamFlashFile(request->url(), tubeicon, sizeof(tubeicon),request);
  else if (request->url() == "/nr.webp") handleStreamFlashFile(request->url(), nricon, sizeof(nricon),request);
  else if (request->url() == "/ibus.webp") handleStreamFlashFile(request->url(), ibus, sizeof(ibus),request);
  else if (request->url() == "/irail.webp") handleStreamFlashFile(request->url(), irail, sizeof(irail),request);
  else if (request->url() == "/itube.webp") handleStreamFlashFile(request->url(), itube, sizeof(itube),request);
  else if (request->url() == "/favicon.png") handleStreamFlashFile(request->url(), faviconpng, sizeof(faviconpng),request);
  else if (request->url() == "/rss.json") handleStreamGzipFlashFile(request->url(), rssjson, sizeof(rssjson),request);
  else sendResponse(404,"Not Found",request);
}

String getResultCodeText(int resultCode) {
  switch (resultCode) {
    case UPD_SUCCESS:
      return "SUCCESS";
      break;
    case UPD_NO_CHANGE:
      return "SUCCESS (NO CHANGES)";
      break;
    case UPD_SEC_CHANGE:
      return "SUCCESS (SECONDARY CHANGES)";
      break;
    case UPD_DATA_ERROR:
      return "DATA ERROR";
      break;
    case UPD_UNAUTHORISED:
      return "UNAUTHORISED";
      break;
    case UPD_HTTP_ERROR:
      return "HTTP ERROR";
      break;
    case UPD_INCOMPLETE:
      return "INCOMPLETE DATA RECEIVED";
      break;
    case UPD_NO_RESPONSE:
      return "NO RESPONSE FROM SERVER";
      break;
    case UPD_TIMEOUT:
      return "TIMEOUT WAITING FOR SERVER";
      break;
    default:
      return "OTHER ERROR";
      break;
  }
}

// Send some useful system & station information to the browser
void handleInfo(AsyncWebServerRequest *request) {
  unsigned long uptime = millis();
  char sysUptime[30];
  int days = uptime / msDay ;
  int hours = (uptime % msDay) / msHour;
  int minutes = ((uptime % msDay) % msHour) / msMin;

  sprintf(sysUptime,"%d days, %d hrs, %d min", days,hours,minutes);

  String message = "Free Heap: " + String(ESP.getFreeHeap()) + "\nMin Heap: " + String(ESP.getMinFreeHeap()) + "\nLargest free block: " + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)) + "\nHostname: " + String(hostname) + "\nFirmware version: v" + String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + " " + getBuildTime() + "\nSystem uptime: " + String(sysUptime) + "\nFree LittleFS space: " + String(LittleFS.totalBytes() - LittleFS.usedBytes());
  message+="\nCore Plaform: " + String(ESP.getCoreVersion()) + "\nCPU speed: " + String(ESP.getCpuFreqMHz()) + "MHz\nCPU Temperature: " + String(temperatureRead()) + "\nWiFi network: " + String(WiFi.SSID()) + "\nWiFi signal strength: " + String(WiFi.RSSI()) + "dB";

  sprintf(sysUptime,"%02d:%02d:%02d %02d/%02d/%04d",timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec,timeinfo.tm_mday,timeinfo.tm_mon+1,timeinfo.tm_year+1900);
  message+="\nSystem clock: " + String(sysUptime);
  if (ghUpdate.releaseId.length()) {
    message+="\nGithub: " + ghUpdate.releaseId;
  }

  if (schedulerActive) message+="\nScheduler active, next event at " + String(nextSlotEventTime);
  else if (carouselActive) message+="\nCarousel active, next event at " + String(nextSlotEventTime);

  message+="\nCurrent location code: " + String(locationCode) + "\nCurrent location name: " + String(locationName) + "\nSuccessful: " + String(dataLoadSuccess) + "\nFailures: " + String(dataLoadFailure) + "\nTime since last data load: " + String((int)((millis()-lastDataLoadTime)/1000)) + " seconds";
  if (dataLoadFailure) message+="\nTime since last failure: " + String((int)((millis()-lastLoadFailure)/1000)) + " seconds";
  message+="\nLast Result: ";
  switch (boardMode) {
    case MODE_RAIL:
      if (useRDMclient) message+="RDMClient: " + String(jsonKeyBuffer.lastResultMessage);
      else message+="darwinClient: " + String(jsonKeyBuffer.lastResultMessage);
      break;

    case MODE_TUBE:
      message+=String(jsonKeyBuffer.lastResultMessage);
      break;

    case MODE_BUS:
      message+=String(jsonKeyBuffer.lastResultMessage);
      break;
  }
  message+="\nUpdate result code: ";
  message+=getResultCodeText(lastUpdateResult);
  message+="\nServices: " + String(station.numServices) + "\nMessages: ";
  int nMsgs = messages.numMessages;
  if (boardMode == MODE_TUBE) nMsgs--;
  message+=String(nMsgs) + "\n";

  if (rssEnabled) {
    message+="Last RSS result: " + getResultCodeText(lastRssUpdateResult) + "\nNext RSS update: " + String(nextRssUpdate-millis()) + "ms\n\n";
  }

  if (weatherEnabled) {
    message+="Last weather result: " + getResultCodeText(lastWeatherUpdateResult) + "\nNext weather update: " + String(nextWeatherUpdate-millis()) + "ms";
  }
  sendResponse(200,message,request);
}

// Stream the index.htm page unless we're in first time setup and need the api keys
void handleRoot(AsyncWebServerRequest *request) {
  if (!apiKeys) {
    if (LittleFS.exists("/keys.htm")) handleStreamFile("/keys.htm",request); else handleStreamGzipFlashFile("/keys.htm",keyshtm,sizeof(keyshtm),request);
  } else {
    if (LittleFS.exists("/index_d.htm")) handleStreamFile("/index_d.htm",request); else handleStreamGzipFlashFile("/index.htm",indexhtm,sizeof(indexhtm),request);
  }
}

// Send the firmware version to the client (called from index.htm)
void handleFirmwareInfo(AsyncWebServerRequest *request) {
  String response = "{\"firmware\":\"B" + String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + "-W" + String(WEBAPPVER_MAJOR) + "." + String(WEBAPPVER_MINOR) + "\"}";
  request->send(200,contentTypeJson,response);
}

// Force a reboot of the ESP32
void handleReboot(AsyncWebServerRequest *request) {
  sendResponse(200,"The Departures Board is restarting...",request);
  restartTimer.once(1, []() { ESP.restart(); });
}

// Erase the stored WiFiManager credentials
void handleEraseWiFi(AsyncWebServerRequest *request) {
  sendResponse(200,"Erasing stored WiFi settings.\n\nYou will need to connect to the \"Departures Board\" network and use WiFi Manager to reconfigure the settings.",request);
  restartTimer.once(1, []() { WiFiManager wm; wm.resetSettings(); ESP.restart();});
}

// "Factory reset" the app - delete WiFi, format file system and reboot
void handleFactoryReset(AsyncWebServerRequest *request) {
  sendResponse(200,"Factory reseting the Departures Board...",request);
  restartTimer.once(1, []() { WiFiManager wm; wm.resetSettings(); LittleFS.format(); ESP.restart();});
}

// Interactively change the brightness of the OLED panel (called from index.htm)
void handleBrightness(AsyncWebServerRequest *request) {
  if (request->hasParam("b")) {
    int level = request->getParam("b")->value().toInt();
    if (level>0 && level<256) {
      u8g2.setContrast(level);
      brightness = level;
      sendResponse(200,"OK",request);
      return;
    }
  }
  sendResponse(200,"invalid request",request);
}

// Web GUI has requested updates be installed
void handleOtaUpdate(AsyncWebServerRequest *request) {
  sendResponse(200,"Update initiated - check the Departure Board display for progress.",request);
  manualUpdateCheck = true;
}

void doManualOtaCheck() {
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText("Getting latest firmware details from GitHub...",26);
  u8g2.sendBuffer();

  if (ghUpdate.getLatestRelease()==UPD_SUCCESS) {
    checkForFirmwareUpdate();
  } else {
    for (int i=15;i>=0;i--) {
      showUpdateCompleteScreen("Firmware Update Check Failed","Unable to retrieve latest release information.",jsonKeyBuffer.lastResultMessage,"",i,false);
      delay(1000);
    }
  }
  // Always restart
  ESP.restart();
}

// Endpoint for controlling sleep mode
void handleControl(AsyncWebServerRequest *request) {
  String resp = "{\"sleeping\":";
  if (request->hasParam("sleep")) {
    if (request->getParam("sleep")->value() == "1") forcedSleep=true; else forcedSleep=false;
  }
  if (request->hasParam("clock")) {
    if (request->getParam("clock")->value() == "1") sleepClock=true; else sleepClock=false;
  }
  resp += (isSleeping || forcedSleep) ? "true":"false";
  resp += ",\"display\":";
  resp += (sleepClock || (!isSleeping && !forcedSleep)) ? "true":"false";
  resp += "}";
  request->send(200, contentTypeJson, resp);
}

/*
 * External data functions - weather, stationpicker, firmware updates
 */

// Call the National Rail Station Picker (called from index.htm)
void handleStationPicker(AsyncWebServerRequest *request)
{
  if (!request->hasParam("q")) {
    sendResponse(400,"Missing Query",request);
    return;
  }

  String query = request->getParam("q")->value();
  if (query.length() <= 2) {
    sendResponse(400,"Query too short",request);
    return;
  }

  const char* host = "stationpicker.nationalrail.co.uk";
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(4000);

  if (!client.connect(host, 443)) {
    sendResponse(408, "NR Connect Timeout",request);
    return;
  }

  client.print(String("GET /stationPicker/") + query + " HTTP/1.0\r\n"
               "Host: stationpicker.nationalrail.co.uk\r\n"
               "Referer: https://www.nationalrail.co.uk\r\n"
               "Origin: https://www.nationalrail.co.uk\r\n"
               "Connection: close\r\n\r\n");

  int requestTimer = 0;
  while (!client.available() && requestTimer<1000) {
    requestTimer++;
    delay(1);
  }

  if (!client.available()) {
    client.stop();
    sendResponse(408,"NRQ Timeout",request);
  }

  String statusLine = client.readStringUntil('\n');

  if (statusLine.indexOf("200") == -1) {
    client.stop();
    sendResponse(503, statusLine, request);
    return;
  }

  // Skip the remaining headers
  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  // Start sending response
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  uint8_t buffer[512];
  unsigned long timeout = millis() + 5000UL;
  while ((client.connected() || client.available()) && millis() < timeout) {
    int len = client.read(buffer, sizeof(buffer));
    if (len > 0) {
      response->write(buffer, len);
      delay(1);
    }
  }

  client.stop();
  request->send(response);
}

/*
 * Setup / Loop functions
*/

//
// The main processing cycle for the National Rail Departures Board
//
void departureBoardLoop() {

  if (millis() > nextDataUpdate && !fetchInProgress && lastUpdateResult != UPD_UNAUTHORISED && !isSleeping && wifiConnected) {
    if (!firstLoad) showUpdateIcon(true);
    // Initiate a background update on Core 0
    fetchMode = FETCH_BOARD;
    fetchInProgress = true;
    xTaskNotifyGive(fetchTaskHandle);
    if (firstLoad) {
      waitForFirstLoad();
      if (lastUpdateResult == UPD_NO_CHANGE || lastUpdateResult == UPD_SEC_CHANGE) lastUpdateResult = UPD_SUCCESS;
    }
  }

  if (fetchComplete && updateIconVisible) showUpdateIcon(false);

  if (fetchComplete && lastUpdateResult == UPD_SEC_CHANGE && !isScrollingService && !isSleeping) {
    fetchComplete = false;
    updateRailDepartures();
    if (station.numServices) {
      if (!station.service[0].via[0]) isShowingVia=false;
      drawPrimaryService(isShowingVia);
      u8g2.updateDisplayArea(0,1,32,3);
      if (station.calling[0] && showFullCalling) {
        for (int i=0;i<numMessages;i++) {
          if (strncmp("Calling",line2[i],7)==0) {
            // refresh the calling at times
            sprintf(line2[i],"Calling at: %s",station.calling);
            break;
          }
        }
      }
    }
    if (noScrolling && station.numServices>1) {
      drawServiceLine(1,LINE2);
    }
  }

  if (fetchComplete && lastUpdateResult != UPD_SEC_CHANGE && !isScrollingService && !isSleeping) {
    if (!isScrollingStops || (!showFullCalling && isShowingCalling) || (!showFullMsgs && !isShowingCalling)) {
      fetchComplete = false;
      // Get the update data if there is any
      if (lastUpdateResult == UPD_SUCCESS) {
        // Retrieve the updated data
        updateRailDepartures();
        drawStationBoard();
      } else if (lastUpdateResult == UPD_NO_CHANGE) {
        lastDataLoadTime = millis();
        noDataLoaded = false;
        dataLoadSuccess++;
      } else if (lastUpdateResult == UPD_DATA_ERROR || lastUpdateResult == UPD_TIMEOUT || lastUpdateResult == UPD_HTTP_ERROR) {
        lastLoadFailure=millis();
        dataLoadFailure++;
        if (noDataLoaded) showNoDataScreen();
      } else if (lastUpdateResult == UPD_UNAUTHORISED) {
        showTokenErrorScreen();
        while (true) { delay(1);}
      } else {
        dataLoadFailure++;
      }
    }
  }

  if (millis()>timer && numMessages && !isScrollingStops && !isSleeping && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR && !noScrolling && !noDataLoaded) {
    // Need to start a new scrolling line 2
    prevMessage = currentMessage;
    prevScrollStopsLength = scrollStopsLength;
    currentMessage++;
    if (currentMessage>=numMessages) currentMessage=0;
    scrollStopsXpos=0;
    scrollStopsYpos=10;
    scrollStopsLength = getStringWidth(line2[currentMessage]);
    isScrollingStops=true;
    if (strncmp("Calling",line2[currentMessage],7)==0) isShowingCalling=true; else isShowingCalling=false;
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

  if (millis()>serviceTimer && !isScrollingService && !isSleeping && !noDataLoaded && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
    // Need to change to the next service if there is one
    if ((station.numServices <= 1 || (station.numServices==2 && noScrolling)) && !weatherMsg[0]) {
      // There's no other services and no weather so just so static attribution.
      drawServiceLine(1+((station.numServices==2 && noScrolling)?1:0),LINE3);
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
    if (!firstLoad) drawCurrentTime();

    // To ensure a consistent refresh rate (for smooth text scrolling), we update the screen every 25ms (around 40fps)
    // so we need to wait any additional ms not used by processing so far before sending the frame to the display controller
    delayMs = frameTimeRail - (millis()-refreshTimer);
    if (delayMs>0) delay(delayMs);
    u8g2.updateDisplayArea(0,3,32,4);
    refreshTimer=millis();
  }
}

//
// Processing loop for London Underground Arrivals board
//
void undergroundArrivalsLoop() {
  bool fullRefresh = false;

  if (millis()>nextDataUpdate && !fetchInProgress && !isSleeping && wifiConnected) {
    if (!firstLoad) showUpdateIcon(true);
    // Initiate a background update on Core 0
    fetchMode = FETCH_BOARD;
    fetchInProgress = true;
    xTaskNotifyGive(fetchTaskHandle);
    if (firstLoad) waitForFirstLoad();
    if (lastUpdateResult == UPD_NO_CHANGE) lastUpdateResult = UPD_SUCCESS;
  }

  if (fetchComplete && updateIconVisible) showUpdateIcon(false);

  if (fetchComplete && lastUpdateResult == UPD_NO_CHANGE && !isScrollingPrimary && !isSleeping) {
    fetchComplete = false;
    updateArrivals();
    // Draw the primary service line(s)
    if (station.numServices) {
      drawUndergroundService(0,ULINE1,(showTubeCurrentLocation && isShowingVia && station.origin[0]));
      if (station.numServices>1) drawUndergroundService(1,ULINE2);
    } else {
      u8g2.setFont(Underground10);
      blankArea(0,ULINE1,256,ULINE3-ULINE1);
      centreText("There are no scheduled arrivals at this station.",ULINE1-1);
    }
    fullRefresh = true;
  }

  if (fetchComplete && lastUpdateResult != UPD_NO_CHANGE && (!isScrollingService || !showFullMsgs) && !isScrollingPrimary && !isSleeping) {
    fetchComplete = false;
    isScrollingService = false;
    // Get the updated data
    if (lastUpdateResult == UPD_SUCCESS) {
      updateArrivals();
      drawUndergroundBoard();
    } else if (lastUpdateResult == UPD_DATA_ERROR || lastUpdateResult == UPD_TIMEOUT || lastUpdateResult == UPD_HTTP_ERROR) {
      lastLoadFailure = millis();
      dataLoadFailure++;
      if (noDataLoaded) showNoDataScreen(); else drawUndergroundBoard();
    } else if (lastUpdateResult == UPD_UNAUTHORISED) {
      showTokenErrorScreen();
      while (true) delay(10);
    } else {
      dataLoadFailure++;
    }
  }

  // Check if we're showing currentLocation
  if (showTubeCurrentLocation && millis()>viaTimer) {
    if (station.numServices && station.origin[0] && !isSleeping && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
      isShowingVia = !isShowingVia;
      drawUndergroundService(0,ULINE1,isShowingVia);
      u8g2.updateDisplayArea(0,1,32,3);
      if (isShowingVia) viaTimer = millis()+3000; else viaTimer = millis()+8000;
    }
  }

  // Scrolling the additional services
  if (millis()>serviceTimer && !isScrollingService && !isSleeping && !noDataLoaded && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
    if (station.numServices<=2 && numMessages==1 && attributionScrolled) {
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
        if (currentMessage>=numMessages) {
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
    else centreText("There are no scheduled arrivals at this station.",scrollPrimaryYpos+ULINE1-1);
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
    drawCurrentTimeUG();

    delayMs = frameTimeTube - (millis()-refreshTimer);
    if (delayMs>0) delay(delayMs);
    if (fullRefresh) u8g2.updateDisplayArea(0,1,32,6); else u8g2.updateDisplayArea(0,5,32,2);
    refreshTimer=millis();
  }
}

//
// Processing loop for Bus Departures board
//
void busDeparturesLoop() {
  bool fullRefresh = false;

  if (millis()>nextDataUpdate && !fetchInProgress && !isSleeping && wifiConnected) {
    if (!firstLoad) showUpdateIcon(true);
    // Initiate a background update on Core 0
    fetchMode = FETCH_BOARD;
    fetchInProgress = true;
    xTaskNotifyGive(fetchTaskHandle);
    if (firstLoad) waitForFirstLoad();
    if (lastUpdateResult == UPD_NO_CHANGE) lastUpdateResult = UPD_SUCCESS;
  }

  if (fetchComplete && updateIconVisible) showUpdateIcon(false);

  if (fetchComplete && lastUpdateResult == UPD_NO_CHANGE) {
    fetchComplete=false;
    updateBusDepartures();
    // Draw the primary service line(s)
    if (station.numServices) {
      drawBusService(0,ULINE1,busDestX);
      if (station.numServices>1) drawBusService(1,ULINE2,busDestX);
    } else {
      u8g2.setFont(NatRailSmall9);
      blankArea(0,ULINE1,256,ULINE3-ULINE1);
      centreText("There are no scheduled services at this stop.",ULINE1-1);
    }
    fullRefresh = true;
  }

  if (fetchComplete && lastUpdateResult != UPD_NO_CHANGE && !isScrollingService && !isScrollingPrimary && !isSleeping) {
    fetchComplete = false;
    if (lastUpdateResult == UPD_SUCCESS) {
      updateBusDepartures();
      drawBusDeparturesBoard();
    } else if (lastUpdateResult == UPD_DATA_ERROR || lastUpdateResult == UPD_TIMEOUT || lastUpdateResult == UPD_HTTP_ERROR) {
      lastLoadFailure = millis();
      dataLoadFailure++;
      if (noDataLoaded) showNoDataScreen(); else drawBusDeparturesBoard();
    } else if (lastUpdateResult == UPD_UNAUTHORISED) {
      showTokenErrorScreen();
      while (true) delay(10);
    } else {
      dataLoadFailure++;
    }
  }

  // Scrolling the additional services
  if (millis()>serviceTimer && !isScrollingPrimary && !isScrollingService && !isSleeping && !noDataLoaded && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
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
    else centreText("There are no scheduled services at this stop.",scrollPrimaryYpos+ULINE1-1);
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
    // just use the Tube clock for bus mode
    if (drawCurrentTimeUG()) u8g2.setFont(NatRailSmall9);

    delayMs = frameTimeBus - (millis()-refreshTimer);
    if (delayMs>0) delay(delayMs);
    if (fullRefresh) u8g2.updateDisplayArea(0,1,32,6); else u8g2.updateDisplayArea(0,5,32,2);
    refreshTimer=millis();
  }
}

// The Core 0 Background Task
void fetchDeparturesTask(void *pvParameters) {
  while(true) {
    // Put task to sleep until triggered by Core 1
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Perform the requested data update...
    fetchInProgress = true;
    switch (fetchMode) {
      case FETCH_BOARD:
        switch (boardMode) {
          case MODE_RAIL:
            if (useRDMclient) {
              lastUpdateResult = rdmRailData.fetchDepartures(&station,&messages,locationCode,rdmDeparturesApiKey,rdmServiceApiKey,MAXBOARDSERVICES,enableBus,callingCrsCode,locationCleanFilter,nrTimeOffset,(showLastSeen && !noScrolling),showServiceMsgs);
            } else {
              lastUpdateResult = darwinRailData.fetchDepartures(&station,&messages,locationCode,nrToken,MAXBOARDSERVICES,enableBus,callingCrsCode,locationCleanFilter,nrTimeOffset,(showLastSeen && !noScrolling),showServiceMsgs);
            }
            nextDataUpdate = millis()+apiRefreshRate;
            break;
          case MODE_TUBE:
            lastUpdateResult = tfldata.fetchArrivals(&station,&messages,locationCode,lineId,lineDirection,(noScrolling || !showServiceMsgs),tflAppKey);
            nextDataUpdate = millis() + UGDATAUPDATEINTERVAL; // default update freq
            break;
          case MODE_BUS:
            lastUpdateResult = busdata.fetchDepartures(&station,locationCode,locationCleanFilter);
            nextDataUpdate = millis() + BUSDATAUPDATEINTERVAL;
            break;
        }
        fetchComplete = true;
        break;

      case FETCH_WEATHER:
        // Update the weather forecast
        lastWeatherUpdateResult = currentWeather.updateWeather(openWeatherMapApiKey, locationLat, locationLon);
        nextWeatherUpdate = millis() + WEATHERUPDATEINTERVAL; // update every 20 mins
        weatherFetchComplete = true;
        break;

      case FETCH_RSS:
        // Update the RSS headlines
        lastRssUpdateResult=rss.loadFeed(rssURL);
        nextRssUpdate = millis() + RSSUPDATEINTERVAL;
        rssFetchComplete = true;
        break;
    }

    // Signal to Core 1 that the fetch is complete
    fetchInProgress = false;
  }
}

//
// Setup code
//
void setup(void) {
  // These are the default wsdl XML SOAP entry points. They can be overridden in the config.json file if necessary
  strlcpy(wsdlHost,"lite.realtime.nationalrail.co.uk",sizeof(wsdlHost));
  strlcpy(wsdlAPI,"/OpenLDBWS/wsdl.aspx?ver=2021-11-01",sizeof(wsdlAPI));
  u8g2.begin();                       // Start the OLED panel
  u8g2.setContrast(brightness);       // Initial brightness
  u8g2.setDrawColor(1);               // Only a monochrome display, so set the colour to "on"
  u8g2.setFontMode(1);                // Transparent fonts
  u8g2.setFontRefHeightAll();         // Count entire font height
  u8g2.setFontPosTop();               // Reference from top
  u8g2.setFont(NatRailTall12);
  String buildDate = String(__DATE__);
  String notice = "\x80 " + buildDate.substring(buildDate.length()-4) + " Gadec Software (github.com/gadec-uk)";

  bool isFSMounted = LittleFS.begin(true);    // Start the File System, format if necessary
  strcpy(station.location,"");                // No default location
  strcpy(weatherMsg,"");                      // No weather message
  strcpy(nrToken,"");                         // No default National Rail token
  strcpy(tflAppKey,"");                       // No default TfL app_key
  loadApiKeys();                              // Load the API keys from the apiKeys.json
  loadConfig(true);                           // Load the configuration settings from config.json
  u8g2.setContrast(brightness);               // Set the panel brightness to the user saved level
  if (flipScreen) u8g2.setFlipMode(1);
  u8g2.clearBuffer();
  u8g2.drawXBM(81,0,gadeclogo_width,gadeclogo_height,gadeclogo_bits);
  centreText(notice.c_str(),48);
  u8g2.sendBuffer();
  delay(5000);

  u8g2.clearBuffer();
  drawStartupHeading();
  u8g2.sendBuffer();
  progressBar("Connecting to Wi-Fi",20);
  WiFi.mode(WIFI_MODE_NULL);        // Reset the WiFi
  WiFi.setSleep(WIFI_PS_NONE);      // Turn off WiFi Powersaving
  WiFi.hostname(hostname);          // Set the hostname ("Departures Board")
  WiFi.mode(WIFI_STA);              // Enter WiFi station mode

  WiFiManager wm;                             // Start WiFiManager
  wm.setAPCallback(wmConfigModeCallback);     // Set the callback for config mode notification
  wm.setWiFiAutoReconnect(true);              // Attempt to auto-reconnect WiFi
  wm.setConnectTimeout(8);
  wm.setConnectRetries(2);
  std::vector<const char *> menu = {"wifi","exit"};
  wm.setMenu(menu);

  bool result = wm.autoConnect("Departures Board");    // Attempt to connect to WiFi (or enter interactive configuration mode)
  if (!result || wifiConfigured) {
    // Need to restart after config (cannot reuse port)
    ESP.restart();
  }

  // Wait for WiFi connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
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
  progressBar("Wi-Fi Connected",30);
  u8g2.sendBuffer();                                              // Send to OLED panel

  // Configure the local webserver paths
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){handleRoot(request);});
  server.on("/erasewifi", HTTP_GET, [](AsyncWebServerRequest *request){handleEraseWiFi(request);});
  server.on("/factoryreset", HTTP_GET, [](AsyncWebServerRequest *request){handleFactoryReset(request);});
  server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request){handleInfo(request);});
  server.on("/formatffs", HTTP_GET, [](AsyncWebServerRequest *request){handleFormatFFS(request);});
  server.on("/dir", HTTP_GET, [](AsyncWebServerRequest *request){handleFileList(request);});
  server.onNotFound([](AsyncWebServerRequest *request){handleNotFound(request);});
  server.on("/cat", HTTP_GET, [](AsyncWebServerRequest *request){handleCat(request);});
  server.on("/del", HTTP_GET, [](AsyncWebServerRequest *request){handleDelete(request);});
  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){handleReboot(request);});
  server.on("/stationpicker", HTTP_GET, [](AsyncWebServerRequest *request){handleStationPicker(request);});
  server.on("/firmware", HTTP_GET, [](AsyncWebServerRequest *request){handleFirmwareInfo(request);});
  server.on("/brightness", HTTP_GET, [](AsyncWebServerRequest *request){handleBrightness(request);});
  server.on("/ota", HTTP_GET, [](AsyncWebServerRequest *request){handleOtaUpdate(request);});
  server.on("/control", HTTP_GET, [](AsyncWebServerRequest *request){handleControl(request);});
  server.on("/success", HTTP_GET, [](AsyncWebServerRequest *request){request->send(200,contentTypeHtml,successPage);});

  //
  // Save settings returned by the Web GUI
  //
  server.on("/savesettings", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->_tempObject) {
      String* body = (String*)(request->_tempObject);
      saveFile("/config.json", body->c_str());

      delete body; // Clean up memory
      request->_tempObject = nullptr;

      if ((!railIsSet && !tubeIsSet && !busIsSet) || request->hasParam("reboot")) {
        // First time setup or base config change, we need a full reboot
        sendResponse(200,"Configuration saved. The Departures Board will now restart.",request);
        restartTimer.once(1, []() { ESP.restart(); });
      } else {
        sendResponse(200,"Configuration updated. The Departures Board will update shortly.",request);
        softResetNeeded = true;
      }
    } else {
      sendResponse(400,"Empty",request);
    }
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!index) {
      // First chunk: Create a String object in RAM
      request->_tempObject = new String("");
    }

    String* body = (String*)(request->_tempObject);
    for (size_t i = 0; i < len; i++) {
      body->concat((char)data[i]);
    }
  });

  //
  // Save the API keys returned from the Web GUI
  //
  server.on("/savekeys", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->_tempObject) {
      String* body = (String*)(request->_tempObject);

      JsonDocument doc;
      bool result = true;
      String msg = "The API keys have been saved successfully.";
      DeserializationError error = deserializeJson(doc, body->c_str());
      if (!error) {
        if (!saveFile("/apikeys.json", body->c_str())) {
          msg = "Failed to save the API keys to the file system (file system corrupt or full?)";
          result = false;
        } else {
          JsonObject settings = doc.as<JsonObject>();
          String nrToken = settings["nrToken"].as<String>();
          String rdmDepToken = settings["rdmDepKey"].as<String>();
          if (!nrToken.length() && !rdmDepToken.length()) msg+="\n\nNote: Only Tube and Bus Departures will be available without either Rail Data or National Rail keys.";
        }
      } else {
        msg = "Invalid JSON format. No changes have been saved.";
        result = false;
      }

      delete body; // Clean up memory
      request->_tempObject = nullptr;

      if (result) {
        // Load/Update the API Keys in memory
        loadApiKeys();
        // If all location codes are blank we're in the setup process. If not, the keys have been changed so just reboot.
        if (!railIsSet && !tubeIsSet && !busIsSet) {
          sendResponse(200,msg,request);
          writeDefaultConfig();
          showSetupCrsHelpScreen();
        } else {
          msg += "\n\nThe Departures Board will now restart.";
          sendResponse(200,msg,request);
          restartTimer.once(1, []() { ESP.restart(); });
        }
      } else {
        sendResponse(400,msg,request);
      }
    } else {
      sendResponse(400,"Empty",request);
    }
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!index) {
      // First chunk: Create a String object in RAM
      request->_tempObject = new String("");
    }

    String* body = (String*)(request->_tempObject);
    for (size_t i = 0; i < len; i++) {
      body->concat((char)data[i]);
    }
  });

  //
  // Handle uploads to LittleFS
  //
  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request){request->send(200,contentTypeHtml,uploadPage);});
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->redirect("/success");
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      String path = "/" + filename;
      if (LittleFS.exists(path)) LittleFS.remove(path);
      size_t fileSize = request->header("Content-Length").toInt();
      size_t availableSpace = LittleFS.totalBytes() - LittleFS.usedBytes() - 1024;

      if (fileSize > availableSpace) {
          sendResponse(507,"Insufficient storage space in File System",request);
          request->client()->close();
          return;
      }
      // First chunk: Create/Open the file and store the handle in _tempObject
      // We use a pointer to a File object so we can keep it open between chunks
      File *file = new File(LittleFS.open(path, FILE_WRITE));
      if (!*file) {
        sendResponse(500,"File System Error",request);
        request->client()->close();
        return;
      }
      request->_tempObject = file;
    }

    // If we have a valid file handle, write the current chunk
    if (len && request->_tempObject) {
      File *file = reinterpret_cast<File *>(request->_tempObject);
      file->write(data, len);
    }

    if (final && request->_tempObject) {
      // Last chunk: Close the file and clean up the pointer
      File *file = reinterpret_cast<File *>(request->_tempObject);
      file->close();
      delete file;
      request->_tempObject = nullptr;
    }
  });

  //
  // Handle manual firmware updates at /update
  //
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){request->send(200,contentTypeHtml,updatePage);});
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Check if the Update library encountered any errors.
    bool shouldReboot = !Update.hasError();

    // Create a response. The AJAX script is just looking for a successful HTTP status.
    AsyncWebServerResponse *response = request->beginResponse((shouldReboot ? 200 : 500), "text/plain", (shouldReboot ? "OK" : "FAIL"));
    response->addHeader("Connection", "close");
    request->send(response);

    // If successful, restart the ESP32 to boot into the new firmware
    if (shouldReboot) restartTimer.once(0.5, []() { ESP.restart(); });
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      // First chunk: Initialize the OTA Update
      // UPDATE_SIZE_UNKNOWN tells the library to just accept chunks until 'final' is true
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        sendResponse(500,"Update begin failed",request);
      }
    }

    // Write chunk data to the flash memory
    if (!Update.hasError() && len) {
      if (Update.write(data, len) != len) {
        sendResponse(500,"Update write failed",request);
      }
    }

    // Final chunk: Close the OTA process
    if (final) {
      if (!Update.end(true)) {
        sendResponse(500,"Update end failed",request);
      }
    }
  });

  server.begin();     // Start the local web server

  // Check for Firmware updates?
  if (firmwareUpdates) {
    progressBar("Checking for firmware updates",40);
    if (ghUpdate.getLatestRelease()==UPD_SUCCESS) {
      checkForFirmwareUpdate();
    } else {
      for (int i=15;i>=0;i--) {
        showUpdateCompleteScreen("Firmware Update Check Failed","Unable to retrieve latest release information.",jsonKeyBuffer.lastResultMessage,"",i,false);
        delay(1000);
      }
      u8g2.clearDisplay();
      drawStartupHeading();
      u8g2.sendBuffer();
    }
  }
  checkPostWebUpgrade();
  // First time configuration?
  if ((!railIsSet && !tubeIsSet && !busIsSet) || (!nrToken[0] && rdmDeparturesApiKey=="" && boardMode==MODE_RAIL)) {
    if (!apiKeys) showSetupKeysHelpScreen();
    else showSetupCrsHelpScreen();
    // First time setup mode will exit with a reboot, so just loop here forever
    while (true) { delay(10); }
  }

  configTzTime(ukTimezone, "uk.pool.ntp.org","time.cloudflare.com","time.windows.com");
  if (timezone!="") {
    setenv("TZ",timezone.c_str(),1);
    tzset();
  }

  // Check the clock has been set successfully before continuing
  int p=50;
  int ntpAttempts=0;
  bool ntpResult=true;
  progressBar("Setting the system clock...",50);
  if(!getLocalTime(&timeinfo,2000)) {              // attempt to set the clock from NTP
    do {
      ntpResult = getLocalTime(&timeinfo,2000);
      ntpAttempts++;
      p+=5;
      progressBar("Setting the system clock...",p);
      if (p>80) p=45;
    } while ((!ntpResult) && (ntpAttempts<10));
  }
  if (!ntpResult) {
    // Sometimes NTP/UDP fails. A reboot usually fixes it.
    progressBar("Failed to set the clock. Rebooting in 5 sec.",0);
    delay(5000);
    ESP.restart();
  }
  prevUpdateCheckDay = timeinfo.tm_mday;
  sprintf(currentTime,"%02d:%02d:%02d",timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);

  // Reload settings (clock has now been set)
  loadConfig();

  station.numServices=0;
  if (rssEnabled && boardMode!=MODE_BUS) {
    progressBar("Loading RSS headlines feed",60);
    updateRssFeed();
  }

  if (weatherEnabled) {
    progressBar("Getting weather conditions",64);
    updateCurrentWeather(locationLat,locationLon);
  }

  // Create the background task pinned to Core 0
  xTaskCreatePinnedToCore(
    fetchDeparturesTask,  // Task function
    "FetchTask",          // Task name
    10240,                // Stack size (CRITICAL: Needs to be large for SSL/XML)
    NULL,                 // Task parameters
    1,                    // Priority
    &fetchTaskHandle,     // Task handle
    0                     // Core 0 (Network/Background core)
  );

  if (boardMode == MODE_RAIL) {
      if (!useRDMclient) {
        // Using legacy darwin XML client
        progressBar("Initialising National Rail interface",67);
        int res = darwinRailData.init(wsdlHost, wsdlAPI);
        if (res != UPD_SUCCESS) {
          showWsdlFailureScreen();
          while (true) {delay(1);}
        }
      }
      progressBar("Initialising National Rail interface",70);
      rdmRailData.cleanFilter(locationFilter,locationCleanFilter,sizeof(locationFilter));
      startupProgressPercent=70;
  } else if (boardMode == MODE_TUBE) {
      progressBar("Initialising TfL interface",70);
      startupProgressPercent=70;
  } else if (boardMode == MODE_BUS) {
      progressBar("Initialising BusTimes interface",70);
      // Create a cleaned filter
      busdata.cleanFilter(locationFilter,locationCleanFilter,sizeof(locationFilter));
      startupProgressPercent=70;
  }
}


void loop(void) {

  if (touchEnabled) button.updateTouchState();

  if (button.wasShortTapped()) {
    if (isSleeping) {
      // force awake
      forcedAwake = true;
    } else {
      switchToNextMode();
    }
  }

  if (millis()-lastTimeUpdate >= 100) {
    // Update the current time
    if (getLocalTime(&timeinfo)) {
      sprintf(currentTime,"%02d:%02d:%02d",timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);
      lastTimeUpdate = millis();
      if (millis()>3888000000 && timeinfo.tm_hour==3) ESP.restart(); // Reboot every 45 days at 3am
    }
  }

  // Check for firmware updates daily if enabled
  if (dailyUpdateCheck && !fetchInProgress && millis()>fwUpdateCheckTimer) {
    fwUpdateCheckTimer = millis() + 3300000 + random(600000); // check again in 55 to 65 mins
    if (timeinfo.tm_mday != prevUpdateCheckDay) {
      if (ghUpdate.getLatestRelease()==UPD_SUCCESS) {
        checkForFirmwareUpdate();
      }
      prevUpdateCheckDay = timeinfo.tm_mday;
    }
  }

  bool wasSleeping = isSleeping;
  isSleeping = isSnoozing();

  if (isSleeping && millis()>timer) {       // If the "screensaver" is active, change the screen every 8 seconds
    drawSleepingScreen();
    timer=millis() + SCREENSAVERINTERVAL;
  } else if (wasSleeping && !isSleeping) {
    // Exit sleep mode cleanly
    softResetBoard(MODE_LOADCONFIG);
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

  if (manualUpdateCheck && !fetchInProgress) doManualOtaCheck();

  if (rssEnabled && boardMode != MODE_BUS && millis() > nextRssUpdate && !fetchInProgress && !isSleeping && wifiConnected) {
    // Start an RSS Update on Core 0
    fetchMode = FETCH_RSS;
    fetchInProgress = true;
    xTaskNotifyGive(fetchTaskHandle);
  }

  if (rssFetchComplete) {
    // Background fetch has completed
    rssFetchComplete = false;
    if (lastRssUpdateResult == UPD_SUCCESS) buildRssMessage();
  }

  if (weatherEnabled && millis()>nextWeatherUpdate && !fetchInProgress && locationLat && locationLon && !isSleeping && wifiConnected) {
    // Start a weather update on Core 0
    fetchMode = FETCH_WEATHER;
    fetchInProgress = true;
    xTaskNotifyGive(fetchTaskHandle);
  }

  if (weatherFetchComplete) {
    weatherFetchComplete = false;
    if (lastWeatherUpdateResult == UPD_SUCCESS) {
      strlcpy(weatherMsg,currentWeather.currentWeatherMessage,MAXWEATHERSIZE);
    } else {
      weatherMsg[0] = '\0';
    }
  }

  if (softResetNeeded && !fetchInProgress) {
    softResetNeeded=false;
    softResetBoard(MODE_LOADCONFIG);
  }

  if ((schedulerActive || (carouselActive && numCarouselSlots>1)) && !isSleeping && !fetchInProgress && millis() > nextSchedulerCheck) {
    int nowTime = getTimeInMinutes();
    if ((activeSlotEventTime < nextSlotEventTime && nowTime >= nextSlotEventTime) || (activeSlotEventTime > nextSlotEventTime && nowTime < activeSlotEventTime && nowTime >= nextSlotEventTime)) {
      if (carouselActive) currentCarouselSlot = (currentCarouselSlot + 1) % numCarouselSlots;
      softResetBoard(MODE_LOADCONFIG);
    }
    nextSchedulerCheck = millis() + 10000;  // ten seconds
  }

}