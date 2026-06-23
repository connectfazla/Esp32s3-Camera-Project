#pragma once

#include <Arduino.h>

bool startMediaServices(const char *hostname);
bool mediaServicesStarted();
uint32_t microphoneRms();
uint32_t microphonePeak();

