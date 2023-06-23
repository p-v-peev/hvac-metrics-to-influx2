#include <Arduino.h>
// Temperature
#include <OneWire.h>
#include <DallasTemperature.h>
// InfluxDB
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
// WiFi
#include <ESP8266WiFi.h>
 
// Time intervals
#define REPORT_TIME 1000 * 60
#define WIFI_HEALTH_CHECK_TIME 1000 * 60

// Sensor pins
#define ELECTRICAL_INTERRUPT_PIN D1
#define TEMPERATURE_SENSORS_BUS_PIN D3

// InfluxBD credentials
#define INFLUXDB_URL ""
#define INFLUXDB_TOKEN ""
#define INFLUXDB_ORG ""
#define INFLUXDB_BUCKET "HEAT_PUMP"
#define TIME_ZONE_INFO "UTC2"

// WiFi Access Point credentials
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define WIFI_CONNECT_THRESHOLD 25

unsigned long report_base_time;
unsigned long wifi_check_base_time;

// Electrical energy meter and water meter
volatile unsigned int electrical_energy_meter_impulses = 0;

// Temperature
OneWire temperatureBus(TEMPERATURE_SENSORS_BUS_PIN);
DallasTemperature temperature_sensors(&temperatureBus);
uint8_t temperature_sensors_count;
DeviceAddress* device_addresses;

// InfluxDB
InfluxDBClient influx_client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
// Influx data point
Point heat_pump_metrics("HEAT_PUMP_METRICS");

void IRAM_ATTR handle_electrical_meter_pulse() {
  electrical_energy_meter_impulses++;
}

String convertAddressToString(DeviceAddress deviceAddress) {
  String addrToReturn = "";
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16) addrToReturn += "0";
    addrToReturn += String(deviceAddress[i], HEX);
  }
  return addrToReturn;
}

void wait_for_wifi_connect() {
  // Flash the builtin led while waiting for WiFi connection
  for (uint8_t i = 0; i < WIFI_CONNECT_THRESHOLD && WiFi.status() != WL_CONNECTED; i++)
  {
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
  }

  if(WiFi.status() != WL_CONNECTED)
  {
    // Turn on the builtin led to notify the user and restart
    digitalWrite(LED_BUILTIN, LOW);
    delay(2000);
    ESP.restart();
  }
}

void check_wifi_and_reconnect_if_needed() {
  if(WiFi.status() != WL_CONNECTED)
  {
    WiFi.reconnect();
    wait_for_wifi_connect();
  }
}

void configure_temperature_sensors() {
  temperature_sensors.begin();
  temperature_sensors_count = temperature_sensors.getDS18Count();
  device_addresses = (DeviceAddress*)calloc(temperature_sensors_count, sizeof(DeviceAddress));

  for (uint8_t i = 0; i < temperature_sensors_count; i++)
  {
    if(!temperature_sensors.getAddress(device_addresses[i], i))
    {
       // Turn on the builtin led to notify the user and restart
      digitalWrite(LED_BUILTIN, LOW);
      delay(3000);
      ESP.restart();
    }
  }  
}

void connect_influx_client() {
  // Check server connection
  if(!influx_client.validateConnection())
  {
    // Turn on the builtin led to notify the user and restart
    digitalWrite(LED_BUILTIN, LOW);
    delay(4000);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wait_for_wifi_connect();

  // The interrupt will be on FALLING EDGE
  pinMode(ELECTRICAL_INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ELECTRICAL_INTERRUPT_PIN), handle_electrical_meter_pulse, FALLING);

  configure_temperature_sensors();

  timeSync(TIME_ZONE_INFO, "time.google.com", "time.facebook.com", "time.facebook.com");
  connect_influx_client();

  // Setup base time
  wifi_check_base_time = report_base_time = millis();
}

void loop() {
  unsigned long time_now = millis();

  if (time_now - wifi_check_base_time >= WIFI_HEALTH_CHECK_TIME)
  {
    wifi_check_base_time = time_now;
    check_wifi_and_reconnect_if_needed();
  }

  if (time_now - report_base_time >= REPORT_TIME)
  {
    report_base_time = time_now;

    unsigned int electrical_energy_meter_impulses_local;

    //Guard the critical section
    noInterrupts();
    electrical_energy_meter_impulses_local = electrical_energy_meter_impulses;
    electrical_energy_meter_impulses = 0;
    interrupts();

    heat_pump_metrics.clearFields();

    // Requesting the temperatures
    temperature_sensors.requestTemperatures();
    for (uint8_t i = 0; i < temperature_sensors_count; i++)
    {
      heat_pump_metrics.addField(convertAddressToString(device_addresses[i]), temperature_sensors.getTempC(device_addresses[i]));
    } 

    heat_pump_metrics.addField("electrical_impulses", electrical_energy_meter_impulses_local);

    // Check the wifi again before sending the data;
    check_wifi_and_reconnect_if_needed();

    Serial.println(heat_pump_metrics.toLineProtocol());

    // Turn on the builtin led to indicate data is being sent
    digitalWrite(LED_BUILTIN, LOW);
    if(influx_client.writePoint(heat_pump_metrics))
      // If the atempt is successfull turn of the led
      digitalWrite(LED_BUILTIN, HIGH);
    }
}