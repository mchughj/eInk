/* e-paper display lib */
#include <GxEPD.h>
#include <GxGDEH029A1/GxGDEH029A1.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>

/* include any other fonts you want to use https://github.com/adafruit/Adafruit-GFX-Library */
#include <Fonts/Picopixel.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/Org_01.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>

/* WiFi libs*/
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiManager.h>

/* Util libs */
#include <Time.h>
#include <TimeLib.h>
#include <ArduinoJson.h>

// Often found in Arduino\hardware\tools\avr\lib\gcc\avr\4.9.2\install-tools\limits.h
#include <Limits.h>

// Local icons used for this project.
#include "icons.h"

const char* WEATHER_HOST = "api.openweathermap.org";
const char* API_KEY = "854f7522429fb2760c19380767bd786d"; //Your API key https://home.openweathermap.org/
const char* CITY_ID = "5809844"; //City ID https://openweathermap.org/find
const int time_zone = -7; // e.g. UTC-05:00 = -5
const boolean IS_METRIC_UNITS = false;
const boolean SHOW_SUNDATA_INSTEAD_OF_HUMIDITY = true;
const boolean SHOW_WIND_INSTEAD_OF_DAY_RANGE = false;

/* Configure pins for display */
GxIO_Class io(SPI, SS, 0, 2);
GxEPD_Class display(io); // default selection of D4, D2

// Summary weather information for a period of time captured by the parts within the structure.
struct WeatherSummary {
  int minTemperature;
  int maxTemperature;
  int minConditionCode;
  int maxConditionCode;
  unsigned long int minTime;
  unsigned long int maxTime;
  int countDataPoints;
};

// We track the current day temperature range because information for a day
// changes as the day goes on.  If we didn't do this then we would lose the
// early hours in the day (and some of the temperature ranges) later in the
// day.
//
// This information is stored within RTC memory as nothing else is available
// across deep sleeps.
WeatherSummary currentDay;

String prettyPrintWeatherSummary( const WeatherSummary &w ) { 
  String result = "WeatherSummary - minTemp: ";

  return result + w.minTemperature + ", maxTemp: " + w.maxTemperature +
    ", minConditionCode: " + w.minConditionCode +
    ", maxConditionCode: " + w.maxConditionCode +
    ", minTime: " + w.minTime +
    ", maxTime: " + w.maxTime +
    ", countDataPoints: " + w.countDataPoints;
}

void readCurrentInfoFromRTC() { 
  if (ESP.rtcUserMemoryRead(0, (uint32_t*) &currentDay, sizeof(currentDay))) {
    Serial.print("Read current day information; ");
    Serial.println( prettyPrintWeatherSummary(currentDay) );
  } else {
    Serial.println("Failure in reading current day information;");
  }
}

void writeCurrentInfoToRTC() { 
  Serial.print("About to write current day information; ");
  Serial.println( prettyPrintWeatherSummary(currentDay) );
 
  if (ESP.rtcUserMemoryWrite(0, (uint32_t*) &currentDay, sizeof(currentDay))) {
    Serial.println("Write completed successfully");
  } else {
    Serial.println("Failure in writing current day information");
  }
}

int getDayForWeatherSummary( const WeatherSummary & w ) {
  return day(w.minTime);
}

void maybeReplaceCurrentWeatherInfo( const WeatherSummary & latestForecast ) { 
  uint8_t d1 = getDayForWeatherSummary(currentDay);
  uint8_t d2 = getDayForWeatherSummary(latestForecast);

  if (d1 != d2) { 
    Serial.print("Compared two dates and found a difference; d1 (currentDay): ");
    Serial.print(d1);
    Serial.print(", d2 (latestForecast): ");
    Serial.println(d2);
    // It is a new day so the latestForecast will replace the currentDay
    memcpy( (void*) &currentDay, (void*) &latestForecast, sizeof(currentDay));

    Serial.print("New day in latestForecast replaces old day; newData: ");
    Serial.println(prettyPrintWeatherSummary(currentDay));
  } else {
    Serial.println("Current day information is the same day as the latest forecast.  Using current day and not new forecast data.");
  }
}

void adjustCurrentWeatherInfo( const int currentTemperature ) {
  // The forecast contains expected values for the day range.  These ranges
  // could be wrong with respect to the current observed temperatures for a few
  // different reasons - the most likely because forecasts could be inaccurate. 
  if (currentTemperature > currentDay.maxTemperature) { 
    Serial.print("Current temperature is greater than currentDay; currentTemperature: ");
    Serial.print(currentTemperature);
    Serial.print(", currentDay.maxTemperature: ");
    Serial.println(currentDay.maxTemperature);
    currentDay.maxTemperature = currentTemperature;
  }
  if (currentTemperature < currentDay.minTemperature) { 
    Serial.print("Current temperature is less than currentDay; currentTemperature: ");
    Serial.print(currentTemperature);
    Serial.print(", currentDay.minTemperature: ");
    Serial.println(currentDay.minTemperature);
    currentDay.minTemperature = currentTemperature;
  }
}


void setup()
{
  display.init();

  // Set the display in landscape mode
  display.setRotation(3); 

  /* WiFi Manager automatically connects using the saved credentials, if that fails it will go into AP mode */
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.autoConnect("Badgy AP");

  Serial.begin(115200);

  // Read the current weather information from the registers that survive the deep sleep so that we can
  // preserve the expected range of temperatures for the current day. 
  readCurrentInfoFromRTC();

  if (getWeatherData() && getForecastData()) {
    writeCurrentInfoToRTC();
    Serial.println("Success in getting current and forecast.  Sleeping.");
    ESP.deepSleep(3600e6, WAKE_RF_DEFAULT);
  } else {
    Serial.println("Failure in getting either the current or forecast.  Sleeping for a tiny bit.");
    writeCurrentInfoToRTC();
    ESP.deepSleep(50e6, WAKE_RF_DEFAULT);
  }
}

void loop()
{
  // loop is never executed in this program as the setup does all the work
  // then puts the ESP into a deep sleep which will cause a reset at the
  // conclusion which runs setup again.
}

void configModeCallback(WiFiManager *myWiFiManager) {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(0, 50);
  display.println("Connect to Badgy AP");
  display.println("to setup your WiFi!");
  display.update();
}

void showErrorText(char *text)
{
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(10, 70);
  display.println(text);
  display.println("Auto-retry in a short period.");
  display.update();
}

void prettyPrintTwoCharacterInt(char c, int value) {
  if (value<=9) {
    display.print(c);
    display.print(value);
  } else {
    display.print(value);
  }
}

bool getWeatherData()
{
  // Example URL:  http://api.openweathermap.org/data/2.5/weather/?id=5809844&units=imperial&appid=854f7522429fb2760c19380767bd786d
  String type = "weather";
  String url = "/data/2.5/" + type + "?id=" + CITY_ID + "&units=" + getUnitsString() + "&appid=" + API_KEY;

  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(WEATHER_HOST, httpPort)) {
    showErrorText("connection failed");
    return false;
  }
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + WEATHER_HOST + "\r\n" +
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      showErrorText("GW:  Client Timeout !");
      client.stop();
      return false;
    }
  }

  // Read response
  String city;
  float current_temp;
  int humidity;
  float temp_min;
  float temp_max;
  float wind;
  String icon_code;
  int condition;
  int server_time;

  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    showErrorText("GW:  HTTP Status Error!");
    return false;
  }

  /* Find the end of headers */
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    showErrorText("GW:  Invalid Response...");
    return false;
  }

  /* Start parsing the JSON in the response body */
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(client);
  if (!root.success()) {
    showErrorText("GW:  JSON parsing failed!");
    return false;
  }
  city = root["name"].as<String>();
  current_temp = root["main"]["temp"];
  humidity = root["main"]["humidity"];
  temp_min = root["main"]["temp_min"];
  temp_max = root["main"]["temp_max"];
  wind = root["wind"]["speed"];
  icon_code = root["weather"][0]["icon"].as<String>();
  condition = root["weather"][0]["id"];

  // Get the server's view of the time.  This is in GMT.
  server_time = root["dt"]; 
  server_time = server_time + (time_zone * 60 * 60); 
  setTime(server_time);

  /* Get icon for weather condition */
  const unsigned char *icon;
  icon = getIcon(condition, icon_code, false, false);
  /* Display weather conditions */
  display.fillScreen(GxEPD_WHITE);
  display.drawBitmap(icon, -5, -15, 80, 80, GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);

  // Right justify the two top lines containing the 
  // date and time information.
  if( day() < 10 ) { 
    display.setCursor(193, 11);
  } else { 
    display.setCursor(182, 11);
  }
  display.print(dayShortStr(weekday()));
  display.print(" ");
  display.print(monthShortStr(month()));
  display.print(" ");
  display.print(day());
  
  int h = hour();
  display.setCursor(237, 31);
  prettyPrintTwoCharacterInt(' ', hour());
  display.print(":");
  prettyPrintTwoCharacterInt('0', minute());

  if (SHOW_WIND_INSTEAD_OF_DAY_RANGE) { 
    // Show the wind information.  If we aren't showing wind then we can show 
    // the expected day range.  This happens as part of the forecast.
    display.drawBitmap(strong_wind_small, 0, 62, 48, 48, GxEPD_WHITE);
    display.setCursor(50, 79);
    if (IS_METRIC_UNITS) {
      display.print(String((int)(wind * 3.6)) + "km/h");
    } else {
      display.print(String((int)wind) + " mph");
    }
  }

  if (SHOW_SUNDATA_INSTEAD_OF_HUMIDITY == false) { 
    display.drawBitmap(humidity_small, 0, 97, 32, 32, GxEPD_WHITE);
    display.setCursor(50, 119);
    display.print(String(humidity) + "%");
  } else {

    long unsigned int sunrise = root["sys"]["sunrise"];
    long unsigned int sunset = root["sys"]["sunset"];

    sunrise += time_zone * 60 * 60;
    sunset += time_zone * 60 * 60;

    // Sunrise and sunset data are offset by the time zone and thus must 
    // be modified by the time_zone * 60 offset.
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(4, 99);

    display.print( "Rise:" );
    display.print(hour(sunrise));
    display.print( ":" );
    prettyPrintTwoCharacterInt('0', minute(sunrise));

    display.setCursor(4, 117);

    display.print( "Set:" );
    display.print(hour(sunset));
    display.print( ":" );
    prettyPrintTwoCharacterInt('0', minute(sunset));

    display.update();
  }
  
  // Current Temp
  display.setCursor(72, 35);
  display.setFont(&FreeMonoBold18pt7b);
  if (IS_METRIC_UNITS) {
    display.println(String((int)current_temp) + "C");
  } else {
    display.println(String((int)current_temp) + "F");
  }

  adjustCurrentWeatherInfo(current_temp);

  display.update();
  return true;
}

void showClientData(WiFiClient &client) {
  while (client.connected() || client.available()) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
    }
  }
}

String getUnitsString() {
  if (IS_METRIC_UNITS) {
    return "metric";
  } else {
    return "imperial";
  }
}


bool extractWeatherSummary(JsonObject& root, WeatherSummary *w, int count) {
  int dayIndex = 0;
  int dataIndex = 0;

  for (int i=0; i < count; i++) { 
    w[i].minTemperature = INT_MAX;
    w[i].maxTemperature = INT_MIN;
    w[i].minTemperature = INT_MAX;
    w[i].maxTemperature = INT_MIN;
    w[i].minConditionCode = INT_MAX;
    w[i].maxConditionCode = INT_MIN;
    w[i].minTime = LONG_MAX;
    w[i].maxTime = 0;
    w[i].countDataPoints = 0;
  }
  
  int priorDay = -1;
  int priorMonth = -1;

  while (dayIndex < count) { 
    if (dataIndex > 8 * count) { 
      showErrorText("EWS:  Went too far in the root object!");
      return false;
    }

    int condition= root["list"][dataIndex]["weather"][0]["id"];
    int temp = root["list"][dataIndex]["main"]["temp"];
    unsigned long t = root["list"][dataIndex]["dt"];
    String icon_code = root["list"][dataIndex]["weather"][0]["icon"];
 
    // Adjust for timezone
    t = t + (time_zone * 60 * 60);

    int currentDay = day(t);
    int currentMonth = month(t);

    if (currentDay != priorDay || currentMonth != priorMonth) { 
      Serial.print( "Moving on to the next day;  priorDay: ");
      Serial.print( priorDay );
      Serial.print( ", priorMonth: ");
      Serial.print( priorMonth );
      Serial.print( ", currentDay: ");
      Serial.print( currentDay );
      Serial.print( ", currentMonth: ");
      Serial.println( currentMonth );
      if (priorDay != -1) { 
        // Give a dump of what was discovered.
        Serial.print( "Day complete; dayIndex: ");
        Serial.print( dayIndex );
        Serial.print( ", ");
        Serial.println(prettyPrintWeatherSummary(w[dayIndex]));

        dayIndex += 1;
      }
      priorDay = currentDay;
      priorMonth = currentMonth;
    }
 
    if (dayIndex == count) { 
      break;
    }

    if (0) { 
      Serial.print( "Current weather summary for day; dataIndex: ");
      Serial.print( dataIndex );
      Serial.print( ", dayIndex: ");
      Serial.print( dayIndex );
      Serial.print( ", " );
      Serial.println(prettyPrintWeatherSummary(w[dayIndex]));
    }

    w[dayIndex].minTemperature = min( w[dayIndex].minTemperature, temp );
    w[dayIndex].maxTemperature = max( w[dayIndex].maxTemperature, temp ); 
    w[dayIndex].minConditionCode = min( w[dayIndex].minConditionCode, condition );
    w[dayIndex].maxConditionCode = max( w[dayIndex].maxConditionCode, condition ); 
    w[dayIndex].minTime = min( w[dayIndex].minTime, t );
    w[dayIndex].maxTime = max( w[dayIndex].minTime, t );
    w[dayIndex].countDataPoints += 1;

    if (0) { 
      Serial.print( "New weather summary for day; dayIndex: ");
      Serial.print( dayIndex );
      Serial.print( ", " );
      Serial.println(prettyPrintWeatherSummary(w[dayIndex]));
    }

    dataIndex = dataIndex + 1;
  }

  for (int i=0; i < count; i++) { 
    Serial.print( "Weather summary for day; dayIndex: ");
    Serial.print( i );
    Serial.print( ", " );
    Serial.println(prettyPrintWeatherSummary(w[i]));
  }

  return true;
}

bool getForecastData()
{ 
  // This API is documented here: https://openweathermap.org/forecast5
  String type = "forecast";
  String url = "/data/2.5/" + type + "/?id=" + CITY_ID + "&units=" + getUnitsString() + "&appid=" + API_KEY;

  // Initiate the http request to the server
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(WEATHER_HOST, httpPort)) {
    showErrorText("connection failed");
    return false;
  }
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
      "Host: " + WEATHER_HOST + "\r\n" +
      "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      showErrorText(">>> Client Timeout !");
      client.stop();
      return false;
    }
  }

  // Read response
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    char buff[100];
    snprintf(buff, 100, "GF: HTTP Status error: %s", status);
    showErrorText(buff);
    return false;
  }

  /* Find the end of headers */
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    showErrorText("Invalid Response...");
    return false;
  }

  if (0) { 
    showClientData(client);
    return false;
  }

  /* Start parsing the JSON in the response body */
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(client);
  if (!root.success()) {
    showErrorText("JSON parsing failed!");
    return false;
  }

  // The forecast data contains weather data every 3 hours for the next 5 days.
  // I extract out the data for today and then the next 3 days.
  WeatherSummary w[4];
  if (!extractWeatherSummary(root, w, 4) ) {
    return false;
  }

  int forecastOffset = 100; 

  for (int i = 1; i <= 3; i++) {
    const unsigned char *icon = getIcon(w[i]);
    display.drawBitmap(icon, (forecastOffset + (i * 48)), 58, 48, 48, GxEPD_WHITE); 

    // Get the time and convert it based on our time zone in order to draw
    // the day of the week.
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor((forecastOffset + 7 + (i * 48)), 58);
    display.print(dayShortStr(weekday(w[i].minTime)));

    // Forecasted temperature
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&Org_01);
    display.setCursor((forecastOffset + 10 + (i * 48)), 115);
    display.print(String(w[i].minTemperature));
    display.print("-");
    display.print(String(w[i].maxTemperature));
  }

  // If we want summary temperature for the rest of the day then that goes here
  // as well since it is pulled, in part, from the summary data for the day.
  if (!SHOW_WIND_INSTEAD_OF_DAY_RANGE) { 

    // We use the 'currentDay' information but allow it to be influenced by the
    // forecast data if the currentDay is no longer the most recent day.
    maybeReplaceCurrentWeatherInfo(w[0]);
    
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(4, 75);
    display.print("L:");
    display.print(String(currentDay.minTemperature));
    display.print(" H:");
    display.print(String(currentDay.maxTemperature));
  }

  display.update();

  return true;
}

const unsigned char * getIcon(WeatherSummary &s) {
  return getIcon( s.minConditionCode, "", true, true);
}

const unsigned char * getIcon(int condition, String icon_code, bool small, bool dayIconOnly) {
  if (condition <= 232) {
    return small ? thunderstorm_small : thunderstorm;
  } else if (condition >= 300 && condition <= 321) {
    return small ? showers_small : showers;
  } else if (condition >= 500 && condition <= 531) {
    return small ? rain_small : rain;
  } else if (condition >= 600 && condition <= 602) {
    return small ? snow_small : snow;
  } else if (condition >= 611 && condition <= 612) {
    return small ? sleet_small : sleet;
  } else if (condition >= 615 && condition <= 622) {
    return small ? rain_mix_small : rain_mix;
  } else if (condition == 701 || condition == 721 || condition == 741) {
    return small ? fog_small : fog;
  } else if (condition == 711) {
    return small ? smoke_small : smoke;
  } else if (condition == 731) {
    return small ? sandstorm_small : sandstorm;
  } else if (condition == 751 || condition == 761) {
    return small ? dust_small : dust;
  } else if (condition == 762) {
    return small ? volcano_small : volcano;
  } else if (condition == 771) {
    return small ? strong_wind_small : strong_wind;
  } else if (condition == 781) {
    return small ? tornado : tornado_small;
  } else if (condition == 800) {
    if (icon_code == "01d" || dayIconOnly) {
      return small ? day_sunny_small : day_sunny;
    } else {
      return small ? night_clear_small : night_clear;
    }
  } else if (condition == 801 || condition == 802) {
    if (icon_code == "02d" || icon_code == "03d" || dayIconOnly) {
      return small ? day_cloudy_small : day_cloudy;
    } else {
      return small ? night_cloudy_small : night_cloudy;
    }
  } else if (condition == 803 || condition == 804) {
    if (icon_code == "04d" || dayIconOnly) {
      return small ? cloudy_small : cloudy;
    } else {
      return small ? night_cloudy_small : night_cloudy;
    }
  }
}
