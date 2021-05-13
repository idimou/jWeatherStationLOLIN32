//Firmware version
const char * FIRMWARE_VERSION = "1.1";

//conditional variables for various purposes
// #define DEBUG_FAST_LOOP //makes looping faster without big delays

//the board should support an ADC resolution of 12bits
//TODO: check if there is a constant to get the ADC resolution of the board at compile time
static const unsigned int ADC_RESOLUTION = 4096;

//TODO: remove pin 39 connection from PCB
//#define MQ7_CO_PIN   39 //ADC GPIO34 - Carbon Monoxide Sensor
#include <Arduino.h>
#include "IotWebConfFactory.h"

#include "HardwareSerial.h"

#include <SPI.h>
#define LOG_PERIOD 15000  //Logging period in milliseconds, recommended value 15000-60000.
#define MAX_PERIOD 60000  //Maximum logging period without modifying this sketch

unsigned long counts;     //variable for GM Tube events
unsigned long cpm;        //variable for CPM
unsigned int multiplier;  //variable for calculation CPM in this sketch
unsigned long previousMillis;  //variable for time measurement

static HardwareSerial console_serial(0); // UART 0 - CONSOLE
static HardwareSerial MHZ19_serial(1); // UART 1
static HardwareSerial PMS7003_serial(2); // UART 2

#include <MHZ19.h>                                         // include main library
#define MHZ19_RX_PIN 32                                          // Rx pin which the MHZ19 Tx pin is attached to
#define MHZ19_TX_PIN 33                                          // Tx pin which the MHZ19 Rx pin is attached to
MHZ19 CO2_MHZ19;                                             // Constructor for MH-Z19 class

#include <PMS.h>
#define PMS7003_RX_PIN 12
#define PMS7003_TX_PIN 14
PMS pms(PMS7003_serial);
PMS::DATA data;

//headers for reading temperature with Dallas probe
// #include <OneWire.h> 
// #include <DallasTemperature.h>

#include <limits.h>

//BME280 atmospheric pressure and hunidity sensor (temperature sensor is not used)
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define SEALEVELPRESSURE_HPA (1019.50)
#define ONBOARD_LED 5

Adafruit_BME280 bme280;

//MiCS-6814
#include <Wire.h>
// #include "MutichannelGasSensor.h"

//library that collects and sends data to the IoT server
#include "telemetry.h"
Telemetry telemetry;

//setup code runs once at the beginning


// void read_carbon_monoxide()
// {
//   telemetry.setCarbonMonoxide(gas.measure_CO());
//   console_serial.println("Carbon Monoxide is: " + (String)telemetry.getCarbonMonoxide() +" ppm"); 
// }

// void read_nitrogen_dioxide()
// {
//   telemetry.setNitrogenDioxide(gas.measure_NO2());
//   console_serial.println("Nitrogen Dioxide is: " + (String)telemetry.getNitrogenDioxide() +" ppm"); 
// }

// void read_hydrogen()
// {
//   telemetry.setHydrogen(gas.measure_H2());
//   console_serial.println("Hydrogen is: " + (String)telemetry.getHydrogen() +" ppm"); 
// }

// void read_temperature()
// {
//   // temperature Data wire is plugged into pin 13 on the Arduino
//   static const byte ONE_WIRE_BUS = 4;
//   // 9 - 12 precision
//   static const byte TEMPERATURE_PRECISION = 12;
  
//   // Setup a oneWire instance to communicate with any OneWire devices  
//   // (not just Maxim/Dallas temperature ICs) 
//   static OneWire oneWire(ONE_WIRE_BUS); 
//   // Pass our oneWire reference to Dallas Temperature. 
//   static DallasTemperature temperature_sensors(&oneWire);

//   console_serial.println("Requesting temperatures...");
//   temperature_sensors.requestTemperatures(); // Send the command to get temperature readings
//   console_serial.print("Temperature is: ");
//   telemetry.setTemperatureCelcius(temperature_sensors.getTempCByIndex(0));
//   console_serial.println(telemetry.getTemperatureCelcius());
// }

void read_barometric_pressure()
{
  telemetry.setBarometricPressure(bme280.readPressure() / 100);
  console_serial.println("Barometric pressure is: " + (String)telemetry.getBarometricPressure() + " hPa");
}

void read_humidity()
{
  telemetry.setHumidity(bme280.readHumidity());
  console_serial.println("Humidity is: " + (String)telemetry.getHumidity() + " %");
}

void read_BMEtemperature()
{
  telemetry.setTemperatureCelcius(bme280.readTemperature());
  console_serial.println("BME280 temperature is: " + (String)telemetry.getTemperatureCelcius() + " C");
}

void read_pms7003_data()
{
  pms.wakeUp();
  #ifdef DEBUG_FAST_LOOP
  console_serial.println("Waking up PMS7003, wait 3 seconds for stable readings...");
  IotWebConfFactory::mydelay(3000);
  #else
  console_serial.println("Waking up PMS7003, wait 30 seconds for stable readings...");
  IotWebConfFactory::mydelay(30000);
  #endif

  console_serial.println("Send PMS7003 read request...");
  pms.requestRead();

  if (pms.readUntil(data, 2000))
  {
    telemetry.setPMS7003_MP_1(data.PM_AE_UG_1_0);
    console_serial.print("PM 1.0 (ug/m3): ");
    console_serial.println(telemetry.getPMS7003_MP_1());
    
    telemetry.setPMS7003_MP_2_5(data.PM_AE_UG_2_5);
    console_serial.print("PM 2.5 (ug/m3): ");
    console_serial.println(telemetry.getPMS7003_MP_2_5());
    
    telemetry.setPMS7003_MP_10(data.PM_AE_UG_10_0);
    console_serial.print("PM 10.0 (ug/m3): ");
    console_serial.println(telemetry.getPMS7003_MP_10());
  }
  else
  {
    console_serial.println("No PMS7003 data.");
    telemetry.setPMS7003_MP_1(-300);
    telemetry.setPMS7003_MP_2_5(-300);
    telemetry.setPMS7003_MP_10(-300);
  }

  console_serial.println("PMS7003 going to sleep.");
  pms.sleep();
}


void read_mh_z19_co2_data()
{
  telemetry.setCarbonDioxide(CO2_MHZ19.getCO2());
  
  console_serial.print("CO2: ");
  console_serial.println(telemetry.getCarbonDioxide());
}


void setup_geiger()
{
  counts = 0;
  cpm = 0;
  multiplier = MAX_PERIOD / LOG_PERIOD;      //calculating multiplier, depend on your log period
  attachInterrupt(0, geiger_tube_impulse, FALLING); //define external interrupts 
}

void geiger_tube_impulse(){       //subprocedure for capturing events from Geiger Kit
  counts++;
}

void check_geiger()
{
  unsigned long currentMillis = millis();
  if(currentMillis - previousMillis > LOG_PERIOD)
  {
    previousMillis = currentMillis;
    cpm = counts * multiplier;
    
    Serial.print(cpm);
    counts = 0;
  }
}

void setup() {
  
  setup_geiger();

  pinMode(ONBOARD_LED,OUTPUT);

  //console output
  Serial.begin(115200, SERIAL_8N1, 3, 1);//required for IotWebConfFactory serial monitoring in debug mode
  console_serial.begin(115200, SERIAL_8N1, 3, 1);

  //pass firmware version to telemetry object
  telemetry.setFirmwareVersion(FIRMWARE_VERSION);

  //setup_pms7003();
  PMS7003_serial.begin(PMS::BAUD_RATE, SERIAL_8N1, PMS7003_RX_PIN, PMS7003_TX_PIN);
  
  //setup_co2();
  delay(100); //delay workaround for MHZ19 sensor readings in standalone power mode 
  MHZ19_serial.begin(9600, SERIAL_8N1, MHZ19_RX_PIN, MHZ19_TX_PIN); // ESP32 Example
  CO2_MHZ19.begin(MHZ19_serial);                                // *Important, Pass your Stream reference 
  CO2_MHZ19.autoCalibration();                              // Turn auto calibration ON (disable with autoCalibration(false))


  delay(1000);

   //MiCS-6814
  // Start the Wire library and init MiCS-6814 sensor only if is detected
  Wire.begin();
  Wire.beginTransmission(0x04);
  if (Wire.endTransmission() == 0) {
   //MiCS-6814
   //IotWebConfFactory::mydelay(1000);
   gas.begin(0x04); //the default I2C address of the slave is 0x04
   gas.powerOn();
  }

  // delay(1500);
  
  // BME280 sensor init
  unsigned bme280_status;
  // default settings
  // (you can also pass in a Wire library object like &Wire2)
  bme280_status = bme280.begin(0x76); //set bme280 address manually  
  if (!bme280_status) {
      console_serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
      console_serial.print("SensorID was: 0x"); 
      console_serial.println(bme280.sensorID(),16);
      console_serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
      console_serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
      console_serial.print("        ID of 0x60 represents a BME 280.\n");
      console_serial.print("        ID of 0x61 represents a BME 680.\n");
  } else {
      console_serial.println("BME280 init success");

      /*
      bme280.setSampling(Adafruit_BME280::MODE_FORCED,
      Adafruit_BME280::SAMPLING_X1, // temperature sensor off
      Adafruit_BME280::SAMPLING_X1, // pressure
      Adafruit_BME280::SAMPLING_X1, // humidity
      Adafruit_BME280::FILTER_OFF);
      */
  }

  console_serial.println("Epaper display EPD initialized");


  IotWebConfFactory::setup();

  console_serial.println("Setup done! Entering environmental monitoring station main loop");
}

void loop() {
  console_serial.println("Begin loop");
  console_serial.println("Firmware version: " + FIRMWARE_VERSION);

  // showVoltagePercentage();

  //wait 60sec to read next sensor data
  #ifdef DEBUG_FAST_LOOP
  static unsigned long DEVICE_DELAY_MS = 5000; //5 seconds
  #else
  static unsigned long DEVICE_DELAY_MS = 60000; //60 seconds
  #endif
  
  IotWebConfFactory::loop();
  //setTelemetryUrl, setTelemetryToken and setTelemetryPort should be called on IoTWebConfFactory::configSaved callback.
  //Till we implement the callback, we use it as it is bellow for simplicity
  telemetry.setTelemetryUrl(IotWebConfFactory::getConfigUrl());
  telemetry.setTelemetryToken(IotWebConfFactory::getConfigToken());
  telemetry.setTelemetryPort(IotWebConfFactory::getConfigPort());

  //reads pms7003 data
  read_pms7003_data();
  
  //reads co2 data
  read_mh_z19_co2_data();

  //reads the temperature from the sensor
  //read_temperature();

  //reads the barometric pressure from the BME280 sensor
  read_barometric_pressure();

  //reads the humidity from the BME280 sensor
  read_humidity();

  read_BMEtemperature();
  
  // //reads the carbon monoxide value from the MiCS-6814 sensor
  // read_carbon_monoxide();

  // //reads the nitrogen dioxide value from the MiCS-6814 sensor
  // read_nitrogen_dioxide();

  // //reads the hydrogen value from the MiCS-6814 sensor
  // read_hydrogen();
  
  //sends all sensor data to the IoT server
  digitalWrite(ONBOARD_LED,LOW);
  telemetry.send_data_to_iot_server();
  // telemetry.send_data_to_iot_server2();
  digitalWrite(ONBOARD_LED,HIGH);



  delay(60000);


  console_serial.println("Delay for: " + (String)(DEVICE_DELAY_MS / 1000) + " sec");
  console_serial.println("\n");
  IotWebConfFactory::mydelay(DEVICE_DELAY_MS);
}
