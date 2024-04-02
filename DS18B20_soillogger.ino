// To quickly switch on and off all Serial.prints,
// or choose between prints to serial monitor
// and serial plotter:
//#define SILENT
#ifndef SILENT
#define DEBUG
#ifndef DEBUG
#define PLOTTER
#endif
#endif

// This makes it easy to turn debugging messages
// on or off, by defining DEBUG above:
#include <DebugMacros.h> // https://forum.arduino.cc/index.php?topic=215334.30

#include <SPI.h>
#include <SD.h> // SD card logging from https://github.com/adafruit/Light-and-Temp-logger/blob/master/lighttemplogger.ino
#include <Wire.h>
#include <OneWire.h> // https://github.com/PaulStoffregen/OneWire
#include <DallasTemperature.h> // https://github.com/milesburton/Arduino-Temperature-Control-Library
#include "RTClib.h"
#include <avr/wdt.h>


// numbers chosen to match pin numbers..
#define ONE_WIRE_BUS2   2
#define ONE_WIRE_BUS4   4

OneWire oneWire_one(ONE_WIRE_BUS2);
OneWire oneWire_two(ONE_WIRE_BUS4);

DallasTemperature sensor_one(&oneWire_one);
DallasTemperature sensor_two(&oneWire_two);

RTC_PCF8523 RTC;

//  15 minutes = 900000 milliseconds
//  1 minute = 6000 milliseconds
unsigned long startShutDownPeriod = 0; // to mark the start of current shutDownPeriod
const unsigned long shutDownPeriod = 900000;

// set global sensor resolution to 9, 10, 11, or 12 bits
const int8_t resolution = 12;

const int chipSelect = 10; // for SD card SPI

File logfile;

char DateAndTimeString[20]; //19 digits plus the null char

/*------------------------------------------------------------------------------
  error
  Prints out an error message,
  stops.
  ------------------------------------------------------------------------------
*/

void error(char *str)
{
  DPRINT("error: ");
  DPRINTLN(str);
  while (1);
}

/*------------------------------------------------------------------------------
  Sets watchdog timer, begins serial connection,
  SD & RTC intialisation,
  and begins data logfile, writing a header line.
  ------------------------------------------------------------------------------
*/

void setup() {

  wdt_disable();  // Disable the watchdog and wait for more than 2 seconds
  delay(3000);  // With this the Arduino doesn't keep resetting infinitely in case of wrong configuration
  wdt_enable(WDTO_8S);

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Wire.begin();

#ifdef SILENT
  Serial.println("The rest is silence.");
#endif

#ifdef PLOTTER
  Serial.println("1.0");
#endif

  wdt_reset();
  DPRINTLN();
  DPRINT("Initializing SD card...");

  wdt_reset();
  // make sure that the default chip select pin is set to
  // output, even if you don't use it:
  pinMode(10, OUTPUT);
  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    error("Card failed, or not present");
  }
  DPRINTLN("Done.");

  wdt_reset();
  DPRINT("Creating a new file...");
  char filename[] = "LOGGER00.CSV";
  for (uint8_t i = 0; i < 100; i++) {
    filename[6] = i / 10 + '0';
    filename[7] = i % 10 + '0';
    if (! SD.exists(filename)) {
      // only open a new file if it doesn't exist
      logfile = SD.open(filename, FILE_WRITE);
      break;  // leave the loop!
    }
  }
  if (! logfile) {
    error("couldnt create file");
  }
  DPRINTLN("Done.");
  DPRINT("Logging to: ");
  DPRINTLN(filename);

  /*------------------------------------------------------------------------------
    Initializes the real time clock,
    and sets time to system time if RTC has lost power.
    ------------------------------------------------------------------------------
  */

  wdt_reset();
  DPRINT("Initializing RTC...");
  Wire.begin();
  if (!RTC.begin()) {
    logfile.println("RTC failed");
    error("RTC failed");
    logfile.flush();
    while (1);
  }
  // following line sets the RTC to the date & time this sketch was compiled
  //RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));
  // This line sets the RTC with an explicit date & time, for example to set
  // April 2, 2024 at 15:27:30 you would call:
  RTC.adjust(DateTime(2024, 4, 2, 15, 27, 30));
  DPRINTLN("Done.");

  /*------------------------------------------------------------------------------
    writing header line to file
    ------------------------------------------------------------------------------
  */

  wdt_reset();
  logfile.println("datetime,temp1,temp2");
  DPRINTLN("datetime,temp1,temp2");

  /*------------------------------------------------------------------------------
    Initializes two temperature sensors,
    gets one set of measurement and prints them.
    ------------------------------------------------------------------------------
  */

  wdt_reset();
  float temperature;
  sensor_one.begin();
  sensor_two.begin();
  sensor_one.setResolution(resolution);
  sensor_one.requestTemperatures();
  DPRINT("Initializing sensor_one... temperature: ");
  DPRINT(sensor_one.getTempCByIndex(0));
  DPRINTLN("...Done.");

  wdt_reset();
  sensor_two.setResolution(resolution);
  sensor_two.requestTemperatures();
  DPRINT("Initializing sensor_two... temperature: ");
  DPRINT(sensor_two.getTempCByIndex(0));
  DPRINTLN("...Done.");

  startShutDownPeriod = millis() - shutDownPeriod; // start shutdownperiod, but start measurements in loop() right away

}

void loop(void) {

  wdt_reset();
  if (millis() - startShutDownPeriod >= shutDownPeriod) {

    float temp1;
    float temp2;
    uint16_t thisYear;
    int8_t thisMonth, thisDay, thisHour, thisMinute, thisSecond;
    DateTime now;

    digitalWrite(LED_BUILTIN, HIGH);

    wdt_reset();
    // offsetting the measurement duration from shut down period
    startShutDownPeriod = millis() ;
    sensor_one.requestTemperatures();
    sensor_two.requestTemperatures();

    wdt_reset();
    // fetch the time
    now = RTC.now();
    // log time
    thisYear = now.year();
    thisMonth = now.month();
    thisDay = now.day();
    thisHour = now.hour();
    thisMinute = now.minute();
    thisSecond = now.second();
    sprintf_P(DateAndTimeString, PSTR("%4d-%02d-%02dT%d:%02d:%02d"), thisYear, thisMonth, thisDay, thisHour, thisMinute, thisSecond);
    logfile.print(DateAndTimeString);
    logfile.print(",");
    DPRINT(DateAndTimeString);
    DPRINT(",");

    wdt_reset();
    temp1 = sensor_one.getTempCByIndex(0);
    logfile.print(temp1);
    logfile.print(",");
    DPRINT(temp1, 2);
    DPRINT(",");

    wdt_reset();
    temp2 = sensor_two.getTempCByIndex(0);
    logfile.println(temp2);
    DPRINTLN(temp2, 2);

#ifdef PLOTTER
    Serial.print(temp1);
    Serial.print(",");
    Serial.println(temp2);
#endif

    logfile.flush();
    digitalWrite(LED_BUILTIN, LOW);

  }

}
