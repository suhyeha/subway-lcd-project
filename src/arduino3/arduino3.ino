#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "config.h"

// 버튼 핀
const int upButtonPin = D6;
const int downButtonPin = D5;

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// 시간 설정
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 9 * 3600, 60000);

bool timeSynced = false;
String currentWeather = "";
float temperature = 0.0;
unsigned long lastWeatherUpdate = 0;
const unsigned long weatherUpdateInterval = 600000;

// 상태 관리
enum Mode { NORMAL, TRAIN };
Mode currentMode = NORMAL;
unsigned long lastActionTime = 0;
const unsigned long trainDisplayDuration = 5000;

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("WiFi connecting...");

  pinMode(upButtonPin, INPUT_PULLUP);
  pinMode(downButtonPin, INPUT_PULLUP);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  lcd.clear();
  lcd.print("WiFi connected");
  delay(1000);
  lcd.clear();

  timeClient.begin();
  fetchWeatherInfo();
}

void loop() {
  timeClient.update();

  if (millis() - lastWeatherUpdate > weatherUpdateInterval) {
    fetchWeatherInfo();
  }

  if (digitalRead(upButtonPin) == LOW) {
    while (digitalRead(upButtonPin) == LOW);
    lcd.clear();
    lcd.print("Up Train Loading");
    fetchTrainInfo("상행");
    currentMode = TRAIN;
    lastActionTime = millis();
  } else if (digitalRead(downButtonPin) == LOW) {
    while (digitalRead(downButtonPin) == LOW);
    lcd.clear();
    lcd.print("Down Train Loading");
    fetchTrainInfo("하행");
    currentMode = TRAIN;
    lastActionTime = millis();
  }

  if (currentMode == TRAIN && millis() - lastActionTime > trainDisplayDuration) {
    currentMode = NORMAL;
    lcd.clear();
  }

  if (currentMode == NORMAL) {
    displayTimeAndWeather();
  }

  delay(500);
}

void fetchWeatherInfo() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClient client;

    String url = "http://api.openweathermap.org/data/2.5/weather?id=" + CITY_ID +
                 "&appid=" + WEATHER_API_KEY + "&units=metric";

    http.begin(client, url);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, payload);
      if (!error) {
        currentWeather = doc["weather"][0]["main"].as<String>();
        temperature = doc["main"]["temp"];
        lastWeatherUpdate = millis();
      }
    }

    http.end();
  }
}

void displayTimeAndWeather() {
  if (timeClient.isTimeSet()) {
    if (!timeSynced) {
      timeSynced = true;
      lcd.clear();
      lcd.print("Time Synced");
      delay(1000);
      lcd.clear();
    }

    lcd.setCursor(0, 0);
    lcd.print("Time: ");
    lcd.print(timeClient.getFormattedTime());

    if (currentWeather.length() > 0) {
      lcd.setCursor(0, 1);
      lcd.print(getWeatherEnglish(currentWeather));
      lcd.print(" ");
      lcd.print((int)temperature);
      lcd.print("C");
    }
  } else {
    lcd.clear();
    lcd.print("Syncing time...");
  }
}

void fetchTrainInfo(String direction) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClient client;

    String url = "http://swopenAPI.seoul.go.kr/api/subway/" + SEOUL_API_KEY +
                 "/json/realtimeStationArrival/0/20/" + STATION_NAME;

    http.begin(client, url);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(8192);
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        lcd.clear();
        lcd.print("JSON Error");
        return;
      }

      JsonArray arr = doc["realtimeArrivalList"];
      bool found = false;
      int minStations = 999;
      String bestMsg = "";

      for (JsonObject obj : arr) {
        String updnLine = obj["updnLine"].as<String>();
        String msg = obj["arvlMsg2"].as<String>();
        String subwayId = obj["subwayId"].as<String>();

        bool isCorrectDirection = (direction == "상행") ? (updnLine.indexOf("상행") != -1) : (updnLine.indexOf("하행") != -1);

        if (isCorrectDirection && subwayId == "1004" && msg.indexOf("번째 전역") != -1) {
          int l = msg.indexOf("[");
          int r = msg.indexOf("]");
          if (l != -1 && r != -1 && r > l) {
            int stations = msg.substring(l + 1, r).toInt();
            if (stations < minStations && stations > 0) {
              minStations = stations;
              bestMsg = msg;
              found = true;
            }
          }
        }
      }

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(direction == "상행" ? "UP: " : "DOWN: ");
      bestMsg = translateTrainMessage(bestMsg);
      lcd.setCursor(0, 1);
      lcd.print(bestMsg);

    } else {
      lcd.clear();
      lcd.print("No Train Info");
    }

    http.end();
  } else {
    lcd.clear();
    lcd.print("WiFi Error");
  }
}

String getWeatherEnglish(String weather) {
  weather.toLowerCase();
  if (weather == "clear") return "Clear";
  else if (weather == "clouds") return "Cloud";
  else if (weather == "rain") return "Rain";
  else if (weather == "drizzle") return "Drizzle";
  else if (weather == "thunderstorm") return "Storm";
  else if (weather == "snow") return "Snow";
  else if (weather == "mist" || weather == "fog") return "Fog";
  else if (weather == "haze") return "Haze";
  else return weather.substring(0, 6);
}

String translateTrainMessage(String msg) {
  msg.replace("번째 전역 진입", " stops away");
  msg.replace("번째 전역", " stops away");
  msg.replace("도착", "Arriving");
  msg.replace("진입", "Entering");
  msg.replace("출발", "Departed");
  msg.replace("전역 출발", "Left");
  msg.replace("전역 진입", "Entering prev");
  msg.replace("전역", "station");

  msg.replace("Arrival", "");
  msg.replace("Arr", "");
  msg.replace("arrival", "");
  msg.replace("arr", "");

  int bracketPos = msg.indexOf('(');
  if (bracketPos != -1) {
    msg = msg.substring(0, bracketPos);
    msg.trim();
  }
  return msg;
}
