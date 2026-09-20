#include <ESPway.h>
#include <ESP_I2S.h>

namespace {
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr size_t BLOCK_BYTES = 3200;  // 100 ms, mono signed PCM16
constexpr uint32_t IDLE_TIMEOUT_MS = 3000;
I2SClass microphone;
bool micOn = false;
uint32_t lastRead = 0;
uint32_t blocks = 0;
uint32_t shortReads = 0;

const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="fr"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Remote Audio</title><style>body{font:16px system-ui;max-width:680px;margin:2rem auto;padding:0 1rem;background:#111827;color:#f9fafb}button{font:inherit;padding:.75rem 1rem;margin:.3rem;border:0;border-radius:.5rem;cursor:pointer}#start{background:#34d399}#stop{background:#f87171}.card{background:#1f2937;padding:1.25rem;border-radius:1rem}dt{color:#9ca3af}dd{margin:0 0 1rem}a{color:#93c5fd}</style><main class="card"><h1>Microphone ESP32-S3</h1><dl><dt>Connexion</dt><dd id="conn">Initialisation</dd><dt>Microphone</dt><dd id="mic">OFF</dd><dt>Format</dt><dd>PCM mono, 16 bits, 16 kHz</dd><dt>Débit audio</dt><dd id="rate">0 kbit/s</dd><dt>Buffer</dt><dd id="buffer">0 ms</dd></dl><button id="start">Démarrer l'écoute</button><button id="stop" disabled>Arrêter</button><p>Le microphone ne démarre qu'après une action sur ce bouton. Il s'arrête après 3 secondes sans lecture.</p><p><a href="/">Accueil ProgHard Link</a></p></main><script>
let context, running=false, pending=[], nextTime=0, bytes=0, started=0;
const $=id=>document.getElementById(id);
async function api(path,method='GET'){const r=await fetch(path,{method,cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);return r}
function update(){ $('mic').textContent=running?'ON':'OFF';$('start').disabled=running;$('stop').disabled=!running;$('rate').textContent=running?((bytes*8/Math.max(1,(Date.now()-started)/1000)/1000).toFixed(1)+' kbit/s'):'0 kbit/s';$('buffer').textContent=running?Math.max(0,Math.round((nextTime-context.currentTime)*1000))+' ms':'0 ms' }
async function pump(){while(running){try{const r=await api('/audio/chunk');const raw=await r.arrayBuffer();if(!running)break;const samples=new Int16Array(raw);const b=context.createBuffer(1,samples.length,16000),out=b.getChannelData(0);for(let i=0;i<samples.length;i++)out[i]=samples[i]/32768;const source=context.createBufferSource();source.buffer=b;source.connect(context.destination);nextTime=Math.max(context.currentTime+.15,nextTime);source.start(nextTime);nextTime+=b.duration;bytes+=raw.byteLength;$('conn').textContent='Connecté';update()}catch(e){$('conn').textContent='Erreur : '+e.message;await stop();break}}}
async function stop(){if(!running)return;running=false;try{await api('/audio/stop','POST')}catch(e){}if(context){await context.close();context=null}nextTime=0;update()}
$('start').onclick=async()=>{try{context=new AudioContext({sampleRate:16000});await context.resume();await api('/audio/start','POST');running=true;bytes=0;started=Date.now();nextTime=0;update();pump()}catch(e){$('conn').textContent='Erreur : '+e.message;await stop()}};
$('stop').onclick=stop;window.addEventListener('pagehide',()=>{if(running)navigator.sendBeacon('/audio/stop');running=false});
api('/audio/status').then(()=>$('conn').textContent='Connecté').catch(e=>$('conn').textContent='Erreur : '+e.message);
</script></html>)HTML";

bool startMic() {
  if (micOn) return true;
  microphone.setPinsPdmRx(42, 41);
  micOn = microphone.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  lastRead = millis();
  Serial.printf("[audio] microphone %s\n", micOn ? "ON" : "ERROR");
  return micOn;
}
void stopMic() {
  if (!micOn) return;
  microphone.end();
  micOn = false;
  Serial.println("[audio] microphone OFF");
}
class RemoteAudioApp : public ESPwayApplication {
 public:
  const char* applicationId() const override { return "remote-audio"; }
  const char* firmwareVersion() const override { return "0.1.0"; }
  void begin(const ESPwayApplicationContext&) override {
    Serial.printf("[audio] PSRAM=%u free_heap=%u\n", ESP.getPsramSize(), ESP.getFreeHeap());
  }
  void loop() override {
    if (micOn && millis() - lastRead > IDLE_TIMEOUT_MS) stopMic();
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
    if (req.path == "/audio/status" && req.method == "GET") {
      res.body = String("{\"microphone\":\"") + (micOn ? "ON" : "OFF") + "\",\"blocks\":" + blocks + ",\"short_reads\":" + shortReads + ",\"free_heap\":" + ESP.getFreeHeap() + ",\"free_psram\":" + ESP.getFreePsram() + "}";
    } else if (req.path == "/audio/start" && req.method == "POST") {
      if (!startMic()) { res.status = 503; res.body = "{\"error\":\"microphone_init\"}"; }
      else res.body = "{\"microphone\":\"ON\"}";
    } else if (req.path == "/audio/stop" && req.method == "POST") {
      stopMic(); res.body = "{\"microphone\":\"OFF\"}";
    } else if (req.path == "/audio/chunk" && req.method == "GET") {
      if (!micOn) { res.status = 409; res.body = "{\"error\":\"microphone_off\"}"; return true; }
      static char block[BLOCK_BYTES];
      const size_t got = microphone.readBytes(block, sizeof(block));
      lastRead = millis();
      ++blocks;
      if (got != sizeof(block)) ++shortReads;
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
