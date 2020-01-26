/**********************************************************************************************************
    Name    : Nightlightomatic
    Author  : Lars Almén
    Created : 2020-01-20
    Last Modified: 2020-01-26
    Version : 1.0.0
    Notes   : An ESP controlled nightlight, that toggles output pins depending on a set schedule.
              Schedule is exposed through an html gui, served by a http server running on the ESP. Settings are persisted on (emulated) EEPROM.
    License : This software is available under MIT License.
              Copyright 2020 Lars Almén
              Permission is hereby granted, free of charge, to any person obtaining a copy
              of this software and associated documentation files (the "Software"),
              to deal in the Software without restriction, including without limitation the rights to use,
              copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
              and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
              The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
              THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
              INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
              IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
              WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 ***********************************************************************************************************/

// Includes
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <TimeAlarms.h>
#include <EEPROM.h>

// Debug stuff
//#define DEBUG_LAMPOMATIC

// Schedule
struct Schedule
{
  String start;
  String end;
};

struct ScheduleContainer
{
  bool persistedInEEPROM;
  bool initialized;
  bool dstActive;
  Schedule weekDay;
  Schedule weekNight;
  Schedule weekendDay;
  Schedule weekendNight;
  int timerIds[28];
  int dayIntensity;
  int nightIntensity;
  int usedTimers;
};

// Wifi settings
const char *ssid = "";
const char *password = "";

// Time settings
const long utcOffsetInSeconds = 3600;
long dstOffsetInSeconds = 0;
// Weekdays, change according to language (Söndag = Sunday, Måndag = Monday etc etc.).
const char daysOfTheWeek[7][12] = {"Söndag", "Måndag", "Tisdag", "Onsdag", "Torsdag", "Fredag", "Lördag"};

unsigned long previousMillis = 0;
const long interval = 60000;
bool firstRun = true;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

// HTTP Server settings
const char *superSecretPassword = "";
ESP8266WebServer server(80);

// Function prototypes for HTTP handlers
void handleRoot();
void handleNotFound();
void handleGetTime();
void handlePostSchedule();
#ifdef DEBUG_LAMPOMATIC
void handleDebugPost();
void getDebug();
#endif

// Other declarations
void setSchedule(String nightStart, String nightEnd, String dayStart, String dayEnd, bool dst, String weekendDayStart = "", String weekendDayEnd = "", String weekendNightStart = "", String weekendNightEnd = "");
void printScheduleAndTime();
void startNight();
void endNight();
void startDay();
void endDay();
void clearOldTimers(int timerIds[], int usedTimers);
void readSavedSettings(int eepromAdress);
bool saveSettings(int eepromAdress, ScheduleContainer state);
bool serverHasRequiredArgs();
bool serverHasOptionalArgs();
void setAlarms(bool weekendActive);

const int nightPin = D1;
const int dayPin = D2;

ScheduleContainer activeSchedules;

const int eepromAdress = 0;
bool currentStatePersisted;

void setup()
{
  EEPROM.begin(512);
  Alarm.delay(10);
#ifdef DEBUG_LAMPOMATIC
  Serial.begin(115200);
#endif
  pinMode(nightPin, OUTPUT);
  pinMode(dayPin, OUTPUT);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
#ifdef DEBUG_LAMPOMATIC
    Serial.print(".");
#endif
  }
  timeClient.begin();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/time", HTTP_GET, handleGetTime);
  server.on("/time", HTTP_POST, handlePostSchedule);
#ifdef DEBUG_LAMPOMATIC
  server.on("/debug", HTTP_GET, getDebug);
  server.on("/debug", HTTP_POST, handleDebugPost);
#endif
  server.onNotFound(handleNotFound);

  server.begin(); // Actually start the server
#ifdef DEBUG_LAMPOMATIC
  Serial.println("HTTP server started");
#endif
}

void loop()
{
  // Loop, update time etc.
  unsigned long currentMillis = millis();

  if (firstRun == true || (currentMillis - previousMillis >= interval))
  {
    // Save last run
    previousMillis = currentMillis;
    // Update from NTP and setup stuff needed for timers and whatnot.
    timeClient.setTimeOffset(utcOffsetInSeconds + dstOffsetInSeconds);
    timeClient.update();
    setTime(timeClient.getEpochTime());
    if (firstRun == true)
    {
#ifdef DEBUG_LAMPOMATIC
      Serial.print("Entering first run loop, firstRun value: ");
      Serial.println(firstRun);
      Serial.print("activeSchedules.persistedInEEPROM: ");
      Serial.println(activeSchedules.persistedInEEPROM);
#endif
      firstRun = false;
      readSavedSettings(eepromAdress);
      if (activeSchedules.persistedInEEPROM == true)
      {
        setSchedule(activeSchedules.weekNight.start, activeSchedules.weekNight.end, activeSchedules.weekDay.start, activeSchedules.weekDay.end, activeSchedules.dstActive, activeSchedules.weekendDay.start, activeSchedules.weekendDay.end, activeSchedules.weekendNight.start, activeSchedules.weekendNight.end);
      }
#ifdef DEBUG_LAMPOMATIC
      Serial.print("Exiting first run loop, firstRun value: ");
      Serial.println(firstRun);
      Serial.print("activeSchedules.persistedInEEPROM: ");
      Serial.println(activeSchedules.persistedInEEPROM);
#endif
    }
    Alarm.delay(0);
#ifdef DEBUG_LAMPOMATIC
    printScheduleAndTime();
#endif
  }
  server.handleClient();
}

// HTTP Handlers
void handleRoot()
{
  server.send(200, "text/html", "<form action=\"/time\" method=\"POST\">Day start: <input type=\"time\" name=\"dayStart\" value=\"" + activeSchedules.weekDay.start + "\"> - end: <input type=\"time\" name=\"dayEnd\"value=\"" + activeSchedules.weekDay.end + "\"><label for=\"dayIntensity\">Intensity (1-100):</label><input type=\"number\" id=\"dayIntensity\" name=\"dayIntensity\" min=\"1\" max=\"100\" value=\"" + activeSchedules.dayIntensity + "\"></br>Night start: <input type=\"time\" name=\"nightStart\" value=\"" + activeSchedules.weekNight.start + "\"> - end: <input type=\"time\" name=\"nightEnd\" value=\"" + activeSchedules.weekNight.end + "\"><label for=\"nightIntensity\">Intensity (1-100):</label><input type=\"number\" id=\"nightIntensity\" name=\"nightIntensity\" min=\"1\" max=\"100\" value=\"" + activeSchedules.nightIntensity + "\"></br><hr><p>Weekend schedule is optional. If omitted, regular schedule will be used.</p>Weekend day start: <input type=\"time\" name=\"weekendDayStart\" value=\"" + activeSchedules.weekendDay.start + "\"> - end: <input type=\"time\" name=\"weekendDayEnd\"value=\"" + activeSchedules.weekendDay.end + "\"></br>Weekend night start: <input type=\"time\" name=\"weekendNightStart\" value=\"" + activeSchedules.weekendNight.start + "\"> - end: <input type=\"time\" name=\"weekendNightEnd\"value=\"" + activeSchedules.weekendNight.end + "\"><hr></br><input type=\"checkbox\" name=\"dst\" id=\"dst\"><label for=\"dst\">Daylight savings time</label></br><input type=\"password\" name=\"gatekeeper\" placeholder=\"Key\"> - <input type=\"submit\" formmethod=\"post\" value=\"Submit\"></form>"); // Send HTTP status 200 (Ok) and send some text to the browser/client
}

void handleGetTime()
{
  String day = daysOfTheWeek[timeClient.getDay()];
  String currentTime = day + ", " + timeClient.getFormattedTime();

#ifdef DEBUG_LAMPOMATIC
  Serial.print("In getTime: ");
  Serial.println(currentTime);
#endif

  server.send(200, "text/html; charset=utf-8", "<P>Current Time: " + currentTime + "</p><p>Day schedule: " + activeSchedules.weekDay.start + "-" + activeSchedules.weekDay.end + ", Intensity: " + activeSchedules.dayIntensity + "</p><p>Night schedule: " + activeSchedules.weekNight.start + "-" + activeSchedules.weekNight.end + ", Intensity: " + activeSchedules.nightIntensity + "</p><hr><p>Weekend day: " + activeSchedules.weekendDay.start + " - " + activeSchedules.weekendDay.end + "</p><p>Weekend night: " + activeSchedules.weekendNight.start + " - " + activeSchedules.weekendNight.end + "</p>");
}

void handlePostSchedule()
{
  if (!server.hasArg("gatekeeper") || server.arg("gatekeeper") == NULL)
  {                                                         // If the POST request doesn't contain password data
    server.send(400, "text/plain", "400: Invalid Request"); // The request is invalid, so send HTTP status 400
    return;
  }
  else if (server.arg("gatekeeper") == superSecretPassword)
  {
    if (serverHasRequiredArgs())
    {
      activeSchedules.nightIntensity = server.arg("nightIntensity").toInt();
      activeSchedules.dayIntensity = server.arg("dayIntensity").toInt();

      bool dst = server.hasArg("dst") && (server.arg("dst") == "on");

      if (serverHasOptionalArgs())
      {
        setSchedule(server.arg("nightStart"), server.arg("nightEnd"), server.arg("dayStart"), server.arg("dayEnd"), dst, server.arg("weekendDayStart"), server.arg("weekendDayEnd"), server.arg("weekendNightStart"), server.arg("weekendNightEnd"));
      }
      else
      {
        setSchedule(server.arg("nightStart"), server.arg("nightEnd"), server.arg("dayStart"), server.arg("dayEnd"), dst);
      }

      currentStatePersisted = saveSettings(eepromAdress, activeSchedules);
      handleGetTime();
      return;
    }
    else
    {
      server.send(400, "text/plain; charset=utf-8", "400: Invalid Request"); // The request is invalid, so send HTTP status 400
      return;
    }
  }
  else
  { // Password don't match
    server.send(401, "text/plain", "401: Unauthorized");
    return;
  }
}

bool serverHasRequiredArgs()
{
  return server.hasArg("nightStart") && server.hasArg("nightEnd") && server.hasArg("dayStart") && server.hasArg("dayEnd") && server.hasArg("nightIntensity") && server.hasArg("dayIntensity") && server.arg("nightStart") != NULL && server.arg("nightEnd") != NULL && server.arg("dayStart") != NULL && server.arg("dayEnd") != NULL && server.arg("nightIntensity") != NULL && server.arg("dayIntensity") != NULL;
}

bool serverHasOptionalArgs()
{
  return server.hasArg("weekendDayStart") && server.hasArg("weekendDayEnd") && server.hasArg("weekendNightStart") && server.hasArg("weekendNightEnd") && server.arg("weekendDayStart") != NULL && server.arg("weekendDayEnd") != NULL && server.arg("weekendNightStart") != NULL && server.arg("weekendNightEnd") != NULL;
}

void handleNotFound()
{
  server.send(404, "text/plain; charset=utf-8", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

#ifdef DEBUG_LAMPOMATIC
void getDebug()
{
  server.send(200, "text/html", "<form action=\"/debug\" method=\"POST\"><label for=\"dayPin\">DAY PIN PWN OUT (0-1023):</label><input type=\"number\" id=\"dayPin\" name=\"dayPin\" min=\"0\" max=\"1023\"><label for=\"nightPin\">NIGHT PIN PWN OUT (0-1023):</label><input type=\"number\" id=\"nightPin\" name=\"nightPin\" min=\"0\" max=\"1023\"><input type=\"submit\" formmethod=\"post\" value=\"Submit\"></form>");
}

void handleDebugPost()
{
  int dayPinPWM = 0;
  int nightPinPWM = 0;
  if (server.hasArg("dayPin") && server.arg("dayPin") != NULL)
  {
    dayPinPWM = server.arg("dayPin").toInt();
    if (dayPinPWM > 0 && dayPinPWM < 1024)
    {
      analogWrite(dayPin, dayPinPWM);
    }
    else
    {
      digitalWrite(dayPin, LOW);
    }
  }

  if (server.hasArg("nightPin") && server.arg("nightPin") != NULL)
  {
    nightPinPWM = server.arg("nightPin").toInt();
    if (nightPinPWM > 0 && nightPinPWM < 1024)
    {
      analogWrite(nightPin, nightPinPWM);
    }
    else
    {
      digitalWrite(nightPin, LOW);
    }
  }

  server.send(200, "text/html", "<form action=\"/debug\" method=\"POST\"><label for=\"dayPin\">DAY PIN PWN OUT (0-1023):</label><input type=\"number\" id=\"dayPin\" name=\"dayPin\" min=\"0\" max=\"1023\" value=\"" + String(dayPinPWM) + "\"><label for=\"nightPin\">NIGHT PIN PWN OUT (0-1023):</label><input type=\"number\" id=\"nightPin\" name=\"nightPin\" min=\"0\" max=\"1023\" value=\"" + String(nightPinPWM) + "\"><input type=\"submit\" formmethod=\"post\" value=\"Submit\"></form>");
}
#endif
// HTTP

// Set-Get-Read schedule and timers.
void setSchedule(String nightStart, String nightEnd, String dayStart, String dayEnd, bool dst, String weekendDayStart, String weekendDayEnd, String weekendNightStart, String weekendNightEnd)
{
  if (activeSchedules.initialized == true)
  {
    clearOldTimers(activeSchedules.timerIds, activeSchedules.usedTimers);
  }

  activeSchedules.dstActive = dst;
  activeSchedules.persistedInEEPROM = false;
  activeSchedules.weekDay.start = dayStart;
  activeSchedules.weekDay.end = dayEnd;
  activeSchedules.weekNight.start = nightStart;
  activeSchedules.weekNight.end = nightEnd;

  dstOffsetInSeconds = dst == true ? 3600 : 0;
  timeClient.setTimeOffset(utcOffsetInSeconds + dstOffsetInSeconds);
  timeClient.update();
  if (!(weekendDayStart.isEmpty() || weekendDayEnd.isEmpty() || weekendNightStart.isEmpty() || weekendNightEnd.isEmpty()))
  {
    activeSchedules.weekendDay.start = weekendDayStart;
    activeSchedules.weekendDay.end = weekendDayEnd;
    activeSchedules.weekendNight.start = weekendNightStart;
    activeSchedules.weekendNight.end = weekendNightEnd;
    setAlarms(true);
  }
  else
  {
    activeSchedules.weekendDay.start = "";
    activeSchedules.weekendDay.end = "";
    activeSchedules.weekendNight.start = "";
    activeSchedules.weekendNight.end = "";
    setAlarms(false);
  }
  activeSchedules.initialized = true;
}

void setAlarms(bool weekendActive)
{
  int usedTimers = 0;
  for (int i = 1; i <= 7; i++)
  {
    if (weekendActive == true && (i == 1 || i == 7) /* Sunday or Saturday */)
    {
      activeSchedules.timerIds[usedTimers++] = Alarm.alarmRepeat(static_cast<timeDayOfWeek_t>(i), activeSchedules.weekendDay.start.substring(0, 2).toInt(), activeSchedules.weekendDay.start.substring(3).toInt(), 0, startDay);
      activeSchedules.timerIds[usedTimers++] = Alarm.alarmRepeat(static_cast<timeDayOfWeek_t>(i), activeSchedules.weekendDay.end.substring(0, 2).toInt(), activeSchedules.weekendDay.end.substring(3).toInt(), 0, endDay);
      activeSchedules.timerIds[usedTimers++] = Alarm.alarmRepeat(static_cast<timeDayOfWeek_t>(i), activeSchedules.weekendNight.start.substring(0, 2).toInt(), activeSchedules.weekendNight.start.substring(3).toInt(), 0, startNight);
      activeSchedules.timerIds[usedTimers++] = Alarm.alarmRepeat(static_cast<timeDayOfWeek_t>(i), activeSchedules.weekendNight.end.substring(0, 2).toInt(), activeSchedules.weekendNight.end.substring(3).toInt(), 0, endNight);
    }
    else
    {
      activeSchedules.timerIds[usedTimers++] = Alarm.alarmRepeat(static_cast<timeDayOfWeek_t>(i), activeSchedules.weekDay.start.substring(0, 2).toInt(), activeSchedules.weekDay.start.substring(3).toInt(), 0, startDay);
      activeSchedules.timerIds[usedTimers++] = Alarm.alarmRepeat(static_cast<timeDayOfWeek_t>(i), activeSchedules.weekDay.end.substring(0, 2).toInt(), activeSchedules.weekDay.end.substring(3).toInt(), 0, endDay);
      activeSchedules.timerIds[usedTimers++] = Alarm.alarmRepeat(static_cast<timeDayOfWeek_t>(i), activeSchedules.weekNight.start.substring(0, 2).toInt(), activeSchedules.weekNight.start.substring(3).toInt(), 0, startNight);
      activeSchedules.timerIds[usedTimers++] = Alarm.alarmRepeat(static_cast<timeDayOfWeek_t>(i), activeSchedules.weekNight.end.substring(0, 2).toInt(), activeSchedules.weekNight.end.substring(3).toInt(), 0, endNight);
    }
  }
  activeSchedules.usedTimers = usedTimers;
}

void clearOldTimers(int timerIds[], int usedTimers)
{
#ifdef DEBUG_LAMPOMATIC
  Serial.print("Clearing old timers");
  for (int i = 0; i < usedTimers; i++)
  {
    Serial.print("ID: ");
    int timerID = timerIds[i];
    Serial.println(timerID);
  }
#endif
  for (int i = 0; i < usedTimers; i++)
  {
    Alarm.free(timerIds[i]);
  }
}

// EEPROM stuff
void readSavedSettings(int eepromAdress)
{
#ifdef DEBUG_LAMPOMATIC
  Serial.print("Reading from eeprom, adress: ");
  Serial.println(eepromAdress);
#endif
  ScheduleContainer savedSchedule;
  savedSchedule = EEPROM.get(eepromAdress, savedSchedule);
#ifdef DEBUG_LAMPOMATIC
  if (savedSchedule.persistedInEEPROM == true)
  {
    Serial.print("Read successful.");
    Serial.println("Read data, day: " + savedSchedule.weekDay.start + "-" + savedSchedule.weekDay.end);
    Serial.println("Read data, night: " + savedSchedule.weekNight.start + "-" + savedSchedule.weekNight.start);
    Serial.println("Read data, weekendDay: " + savedSchedule.weekendDay.start + "-" + savedSchedule.weekendDay.start);
    Serial.println("Read data, weekendNight: " + savedSchedule.weekendNight.start + "-" + savedSchedule.weekendNight.end);
    Serial.print("DST: ");
    Serial.println(savedSchedule.dstActive);
    Serial.print("Day brightness: ");
    Serial.println(savedSchedule.dayIntensity);
    Serial.print("Night brightness: ");
    Serial.println(savedSchedule.nightIntensity);
  }
  else
  {
    Serial.println("Failed to read data from EEPROM.");
  }
#endif
  if (savedSchedule.persistedInEEPROM == true)
  {
    dstOffsetInSeconds = savedSchedule.dstActive == true ? 3600 : 0;
    activeSchedules = savedSchedule;
  }
}

bool saveSettings(int eepromAdress, ScheduleContainer currentState)
{
  ScheduleContainer toSave = currentState;
#ifdef DEBUG_LAMPOMATIC
  Serial.print("Starting save to eeprom adress: ");
  Serial.println(eepromAdress);
#endif
  toSave.persistedInEEPROM = true;
  EEPROM.put(eepromAdress, toSave);
  bool saveOk = EEPROM.commit();
  activeSchedules.persistedInEEPROM = saveOk;
#ifdef DEBUG_LAMPOMATIC
  Serial.print("Save status: ");
  Serial.println(saveOk == true ? "OK" : "FAILED");
#endif
  return saveOk;
}

// Blinkenlights
void startNight()
{
  int pwmOut = map(activeSchedules.nightIntensity, 0, 100, 0, 1023);
  analogWrite(nightPin, pwmOut);
}

void endNight()
{
  digitalWrite(nightPin, LOW);
}

void startDay()
{
  int pwmOut = map(activeSchedules.dayIntensity, 0, 100, 0, 1023);
  analogWrite(dayPin, pwmOut);
}

void endDay()
{
  digitalWrite(dayPin, LOW);
}

// Print some debug stuff to serial
#ifdef DEBUG_LAMPOMATIC
void printScheduleAndTime()
{
  Serial.print("Day schedule: ");
  Serial.println(activeSchedules.weekDay.start);
  Serial.println(" - ");
  Serial.println(activeSchedules.weekDay.end);
  Serial.print("Night schedule: ");
  Serial.println(activeSchedules.weekNight.start);
  Serial.println(" - ");
  Serial.println(activeSchedules.weekNight.end);

  Serial.print("Weekend day schedule: ");
  Serial.println(activeSchedules.weekendDay.start);
  Serial.println(" - ");
  Serial.println(activeSchedules.weekendDay.end);

  Serial.print("Weekend night schedule: ");
  Serial.println(activeSchedules.weekendNight.start);
  Serial.println(" - ");
  Serial.println(activeSchedules.weekendNight.end);

  Serial.print("TimeClient.GetDay");
  Serial.println(timeClient.getDay());
  Serial.print(daysOfTheWeek[timeClient.getDay()]);
  Serial.print(", ");
  Serial.println(timeClient.getFormattedTime());

  Serial.print("Time epoch time: ");
  Serial.println(now());
  Serial.print("NTPClient epoch time: ");
  Serial.println(timeClient.getEpochTime());

  Serial.print("DST Offset: ");
  Serial.println(dstOffsetInSeconds);

  Serial.print("Night-pin status: ");
  Serial.println(digitalRead(nightPin));
  Serial.print("Night-pin PWM setting: ");
  Serial.println(activeSchedules.nightIntensity);

  Serial.print("Day-pin status: ");
  Serial.println(digitalRead(dayPin));
  Serial.print("Day-pin PWM setting: ");
  Serial.println(activeSchedules.dayIntensity);

  Serial.print("Current state saved to EEPROM: ");
  Serial.println(currentStatePersisted == true ? "YES" : "NO");

  Serial.print("Used timers: ");
  Serial.println(activeSchedules.usedTimers);

  Serial.print("Activeschedules init: ");
  Serial.println(activeSchedules.initialized);
}
#endif