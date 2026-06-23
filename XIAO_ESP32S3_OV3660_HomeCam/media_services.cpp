#include "media_services.h"

#include <ESP_I2S.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <math.h>

#include "camera_pins.h"
#include "config.h"

namespace {

constexpr char kStreamBoundary[] = "homecam-frame-boundary";
constexpr char kStreamContentType[] =
    "multipart/x-mixed-replace;boundary=homecam-frame-boundary";

const char kDashboardHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>XIAO HomeCam</title>
  <style>
    :root{color-scheme:dark;font-family:system-ui,sans-serif;background:#101417;color:#ecf1f4}
    body{max-width:900px;margin:auto;padding:20px}
    h1{font-size:1.5rem}.card{background:#192126;border:1px solid #30404a;border-radius:14px;padding:16px;margin:14px 0}
    img{display:block;width:100%;height:auto;background:#080a0b;border-radius:10px}
    button,a.button{display:inline-block;background:#50c878;color:#07120b;border:0;border-radius:9px;padding:11px 15px;margin:5px 5px 5px 0;text-decoration:none;font-weight:700;cursor:pointer}
    code{color:#9de8b4}.muted{color:#aab7be;font-size:.92rem}
  </style>
</head>
<body>
  <h1>XIAO ESP32S3 Sense · OV3660</h1>
  <div class="card">
    <img id="stream" alt="Live camera stream">
    <p>
      <button onclick="startStream()">Start live view</button>
      <button onclick="stopStream()">Stop</button>
      <a class="button" href="/capture" target="_blank">Take photo</a>
    </p>
  </div>
  <div class="card">
    <h2>Microphone</h2>
    <p>Level: <code id="mic">waiting…</code></p>
    <a class="button" href="/audio.wav">Record 3-second WAV</a>
    <p class="muted">Recording briefly pauses the level meter. This endpoint is intended for testing and short clips.</p>
  </div>
  <div class="card">
    <h2>Device</h2>
    <pre id="status">waiting…</pre>
    <p class="muted">This page is local-network only. Do not port-forward it directly to the internet; put authentication and TLS on a gateway or cloud relay.</p>
  </div>
<script>
const stream=document.getElementById('stream');
function startStream(){stream.src=`http://${location.hostname}:81/stream?t=${Date.now()}`}
function stopStream(){stream.removeAttribute('src')}
async function refresh(){
  try{
    const s=await (await fetch('/status',{cache:'no-store'})).json();
    document.getElementById('status').textContent=JSON.stringify(s,null,2);
    document.getElementById('mic').textContent=`RMS ${s.microphone_rms} · peak ${s.microphone_peak}`;
  }catch(e){document.getElementById('status').textContent='Device unavailable'}
}
setInterval(refresh,2000);refresh();startStream();
</script>
</body>
</html>
)HTML";

I2SClass g_microphone;
SemaphoreHandle_t g_audioMutex = nullptr;
httpd_handle_t g_controlServer = nullptr;
httpd_handle_t g_streamServer = nullptr;
volatile uint32_t g_micRms = 0;
volatile uint32_t g_micPeak = 0;
bool g_servicesStarted = false;

const char *cameraName() {
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor == nullptr) {
    return "not-detected";
  }
  switch (sensor->id.PID) {
    case OV3660_PID: return "OV3660";
    case OV2640_PID: return "OV2640";
    case OV5640_PID: return "OV5640";
    default: return "unknown";
  }
}

esp_err_t rootHandler(httpd_req_t *request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, kDashboardHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t statusHandler(httpd_req_t *request) {
  char json[420];
  const int length = snprintf(
      json, sizeof(json),
      "{\"camera\":\"%s\",\"ip\":\"%s\",\"wifi_rssi_dbm\":%d,"
      "\"uptime_seconds\":%lu,\"free_heap_bytes\":%lu,"
      "\"free_psram_bytes\":%lu,\"microphone_rms\":%lu,"
      "\"microphone_peak\":%lu}",
      cameraName(), WiFi.localIP().toString().c_str(), WiFi.RSSI(),
      static_cast<unsigned long>(millis() / 1000),
      static_cast<unsigned long>(ESP.getFreeHeap()),
      static_cast<unsigned long>(ESP.getFreePsram()),
      static_cast<unsigned long>(g_micRms),
      static_cast<unsigned long>(g_micPeak));

  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, json, length);
}

esp_err_t captureHandler(httpd_req_t *request) {
  camera_fb_t *frame = esp_camera_fb_get();
  if (frame == nullptr) {
    httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Camera capture failed");
    return ESP_FAIL;
  }

  if (frame->format != PIXFORMAT_JPEG) {
    esp_camera_fb_return(frame);
    httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Camera is not in JPEG mode");
    return ESP_FAIL;
  }

  char disposition[64];
  snprintf(disposition, sizeof(disposition),
           "inline; filename=homecam-%lu.jpg",
           static_cast<unsigned long>(millis()));
  httpd_resp_set_type(request, "image/jpeg");
  httpd_resp_set_hdr(request, "Content-Disposition", disposition);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  const esp_err_t result =
      httpd_resp_send(request, reinterpret_cast<const char *>(frame->buf),
                      frame->len);
  esp_camera_fb_return(frame);
  return result;
}

esp_err_t streamHandler(httpd_req_t *request) {
  esp_err_t result = httpd_resp_set_type(request, kStreamContentType);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");

  while (result == ESP_OK) {
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr) {
      return ESP_FAIL;
    }

    if (frame->format != PIXFORMAT_JPEG) {
      esp_camera_fb_return(frame);
      return ESP_FAIL;
    }

    char header[128];
    const int headerLength = snprintf(
        header, sizeof(header),
        "\r\n--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
        kStreamBoundary, static_cast<unsigned int>(frame->len));

    result = httpd_resp_send_chunk(request, header, headerLength);
    if (result == ESP_OK) {
      result = httpd_resp_send_chunk(
          request, reinterpret_cast<const char *>(frame->buf), frame->len);
    }
    esp_camera_fb_return(frame);

    // A small yield keeps Wi-Fi, provisioning and audio responsive.
    vTaskDelay(pdMS_TO_TICKS(8));
  }

  return result;
}

esp_err_t audioHandler(httpd_req_t *request) {
  if (g_audioMutex == nullptr ||
      xSemaphoreTake(g_audioMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    httpd_resp_set_status(request, "503 Service Unavailable");
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_sendstr(request, "Microphone busy; try again");
    return ESP_FAIL;
  }

  size_t wavSize = 0;
  uint8_t *wav =
      g_microphone.recordWAV(HOME_CAM_AUDIO_CLIP_SECONDS, &wavSize);
  xSemaphoreGive(g_audioMutex);

  if (wav == nullptr || wavSize == 0) {
    free(wav);
    httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Audio recording failed");
    return ESP_FAIL;
  }

  httpd_resp_set_type(request, "audio/wav");
  httpd_resp_set_hdr(request, "Content-Disposition",
                     "attachment; filename=homecam-audio.wav");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  const esp_err_t result = httpd_resp_send(
      request, reinterpret_cast<const char *>(wav), wavSize);
  free(wav);
  return result;
}

void audioMeterTask(void *) {
  constexpr size_t kSampleCount = 512;
  int16_t samples[kSampleCount];

  while (true) {
    if (xSemaphoreTake(g_audioMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    const size_t bytes = g_microphone.readBytes(
        reinterpret_cast<char *>(samples), sizeof(samples));
    xSemaphoreGive(g_audioMutex);

    const size_t count = bytes / sizeof(int16_t);
    if (count == 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    uint64_t sumSquares = 0;
    uint32_t peak = 0;
    for (size_t index = 0; index < count; ++index) {
      const int32_t sample = samples[index];
      const uint32_t magnitude = sample < 0 ? -sample : sample;
      sumSquares += static_cast<int64_t>(sample) * sample;
      if (magnitude > peak) {
        peak = magnitude;
      }
    }

    const uint32_t rms =
        static_cast<uint32_t>(sqrt(static_cast<double>(sumSquares) / count));
    // Light smoothing prevents a nervous-looking meter.
    g_micRms = (g_micRms * 3 + rms) / 4;
    g_micPeak = peak;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

bool startCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d7 = CAM_PIN_D7;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = HOME_CAM_FRAME_SIZE;
  config.jpeg_quality = HOME_CAM_JPEG_QUALITY;
  config.fb_count = psramFound() ? 2 : 1;
  config.fb_location =
      psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode =
      psramFound() ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;

  const esp_err_t error = esp_camera_init(&config);
  if (error != ESP_OK) {
    Serial.printf("Camera initialization failed: 0x%x\n", error);
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != nullptr && sensor->id.PID == OV3660_PID) {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 1);
    sensor->set_saturation(sensor, -2);
  }

  Serial.printf("Camera detected: %s\n", cameraName());
  return true;
}

bool startMicrophone() {
  g_audioMutex = xSemaphoreCreateMutex();
  if (g_audioMutex == nullptr) {
    Serial.println("Could not create microphone mutex");
    return false;
  }

  g_microphone.setPinsPdmRx(MIC_PIN_CLK, MIC_PIN_DATA);
  if (!g_microphone.begin(I2S_MODE_PDM_RX, HOME_CAM_AUDIO_SAMPLE_RATE,
                          I2S_DATA_BIT_WIDTH_16BIT,
                          I2S_SLOT_MODE_MONO)) {
    Serial.printf("Microphone initialization failed: %d\n",
                  g_microphone.lastError());
    return false;
  }

  if (xTaskCreatePinnedToCore(audioMeterTask, "audio-meter", 4096, nullptr, 1,
                              nullptr, 0) != pdPASS) {
    Serial.println("Could not start microphone task");
    return false;
  }
  Serial.println("PDM microphone ready");
  return true;
}

bool registerControlHandlers() {
  httpd_uri_t root = {};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = rootHandler;

  httpd_uri_t status = {};
  status.uri = "/status";
  status.method = HTTP_GET;
  status.handler = statusHandler;

  httpd_uri_t capture = {};
  capture.uri = "/capture";
  capture.method = HTTP_GET;
  capture.handler = captureHandler;

  httpd_uri_t audio = {};
  audio.uri = "/audio.wav";
  audio.method = HTTP_GET;
  audio.handler = audioHandler;

  return httpd_register_uri_handler(g_controlServer, &root) == ESP_OK &&
         httpd_register_uri_handler(g_controlServer, &status) == ESP_OK &&
         httpd_register_uri_handler(g_controlServer, &capture) == ESP_OK &&
         httpd_register_uri_handler(g_controlServer, &audio) == ESP_OK;
}

bool startWebServers() {
  httpd_config_t controlConfig = HTTPD_DEFAULT_CONFIG();
  controlConfig.server_port = 80;
  controlConfig.max_uri_handlers = 8;
  controlConfig.stack_size = 8192;
  if (httpd_start(&g_controlServer, &controlConfig) != ESP_OK ||
      !registerControlHandlers()) {
    Serial.println("Could not start control web server");
    return false;
  }

  httpd_config_t streamConfig = HTTPD_DEFAULT_CONFIG();
  streamConfig.server_port = 81;
  streamConfig.ctrl_port = controlConfig.ctrl_port + 1;
  streamConfig.stack_size = 8192;
  if (httpd_start(&g_streamServer, &streamConfig) != ESP_OK) {
    Serial.println("Could not start stream web server");
    return false;
  }

  httpd_uri_t stream = {};
  stream.uri = "/stream";
  stream.method = HTTP_GET;
  stream.handler = streamHandler;
  if (httpd_register_uri_handler(g_streamServer, &stream) != ESP_OK) {
    Serial.println("Could not register stream endpoint");
    return false;
  }
  return true;
}

}  // namespace

bool startMediaServices(const char *hostname) {
  if (g_servicesStarted) {
    return true;
  }
  if (!psramFound()) {
    Serial.println("Warning: PSRAM was not detected; streaming may be unstable");
  }
  if (!startCamera() || !startMicrophone() || !startWebServers()) {
    return false;
  }

  if (MDNS.begin(hostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS: http://%s.local/\n", hostname);
  }

  g_servicesStarted = true;
  Serial.printf("Dashboard: http://%s/\n", WiFi.localIP().toString().c_str());
  Serial.printf("Stream: http://%s:81/stream\n",
                WiFi.localIP().toString().c_str());
  return true;
}

bool mediaServicesStarted() { return g_servicesStarted; }

uint32_t microphoneRms() { return g_micRms; }

uint32_t microphonePeak() { return g_micPeak; }
