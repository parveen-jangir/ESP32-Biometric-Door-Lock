#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define MQTT_MAX_BUFFER_SIZE 1024

#define DEBUG true

#define IP "65.0.75.212"

#if (DEBUG == true)
#define BAUD_RATE 115200
#endif

#endif