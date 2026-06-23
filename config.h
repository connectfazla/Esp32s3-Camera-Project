#pragma once

// Change this before flashing. It is the proof-of-possession code entered by
// the owner during Bluetooth Wi-Fi provisioning.
#define HOME_CAM_PROVISIONING_POP "change-me-7391"

// Conservative defaults so the OV3660, Wi-Fi and microphone can run together.
#define HOME_CAM_FRAME_SIZE FRAMESIZE_VGA
#define HOME_CAM_JPEG_QUALITY 12
#define HOME_CAM_AUDIO_SAMPLE_RATE 16000
#define HOME_CAM_AUDIO_CLIP_SECONDS 3

// Hold the BOOT button for this long after startup to erase Wi-Fi settings.
#define HOME_CAM_FACTORY_RESET_MS 8000

