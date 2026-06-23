# XIAO ESP32S3 Sense OV3660 HomeCam

Arduino IDE starter firmware for the Seeed Studio XIAO ESP32S3 Sense with the
OV3660 camera. It provides:

- Bluetooth phone provisioning for 2.4 GHz Wi-Fi
- Proof-of-possession protected provisioning (Security 1 in Arduino Core 3.x)
- Unique provisioning and network names based on the board ID
- VGA JPEG live camera view on port 81
- JPEG snapshot endpoint
- PDM microphone level measurement
- Reliable three-second WAV downloads from a PSRAM circular buffer
- Continuous five-minute WAV segments on microSD
- Automatic JPEG snapshots on microSD every 30 seconds
- Automatic support for current GPIO3 and legacy GPIO21 SD chip-select wiring
- A responsive 90s green-phosphor terminal dashboard
- Automatic Wi-Fi reconnect
- BOOT-button factory reset

This is the device-side MVP. Its web endpoints do **not** have internet-grade
authentication. Do not forward ports 80 or 81 from your router. For access
outside the home, place it behind an authenticated Raspberry Pi/Tailscale
gateway or connect it to a separate TLS cloud relay.

## Arduino IDE setup

1. Install Arduino IDE 2.x.
2. In **Preferences → Additional boards manager URLs**, add:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. In **Boards Manager**, install **esp32 by Espressif Systems 3.3.8** or a
   later compatible 3.x release.
4. Open
   `XIAO_ESP32S3_OV3660_HomeCam/XIAO_ESP32S3_OV3660_HomeCam.ino`.
5. Change `HOME_CAM_PROVISIONING_POP` in `config.h`. Use a private random code.
6. Select **XIAO_ESP32S3** as the board.
7. Select settings that enable **8 MB PSRAM** and an **8 MB flash partition
   with at least a 3 MB application slot**. Avoid a partition layout with a
   very small application slot.
8. Connect the external antenna before testing Wi-Fi or Bluetooth.
9. Upload, then open Serial Monitor at **115200 baud**.

If upload does not begin, hold **BOOT**, tap **RESET**, release **BOOT**, and
try the upload again.

## Provision Wi-Fi from the phone

1. Install Espressif's **ESP BLE Provisioning** / **ESP Provisioning** app.
2. Reset the board and wait for `Bluetooth Wi-Fi provisioning started`.
3. Scan the QR code printed in Serial Monitor, or select the device named
   `PROV_xxxxxx`.
4. Enter the proof-of-possession value from `config.h`.
5. Select the home's **2.4 GHz** Wi-Fi and enter its password.

When provisioning completes, Serial Monitor prints the dashboard address:

```text
http://192.168.x.x/
```

On many networks this name also works:

```text
http://xiao-homecam-xxxxxx.local/
```

## Endpoints

| Endpoint | Purpose |
|---|---|
| `http://DEVICE/` | Dashboard |
| `http://DEVICE/capture` | Current JPEG photo |
| `http://DEVICE/status` | Device, Wi-Fi and microphone JSON |
| `http://DEVICE/audio.wav` | Download the most recent three seconds as WAV |
| `http://DEVICE:81/stream` | MJPEG live camera view |

Only one live MJPEG viewer is recommended for this MVP.

## Automatic microSD recording

Insert a FAT32 microSD card before boot. The firmware tries SD chip select
GPIO3 first for current OV3660 Sense boards, then GPIO21 for older boards.
Recordings start automatically after the microphone initializes:

```text
/homecam/audio/AUD_YYYYMMDD_HHMMSS_00.WAV
/homecam/images/IMG_YYYYMMDD_HHMMSS_00.JPG
```

Audio is written continuously as standard mono, 16 kHz, 16-bit PCM WAV files.
Files are split every five minutes, and the active WAV header is repaired and
flushed every five seconds so a sudden power loss sacrifices as little as
possible. Photos are saved every 30 seconds.

The recorder stops when less than 256 MB remains. It does not silently delete
old recordings. Recording intervals and the low-space threshold are adjustable
in `config.h`.

The dashboard's audio button no longer starts a second competing I2S read.
Instead, one capture task continuously fills a five-second PSRAM ring buffer,
feeds the level meter and writes the SD card. The endpoint copies the latest
three seconds from that buffer into a valid WAV file.

## Factory reset

After the board has booted normally, hold the **BOOT** button for eight
seconds. This erases saved provisioning data and restarts the board. Do not
hold BOOT while powering on; that enters firmware-upload mode.

## OV3660 image orientation

The sketch applies the standard OV3660 vertical-flip, brightness and
saturation corrections used by Espressif's camera example. If your particular
camera is mounted differently, change these calls in `startCamera()` inside
`media_services.cpp`:

```cpp
sensor->set_vflip(sensor, 1);
sensor->set_brightness(sensor, 1);
sensor->set_saturation(sensor, -2);
```

## Secure outside-home access with Tailscale

Tailscale does not run inside this Arduino firmware. Use an always-on Raspberry
Pi or other Linux machine on the same LAN:

1. The Pi reads the ESP32 dashboard/stream locally.
2. Tailscale is installed on the Pi and phone.
3. The included Nginx configuration merges the dashboard on port 80 and MJPEG
   stream on port 81 into one localhost service.
4. Tailscale Serve publishes that localhost service over private HTTPS only to
   your tailnet.

Follow [gateway/README.md](gateway/README.md). Do not enable Tailscale Funnel,
and do not port-forward the ESP32. The ESP32 should never be placed directly on
the public internet.
