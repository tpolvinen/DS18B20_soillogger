// To quickly switch on and off all Serial.prints,
// or choose between prints to serial monitor
// and serial plotter:
//#define SILENT
//#ifndef SILENT
#define CLOCKTEST
#ifndef CLOCKTEST
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

unsigned long currentMillis;
unsigned long startClockCheckInterval = 0;
const unsigned long clockCheckInterval = 1000;

RTC_PCF8523 RTC;

DateTime now;
unsigned long startMeasurementInterval = 0; // to mark the start of current measurementInterval in RTC unixtime
const unsigned long measurementInterval = 30; // seconds, measured in RTC unixtime
uint16_t thisYear;
int8_t thisMonth, thisDay, thisHour, thisMinute, thisSecond;

// set global sensor resolution to 9, 10, 11, or 12 bits
const int8_t resolution = 12;

const int chipSelect = 10; // for SD card SPI

File datafile;

char DateAndTimeString[20]; //19 digits plus the null char

float temp1;
float temp2;

const int led1Pin = 5;
const int led2Pin = 6;
bool led1State = false;
bool led2State = false;
unsigned long startLed1Timer = 0;
unsigned long startLed2Timer = 0;
const unsigned long ledTimer = 1000;

const int buttonPin = 7;
int currentButtonState;
int lastButtonState = HIGH;   // the previous reading from the input pin
bool writeButtonPress = false;
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

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
  and begins data datafile, writing a header line.
  ------------------------------------------------------------------------------
*/

void setup() {

  wdt_disable();  // Disable the watchdog and wait for more than 2 seconds
  delay(3000);  // With this the Arduino doesn't keep resetting infinitely in case of wrong configuration
  wdt_enable(WDTO_8S);

  pinMode(LED_BUILTIN, OUTPUT);

  // initialize the pushbutton pin as an pull-up input
  // the pull-up input pin will be HIGH when the switch is open and LOW when the switch is closed.
  pinMode(buttonPin, INPUT_PULLUP);

  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Wire.begin();

#ifdef SILENT
  Serial.println("Silent mode on.");
#endif

#ifdef CLOCKTEST
  Serial.println("Clock test mode on.");
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
      datafile = SD.open(filename, FILE_WRITE);
      break;  // leave the loop!
    }
  }
  if (! datafile) {
    error("couldnt create file");
  }
  DPRINTLN("Done.");
  DPRINT("Logging to: ");
  DPRINTLN(filename);

  /*------------------------------------------------------------------------------
    Initializes the real time clock,
    and sets time to system time if RTC has lost power,
    IF relevant command uncommented!
    After re-setting the RTC to correct time,
    COMMENT ALL RTC.adjust COMMANDS!
    Otherwise it might re-set clock on each restart. Maybe. Don't know.
    ------------------------------------------------------------------------------
  */

  wdt_reset();
  DPRINT("Initializing RTC...");
  Wire.begin();
  if (!RTC.begin()) {
    datafile.println("RTC failed");
    error("RTC failed");
    datafile.flush();
    while (1) delay(10);
  }
  // following line sets the RTC to the date & time this sketch was compiled
  // RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));
  // This line sets the RTC with an explicit date & time, for example to set
  // April 8, 2024 at 9:25:30 you would call:
  // RTC.adjust(DateTime(2024, 4, 15, 13, 22, 0));

  // When the RTC was stopped and stays connected to the battery, it has
  // to be restarted by clearing the STOP bit. Let's do this to ensure
  // the RTC is running.
  RTC.start();

  // The PCF8523 can be calibrated for:
  //        - Aging adjustment
  //        - Temperature compensation
  //        - Accuracy tuning
  // The offset mode to use, once every two hours or once every minute.
  // The offset Offset value from -64 to +63. See the Application Note for calculation of offset values.
  // https://www.nxp.com/docs/en/application-note/AN11247.pdf
  // The deviation in parts per million can be calculated over a period of observation. Both the drift (which can be negative)
  // and the observation period must be in seconds. For accuracy the variation should be observed over about 1 week.
  // Note: any previous calibration should cancelled prior to any new observation period.
  // Example - RTC gaining 43 seconds in 1 week
  float drift = 54; //43; // seconds plus or minus over oservation period - set to 0 to cancel previous calibration.
  float period_sec = 598222; //(7 * 86400);  // total obsevation period in seconds (86400 = seconds in 1 day:  7 days = (7 * 86400) seconds )
  float deviation_ppm = (drift / period_sec * 1000000); //  deviation in parts per million (μs)
  float drift_unit = 4.34; // use with offset mode PCF8523_TwoHours
  // float drift_unit = 4.069; //For corrections every min the drift_unit is 4.069 ppm (use with offset mode PCF8523_OneMinute)
  int offset = round(deviation_ppm / drift_unit);
  // RTC.calibrate(PCF8523_TwoHours, offset); // Un-comment to perform calibration once drift (seconds) and observation period (seconds) are correct
  // RTC.calibrate(PCF8523_TwoHours, 0); // Un-comment to cancel previous calibration
  
#ifdef CLOCKTEST
  Serial.print("Offset is "); Serial.println(offset); // Print to control offset
#endif



  DPRINTLN("Done.");

  /*------------------------------------------------------------------------------
    writing header line to file
    ------------------------------------------------------------------------------
  */

  wdt_reset();
  datafile.println("datetime,temp1,temp2,note");
  DPRINTLN("datetime,temp1,temp2,note");

  /*------------------------------------------------------------------------------
    Initializes two temperature sensors,
    gets one set of measurement and prints them.
    Does not write data to a file.
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

  // fetch the time
  now = RTC.now();
  // start measurementInterval and set it back by interval time -> starts measurements in loop() right away
  startMeasurementInterval = now.unixtime() - measurementInterval;

  //set clockCheckInterval
  startClockCheckInterval = millis();

}

void loop(void) {

  wdt_reset();

  int reading = digitalRead(buttonPin);

  currentMillis = millis();

  if (reading != lastButtonState) {
    lastDebounceTime = currentMillis;
    currentButtonState = reading;
  }

  if (currentMillis - lastDebounceTime >= debounceDelay) {
    if (lastButtonState == LOW && currentButtonState == HIGH) {
      writeButtonPress = true;
      DPRINTLN("Button press set to true.");
      led1State = true;
      startLed1Timer = currentMillis;
      led2State = true;
      startLed2Timer = currentMillis;
    }
    lastButtonState = currentButtonState;
  }

  if (currentMillis - startLed1Timer >= ledTimer) {
    led1State = false;
  }
  if (currentMillis - startLed2Timer >= ledTimer) {
    led2State = false;
  }

  digitalWrite(led1Pin, led1State);
  digitalWrite(led2Pin, led2State);

  wdt_reset();

  if (currentMillis - startClockCheckInterval >= clockCheckInterval) {
    startClockCheckInterval = currentMillis;
    // fetch the time
    now = RTC.now();
  }

  if (now.unixtime() - startMeasurementInterval >= measurementInterval) {

    startMeasurementInterval = now.unixtime();

    digitalWrite(led1Pin, HIGH);

    wdt_reset();
    // offsetting the measurement duration from measurement interval (?)
    sensor_one.requestTemperatures();
    sensor_two.requestTemperatures();

    wdt_reset();
    // log time
    thisYear = now.year();
    thisMonth = now.month();
    thisDay = now.day();
    thisHour = now.hour();
    thisMinute = now.minute();
    thisSecond = now.second();
    sprintf_P(DateAndTimeString, PSTR("%4d-%02d-%02dT%d:%02d:%02d"), thisYear, thisMonth, thisDay, thisHour, thisMinute, thisSecond);
    datafile.print(DateAndTimeString);
    datafile.print(",");
    DPRINT(DateAndTimeString);
    DPRINT(",");

#ifdef CLOCKTEST
    Serial.println(DateAndTimeString);
#endif

    wdt_reset();
    temp1 = sensor_one.getTempCByIndex(0);
    datafile.print(temp1);
    datafile.print(",");
    DPRINT(temp1, 2);
    DPRINT(",");

    wdt_reset();
    temp2 = sensor_two.getTempCByIndex(0);
    datafile.print(temp2);
    datafile.print(",");
    DPRINT(temp2, 2);
    DPRINT(",");

    if (writeButtonPress) {
      datafile.println("Kissaa rapsutettu!");
      DPRINTLN("Kissaa rapsutettu!");
      writeButtonPress = false;
    } else {
      datafile.println("");
      DPRINTLN("");
    }

#ifdef PLOTTER
    Serial.print(temp1);
    Serial.print(",");
    Serial.println(temp2);
#endif

    datafile.flush();
    digitalWrite(led1Pin, LOW);
  }

}
