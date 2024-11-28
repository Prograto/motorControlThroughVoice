#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

const char* ssid = "motor";         
const char* password = "motorcontrol";    

const char* scriptUrl = "https://script.google.com/macros/s/AKfycbyDuJG6FAy-ZQDanPb3qomBpis9xQklbDQX1iH6W0OByzCN5s-39ndb-3o_oZoP3H8ZTw/exec";  // Replace with your Google Apps Script URL
const char* GOOGLE_SCRIPT_ID = "AKfycbyDuJG6FAy-ZQDanPb3qomBpis9xQklbDQX1iH6W0OByzCN5s-39ndb-3o_oZoP3H8ZTw";

ESP8266WebServer server(80);

const int relayPin = 5;      
bool relayState = LOW;    

WiFiClientSecure client;

void updateGoogleSheet(const String& action);
String fetchStatusFromGoogleSheets();
String getTimeStamp();

void handleRoot() {
    String html = "<html>\
  <head>\
    <title>Appliance Control</title>\
    <meta name='viewport' content='width=device-width, initial-scale=1'>\
    <script src='https://ajax.googleapis.com/ajax/libs/jquery/3.6.0/jquery.min.js'></script>\
    <style>\
      body { font-family: Arial, sans-serif; text-align: center; }\
      .button { padding: 16px 40px; font-size: 24px; margin: 20px; cursor: pointer; }\
      .on { background-color: #4CAF50; color: white; }\
      .off { background-color: #f44336; color: white; }\
    </style>\
  </head>\
  <body>\
    <h1>Appliance Control</h1>\
    <div id='status'>Loading...</div>\
    <button id='onButton' class='button on'>Turn ON</button>\
    <button id='offButton' class='button off'>Turn OFF</button>\
    <script>\
      const scriptUrl = '" + String(scriptUrl) + "';\
      $(document).ready(function() {\
        updateStatus();\
        $('#onButton').click(function() {\
          sendAction('ON');\
        });\
        $('#offButton').click(function() {\
          sendAction('OFF');\
        });\
      });\
      function updateStatus() {\
        $.get(scriptUrl, {action: 'getStatus'}, function(data) {\
          $('#status').text('Status: ' + data);\
          if (data === 'ON') {\
            $('#onButton').prop('disabled', true).text('ON (Active)');\
            $('#offButton').prop('disabled', false).text('Turn OFF');\
          } else {\
            $('#onButton').prop('disabled', false).text('Turn ON');\
            $('#offButton').prop('disabled', true).text('OFF (Inactive)');\
          }\
        });\
      }\
      function sendAction(action) {\
        var payload = {\
          timestamp: new Date().toISOString(),\
          action: action\
        };\
        $.post(scriptUrl, JSON.stringify(payload), function() {\
          updateStatus();\
        });\
      }\
      setInterval(function() {\
        updateStatus();\
      }, 10000); // Update status every 10 seconds\
    </script>\
  </body>\
  </html>";
  
    server.send(200, "text/html", html);
}

void handleOn() {
    relayState = HIGH;
    digitalWrite(relayPin, relayState);
    updateGoogleSheet("ON");
    handleRoot();
}

void handleOff() {
    relayState = LOW;
    digitalWrite(relayPin, relayState);
    updateGoogleSheet("OFF");
    handleRoot();
}

String getStatus() {
    return relayState == HIGH ? "ON" : "OFF";
}

void updateGoogleSheet(const String& action) {
    HTTPClient http;
    http.begin(client, scriptUrl);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"timestamp\":\"" + getTimeStamp() + "\",\"action\":\"" + action + "\"}";
    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
        Serial.println("Google Sheets updated successfully");
    } else {
        Serial.println("Error updating Google Sheets");
    }

    http.end();
}

String fetchStatusFromGoogleSheets() {
    if (WiFi.status() == WL_CONNECTED) {
      client.setInsecure();

      HTTPClient https;

      String url = String("https://script.google.com/macros/s/") + GOOGLE_SCRIPT_ID + "/exec?action=getStatus";
      https.begin(client, url);

      https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

      int httpCode = https.GET();
      Serial.println(httpCode);
      if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
          String payload = https.getString();
          Serial.println("Data from Google Sheet: " + payload);
          return payload;
        }
      } else {
        Serial.println("Error on HTTP request: " + httpCode);
      }

      // End the HTTP connection
      https.end();
    } else {
      Serial.println("Wi-Fi not connected");
    }
    return "";
}

String getTimeStamp() {
    char timestamp[20];
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d",
            timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
            timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    return String(timestamp);
}

void setup() {
    Serial.begin(9600);
    pinMode(relayPin, OUTPUT);
    digitalWrite(relayPin, relayState);

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        
        // If WiFi takes more than 20 seconds to connect, reset WiFi
        if (millis() - startTime > 20000) {
            Serial.println("Failed to connect, resetting WiFi...");
            WiFi.disconnect();
            WiFi.begin(ssid, password);
            startTime = millis();
        }
    }

    Serial.println(" connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    server.on("/", handleRoot);
    server.on("/on", handleOn);
    server.on("/off", handleOff);
    server.begin();
    Serial.println("HTTP server started");

    // Initial status fetch and update
    String initialStatus = fetchStatusFromGoogleSheets();
    if (initialStatus == "ON") {
        relayState = LOW;
        digitalWrite(relayPin, LOW);
    } else if (initialStatus == "OFF") {
        relayState = HIGH;
        digitalWrite(relayPin, HIGH);
    }
}

void loop() {
    server.handleClient();

    // Fetch status from Google Sheets every 10 seconds
    static unsigned long lastFetchTime = 0;
    unsigned long currentMillis = millis();
    if (currentMillis - lastFetchTime > 10000) {
        lastFetchTime = currentMillis;
        String currentStatus = fetchStatusFromGoogleSheets();
        Serial.println("Current Status: " + currentStatus);
        if (currentStatus == "ON") {
            relayState = LOW;
            digitalWrite(relayPin, LOW);
        } else if (currentStatus == "OFF") {
            relayState = HIGH;
            digitalWrite(relayPin, HIGH);
        }
        handleRoot();
    }
}
