#pragma once

#include <Arduino.h>

struct StorageRecorderStatus {
  bool mounted;
  bool audioRecording;
  bool storageLow;
  uint8_t chipSelectPin;
  uint64_t totalBytes;
  uint64_t usedBytes;
  uint32_t audioSegmentsCompleted;
  uint32_t photosSaved;
  uint32_t writeErrors;
  char currentAudioFile[96];
  char lastPhotoFile[96];
};

bool storageRecorderBegin();
bool storageRecorderReady();
void storageRecorderAppendAudio(const int16_t *samples, size_t sampleCount);
bool storageRecorderSaveJpeg(const uint8_t *data, size_t length);
void storageRecorderGetStatus(StorageRecorderStatus *status);

