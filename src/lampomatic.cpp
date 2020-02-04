/**********************************************************************************************************
    Name    : Nightlightomatic
    Author  : Lars Almén
    Created : 2020-01-20
    Last Modified: 2020-02-04
    Version : 1.2.0
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
  int timerIds[6];
  int startHour;
  int startMinute;
  int endHour;
  int endMinute;
};

struct OutputState
{
  bool dayActive;
  bool nightActive;
};

struct StateContainer
{
  bool persistedInEEPROM;
  bool initialized;
  bool dstActive;
  Schedule day;
  Schedule night;
  Schedule weekendDay;
  Schedule weekendNight;
  int dayIntensity;
  int nightIntensity;
  OutputState currentState;
};

typedef enum
{
  dayStart,
  dayEnd,
  nightStart,
  nightEnd,
  weekendDayStart,
  weekendDayEnd,
  weekendNightStart,
  weekendNightEnd
} scheduleType_t;

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
const char *superSecretPassword = "zuul";
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
void setSchedule(Schedule day, Schedule night, bool dst, Schedule weekendDay, Schedule weekendNight);
void printScheduleAndTime();
void startNight();
void endNight();
void startDay();
void endDay();
void clearOldTimers(int timerIds[], int size);
void readSavedSettings(int eepromAdress);
bool saveSettings(int eepromAdress, StateContainer state);
bool serverHasRequiredArgs();
bool serverHasOptionalArgs();
void setAlarms(bool weekendActive);
String getFormattedHourMinuteConcatenation(scheduleType_t scheduleType);
void setWeekendTimerState();
void setOutputState();

const int nightPin = D1;
const int dayPin = D2;

StateContainer activeSchedules;

const int eepromAdress = 0;
bool currentStatePersisted;

void setup()
{
  EEPROM.begin(512);
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
  timeClient.setTimeOffset(utcOffsetInSeconds + dstOffsetInSeconds);
  timeClient.begin();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/time", HTTP_GET, handleGetTime);
  server.on("/time", HTTP_POST, handlePostSchedule);
  server.onNotFound(handleNotFound);
#ifdef DEBUG_LAMPOMATIC
  server.on("/debug", HTTP_GET, getDebug);
  server.on("/debug", HTTP_POST, handleDebugPost);
#endif
  server.begin(); // Actually start the server
#ifdef DEBUG_LAMPOMATIC
  Serial.println("HTTP server started");
#endif
}

void loop()
{
  // Continously call Alarm.delay to force the ESP to check if time for the alarm has passed.
  Alarm.delay(50);
  // First run
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
      setSchedule(activeSchedules.day, activeSchedules.night, activeSchedules.dstActive, activeSchedules.weekendDay, activeSchedules.weekendNight);
      setOutputState();
    }
#ifdef DEBUG_LAMPOMATIC
    Serial.print("Exiting first run loop, firstRun value: ");
    Serial.println(firstRun);
    Serial.print("activeSchedules.persistedInEEPROM: ");
    Serial.println(activeSchedules.persistedInEEPROM);
#endif
  }

  // Loop, update time etc.
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval)
  {
    // Save last run
    previousMillis = currentMillis;

    timeClient.update();
    setTime(timeClient.getEpochTime());
#ifdef DEBUG_LAMPOMATIC
    printScheduleAndTime();
#endif
    // Set outpuState if there is and initialized schedule in RAM
    if (activeSchedules.initialized)
    {
      // Enable or disable timers deending on day and if weekend is active.
      if (activeSchedules.weekendDay.startHour != -1)
      {
        setWeekendTimerState();
      }
      setOutputState();
    }
  }
  server.handleClient();
}

// HTTP Handlers
void handleRoot()
{
  String dayStartTime = getFormattedHourMinuteConcatenation(dayStart);
  String dayEndTime = getFormattedHourMinuteConcatenation(dayEnd);
  String nightStartTime = getFormattedHourMinuteConcatenation(nightStart);
  String nightEndTime = getFormattedHourMinuteConcatenation(nightEnd);
  String weekendDayStartTime = getFormattedHourMinuteConcatenation(weekendDayStart);
  String weekendDayEndTime = getFormattedHourMinuteConcatenation(weekendDayEnd);
  String weekendNightStartTime = getFormattedHourMinuteConcatenation(weekendNightStart);
  String weekendNightEndTime = getFormattedHourMinuteConcatenation(weekendNightEnd);
  server.send(200, "text/html", "<form action=\"/time\" method=\"POST\">Day start: <input type=\"time\" name=\"dayStart\" value=\"" + dayStartTime + "\"> - end: <input type=\"time\" name=\"dayEnd\"value=\"" + dayEndTime + "\"><label for=\"dayIntensity\">Intensity (1-100):</label><input type=\"number\" id=\"dayIntensity\" name=\"dayIntensity\" min=\"1\" max=\"100\" value=\"" + activeSchedules.dayIntensity + "\"></br>Night start: <input type=\"time\" name=\"nightStart\" value=\"" + nightStartTime + "\"> - end: <input type=\"time\" name=\"nightEnd\" value=\"" + nightEndTime + "\"><label for=\"nightIntensity\">Intensity (1-100):</label><input type=\"number\" id=\"nightIntensity\" name=\"nightIntensity\" min=\"1\" max=\"100\" value=\"" + activeSchedules.nightIntensity + "\"></br><hr><p>Weekend schedule is optional. If omitted, regular schedule will be used.</p>Weekend day start: <input type=\"time\" name=\"weekendDayStart\" value=\"" + weekendDayStartTime + "\"> - end: <input type=\"time\" name=\"weekendDayEnd\"value=\"" + weekendDayEndTime + "\"></br>Weekend night start: <input type=\"time\" name=\"weekendNightStart\" value=\"" + weekendNightStartTime + "\"> - end: <input type=\"time\" name=\"weekendNightEnd\"value=\"" + weekendNightEndTime + "\"><hr></br><input type=\"checkbox\" name=\"dst\" id=\"dst\"><label for=\"dst\">Daylight savings time</label></br><input type=\"password\" name=\"gatekeeper\" placeholder=\"Key\"> - <input type=\"submit\" formmethod=\"post\" value=\"Submit\"></form>");
}

void handleGetTime()
{
  String day = daysOfTheWeek[timeClient.getDay()];
  String currentTime = day + ", " + timeClient.getFormattedTime();

#ifdef DEBUG_LAMPOMATIC
  Serial.print("In getTime: ");
  Serial.println(currentTime);
#endif

  server.send(200, "text/html; charset=utf-8", "<P>Current Time: " + currentTime + "</p><p>Day schedule: " + getFormattedHourMinuteConcatenation(dayStart) + "-" + getFormattedHourMinuteConcatenation(dayEnd) + ", Intensity: " + activeSchedules.dayIntensity + "</p><p>Night schedule: " + getFormattedHourMinuteConcatenation(nightStart) + "-" + getFormattedHourMinuteConcatenation(nightEnd) + ", Intensity: " + activeSchedules.nightIntensity + "</p><hr><p>Weekend day: " + getFormattedHourMinuteConcatenation(weekendDayStart) + " - " + getFormattedHourMinuteConcatenation(weekendDayEnd) + "</p><p>Weekend night: " + getFormattedHourMinuteConcatenation(weekendNightStart) + " - " + getFormattedHourMinuteConcatenation(weekendNightEnd) + "</p>");
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
      Schedule day;
      day.startHour = server.arg("dayStart").substring(0, 2).toInt();
      day.startMinute = server.arg("dayStart").substring(3).toInt();
      day.endHour = server.arg("dayEnd").substring(0, 2).toInt();
      day.endMinute = server.arg("dayEnd").substring(3).toInt();
      Schedule night;
      night.startHour = server.arg("nightStart").substring(0, 2).toInt();
      night.startMinute = server.arg("nightStart").substring(3).toInt();
      night.endHour = server.arg("nightEnd").substring(0, 2).toInt();
      night.endMinute = server.arg("nightEnd").substring(3).toInt();
      Schedule weekendDay;
      weekendDay.startHour = -1;
      Schedule weekendNight;
      weekendNight.startHour = -1;

      if (serverHasOptionalArgs())
      {
#ifdef DEBUG_LAMPOMATIC
        Serial.println(server.arg("weekendDayStart"));
#endif
        weekendDay.startHour = server.arg("weekendDayStart").substring(0, 2).toInt();
        weekendDay.startMinute = server.arg("weekendDayStart").substring(3).toInt();
        weekendDay.endHour = server.arg("weekendDayEnd").substring(0, 2).toInt();
        weekendDay.endMinute = server.arg("weekendDayEnd").substring(3).toInt();

        weekendNight.startHour = server.arg("weekendNightStart").substring(0, 2).toInt();
        weekendNight.startMinute = server.arg("weekendNightStart").substring(3).toInt();
        weekendNight.endHour = server.arg("weekendNightEnd").substring(0, 2).toInt();
        weekendNight.endMinute = server.arg("weekendNightEnd").substring(3).toInt();
      }

      setSchedule(day, night, dst, weekendDay, weekendNight);

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
void setSchedule(Schedule day, Schedule night, bool dst, Schedule weekendDay, Schedule weekendNight)
{
  if (activeSchedules.initialized == true)
  {
    clearOldTimers(activeSchedules.day.timerIds, 2);
    clearOldTimers(activeSchedules.night.timerIds, 2);
    if (activeSchedules.weekendDay.startHour != -1)
    {
      clearOldTimers(activeSchedules.weekendDay.timerIds, 6);
      clearOldTimers(activeSchedules.weekendNight.timerIds, 5);
    }
  }

  activeSchedules.dstActive = dst;
  activeSchedules.persistedInEEPROM = false;
  activeSchedules.day = day;
  activeSchedules.night = night;
  activeSchedules.weekendDay = weekendDay;
  activeSchedules.weekendNight = weekendNight;

  dstOffsetInSeconds = dst == true ? 3600 : 0;
  timeClient.setTimeOffset(utcOffsetInSeconds + dstOffsetInSeconds);
  timeClient.update();

  setAlarms(weekendDay.startHour != -1);

  activeSchedules.initialized = true;
}

void setAlarms(bool weekendActive)
{
  if (weekendActive)
  {
    // Days
    // Friday
    activeSchedules.weekendDay.timerIds[0] = Alarm.alarmRepeat(dowFriday, activeSchedules.day.startHour, activeSchedules.day.startMinute, 0, startDay);
    activeSchedules.weekendDay.timerIds[1] = Alarm.alarmRepeat(dowFriday, activeSchedules.weekendDay.endHour, activeSchedules.weekendDay.endMinute, 0, endDay);
    // Saturday
    activeSchedules.weekendDay.timerIds[2] = Alarm.alarmRepeat(dowSaturday, activeSchedules.weekendDay.startHour, activeSchedules.weekendDay.startMinute, 0, startDay);
    activeSchedules.weekendDay.timerIds[3] = Alarm.alarmRepeat(dowSaturday, activeSchedules.weekendDay.endHour, activeSchedules.weekendDay.endMinute, 0, endDay);
    // Sunday
    activeSchedules.weekendDay.timerIds[4] = Alarm.alarmRepeat(dowSunday, activeSchedules.weekendDay.startHour, activeSchedules.weekendDay.startMinute, 0, startDay);
    activeSchedules.weekendDay.timerIds[5] = Alarm.alarmRepeat(dowSunday, activeSchedules.day.endHour, activeSchedules.day.endMinute, 0, endDay);

    // Nights
    // Friday (start Fri, end Sat)
    activeSchedules.weekendNight.timerIds[0] = Alarm.alarmRepeat(dowFriday, activeSchedules.weekendNight.startHour, activeSchedules.weekendNight.startMinute, 0, startNight);
    activeSchedules.weekendNight.timerIds[1] = Alarm.alarmRepeat(dowSaturday, activeSchedules.weekendNight.endHour, activeSchedules.weekendNight.endMinute, 0, endNight);
    // Saturday (start Sat, end Sun)
    activeSchedules.weekendNight.timerIds[2] = Alarm.alarmRepeat(dowSaturday, activeSchedules.weekendNight.startHour, activeSchedules.weekendNight.startMinute, 0, startNight);
    activeSchedules.weekendNight.timerIds[3] = Alarm.alarmRepeat(dowSunday, activeSchedules.weekendNight.endHour, activeSchedules.weekendNight.endMinute, 0, endNight);
    // Sunday (start sat, ends with regular schedule that gets activated sun-mon midnight rollover)
    activeSchedules.weekendNight.timerIds[4] = Alarm.alarmRepeat(dowSunday, activeSchedules.night.startHour, activeSchedules.night.startMinute, 0, startNight);
  }
  // Regular programming
  activeSchedules.day.timerIds[0] = Alarm.alarmRepeat(activeSchedules.day.startHour, activeSchedules.day.startMinute, 0, startDay);
  activeSchedules.day.timerIds[1] = Alarm.alarmRepeat(activeSchedules.day.endHour, activeSchedules.day.endMinute, 0, endDay);
  activeSchedules.night.timerIds[0] = Alarm.alarmRepeat(activeSchedules.night.startHour, activeSchedules.night.startMinute, 0, startNight);
  activeSchedules.night.timerIds[1] = Alarm.alarmRepeat(activeSchedules.night.endHour, activeSchedules.night.endMinute, 0, endNight);
}

void clearOldTimers(int timerIds[], int size)
{
#ifdef DEBUG_LAMPOMATIC
  Serial.print("Clearing old timers");

  for (int i = 0; i < size; i++)
  {
    Serial.print("ID: ");
    int timerID = timerIds[i];
    Serial.println(timerID);
  }
#endif
  for (int i = 0; i < size; i++)
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
  StateContainer savedSchedule;
  savedSchedule = EEPROM.get(eepromAdress, savedSchedule);
#ifdef DEBUG_LAMPOMATIC
  if (savedSchedule.persistedInEEPROM == true)
  {
    Serial.print("Read successful.");
    Serial.println("Read data, day: " + getFormattedHourMinuteConcatenation(dayStart) + "-" + getFormattedHourMinuteConcatenation(dayEnd));
    Serial.println("Read data, night: " + getFormattedHourMinuteConcatenation(nightStart) + "-" + getFormattedHourMinuteConcatenation(nightEnd));
    Serial.println("Read data, weekendDay: " + getFormattedHourMinuteConcatenation(weekendDayStart) + "-" + getFormattedHourMinuteConcatenation(weekendDayEnd));
    Serial.println("Read data, weekendNight: " + getFormattedHourMinuteConcatenation(weekendNightStart) + "-:" + getFormattedHourMinuteConcatenation(weekendNightEnd));
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

bool saveSettings(int eepromAdress, StateContainer currentState)
{
  StateContainer toSave = currentState;
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

void setWeekendTimerState()
{
  // Weekend baby.
  if (weekday() == static_cast<int>(dowFriday) || weekday() == static_cast<int>(dowSaturday) || weekday() == static_cast<int>(dowSunday))
  {
    Alarm.disable(activeSchedules.day.timerIds[0]);
    Alarm.disable(activeSchedules.day.timerIds[1]);

    Alarm.disable(activeSchedules.night.timerIds[0]);
    Alarm.disable(activeSchedules.night.timerIds[1]);
  }
  else // Not weekend. :-(
  {
    Alarm.enable(activeSchedules.night.timerIds[0]);
    Alarm.enable(activeSchedules.night.timerIds[1]);

    Alarm.enable(activeSchedules.day.timerIds[0]);
    Alarm.enable(activeSchedules.day.timerIds[1]);
  }
}

// Blinkenlights
void startDay()
{
  activeSchedules.currentState.dayActive = true;
  saveSettings(0, activeSchedules);
}

void endDay()
{
  activeSchedules.currentState.dayActive = false;
  saveSettings(0, activeSchedules);
}

void startNight()
{
  activeSchedules.currentState.nightActive = true;
  saveSettings(0, activeSchedules);
}

void endNight()
{
  activeSchedules.currentState.nightActive = false;
  saveSettings(0, activeSchedules);
}

void setOutputState()
{
  if (activeSchedules.currentState.dayActive)
  {
    int pwmOut = map(activeSchedules.dayIntensity, 0, 100, 0, 1023);
    analogWrite(dayPin, pwmOut);
  }
  else
  {
    digitalWrite(dayPin, LOW);
  }

  if (activeSchedules.currentState.nightActive)
  {
    int pwmOut = map(activeSchedules.nightIntensity, 0, 100, 0, 1023);
    analogWrite(nightPin, pwmOut);
  }
  else
  {
    digitalWrite(nightPin, LOW);
  }
}

String getFormattedHourMinuteConcatenation(scheduleType_t scheduleType)
{
  String hoursStr;
  String minuteStr;
  switch (scheduleType)
  {
  case dayStart:
    hoursStr = activeSchedules.day.startHour < 10 ? "0" + String(activeSchedules.day.startHour) : String(activeSchedules.day.startHour);
    minuteStr = activeSchedules.day.startMinute < 10 ? "0" + String(activeSchedules.day.startMinute) : String(activeSchedules.day.startMinute);
    break;
  case dayEnd:
    hoursStr = activeSchedules.day.endHour < 10 ? "0" + String(activeSchedules.day.endHour) : String(activeSchedules.day.endHour);
    minuteStr = activeSchedules.day.endMinute < 10 ? "0" + String(activeSchedules.day.endMinute) : String(activeSchedules.day.endMinute);
    break;
  case nightStart:
    hoursStr = activeSchedules.night.startHour < 10 ? "0" + String(activeSchedules.night.startHour) : String(activeSchedules.night.startHour);
    minuteStr = activeSchedules.night.startMinute < 10 ? "0" + String(activeSchedules.night.startMinute) : String(activeSchedules.night.startMinute);
    break;
  case nightEnd:
    hoursStr = activeSchedules.night.endHour < 10 ? "0" + String(activeSchedules.night.endHour) : String(activeSchedules.night.endHour);
    minuteStr = activeSchedules.night.endMinute < 10 ? "0" + String(activeSchedules.night.endMinute) : String(activeSchedules.night.endMinute);
    break;
  case weekendDayStart:
    if (activeSchedules.weekendDay.startHour != -1)
    {
      hoursStr = activeSchedules.weekendDay.startHour < 10 ? "0" + String(activeSchedules.weekendDay.startHour) : String(activeSchedules.weekendDay.startHour);
      minuteStr = activeSchedules.weekendDay.startMinute < 10 ? "0" + String(activeSchedules.weekendDay.startMinute) : String(activeSchedules.weekendDay.startMinute);
    }
    break;
  case weekendDayEnd:
    if (activeSchedules.weekendDay.startHour != -1)
    {
      hoursStr = activeSchedules.weekendDay.endHour < 10 ? "0" + String(activeSchedules.weekendDay.endHour) : String(activeSchedules.weekendDay.endHour);
      minuteStr = activeSchedules.weekendDay.endMinute < 10 ? "0" + String(activeSchedules.weekendDay.endMinute) : String(activeSchedules.weekendDay.endMinute);
    }
    break;
  case weekendNightStart:
    if (activeSchedules.weekendNight.startHour != -1)
    {
      hoursStr = activeSchedules.weekendNight.startHour < 10 ? "0" + String(activeSchedules.weekendNight.startHour) : String(activeSchedules.weekendNight.startHour);
      minuteStr = activeSchedules.weekendNight.startMinute < 10 ? "0" + String(activeSchedules.weekendNight.startMinute) : String(activeSchedules.weekendNight.startMinute);
    }
    break;
  case weekendNightEnd:
    if (activeSchedules.weekendNight.startHour != -1)
    {
      hoursStr = activeSchedules.weekendNight.endHour < 10 ? "0" + String(activeSchedules.weekendNight.endHour) : String(activeSchedules.weekendNight.endHour);
      minuteStr = activeSchedules.weekendNight.endMinute < 10 ? "0" + String(activeSchedules.weekendNight.endMinute) : String(activeSchedules.weekendNight.endMinute);
    }
    break;
  }

  return hoursStr + ":" + minuteStr;
}

// Print some debug stuff to serial
#ifdef DEBUG_LAMPOMATIC
void printScheduleAndTime()
{
  Serial.print("Day schedule: ");
  Serial.println(getFormattedHourMinuteConcatenation(dayStart));
  Serial.println(" - ");
  Serial.println(getFormattedHourMinuteConcatenation(dayEnd));
  Serial.print("Night schedule: ");
  Serial.println(getFormattedHourMinuteConcatenation(nightStart));
  Serial.println(" - ");
  Serial.println(getFormattedHourMinuteConcatenation(nightEnd));

  Serial.print("Weekend day schedule: ");
  Serial.println(getFormattedHourMinuteConcatenation(weekendDayStart));
  Serial.println(" - ");
  Serial.println(getFormattedHourMinuteConcatenation(weekendDayEnd));

  Serial.print("Weekend night schedule: ");
  Serial.println(getFormattedHourMinuteConcatenation(weekendNightStart));
  Serial.println(" - ");
  Serial.println(getFormattedHourMinuteConcatenation(weekendNightEnd));

  Serial.print("WeekDay");
  Serial.println(weekday());
  Serial.print(daysOfTheWeek[timeClient.getDay()]);
  Serial.print(", ");
  Serial.println(timeClient.getFormattedTime());

  Serial.print("Time epoch time: ");
  Serial.println(now());
  Serial.print("NTPClient epoch time: ");
  Serial.println(timeClient.getEpochTime());

  Serial.print("DST Offset: ");
  Serial.println(dstOffsetInSeconds);

  Serial.print("nightActive status: ");
  Serial.println(nightActive);
  Serial.print("Night-pin PWM setting: ");
  Serial.println(activeSchedules.nightIntensity);

  Serial.print("dayActive status: ");
  Serial.println(dayActive);
  Serial.print("Day-pin PWM setting: ");
  Serial.println(activeSchedules.dayIntensity);

  Serial.print("Current state saved to EEPROM: ");
  Serial.println(currentStatePersisted == true ? "YES" : "NO");

  Serial.print("Activeschedules init: ");
  Serial.println(activeSchedules.initialized);
}
#endif