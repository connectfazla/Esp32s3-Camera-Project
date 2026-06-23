#include "media_services.h"

#include <ESP_I2S.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <math.h>

#include "camera_pins.h"
#include "config.h"
#include "storage_recorder.h"

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
  <meta name="theme-color" content="#020603">
  <title>HOME_CAM // ROOT CONSOLE</title>
  <style>
    :root{
      color-scheme:dark;--bg:#020603;--panel:#061008;--phosphor:#73ff79;
      --bright:#c5ffc8;--dim:#45a94c;--line:#1b6b25;--amber:#ffd166;
      --danger:#ff6b6b;--shadow:rgba(83,255,101,.24);
      font-family:"Courier New",Courier,monospace
    }
    *{box-sizing:border-box}html{background:var(--bg);scroll-behavior:smooth}
    body{margin:0;min-height:100dvh;background:
      radial-gradient(circle at 50% -20%,#0a2b11 0,transparent 46%),var(--bg);
      color:var(--phosphor);font-size:16px;line-height:1.55;text-shadow:0 0 7px var(--shadow)}
    body::after{content:"";position:fixed;inset:0;z-index:20;pointer-events:none;
      background:repeating-linear-gradient(0deg,rgba(0,0,0,.14) 0,rgba(0,0,0,.14) 1px,transparent 1px,transparent 4px)}
    .skip{position:absolute;left:8px;top:-80px;background:var(--phosphor);color:#001503;padding:12px;z-index:30}
    .skip:focus{top:8px}.shell{width:min(1120px,100%);margin:auto;padding:16px}
    header{border:1px solid var(--line);background:rgba(3,14,6,.94);padding:16px;margin-bottom:16px;box-shadow:0 0 22px rgba(38,180,59,.12)}
    .logo{margin:0;white-space:pre;overflow:hidden;color:var(--bright);font-size:clamp(10px,2vw,15px);line-height:1.05}
    .subline{display:flex;flex-wrap:wrap;gap:8px 20px;margin-top:14px;color:var(--dim);font-size:.88rem;text-transform:uppercase}
    .blink{animation:blink 1.1s steps(1,end) infinite}@keyframes blink{50%{opacity:.18}}
    .grid{display:grid;gap:16px}.panel{position:relative;border:1px solid var(--line);background:rgba(4,15,7,.92);padding:16px;min-width:0;box-shadow:inset 0 0 25px rgba(20,90,31,.1)}
    .panel::before{content:attr(data-label);position:absolute;top:-11px;left:12px;background:var(--bg);padding:0 8px;color:var(--bright);font-weight:700;letter-spacing:.08em}
    .camera{aspect-ratio:4/3;width:100%;display:block;object-fit:contain;background:#000;border:1px solid var(--line);image-rendering:auto}
    .actions{display:flex;flex-wrap:wrap;gap:10px;margin-top:14px}
    button,.button{appearance:none;min-height:46px;border:1px solid var(--phosphor);border-radius:0;background:#061809;color:var(--bright);padding:10px 14px;font:700 .9rem/1.2 inherit;letter-spacing:.04em;text-transform:uppercase;text-decoration:none;cursor:pointer;box-shadow:3px 3px 0 #123d18;touch-action:manipulation}
    button:hover,.button:hover{background:#0b2a11}button:active,.button:active{transform:translate(2px,2px);box-shadow:1px 1px 0 #123d18}
    button:focus-visible,.button:focus-visible{outline:3px solid var(--amber);outline-offset:3px}button:disabled{opacity:.45;cursor:not-allowed;transform:none}
    .readout{display:grid;grid-template-columns:minmax(130px,.8fr) minmax(0,1.2fr);gap:6px 12px;margin:0}.readout dt{color:var(--dim);text-transform:uppercase}.readout dd{margin:0;color:var(--bright);overflow-wrap:anywhere}
    .meter{height:14px;border:1px solid var(--line);margin-top:10px;background:#010301}.meter>span{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--phosphor),var(--amber),var(--danger));transition:width .18s ease-out}
    .terminal{min-height:144px;margin:0;padding:12px;border:1px dashed var(--line);background:#010402;color:var(--dim);white-space:pre-wrap;overflow-wrap:anywhere}
    .ok{color:var(--phosphor)}.warn{color:var(--amber)}.error{color:var(--danger)}
    .storagebar{height:20px;border:1px solid var(--line);background:#010301;position:relative;margin:8px 0}.storagebar span{display:block;height:100%;width:0;background:repeating-linear-gradient(90deg,var(--dim) 0,var(--dim) 8px,#0b2810 8px,#0b2810 11px)}
    .storagebar output{position:absolute;inset:0;text-align:center;color:var(--bright);font-size:.78rem;line-height:18px}
    footer{color:var(--dim);padding:18px 2px 4px;font-size:.82rem}.cursor::after{content:"_";color:var(--bright);animation:blink 1.1s steps(1,end) infinite}
    @media(min-width:820px){.grid{grid-template-columns:minmax(0,1.65fr) minmax(300px,.85fr)}.wide{grid-column:1/-1}.shell{padding:28px}}
    @media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}.blink,.cursor::after{animation:none}.meter>span{transition:none}}
  </style>
</head>
<body>
  <a class="skip" href="#main">Skip to console</a>
  <div class="shell">
    <header>
<pre class="logo" aria-label="HomeCam root console">
 _  _  ___  __  __ ___    ___   _   __  __
| || |/ _ \|  \/  | __|  / __| /_\ |  \/  |
| __ | (_) | |\/| | _|  | (__ / _ \| |\/| |
|_||_|\___/|_|  |_|___|  \___/_/ \_\_|  |_|
</pre>
      <div class="subline"><span>NODE: XIAO_ESP32S3</span><span>OPTIC: OV3660</span><span>SESSION: <b class="ok">ENCRYPTED_LAN</b></span><span class="blink">● LINK ACTIVE</span></div>
    </header>
    <main id="main" class="grid">
      <section class="panel" data-label="[01] LIVE OPTICAL FEED">
        <img id="stream" class="camera" alt="Live video from the HomeCam OV3660 camera">
        <div class="actions">
          <button type="button" onclick="startStream()">[ RUN FEED ]</button>
          <button type="button" onclick="stopStream()">[ HALT FEED ]</button>
          <a class="button" href="/capture" target="_blank" rel="noopener">[ SNAPSHOT ]</a>
        </div>
      </section>
      <section class="panel" data-label="[02] SYSTEM TELEMETRY">
        <dl class="readout">
          <dt>camera</dt><dd id="camera">QUERYING</dd>
          <dt>network</dt><dd id="network">QUERYING</dd>
          <dt>signal</dt><dd id="signal">QUERYING</dd>
          <dt>microphone</dt><dd id="micText">QUERYING</dd>
          <dt>auto record</dt><dd id="recording">QUERYING</dd>
          <dt>sd interface</dt><dd id="sdState">QUERYING</dd>
        </dl>
        <div class="meter" aria-label="Microphone input level"><span id="micMeter"></span></div>
        <div class="actions">
          <button id="audioButton" type="button" onclick="downloadAudio(this)">[ GET LAST 3S WAV ]</button>
        </div>
        <p id="audioFeedback" class="ok" role="status" aria-live="polite">AUDIO BUFFER STANDBY</p>
      </section>
      <section class="panel wide" data-label="[03] EXTERNAL STORAGE">
        <div class="storagebar"><span id="storageFill"></span><output id="storageText">SCANNING MEDIA...</output></div>
        <dl class="readout">
          <dt>active audio</dt><dd id="audioFile">NONE</dd>
          <dt>latest image</dt><dd id="photoFile">NONE</dd>
          <dt>segments</dt><dd id="segments">0</dd>
          <dt>snapshots</dt><dd id="photos">0</dd>
          <dt>write faults</dt><dd id="errors">0</dd>
        </dl>
      </section>
      <section class="panel wide" data-label="[04] RAW STATUS CHANNEL">
        <pre id="status" class="terminal">BOOTSTRAP&gt; acquiring node telemetry...</pre>
      </section>
    </main>
    <footer>ROOT@HOME_CAM:~$ <span class="cursor">monitor --continuous</span></footer>
  </div>
<script>
const stream=document.getElementById('stream');
const $=id=>document.getElementById(id);
function streamUrl(){return location.protocol==='https:'?'/stream':`http://${location.hostname}:81/stream`}
function startStream(){stream.src=`${streamUrl()}?t=${Date.now()}`}
function stopStream(){stream.removeAttribute('src')}
function mb(value){return (Number(value||0)/1048576).toFixed(1)}
async function downloadAudio(button){
  button.disabled=true;$('audioFeedback').textContent='CAPTURE> assembling recent PCM buffer...';
  try{
    const response=await fetch('/audio.wav',{cache:'no-store'});
    if(!response.ok)throw new Error(await response.text());
    const blob=await response.blob();const url=URL.createObjectURL(blob);
    const link=document.createElement('a');link.href=url;link.download=`homecam_${Date.now()}.wav`;link.click();
    setTimeout(()=>URL.revokeObjectURL(url),1000);$('audioFeedback').textContent=`CAPTURE> WAV READY (${Math.round(blob.size/1024)} KiB)`;
  }catch(error){$('audioFeedback').textContent=`FAULT> ${error.message}`;$('audioFeedback').className='error'}
  finally{button.disabled=false}
}
async function refresh(){
  try{
    const s=await (await fetch('/status',{cache:'no-store'})).json();
    $('camera').textContent=s.camera;
    $('network').textContent=s.ip;
    $('signal').textContent=`${s.wifi_rssi_dbm} dBm`;
    $('micText').textContent=`RMS ${s.microphone_rms} / PEAK ${s.microphone_peak}`;
    $('micMeter').style.width=`${Math.min(100,Math.round((s.microphone_peak/32767)*100))}%`;
    $('recording').textContent=s.sd_audio_recording?'[REC] CONTINUOUS WAV':(s.sd_mounted?'ARMED / WAITING':'NO MEDIA');
    $('recording').className=s.sd_audio_recording?'error':(s.sd_mounted?'ok':'warn');
    $('sdState').textContent=s.sd_mounted?`ONLINE // CS GPIO${s.sd_cs_pin}`:'OFFLINE';
    const percent=s.sd_total_bytes?Math.min(100,Math.round((s.sd_used_bytes/s.sd_total_bytes)*100)):0;
    $('storageFill').style.width=`${percent}%`;
    $('storageText').textContent=s.sd_mounted?`${percent}% USED // ${mb(s.sd_used_bytes)} MiB OF ${mb(s.sd_total_bytes)} MiB`:'NO CARD DETECTED';
    $('audioFile').textContent=s.sd_current_audio_file||'NONE';
    $('photoFile').textContent=s.sd_last_photo_file||'NONE';
    $('segments').textContent=s.sd_audio_segments_completed;
    $('photos').textContent=s.sd_photos_saved;
    $('errors').textContent=s.sd_write_errors;
    $('status').textContent=`BOOTSTRAP> NODE ONLINE\nUPTIME> ${s.uptime_seconds}s\nHEAP> ${s.free_heap_bytes} bytes\nPSRAM> ${s.free_psram_bytes} bytes\nAUDIO_RING> ${s.audio_ring_samples} samples\nSTORAGE_LOW> ${s.sd_storage_low}\nSTATUS> NOMINAL`;
  }catch(error){$('status').textContent=`FAULT> NODE UNREACHABLE\nDETAIL> ${error.message}`;$('status').classList.add('error')}
}
setInterval(refresh,2000);refresh();startStream();
</script>
</body>
</html>
)HTML";

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

I2SClass g_microphone;
SemaphoreHandle_t g_audioRingMutex = nullptr;
SemaphoreHandle_t g_cameraMutex = nullptr;
httpd_handle_t g_controlServer = nullptr;
httpd_handle_t g_streamServer = nullptr;
int16_t *g_audioRing = nullptr;
size_t g_audioRingCapacity = 0;
size_t g_audioRingWriteIndex = 0;
size_t g_audioRingValidSamples = 0;
volatile uint32_t g_micRms = 0;
volatile uint32_t g_micPeak = 0;
bool g_servicesStarted = false;

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
  header.blockAlign = 2;
  header.byteRate = header.sampleRate * header.blockAlign;
  memcpy(header.data, "data", 4);
  header.dataSize = dataBytes;
  return header;
}

void sendServiceUnavailable(httpd_req_t *request, const char *message) {
  httpd_resp_set_status(request, "503 Service Unavailable");
  httpd_resp_set_type(request, "text/plain; charset=utf-8");
  httpd_resp_sendstr(request, message);
}

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
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  return httpd_resp_send(request, kDashboardHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t statusHandler(httpd_req_t *request) {
  StorageRecorderStatus storage = {};
  storageRecorderGetStatus(&storage);

  size_t ringSamples = 0;
  if (g_audioRingMutex != nullptr &&
      xSemaphoreTake(g_audioRingMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    ringSamples = g_audioRingValidSamples;
    xSemaphoreGive(g_audioRingMutex);
  }

  char json[1280];
  const int requestedLength = snprintf(
      json, sizeof(json),
      "{\"camera\":\"%s\",\"ip\":\"%s\",\"wifi_rssi_dbm\":%d,"
      "\"uptime_seconds\":%lu,\"free_heap_bytes\":%lu,"
      "\"free_psram_bytes\":%lu,\"microphone_rms\":%lu,"
      "\"microphone_peak\":%lu,\"audio_ring_samples\":%lu,"
      "\"sd_mounted\":%s,\"sd_audio_recording\":%s,"
      "\"sd_storage_low\":%s,\"sd_cs_pin\":%u,"
      "\"sd_total_bytes\":%llu,\"sd_used_bytes\":%llu,"
      "\"sd_audio_segments_completed\":%lu,\"sd_photos_saved\":%lu,"
      "\"sd_write_errors\":%lu,\"sd_current_audio_file\":\"%s\","
      "\"sd_last_photo_file\":\"%s\"}",
      cameraName(), WiFi.localIP().toString().c_str(), WiFi.RSSI(),
      static_cast<unsigned long>(millis() / 1000),
      static_cast<unsigned long>(ESP.getFreeHeap()),
      static_cast<unsigned long>(ESP.getFreePsram()),
      static_cast<unsigned long>(g_micRms),
      static_cast<unsigned long>(g_micPeak),
      static_cast<unsigned long>(ringSamples), storage.mounted ? "true" : "false",
      storage.audioRecording ? "true" : "false",
      storage.storageLow ? "true" : "false", storage.chipSelectPin,
      static_cast<unsigned long long>(storage.totalBytes),
      static_cast<unsigned long long>(storage.usedBytes),
      static_cast<unsigned long>(storage.audioSegmentsCompleted),
      static_cast<unsigned long>(storage.photosSaved),
      static_cast<unsigned long>(storage.writeErrors), storage.currentAudioFile,
      storage.lastPhotoFile);

  const size_t length = requestedLength < 0
                            ? 0
                            : min(static_cast<size_t>(requestedLength),
                                  sizeof(json) - 1);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, json, length);
}

esp_err_t captureHandler(httpd_req_t *request) {
  if (g_cameraMutex == nullptr ||
      xSemaphoreTake(g_cameraMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    sendServiceUnavailable(request, "Camera busy; try again");
    return ESP_FAIL;
  }

  camera_fb_t *frame = esp_camera_fb_get();
  if (frame == nullptr || frame->format != PIXFORMAT_JPEG) {
    if (frame != nullptr) {
      esp_camera_fb_return(frame);
    }
    xSemaphoreGive(g_cameraMutex);
    httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Camera capture failed");
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
  xSemaphoreGive(g_cameraMutex);
  return result;
}

esp_err_t streamHandler(httpd_req_t *request) {
  esp_err_t result = httpd_resp_set_type(request, kStreamContentType);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");

  while (result == ESP_OK) {
    if (xSemaphoreTake(g_cameraMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr || frame->format != PIXFORMAT_JPEG) {
      if (frame != nullptr) {
        esp_camera_fb_return(frame);
      }
      xSemaphoreGive(g_cameraMutex);
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
    xSemaphoreGive(g_cameraMutex);
    vTaskDelay(pdMS_TO_TICKS(8));
  }
  return result;
}

esp_err_t audioHandler(httpd_req_t *request) {
  const size_t requestedSamples =
      HOME_CAM_AUDIO_SAMPLE_RATE * HOME_CAM_AUDIO_CLIP_SECONDS;
  const size_t maximumBytes = requestedSamples * sizeof(int16_t);
  uint8_t *wav = static_cast<uint8_t *>(heap_caps_malloc(
      sizeof(WavHeader) + maximumBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (wav == nullptr) {
    wav = static_cast<uint8_t *>(malloc(sizeof(WavHeader) + maximumBytes));
  }
  if (wav == nullptr) {
    httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Not enough memory for WAV clip");
    return ESP_FAIL;
  }

  if (g_audioRingMutex == nullptr ||
      xSemaphoreTake(g_audioRingMutex, pdMS_TO_TICKS(250)) != pdTRUE) {
    free(wav);
    sendServiceUnavailable(request, "Audio buffer busy; try again");
    return ESP_FAIL;
  }

  const size_t availableSamples = min(requestedSamples, g_audioRingValidSamples);
  if (availableSamples < HOME_CAM_AUDIO_SAMPLE_RATE / 4) {
    xSemaphoreGive(g_audioRingMutex);
    free(wav);
    sendServiceUnavailable(request, "Audio buffer is still warming up");
    return ESP_FAIL;
  }

  const size_t start =
      (g_audioRingWriteIndex + g_audioRingCapacity - availableSamples) %
      g_audioRingCapacity;
  int16_t *destination = reinterpret_cast<int16_t *>(wav + sizeof(WavHeader));
  const size_t firstPart = min(availableSamples, g_audioRingCapacity - start);
  memcpy(destination, g_audioRing + start, firstPart * sizeof(int16_t));
  if (firstPart < availableSamples) {
    memcpy(destination + firstPart, g_audioRing,
           (availableSamples - firstPart) * sizeof(int16_t));
  }
  xSemaphoreGive(g_audioRingMutex);

  const uint32_t dataBytes = availableSamples * sizeof(int16_t);
  const WavHeader header = makeWavHeader(dataBytes);
  memcpy(wav, &header, sizeof(header));

  httpd_resp_set_type(request, "audio/wav");
  httpd_resp_set_hdr(request, "Content-Disposition",
                     "attachment; filename=homecam-recent.wav");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  const esp_err_t result = httpd_resp_send(
      request, reinterpret_cast<const char *>(wav), sizeof(header) + dataBytes);
  free(wav);
  return result;
}

void writeAudioRing(const int16_t *samples, size_t count) {
  if (g_audioRing == nullptr || count == 0 ||
      xSemaphoreTake(g_audioRingMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return;
  }
  const size_t firstPart = min(count, g_audioRingCapacity - g_audioRingWriteIndex);
  memcpy(g_audioRing + g_audioRingWriteIndex, samples,
         firstPart * sizeof(int16_t));
  if (firstPart < count) {
    memcpy(g_audioRing, samples + firstPart,
           (count - firstPart) * sizeof(int16_t));
  }
  g_audioRingWriteIndex = (g_audioRingWriteIndex + count) % g_audioRingCapacity;
  g_audioRingValidSamples = min(g_audioRingCapacity,
                                g_audioRingValidSamples + count);
  xSemaphoreGive(g_audioRingMutex);
}

void audioCaptureTask(void *) {
  constexpr size_t kSampleCount = 512;
  int16_t samples[kSampleCount];

  while (true) {
    const size_t bytes = g_microphone.readBytes(
        reinterpret_cast<char *>(samples), sizeof(samples));
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
      peak = max(peak, magnitude);
    }
    const uint32_t rms =
        static_cast<uint32_t>(sqrt(static_cast<double>(sumSquares) / count));
    g_micRms = (g_micRms * 3 + rms) / 4;
    g_micPeak = peak;

    writeAudioRing(samples, count);
    storageRecorderAppendAudio(samples, count);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void automaticSnapshotTask(void *) {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(HOME_CAM_AUTO_SNAPSHOT_SECONDS * 1000UL));
    if (!storageRecorderReady() ||
        xSemaphoreTake(g_cameraMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
      continue;
    }
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame != nullptr && frame->format == PIXFORMAT_JPEG) {
      storageRecorderSaveJpeg(frame->buf, frame->len);
    }
    if (frame != nullptr) {
      esp_camera_fb_return(frame);
    }
    xSemaphoreGive(g_cameraMutex);
  }
}

bool startCamera() {
  g_cameraMutex = xSemaphoreCreateMutex();
  if (g_cameraMutex == nullptr) {
    Serial.println("Could not create camera mutex");
    return false;
  }

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
  g_audioRingMutex = xSemaphoreCreateMutex();
  g_audioRingCapacity =
      HOME_CAM_AUDIO_SAMPLE_RATE * HOME_CAM_AUDIO_RING_SECONDS;
  g_audioRing = static_cast<int16_t *>(heap_caps_calloc(
      g_audioRingCapacity, sizeof(int16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (g_audioRingMutex == nullptr || g_audioRing == nullptr) {
    Serial.println("Could not allocate PSRAM audio ring buffer");
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

  if (xTaskCreatePinnedToCore(audioCaptureTask, "audio-capture", 6144, nullptr,
                              2, nullptr, 0) != pdPASS) {
    Serial.println("Could not start microphone task");
    return false;
  }
  Serial.println("PDM microphone and PSRAM audio ring ready");
  return true;
}

bool startSnapshotRecorder() {
  if (!storageRecorderReady()) {
    return true;
  }
  if (xTaskCreatePinnedToCore(automaticSnapshotTask, "auto-snapshot", 4096,
                              nullptr, 1, nullptr, 1) != pdPASS) {
    Serial.println("Could not start automatic snapshot task");
    return false;
  }
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
  controlConfig.stack_size = 10240;
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
    Serial.println("PSRAM is required for reliable camera/audio operation");
    return false;
  }
  if (!startCamera()) {
    return false;
  }

  // Missing media must not disable the camera or downloadable audio.
  storageRecorderBegin();
  if (!startMicrophone() || !startSnapshotRecorder() || !startWebServers()) {
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
