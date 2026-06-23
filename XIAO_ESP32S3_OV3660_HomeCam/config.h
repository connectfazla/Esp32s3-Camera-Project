#pragma once

// Change this before flashing. It is the proof-of-possession code entered by
// the owner during Bluetooth Wi-Fi provisioning.
#define HOME_CAM_PROVISIONING_POP "change-me-7391"

// Conservative defaults so the OV3660, Wi-Fi and microphone can run together.
#define HOME_CAM_FRAME_SIZE FRAMESIZE_VGA
#define HOME_CAM_JPEG_QUALITY 12
#define HOME_CAM_AUDIO_SAMPLE_RATE 16000

// The dashboard downloads the most recent audio from a PSRAM ring buffer.
// Only the audio capture task reads I2S, preventing competing reads and failed
// WAV recordings.
#define HOME_CAM_AUDIO_RING_SECONDS 5
#define HOME_CAM_AUDIO_CLIP_SECONDS 3

// Automatic microSD recording. Audio is continuous PCM WAV split into segments;
// the camera saves periodic JPEG snapshots. This is intentionally not fake AVI:
// every file written by the firmware is independently playable/viewable.
#define HOME_CAM_AUTO_RECORD_AUDIO 1
#define HOME_CAM_AUDIO_SEGMENT_SECONDS 300
#define HOME_CAM_AUTO_SNAPSHOT_SECONDS 30

// Current OV3660 Sense boards use GPIO3 for SD CS. Older Sense boards used
// GPIO21; the firmware tries both automatically.
#define HOME_CAM_SD_CS_PRIMARY 3
#define HOME_CAM_SD_CS_LEGACY 21
#define HOME_CAM_SD_SPI_HZ 10000000

// Stop safely instead of overwriting recordings when the card is nearly full.
#define HOME_CAM_SD_MIN_FREE_MB 256

// Hold the BOOT button for this long after startup to erase Wi-Fi settings.
#define HOME_CAM_FACTORY_RESET_MS 8000
