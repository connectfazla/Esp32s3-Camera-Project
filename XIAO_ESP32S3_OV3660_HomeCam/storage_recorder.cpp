#include "storage_recorder.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>

#include "camera_pins.h"
#include "config.h"

namespace {

constexpr char kRootDirectory[] = "/homecam";
constexpr char kAudioDirectory[] = "/homecam/audio";
constexpr char kImageDirectory[] = "/homecam/images";
constexpr uint32_t kCapacityRefreshMs = 30000;
constexpr uint32_t kHeaderRefreshMs = 5000;

struct __attribute__((packed)) WavHeader {
  char riff[4];
  uint32_t fileSizeMinus8;
  char wave[4];
  char fmt[4];
  uint32_t fmtSize;
  uint16_t audioFormat;
  uint16_t channels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
  char data[4];
  uint32_t dataSize;
};

static_assert(sizeof(WavHeader) == 44, "WAV header must be 44 bytes");

SPIClass g_sdSpi(FSPI);
SemaphoreHandle_t g_sdMutex = nullptr;
File g_audioFile;
bool g_mounted = false;
bool g_audioRecording = false;
bool g_storageLow = false;
uint8_t g_chipSelectPin = 0;
uint64_t g_totalBytes = 0;
uint64_t g_usedBytes = 0;
uint32_t g_audioDataBytes = 0;
uint32_t g_audioSegmentsCompleted = 0;
uint32_t g_photosSaved = 0;
uint32_t g_writeErrors = 0;
uint32_t g_lastCapacityRefresh = 0;
uint32_t g_lastHeaderRefresh = 0;
char g_currentAudioFile[96] = "";
char g_lastPhotoFile[96] = "";

WavHeader makeWavHeader(uint32_t dataBytes) {
  WavHeader header = {};
  memcpy(header.riff, "RIFF", 4);
  header.fileSizeMinus8 = dataBytes + sizeof(WavHeader) - 8;
  memcpy(header.wave, "WAVE", 4);
  memcpy(header.fmt, "fmt ", 4);
  header.fmtSize = 16;
  header.audioFormat = 1;
  header.channels = 1;
  header.sampleRate = HOME_CAM_AUDIO_SAMPLE_RATE;
  header.bitsPerSample = 16;
  header.blockAlign = header.channels * header.bitsPerSample / 8;
  header.byteRate = header.sampleRate * header.blockAlign;
  memcpy(header.data, "data", 4);
  header.dataSize = dataBytes;
  return header;
}

bool mountCardAt(uint8_t chipSelectPin) {
  pinMode(chipSelectPin, OUTPUT);
  digitalWrite(chipSelectPin, HIGH);
  if (!g_sdSpi.begin(SD_PIN_SCK, SD_PIN_MISO, SD_PIN_MOSI, chipSelectPin)) {
    return false;
  }
  if (!SD.begin(chipSelectPin, g_sdSpi, HOME_CAM_SD_SPI_HZ, "/sd", 10,
                false)) {
    SD.end();
    g_sdSpi.end();
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    SD.end();
    g_sdSpi.end();
    return false;
  }
  g_chipSelectPin = chipSelectPin;
  return true;
}

bool ensureDirectory(const char *path) {
  return SD.exists(path) || SD.mkdir(path);
}

void refreshCapacityLocked(bool force = false) {
  if (!g_mounted) {
    return;
  }
  const uint32_t now = millis();
  if (!force && now - g_lastCapacityRefresh < kCapacityRefreshMs) {
    return;
  }
  g_lastCapacityRefresh = now;
  g_totalBytes = SD.totalBytes();
  g_usedBytes = SD.usedBytes();
  const uint64_t freeBytes =
      g_totalBytes > g_usedBytes ? g_totalBytes - g_usedBytes : 0;
  g_storageLow =
      freeBytes < static_cast<uint64_t>(HOME_CAM_SD_MIN_FREE_MB) * 1024ULL *
                      1024ULL;
}

bool makeUniquePath(const char *directory, const char *prefix,
                    const char *extension, char *output, size_t outputSize) {
  char stamp[32];
  const time_t now = time(nullptr);
  struct tm timeInfo = {};
  if (now > 1700000000 && localtime_r(&now, &timeInfo) != nullptr) {
    strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &timeInfo);
  } else {
    const uint32_t chipId =
        static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFF);
    snprintf(stamp, sizeof(stamp), "BOOT_%06lX_%010lu",
             static_cast<unsigned long>(chipId),
             static_cast<unsigned long>(millis()));
  }

  for (uint8_t sequence = 0; sequence < 100; ++sequence) {
    snprintf(output, outputSize, "%s/%s_%s_%02u.%s", directory, prefix,
             stamp, sequence, extension);
    if (!SD.exists(output)) {
      return true;
    }
  }
  return false;
}

bool updateAudioHeaderLocked(bool flushFile) {
  if (!g_audioFile) {
    return false;
  }
  const size_t endPosition = sizeof(WavHeader) + g_audioDataBytes;
  const WavHeader header = makeWavHeader(g_audioDataBytes);
  if (!g_audioFile.seek(0) ||
      g_audioFile.write(reinterpret_cast<const uint8_t *>(&header),
                        sizeof(header)) != sizeof(header) ||
      !g_audioFile.seek(endPosition)) {
    ++g_writeErrors;
    return false;
  }
  if (flushFile) {
    g_audioFile.flush();
  }
  g_lastHeaderRefresh = millis();
  return true;
}

void closeAudioSegmentLocked() {
  if (!g_audioFile) {
    return;
  }
  updateAudioHeaderLocked(true);
  g_audioFile.close();
  g_audioRecording = false;
  g_currentAudioFile[0] = '\0';
  if (g_audioDataBytes > 0) {
    ++g_audioSegmentsCompleted;
  }
  g_audioDataBytes = 0;
}

bool openAudioSegmentLocked() {
  refreshCapacityLocked(true);
  if (!g_mounted || g_storageLow ||
      !makeUniquePath(kAudioDirectory, "AUD", "WAV", g_currentAudioFile,
                      sizeof(g_currentAudioFile))) {
    return false;
  }

  g_audioFile = SD.open(g_currentAudioFile, FILE_WRITE);
  if (!g_audioFile) {
    ++g_writeErrors;
    g_currentAudioFile[0] = '\0';
    return false;
  }
  g_audioFile.setBufferSize(8192);
  g_audioDataBytes = 0;
  const WavHeader header = makeWavHeader(0);
  if (g_audioFile.write(reinterpret_cast<const uint8_t *>(&header),
                        sizeof(header)) != sizeof(header)) {
    ++g_writeErrors;
    g_audioFile.close();
    g_currentAudioFile[0] = '\0';
    return false;
  }
  g_audioRecording = true;
  g_lastHeaderRefresh = millis();
  Serial.printf("Automatic audio recording: %s\n", g_currentAudioFile);
  return true;
}

}  // namespace

bool storageRecorderBegin() {
  if (g_mounted) {
    return true;
  }
  g_sdMutex = xSemaphoreCreateMutex();
  if (g_sdMutex == nullptr) {
    Serial.println("microSD: could not create storage mutex");
    return false;
  }

  if (!mountCardAt(HOME_CAM_SD_CS_PRIMARY)) {
    Serial.printf("microSD: no card on CS GPIO%u; trying legacy GPIO%u\n",
                  HOME_CAM_SD_CS_PRIMARY, HOME_CAM_SD_CS_LEGACY);
    if (!mountCardAt(HOME_CAM_SD_CS_LEGACY)) {
      Serial.println("microSD unavailable; dashboard recording still works");
      return false;
    }
  }

  if (!ensureDirectory(kRootDirectory) || !ensureDirectory(kAudioDirectory) ||
      !ensureDirectory(kImageDirectory)) {
    Serial.println("microSD: could not create /homecam directories");
    SD.end();
    g_sdSpi.end();
    return false;
  }

  g_mounted = true;
  refreshCapacityLocked(true);
  Serial.printf("microSD mounted on CS GPIO%u: %llu MB total\n",
                g_chipSelectPin,
                static_cast<unsigned long long>(g_totalBytes / 1024 / 1024));
  return true;
}

bool storageRecorderReady() { return g_mounted; }

void storageRecorderAppendAudio(const int16_t *samples, size_t sampleCount) {
#if !HOME_CAM_AUTO_RECORD_AUDIO
  (void)samples;
  (void)sampleCount;
  return;
#else
  if (!g_mounted || samples == nullptr || sampleCount == 0 ||
      xSemaphoreTake(g_sdMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }

  refreshCapacityLocked();
  if (g_storageLow) {
    closeAudioSegmentLocked();
    xSemaphoreGive(g_sdMutex);
    return;
  }
  if (!g_audioFile && !openAudioSegmentLocked()) {
    xSemaphoreGive(g_sdMutex);
    return;
  }

  const size_t byteCount = sampleCount * sizeof(int16_t);
  const size_t written = g_audioFile.write(
      reinterpret_cast<const uint8_t *>(samples), byteCount);
  if (written != byteCount) {
    ++g_writeErrors;
    closeAudioSegmentLocked();
    xSemaphoreGive(g_sdMutex);
    return;
  }
  g_audioDataBytes += written;

  const uint32_t segmentBytes =
      HOME_CAM_AUDIO_SEGMENT_SECONDS * HOME_CAM_AUDIO_SAMPLE_RATE *
      sizeof(int16_t);
  if (g_audioDataBytes >= segmentBytes) {
    closeAudioSegmentLocked();
  } else if (millis() - g_lastHeaderRefresh >= kHeaderRefreshMs) {
    updateAudioHeaderLocked(true);
  }
  xSemaphoreGive(g_sdMutex);
#endif
}

bool storageRecorderSaveJpeg(const uint8_t *data, size_t length) {
  if (!g_mounted || data == nullptr || length == 0 ||
      xSemaphoreTake(g_sdMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return false;
  }

  refreshCapacityLocked();
  if (g_storageLow ||
      !makeUniquePath(kImageDirectory, "IMG", "JPG", g_lastPhotoFile,
                      sizeof(g_lastPhotoFile))) {
    xSemaphoreGive(g_sdMutex);
    return false;
  }

  File image = SD.open(g_lastPhotoFile, FILE_WRITE);
  const bool saved =
      image && image.write(data, length) == length;
  if (image) {
    image.close();
  }
  if (saved) {
    ++g_photosSaved;
  } else {
    ++g_writeErrors;
    g_lastPhotoFile[0] = '\0';
  }
  xSemaphoreGive(g_sdMutex);
  return saved;
}

void storageRecorderGetStatus(StorageRecorderStatus *status) {
  if (status == nullptr) {
    return;
  }
  memset(status, 0, sizeof(*status));

  if (g_sdMutex != nullptr &&
      xSemaphoreTake(g_sdMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    refreshCapacityLocked();
    status->mounted = g_mounted;
    status->audioRecording = g_audioRecording;
    status->storageLow = g_storageLow;
    status->chipSelectPin = g_chipSelectPin;
    status->totalBytes = g_totalBytes;
    status->usedBytes = g_usedBytes;
    status->audioSegmentsCompleted = g_audioSegmentsCompleted;
    status->photosSaved = g_photosSaved;
    status->writeErrors = g_writeErrors;
    snprintf(status->currentAudioFile, sizeof(status->currentAudioFile), "%s",
             g_currentAudioFile);
    snprintf(status->lastPhotoFile, sizeof(status->lastPhotoFile), "%s",
             g_lastPhotoFile);
    xSemaphoreGive(g_sdMutex);
    return;
  }

  status->mounted = g_mounted;
  status->audioRecording = g_audioRecording;
  status->storageLow = g_storageLow;
  status->writeErrors = g_writeErrors;
}

