#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include <Wire.h>
#include <AHTxx.h>

#include "config.h"

const char* mqtt_topic_particles = "custom/" DEVICE_NAME "/particles";
const char* mqtt_topic_temperature = "custom/" DEVICE_NAME "/temperature";
const char* mqtt_topic_humidity = "custom/" DEVICE_NAME "/humidity";
const char* mqtt_topic_set = "custom/" DEVICE_NAME "/set";

unsigned int interval = 30 * 1000;
unsigned long last_publish = 0;
unsigned long last_particles = 0;

unsigned int data_in = 0;
unsigned int data_out = 0;

unsigned int particles = 0;
float temperature = 0;
float humidity = 0;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

AHTxx aht10(AHTXX_ADDRESS_X38, AHT1x_SENSOR);

void handleSerial();
void handleNetwork();
void handleMQTT(char* topic, byte* payload, unsigned int length);

void setup() {  
  ArduinoOTA.onStart([]() {
    interval = 1000000;
    delay(5000);
  });

  ArduinoOTA.onEnd([]() {
    ESP.restart();
  });

  ArduinoOTA.begin();

  Serial.begin(9600);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
  }

  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(handleMQTT);

  aht10.begin(0, 2);
}

void loop() {
  ArduinoOTA.handle();
  handleSerial();
  handleNetwork();
}

// Inspired by https://github.com/bertrik/pm1006/blob/master/pm1006.cpp
void handleSerial() {
  static char state = 0;
  static byte data_index = 0;
  static byte data_length = 0;
  static byte data_command = 0;
  static byte checksum = 0; // http://easyonlineconverter.com/converters/checksum_converter.html

  while (Serial.available()) {
    byte received_byte = Serial.read();
    byte outgoing_byte = received_byte;

    // Header
    if (state == 0 && received_byte == 0x16) {
      state = 1;
      data_index = 0;
      data_in = 0;
      checksum = 0;
    // Length
    } else if (state == 1 && received_byte == 0x11) {
      data_length = received_byte;
      state = 2;
    // Command
    } else if (state == 2 && received_byte == 0x0B) {
      data_command = received_byte;
      state = 3;
    // Data
    } else if (state == 3) {
      data_index++;
      if (data_index == 3) {
        data_in += received_byte * 256;
        if (data_out > 0)
          outgoing_byte = data_out / 256;
      }
      if (data_index == 4) {
        data_in += received_byte;
        if (data_out > 0)
          outgoing_byte = data_out % 256;
      }
      if (data_index == data_length) {
        if (data_command == 0x0B) {
          particles = data_in;
          last_particles = millis();
        }

        state = 0;
        checksum = (checksum^0xFF);
        checksum++;
        outgoing_byte = checksum;
      }
    } else {
      state = 0;
    }

    checksum += outgoing_byte;
    Serial.write(outgoing_byte);
    ArduinoOTA.handle();
    delay(1);
  }
}

void handleNetwork() {
  if (!mqttClient.connected()) {
    mqttClient.connect(mqtt_id);
    mqttClient.subscribe(mqtt_topic_set);
  }

  unsigned long now = millis();
  unsigned long elapsed = now - last_publish;
  
  if (elapsed > interval) {
    last_publish = now;

    elapsed = now - last_particles;
    if (elapsed < interval)
      mqttClient.publish(mqtt_topic_particles, String(particles).c_str());

    temperature = aht10.readTemperature();
    humidity = aht10.readHumidity(AHTXX_USE_READ_DATA);

    mqttClient.publish(mqtt_topic_temperature, String(temperature).c_str());
    mqttClient.publish(mqtt_topic_humidity, String(humidity).c_str());
  }

  mqttClient.loop();
}

void handleMQTT(char* topic, byte* payload, unsigned int length) {
  if ( strcmp(topic, mqtt_topic_set) == 0 ) {
    char number[length + 1];  // Create a char array with space for the number and null terminator
    strncpy(number, (char*)payload, length);  // Copy the payload to the number array
    number[length] = '\0';  // Null-terminate the number array

    data_out = atoi(number);  // Convert the number array to an integer
  }
}