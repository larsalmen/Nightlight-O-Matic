# Nightlight-O-Matic
A wifi-enabled dual-color nightlight, with schedules for day and night (and separate weekend schedules, cus' who doesn't like to sleep in on saturdays..?).
***

## Setup
### Wifi
To connect it to the wifi, just edit the constants:
```
const char *ssid = "";
const char *password = "";
```
### Time
It keeps time by connecting to NTP servers (default pool.ntp.org) every minute.

To set up timezone, change the `utcOffsetInSeconds` to match your timezones offset in seconds from UTC.
If you use daylightsavings time, input the offset in seconds in the `dstOffsetInSeconds`. (this is also exposed as a checkbox in the gui, setting it here and then ticking the checkbox skews it one hour of. Beware.)
```
const long utcOffsetInSeconds = 3600;
long dstOffsetInSeconds = 0;
```
To change the names of weekdays (exposed in gui when checking the current schedule), edit the `daysOfTheWeek` array to your liking.
```
const char daysOfTheWeek[7][12] = {"Söndag", "Måndag", "Tisdag", "Onsdag", "Torsdag", "Fredag", "Lördag"};

```

To password-protect the schedules, set a password in:
```
const char *superSecretPassword = "";
```

## Schedules

The Nightlight-O-Matic exposes a (very simple) gui if you browse to it's root, that lets you set a schedule for day and night light, and their respective intensity.

You may also optionally set a seperate schedule for the weekend (sat-sun) that overrides those days, all others gets the regular schedule. If you don't, the same schedule is used every day.

To check the current schedule and time, browse to /time

## Why I made this, you ask?

Well, my kids keep waking up at ungodly hours, wandering into my bedroom and waking me and my wife just to ask "Is it morning?".
Since they can´t read the clock yet, now they can just glance up at the soooothing red light coming from the Nightlight-O-Matic and roll-over and go back to sleep. (Or, if its shining bright and blue, come wake me up).

## Changelog:

- Version 1.1.0 - Re-wrote basically everything to better handle alarms. Used to many alarms (and RAM) previously, causing some alarms to not be saved.
- Version 1.0.0 - Initial commit.

