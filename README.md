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

Changelog:

Version 1.0.0 - Initial commit.
