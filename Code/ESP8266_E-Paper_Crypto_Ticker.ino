#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <Ticker.h>  // For optional hardware watchdog

// WiFi credentials
const char* ssid = "<YOUR SSID>";
const char* password = "<YOUR PASSWORD>";

// Time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200, 60000);  // GMT+2, 60s update

// Kraken API
const char* host = "api.kraken.com";
const char* pricePath = "/0/public/Ticker?pair=XRPEUR"; // Choose your asset

// Display config
GxEPD2_BW<GxEPD2_290_T5, GxEPD2_290_T5::HEIGHT> display(GxEPD2_290_T5(SS, 4, 2, 5));
uint16_t screenWidth = 128;
uint16_t screenLength = 296;

float numer_of_tokens = 1234.56; // Enter your amount of tokens here 
float priceData[10] = {0};
float currentPrice = 0.0;
float openingPrice = 0.0;
float percentualDifference = 0.0;
float valueDifference = 0.0;

// Watchdog and timer
unsigned long lastUpdateMillis = 0;
const unsigned long updateInterval = 30 * 60 * 1000UL;
const int maxHttpRetries = 2;
bool rebootRequired = false;
Ticker watchdogTicker;  // Optional hardware watchdog

const unsigned char coinLogo[] PROGMEM = {
  // ... (same logo data as before)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x1e, 
  0x3c, 0x00, 0x00, 0x3c, 0x1e, 0x00, 0x00, 0x78, 0x0f, 0x00, 0x00, 0xf0, 0x07, 0x80, 0x01, 0xe0, 
  0x03, 0xc0, 0x03, 0xc0, 0x01, 0xf0, 0x0f, 0x80, 0x00, 0xf8, 0x1f, 0x00, 0x00, 0x7e, 0x7e, 0x00, 
  0x00, 0x3f, 0xfc, 0x00, 0x00, 0x1f, 0xf8, 0x00, 0x00, 0x07, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xe0, 0x00, 0x00, 0x1f, 0xf8, 0x00, 0x00, 0x3f, 0xfc, 0x00, 
  0x00, 0x7e, 0x7e, 0x00, 0x00, 0xf8, 0x1f, 0x00, 0x01, 0xe0, 0x07, 0x80, 0x03, 0xc0, 0x03, 0xc0, 
  0x07, 0x80, 0x01, 0xe0, 0x0f, 0x00, 0x00, 0xf0, 0x1e, 0x00, 0x00, 0x78, 0x3c, 0x00, 0x00, 0x3c, 
  0x78, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void connectWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println(" Connected.");
}

void displayText(uint16_t x, uint16_t y, String input) {
  char text[input.length() + 1];
  input.toCharArray(text, sizeof(text));
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
  display.setCursor(x, y);
  display.print(text);
}

void displayFreeMonoBold9pt7bCenter(int y, String text) {
  display.setFont(&FreeMonoBold9pt7b);
  int xpos = 0;
  int maxNumberLetters = 11;
  if (text.length() > maxNumberLetters) {
    text = text.substring(0, maxNumberLetters);
  }
  xpos = (screenWidth - (screenWidth / maxNumberLetters * text.length())) / 2;
  displayText(xpos, y, text);
}

void fetchPriceAndChart() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = String("https://") + host + pricePath;
  if (https.begin(client, url)) {
    int httpCode = https.GET();
    if (httpCode == HTTP_CODE_OK) {
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, https.getString());
      if (!error) {
        JsonObject result = doc["result"];
        for (JsonPair kv : result) {
          JsonArray p = kv.value()["p"];
          currentPrice = p[0].as<float>();
          openingPrice = kv.value()["o"].as<float>();
          percentualDifference = ((currentPrice * 100) / openingPrice) - 100;
          valueDifference = (currentPrice - openingPrice) * numer_of_tokens;
          break;
        }
      }
    }
    https.end();
  }

  unsigned long now = timeClient.getEpochTime();
  unsigned long since = now - 24 * 3600;
  String chartPathSince = String("/0/public/OHLC?pair=XRPEUR&interval=60&since=") + since;
  url = String("https://") + host + chartPathSince;

  if (https.begin(client, url)) {
    int httpCode = https.GET();
    if (httpCode == HTTP_CODE_OK) {
      DynamicJsonDocument doc(8192);
      DeserializationError error = deserializeJson(doc, https.getString());
      if (!error) {
        JsonObject result = doc["result"];
        for (JsonPair kv : result) {
          if (strcmp(kv.key().c_str(), "last") == 0) continue;
          JsonArray ohlc = kv.value().as<JsonArray>();
          int len = min(10, (int)ohlc.size());
          for (int i = 0; i < len; i++) {
            priceData[i] = ohlc[i][4].as<float>();
          }
          break;
        }
      }
    }
    https.end();
  }
}

void drawScreen() {
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawBitmap(10, 10, coinLogo, 32, 32, GxEPD_BLACK);
    display.setFont(&FreeMonoBold18pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(60, 35);
    display.print("XRP");
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(5, 65);
    display.print("$ ");
    display.print(numer_of_tokens * currentPrice, 0);
    display.fillRect(5, 80, 115, 3, GxEPD_BLACK);

    float minVal = priceData[0], maxVal = priceData[0];
    for (int i = 1; i < 10; i++) {
      if (priceData[i] < minVal) minVal = priceData[i];
      if (priceData[i] > maxVal) maxVal = priceData[i];
    }
    float scale = (maxVal - minVal) == 0 ? 1 : (maxVal - minVal);
    int chartX = 5, chartY = 130, chartHeight = 40;

    for (int i = 0; i < 9; i++) {
      int x1 = chartX + i * 13;
      int x2 = chartX + (i + 1) * 13;
      int y1 = chartY - (int)(((priceData[i] - minVal) / scale) * chartHeight);
      int y2 = chartY - (int)(((priceData[i + 1] - minVal) / scale) * chartHeight);
      for (int offset = -1; offset <= 1; offset++) {
        display.drawLine(x1, y1 + offset, x2, y2 + offset, GxEPD_BLACK);
      }
    }

    display.fillRect(5, 140, 115, 3, GxEPD_BLACK);
    displayFreeMonoBold9pt7bCenter(165, "$" + String(currentPrice));
    display.fillRect(15, 180, 95, 3, GxEPD_BLACK);
    displayFreeMonoBold9pt7bCenter(205, "#" + String(numer_of_tokens));
    display.fillRect(15, 220, 95, 3, GxEPD_BLACK);

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(5, 250);
    if (valueDifference >= 0) display.print("+");
    display.print(percentualDifference, 2);
    display.print(" %");

    display.setCursor(5, 265);
    if (valueDifference >= 0) display.print("+");
    display.print(valueDifference, 2);
    display.print(" $");

    time_t rawTime = timeClient.getEpochTime();
    struct tm* timeInfo = localtime(&rawTime);
    
    // Format the timestamp: hh:mm dd.Mon.yy
    char dateStr[20];  // "hh:mm dd.Mon.yy" + null terminator
    
    const char* months[] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    
    sprintf(dateStr, "%02d:%02d %02d.%s.%02d",
            timeInfo->tm_hour,
            timeInfo->tm_min,
            timeInfo->tm_mday,
            months[timeInfo->tm_mon],
            (timeInfo->tm_year + 1900) % 100);

    display.setFont(&FreeMono9pt7b);
    display.setCursor(0, 290);
    display.print(dateStr);
  } while (display.nextPage());
}

void resetWatchdog() {
  ESP.wdtFeed();  // Optional hardware watchdog
}

void safeFetch() {
  int retries = 0;
  while (retries < maxHttpRetries) {
    fetchPriceAndChart();
    if (currentPrice > 0.0 && openingPrice > 0.0) return;
    retries++;
    delay(1000);
  }
  rebootRequired = true;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Booting...");

  display.init();
  connectWiFi();

  timeClient.begin();
  timeClient.update();

  safeFetch();
  /*for(int i = 0; i < 3; i++){
    drawScreen();
    delay(0.2);
  }*/

  watchdogTicker.attach(1, resetWatchdog);  // Optional
}

void loop() {
  unsigned long currentMillis = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost. Rebooting...");
    delay(1000);
    ESP.restart();
  }

  timeClient.update();
  time_t now = timeClient.getEpochTime();
  static int lastDay = -1;
  struct tm* timeInfo = localtime(&now);
  if (timeInfo->tm_hour == 0 && timeInfo->tm_min == 0 && timeInfo->tm_mday != lastDay) {
    Serial.println("Midnight reached. Rebooting...");
    lastDay = timeInfo->tm_mday;  // Prevent repeated reboots
    delay(1000);
    ESP.restart();
  }


  if (currentMillis - lastUpdateMillis >= updateInterval || lastUpdateMillis == 0) {
    safeFetch();
    for(int i = 0; i < 3; i++){
      drawScreen();
      delay(0.2);
    }
    lastUpdateMillis = currentMillis;
  }

  if (rebootRequired) {
    Serial.println("Rebooting due to failure.");
    delay(1000);
    ESP.restart();
  }

  delay(1000);
}
