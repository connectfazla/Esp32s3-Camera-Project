# XIAO ESP32S3 Sense OV3660 HomeCam

Arduino IDE starter firmware for the Seeed Studio XIAO ESP32S3 Sense with the
OV3660 camera. It provides:

- Bluetooth phone provisioning for 2.4 GHz Wi-Fi
- Proof-of-possession protected provisioning (Security 1 in Arduino Core 3.x)
- Unique provisioning and network names based on the board ID
- VGA JPEG live camera view on port 81
- JPEG snapshot endpoint
- PDM microphone level measurement
- Three-second WAV recording endpoint
- A phone-friendly local dashboard
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
4. Open `XIAO_ESP32S3_OV3660_HomeCam.ino`.
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
| `http://DEVICE/audio.wav` | Record/download a three-second WAV clip |
| `http://DEVICE:81/stream` | MJPEG live camera view |

Only one live MJPEG viewer is recommended for this MVP.

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

## Next step for outside-home access

The safest personal setup is an always-on Raspberry Pi on the same home LAN:

1. The Pi reads the ESP32 dashboard/stream locally.
2. Tailscale is installed on the Pi and phone.
3. A small authenticated reverse proxy exposes the camera only inside the
   private Tailscale network.

That gateway should also terminate HTTPS, enforce login, limit sessions and
optionally record clips. The ESP32 should never be placed directly on the
public internet.
