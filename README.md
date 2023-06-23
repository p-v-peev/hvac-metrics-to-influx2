# hvac-metrics-to-influx2
ESP8266 program to collect metrics about hvac system

The program collect the metrics from the connected DS18B20 temperature sensors and the attached electrical energy meter and sends them to influxdb2 server.The temperature sensors are discoverred automatically when the controller is powered up. The electrical energy is measured as impulces from the SO interface of the electrical energy meter.
The code doesn't perform any processing on the data. You must write the appropriate query for what you need in influxdb2.
