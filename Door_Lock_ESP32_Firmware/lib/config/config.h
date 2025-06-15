#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define MQTT_MAX_BUFFER_SIZE 1024
uint16_t MAX_CAPACITY  = 1000;

#define DEBUG true

#define IP "65.0.75.212"

// I2C pins for ESP32 (default)
#define SDA_PIN 21
#define SCL_PIN 22

#define TIME_SYNC_DELAY 86400000
#define TIME_OFFSET     19800   //Time offset for India

//Error code
#define F_SEN_COMMU -1   //Error: Cannot communicate with sensor
#define F_SEN_FULL  -2   //Error: Fingerprint sensor storage is full
#define SPIFFS_READ -2;  //Unable to read file from SPIFFS

#if (DEBUG == true)
#define BAUD_RATE 115200
#endif

#endif