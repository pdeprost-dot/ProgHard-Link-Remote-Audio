#include <ESPway.h>
#include <ESP_I2S.h>
#include <WiFi.h>
#include <math.h>

namespace {
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr size_t BLOCK_BYTES = 2048;  // up to 256 ms, mono G.711 mu-law at 8 kHz
constexpr uint32_t IDLE_TIMEOUT_MS = 10000;
constexpr int32_t AUDIO_GAIN = 8;
I2SClass microphone;
volatile bool micOn = false;
char ring[32768];
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t captureTask = nullptr;
TaskHandle_t watchdogTask = nullptr;
SemaphoreHandle_t controlLock = nullptr;
volatile bool captureBusy = false;
size_t ringWrite = 0, ringRead = 0, ringUsed = 0;
uint32_t droppedBytes = 0;
volatile uint32_t lastRead = 0;
uint32_t blocks = 0;
uint32_t shortReads = 0;
uint32_t servedBytes = 0;
uint32_t clippedSamples = 0;
int32_t dcAccumulator = 0;
uint32_t pcmCount = 0;
int64_t pcmSum = 0;
uint64_t pcmSumSquares = 0;
int16_t pcmMin = 32767, pcmMax = -32768;
uint32_t pcmWindowStarted = 0;

uint8_t encodeMuLaw(int16_t value) {
  int sample = value;
  const int sign = sample < 0 ? 0x80 : 0;
  if (sample < 0) sample = -sample;
  if (sample > 32635) sample = 32635;
  sample += 0x84;
  int exponent = 7;
  for (int mask = 0x4000; exponent > 0 && !(sample & mask); mask >>= 1) --exponent;
  const int mantissa = (sample >> (exponent + 3)) & 0x0f;
  return static_cast<uint8_t>(~(sign | (exponent << 4) | mantissa));
}

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="fr"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Remote Audio</title>
<style>
body{font:16px system-ui;max-width:680px;margin:2rem auto;padding:0 1rem;background:#111827;color:#f9fafb}
button{font:inherit;padding:.75rem 1rem;margin:.3rem;border:0;border-radius:.5rem;cursor:pointer}
#start{background:#34d399}#stop{background:#f87171}
.card{background:#1f2937;padding:1.25rem;border-radius:1rem}
dt{color:#9ca3af}dd{margin:0 0 1rem}a{color:#93c5fd}
</style><main class="card">
<h1>Microphone ESP32-S3</h1>
<dl><dt>Connexion</dt><dd id="conn">Initialisation</dd>
<dt>Microphone</dt><dd id="mic">OFF</dd>
<dt>Format</dt><dd>G.711 &mu;-law mono, 8 bits, 8 kHz</dd>
<dt>D&eacute;bit audio</dt><dd id="rate">0 kbit/s</dd>
<dt>Buffer</dt><dd id="buffer">0 ms</dd>
<dt>Wi-Fi RSSI</dt><dd id="rssi">-- dBm</dd></dl>
<button id="start">D&eacute;marrer l'&eacute;coute</button><button id="stop" disabled>Arr&ecirc;ter</button>
<p>Le microphone ne d&eacute;marre qu'apr&egrave;s une action explicite. Il s'arr&ecirc;te apr&egrave;s 10 secondes sans lecture.</p>
<p><a href="/">Accueil ProgHard Link</a></p></main><script>
let context=null,running=false,micActive=false,bytes=0,started=0,nextTime=0,refreshBusy=false;
const el=id=>document.getElementById(id);
async function api(path,method='GET',timeout=8000){
  const controller=new AbortController();
  const timer=setTimeout(()=>controller.abort(),timeout);
  try{
    const response=await fetch(path,{method,cache:'no-store',signal:controller.signal});
    if(!response.ok)throw Error('HTTP '+response.status);
    return response;
  }finally{clearTimeout(timer)}
}
function update(){
  el('mic').textContent=micActive?'ON':'OFF';
  el('start').disabled=running;
  el('stop').disabled=!running;
  el('rate').textContent=running?(bytes*8/Math.max(1,(Date.now()-started)/1000)/1000).toFixed(1)+' kbit/s':'0 kbit/s';
  el('buffer').textContent=running&&context?Math.max(0,Math.round((nextTime-context.currentTime)*1000))+' ms':'0 ms';
}
async function refresh(){
  if(refreshBusy)return;
  refreshBusy=true;
  try{
    const state=await(await api('/audio/status','GET',3000)).json();
    micActive=state.microphone==='ON';
    el('rssi').textContent=state.wifi_rssi+' dBm';
    el('conn').textContent='Connecte';
    update();
  }catch(e){el('conn').textContent='Erreur : '+e.message}
  finally{refreshBusy=false}
}
async function stop(){
  const shouldStop=running||micActive;
  running=false;
  if(shouldStop)try{await api('/audio/stop','POST',3000);micActive=false}catch(e){}
  if(context){await context.close();context=null}
  nextTime=0;update();refresh();
}
async function pump(){
  while(running){
    try{
      const raw=await(await api('/audio/chunk')).arrayBuffer();
      if(!running)break;
      if(!raw.byteLength){await new Promise(resolve=>setTimeout(resolve,30));continue}
      if(nextTime-context.currentTime>.8)continue;
      const samples=new Uint8Array(raw);
      const buffer=context.createBuffer(1,samples.length,8000),out=buffer.getChannelData(0);
      for(let i=0;i<samples.length;i++){
        const code=(~samples[i])&255;
        let value=(((code&15)<<3)+132)<<((code>>4)&7);
        value-=132;
        out[i]=(code&128?-value:value)/32768;
      }
      const source=context.createBufferSource();
      source.buffer=buffer;source.connect(context.destination);
      nextTime=Math.max(context.currentTime+.15,nextTime);
      source.start(nextTime);
      nextTime+=buffer.duration;
      bytes+=raw.byteLength;micActive=true;el('conn').textContent='Connecte';update();
    }catch(e){el('conn').textContent='Erreur : '+e.message;await stop();break}
  }
}
el('start').onclick=async()=>{
  try{
    context=new AudioContext();
    await context.resume();
    await api('/audio/start','POST');
    running=true;micActive=true;bytes=0;started=Date.now();nextTime=0;update();pump();
  }catch(e){el('conn').textContent='Erreur : '+e.message;await stop()}
};
el('stop').onclick=stop;
window.addEventListener('pagehide',()=>{if(running)navigator.sendBeacon('/audio/stop');running=false});
refresh();setInterval(refresh,2000);
</script></html>)HTML";

void captureLoop(void*) {
  char data[512];
  for (;;) {
    portENTER_CRITICAL(&audioMux);
    const bool active = micOn;
    if (active) captureBusy = true;
    portEXIT_CRITICAL(&audioMux);
    if (!active) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
    const size_t got = microphone.readBytes(data, sizeof(data));
    uint8_t encoded[128];
    size_t encodedCount = 0;
    int64_t sampleSum = 0;
    uint64_t sampleSquares = 0;
    int16_t sampleMin = 32767, sampleMax = -32768;
    uint32_t localClipped = 0;
    for (size_t i = 0; i + 1 < got; i += 2) {
      const int16_t pcm = static_cast<int16_t>(
        static_cast<uint16_t>(static_cast<uint8_t>(data[i])) |
        (static_cast<uint16_t>(static_cast<uint8_t>(data[i + 1])) << 8)
      );
      sampleSum += pcm;
      sampleSquares += static_cast<int32_t>(pcm) * static_cast<int32_t>(pcm);
      if (pcm < sampleMin) sampleMin = pcm;
      if (pcm > sampleMax) sampleMax = pcm;
      // Track the DC offset before applying a fixed gain to the AC signal.
      dcAccumulator += pcm - (dcAccumulator >> 10);
      if ((i & 2) == 0) {
        int32_t amplified = (static_cast<int32_t>(pcm) - (dcAccumulator >> 10)) * AUDIO_GAIN;
        if (amplified > 32767) { amplified = 32767; ++localClipped; }
        if (amplified < -32768) { amplified = -32768; ++localClipped; }
        encoded[encodedCount++] = encodeMuLaw(static_cast<int16_t>(amplified));
      }
    }
    portENTER_CRITICAL(&audioMux);
    if (got != sizeof(data)) ++shortReads;
    if (micOn) {
      pcmCount += got / 2;
      clippedSamples += localClipped;
      pcmSum += sampleSum;
      pcmSumSquares += sampleSquares;
      if (sampleMin < pcmMin) pcmMin = sampleMin;
      if (sampleMax > pcmMax) pcmMax = sampleMax;
      for (size_t i = 0; i < encodedCount; ++i) {
        if (ringUsed == sizeof(ring)) {
          ringRead = (ringRead + 1) % sizeof(ring);
          --ringUsed;
          ++droppedBytes;
        }
        ring[ringWrite] = static_cast<char>(encoded[i]);
        ringWrite = (ringWrite + 1) % sizeof(ring);
        ++ringUsed;
      }
    }
    captureBusy = false;
    portEXIT_CRITICAL(&audioMux);
    vTaskDelay(1);
  }
}
bool startMic() {
  if (!controlLock || !captureTask || !watchdogTask) return false;
  xSemaphoreTake(controlLock, portMAX_DELAY);
  if (!micOn) {
    microphone.setPinsPdmRx(42, 41);
    const bool ready = microphone.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    portENTER_CRITICAL(&audioMux);
    ringRead = ringWrite = ringUsed = 0;
    pcmCount = 0; pcmSum = 0; pcmSumSquares = 0;
    pcmMin = 32767; pcmMax = -32768; pcmWindowStarted = millis();
    clippedSamples = 0; dcAccumulator = 0;
    micOn = ready;
    portEXIT_CRITICAL(&audioMux);
  }
  lastRead = millis();
  const bool active = micOn;
  xSemaphoreGive(controlLock);
  Serial.printf("[audio] microphone %s\n", active ? "ON" : "ERROR");
  return active;
}
void stopMic() {
  if (!controlLock) return;
  xSemaphoreTake(controlLock, portMAX_DELAY);
  if (micOn) {
    portENTER_CRITICAL(&audioMux);
    micOn = false;
    portEXIT_CRITICAL(&audioMux);
    while (captureBusy) vTaskDelay(1);
    microphone.end();
    portENTER_CRITICAL(&audioMux);
    ringRead = ringWrite = ringUsed = 0;
    portEXIT_CRITICAL(&audioMux);
    Serial.println("[audio] microphone OFF");
  }
  xSemaphoreGive(controlLock);
}
void watchdogLoop(void*) {
  for (;;) {

    if (micOn && static_cast<uint32_t>(millis() - lastRead) > IDLE_TIMEOUT_MS) stopMic();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
class RemoteAudioApp : public ESPwayApplication {
 public:
  const char* applicationId() const override { return "remote-audio"; }
  const char* firmwareVersion() const override { return "1.0.0"; }
  void begin(const ESPwayApplicationContext&) override {
    controlLock = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(captureLoop, "audio-capture", 4096, nullptr, 0, &captureTask, 1);
    xTaskCreatePinnedToCore(watchdogLoop, "audio-watchdog", 3072, nullptr, 2, &watchdogTask, 0);
    Serial.printf("[audio] PSRAM=%u free_heap=%u\n", ESP.getPsramSize(), ESP.getFreeHeap());
  }
  void loop() override {
    // Microphone timeout is enforced by watchdogLoop even if network work blocks.
  }
  const ESPwayNavigationItem* navigationItems(size_t& count) const override {
    static const ESPwayNavigationItem items[] = {{"Audio", "/audio"}};
    count = 1;
    return items;
  }
  bool handle(const WebRequest& req, WebResponse& res) override {
    if (req.path == "/audio") {
      if (req.method != "GET") { res.status = 405; return true; }
      res.contentType = "text/html; charset=utf-8";
      res.body = PAGE;
      return true;
    }
    if (!req.path.startsWith("/audio/")) return false;
    if (req.path == "/audio/bench" && req.method == "GET") {
      static char payload[2048];
      static bool initialized = false;
      if (!initialized) {
        for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<char>('A' + (i % 26));
        initialized = true;
      }
      res.contentType = "application/octet-stream";
      res.body = String(payload, sizeof(payload));
      return true;
    }
    if (req.path == "/audio/level" && req.method == "GET") {
      uint32_t count, elapsed;
      int64_t sum;
      uint64_t squares;
      int16_t minimum, maximum;
      portENTER_CRITICAL(&audioMux);
      count = pcmCount; sum = pcmSum; squares = pcmSumSquares;
      minimum = pcmMin; maximum = pcmMax;
      elapsed = millis() - pcmWindowStarted;
      pcmCount = 0; pcmSum = 0; pcmSumSquares = 0;
      pcmMin = 32767; pcmMax = -32768; pcmWindowStarted = millis();
      portEXIT_CRITICAL(&audioMux);
      const double mean = count ? static_cast<double>(sum) / count : 0;
      const double rms = count ? sqrt(static_cast<double>(squares) / count) : 0;
      const double acRms = sqrt(fmax(0.0, rms * rms - mean * mean));
      const int32_t peak = count ? max(abs(static_cast<int32_t>(minimum)), abs(static_cast<int32_t>(maximum))) : 0;
      res.body = String("{\"samples\":") + count + ",\"duration_ms\":" + elapsed +
        ",\"mean\":" + String(mean, 1) + ",\"rms\":" + String(rms, 1) +
        ",\"ac_rms\":" + String(acRms, 1) + ",\"min\":" + (count ? minimum : 0) +
        ",\"max\":" + (count ? maximum : 0) + ",\"peak\":" + peak + "}";
      return true;
    }
    if (req.path == "/audio/status" && req.method == "GET") {
      res.body = String("{\"microphone\":\"") + (micOn ? "ON" : "OFF") + "\",\"blocks\":" + blocks + ",\"short_reads\":" + shortReads + ",\"dropped_bytes\":" + droppedBytes + ",\"queued_bytes\":" + ringUsed + ",\"free_heap\":" + ESP.getFreeHeap() + ",\"free_psram\":" + ESP.getFreePsram() + ",\"served_bytes\":" + servedBytes + ",\"wifi_rssi\":" + WiFi.RSSI() + ",\"gain\":" + AUDIO_GAIN + ",\"clipped_samples\":" + clippedSamples + "}";
    } else if (req.path == "/audio/start" && req.method == "POST") {
      if (!startMic()) { res.status = 503; res.body = "{\"error\":\"microphone_init\"}"; }
      else res.body = "{\"microphone\":\"ON\"}";
    } else if (req.path == "/audio/stop" && req.method == "POST") {
      stopMic(); res.body = "{\"microphone\":\"OFF\"}";
    } else if (req.path == "/audio/chunk" && req.method == "GET") {
      if (!micOn) { res.status = 409; res.body = "{\"error\":\"microphone_off\"}"; return true; }
      static char block[BLOCK_BYTES];
      lastRead = millis();
      portENTER_CRITICAL(&audioMux);
      const size_t got = min(ringUsed, sizeof(block));
      for (size_t i = 0; i < got; ++i) {
        block[i] = ring[ringRead];
        ringRead = (ringRead + 1) % sizeof(ring);
      }
      ringUsed -= got;
      if (got) { ++blocks; servedBytes += got; }
      portEXIT_CRITICAL(&audioMux);
      res.contentType = "application/octet-stream";
      res.body = String(block, static_cast<unsigned int>(got));
    } else {
      res.status = 404; res.body = "{\"error\":\"not_found\"}";
    }
    return true;
  }
};
RemoteAudioApp app;
ESPwayFramework espway;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  espway.begin(app);
}
void loop() { espway.loop(); }
