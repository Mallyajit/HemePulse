#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <esp_timer.h>

// ---------- Hardware configuration ----------
const char* WIFI_SSID = "Tumhare Papa";
const char* WIFI_PASSWORD = "mallyajitwifi";
const uint8_t PHOTODIODE_PIN = 0;
const uint8_t RED_LED_PIN = 3;
const uint8_t IR_LED_PIN = 4;

// ---------- Timing constants ----------
const unsigned long SAMPLE_INTERVAL_US = 10000; // 100 Hz sampling
const unsigned long LED_TOGGLE_INTERVAL_MS = 30; // alternate LEDs less often
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 5000;
const uint8_t SEND_BATCH_SIZE = 8; // send ~12 Hz updates

// ---------- Networking ----------
WebServer server(80);
WebSocketsServer webSocket(81);

// ---------- Web UI assets ----------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>HemePulse Live</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
  <style>
    :root {
      --bg: #05060a;
      --card: rgba(20, 29, 63, 0.85);
      --accent: #f64e5b;
      --accent-muted: #5ee1ab;
      font-family: 'Space Grotesk', 'Segoe UI', system-ui, sans-serif;
      color: #f8f9ff;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      background: linear-gradient(135deg, #0b1430 0%, #05060a 100%);
      padding: 1.25rem;
      display: flex;
      justify-content: center;
    }
    main {
      width: min(1100px, 100%);
      display: grid;
      gap: 1rem;
    }
    .panel {
      background: var(--card);
      border-radius: 1rem;
      padding: 1.5rem;
      border: 1px solid rgba(255, 255, 255, 0.08);
      box-shadow: 0 25px 60px rgba(0, 0, 0, 0.5);
    }
    .panel h1 {
      margin: 0;
      font-size: 1.6rem;
    }
    .status {
      color: rgba(255, 255, 255, 0.7);
      font-size: 0.85rem;
    }
    .chart-wrapper {
      position: relative;
      height: 360px;
    }
    .grid {
      margin-top: 1rem;
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 0.75rem;
    }
    .metric {
      background: rgba(255, 255, 255, 0.03);
      border-radius: 0.9rem;
      padding: 1rem;
      border: 1px solid rgba(255, 255, 255, 0.06);
    }
    .metric h3 {
      margin: 0;
      font-size: 0.8rem;
      text-transform: uppercase;
      letter-spacing: 0.2rem;
      color: rgba(255, 255, 255, 0.6);
    }
    .metric .value {
      margin-top: 0.3rem;
      font-size: 2.1rem;
      font-weight: 600;
      color: var(--accent);
    }
    .badge {
      width: 36px;
      height: 36px;
      border-radius: 50%;
      border: 2px solid rgba(255, 255, 255, 0.3);
      display: inline-flex;
      align-items: center;
      justify-content: center;
      font-weight: 600;
      color: var(--accent);
      transition: transform 0.2s ease;
    }
    .badge.active {
      transform: scale(1.1);
      border-color: var(--accent-muted);
      color: var(--accent-muted);
      box-shadow: 0 0 12px rgba(94, 225, 171, 0.6);
    }
  </style>
</head>
<body>
  <main>
    <section class="panel">
      <div style="display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:0.5rem;">
        <h1>HemePulse Live</h1>
        <div class="status">WebSocket: <span id="wsStatus">Connecting...</span></div>
      </div>
      <div class="chart-wrapper">
        <canvas id="waveform"></canvas>
      </div>
      <div class="grid">
        <div class="metric">
          <h3>Voltage</h3>
          <div class="value" id="voltage">0.000 V</div>
          <p class="status">ADC voltage on GPIO0</p>
        </div>
        <div class="metric">
          <h3>Raw ADC</h3>
          <div class="value" id="adc">0</div>
          <p class="status">12-bit sampling</p>
        </div>
        <div class="metric">
          <h3>BPM</h3>
          <div class="value" id="bpm">--</div>
          <p class="status">Peak-to-peak intervals</p>
        </div>
        <div class="metric" style="display:flex;flex-direction:column;align-items:center;gap:0.25rem;">
          <div>Peak</div>
          <div class="badge" id="peak">▲</div>
        </div>
      </div>
    </section>
  </main>

  <script>
    const ctx = document.getElementById('waveform').getContext('2d');
    const MAX_POINTS = 150;
    const VIEW_WINDOW_MS = 6000;
    const CHART_FLUSH_MS = 300;
    const pendingPoints = [];
    const chart = new Chart(ctx, {
      type: 'line',
      data: {
        datasets: [{
          label: 'Photodiode',
          borderColor: '#f64e5b',
          borderWidth: 2,
          pointRadius: 0,
          tension: 0.2,
          fill: false,
          data: []
        }]
      },
      options: {
        animation: false,
        responsive: true,
        maintainAspectRatio: false,
        scales: {
          x: {
            type: 'linear',
            ticks: { callback: (value) => ((value / 1000) % 60).toFixed(1) + 's' },
            title: { display: true, text: 'Time (s)' }
          },
          y: {
            min: 0,
            max: 3.3,
            title: { display: true, text: 'Voltage (V)' }
          }
        },
        plugins: { legend: { display: false } }
      }
    });

    function flushChart() {
      if (!pendingPoints.length) return;
      pendingPoints.forEach((point) => chart.data.datasets[0].data.push(point));
      pendingPoints.length = 0;
      while (chart.data.datasets[0].data.length > MAX_POINTS) {
        chart.data.datasets[0].data.shift();
      }
      const now = Date.now();
      chart.options.scales.x.min = now - VIEW_WINDOW_MS;
      chart.options.scales.x.max = now;
      chart.update('none');
    }

    setInterval(flushChart, CHART_FLUSH_MS);

    function enqueueVoltage(voltage) {
      pendingPoints.push({ x: Date.now(), y: voltage });
    }

    const peakBadge = document.getElementById('peak');
    const wsStatus = document.getElementById('wsStatus');
    const voltageLabel = document.getElementById('voltage');
    const adcLabel = document.getElementById('adc');
    const bpmLabel = document.getElementById('bpm');

    function handleMessage(payload) {
      enqueueVoltage(payload.voltage);
      voltageLabel.textContent = payload.voltage.toFixed(3) + ' V';
      adcLabel.textContent = payload.adc;
      bpmLabel.textContent = payload.bpm > 0 ? payload.bpm.toFixed(1) : '--';
      if (payload.peak) {
        peakBadge.classList.add('active');
        setTimeout(() => peakBadge.classList.remove('active'), 180);
      }
    }

    const socket = new WebSocket('ws://' + window.location.hostname + ':81/');

    socket.addEventListener('open', () => {
      wsStatus.textContent = 'Connected';
      wsStatus.style.color = '#5ee1ab';
    });

    socket.addEventListener('message', (event) => {
      try {
        const data = JSON.parse(event.data);
        handleMessage(data);
      } catch (err) {
        console.warn('Malformed payload', event.data);
      }
    });

    socket.addEventListener('close', () => {
      wsStatus.textContent = 'Disconnected';
      wsStatus.style.color = '#f64e5b';
      setTimeout(() => {
        wsStatus.textContent = 'Reconnecting...';
      }, 1000);
    });
  </script>
</body>
</html>
)rawliteral";

// ---------- State variables ----------
unsigned long lastSampleMicros = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long wifiConnectStartMs = 0;
bool redLedState = false;
uint8_t pendingSampleCount = 0;
uint32_t adcAccumulator = 0;
float voltageAccumulator = 0.0f;
bool pendingPeak = false;

float baselineVoltage = 0.0;
bool wasAboveThreshold = false;
unsigned long lastPeakMillis = 0;
float bpm = 0.0;
const uint8_t WS_QUEUE_SIZE = 6;
struct Measurement { uint16_t adc; float voltage; bool peak; };
Measurement wsQueue[WS_QUEUE_SIZE];
uint8_t wsQueueStart = 0;
uint8_t wsQueueCount = 0;
unsigned long lastWsFlushMs = 0;
const unsigned long WS_SEND_INTERVAL_MS = 80;

WiFiState wifiState = WiFiState::Idle;
bool wifiConnectedLogged = false;

// ---------- Forward declarations ----------
void handleRoot();
void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
bool detectPeak(float voltage);
void queueMeasurement(uint16_t adc, float voltage, bool peakDetected);
void attemptSendQueue();
void ensureWiFiConnected();

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(IR_LED_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(IR_LED_PIN, LOW);

  esp_timer_create_args_t ledTimerArgs = {
    .callback = ledToggleCallback,
    .name = "led-toggle"
  };
  esp_timer_create(&ledTimerArgs, &ledTimer);
  esp_timer_start_periodic(ledTimer, LED_TOGGLE_INTERVAL_MS * 1000);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  ensureWiFiConnected();

  server.on("/", handleRoot);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(handleWebSocketEvent);
  Serial.println("Web server and WebSocket initialized");
}

enum class WiFiState { Idle, Connecting, Connected };
WiFiState wifiState = WiFiState::Idle;

esp_timer_handle_t ledTimer;

void ledToggleCallback(void*) {
  redLedState = !redLedState;
  digitalWrite(RED_LED_PIN, redLedState ? HIGH : LOW);
  digitalWrite(IR_LED_PIN, redLedState ? LOW : HIGH);
}

void loop() {
  server.handleClient();
  webSocket.loop();
  ensureWiFiConnected();

  unsigned long nowMicros = micros();
  if (nowMicros - lastSampleMicros >= SAMPLE_INTERVAL_US) {
    lastSampleMicros = nowMicros;
    uint16_t adcValue = analogRead(PHOTODIODE_PIN);
    float voltage = (adcValue * 3.3f) / 4095.0f;
    bool peak = detectPeak(voltage);
    pendingPeak = pendingPeak || peak;
    adcAccumulator += adcValue;
    voltageAccumulator += voltage;
    pendingSampleCount++;
    if (pendingSampleCount >= SEND_BATCH_SIZE) {
      float avgVoltage = voltageAccumulator / pendingSampleCount;
      uint16_t avgAdc = adcAccumulator / pendingSampleCount;
      queueMeasurement(avgAdc, avgVoltage, pendingPeak);
      pendingSampleCount = 0;
      adcAccumulator = 0;
      voltageAccumulator = 0.0f;
      pendingPeak = false;
    }
  }

  attemptSendQueue();

}

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_DISCONNECTED) {
    Serial.printf("WebSocket [%u] disconnected\n", num);
  } else if (type == WStype_CONNECTED) {
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("WebSocket [%u] connected from %u.%u.%u.%u\n", num, ip[0], ip[1], ip[2], ip[3]);
  }
}

bool detectPeak(float voltage) {
  const float smoothing = 0.01f;
  baselineVoltage = (baselineVoltage * (1.0f - smoothing)) + (voltage * smoothing);
  const float threshold = baselineVoltage + 0.025f;
  unsigned long now = millis();
  bool triggered = false;
  if (!wasAboveThreshold && voltage > threshold && (now - lastPeakMillis) > 300) {
    if (lastPeakMillis != 0) {
      float interval = now - lastPeakMillis;
      bpm = 60000.0f / interval;
    }
    lastPeakMillis = now;
    triggered = true;
  }
  wasAboveThreshold = voltage > threshold;
  return triggered;
}

void queueMeasurement(uint16_t adc, float voltage, bool peakDetected) {
  if (wsQueueCount == WS_QUEUE_SIZE) {
    wsQueueStart = (wsQueueStart + 1) % WS_QUEUE_SIZE;
    wsQueueCount--;
  }
  uint8_t idx = (wsQueueStart + wsQueueCount) % WS_QUEUE_SIZE;
  wsQueue[idx] = { adc, voltage, peakDetected };
  wsQueueCount++;
  attemptSendQueue();
}

void attemptSendQueue() {
  if (!wsQueueCount) {
    return;
  }
  unsigned long now = millis();
  if (now - lastWsFlushMs < WS_SEND_INTERVAL_MS) {
    return;
  }
  if (!webSocket.connectedClients()) {
    return;
  }
  Measurement m = wsQueue[wsQueueStart];
  char buffer[128];
  int len = snprintf(buffer, sizeof(buffer), "{\"adc\":%u,\"voltage\":%.4f,\"bpm\":%.1f,\"peak\":%d}",
                     m.adc, m.voltage, bpm, m.peak ? 1 : 0);
  webSocket.broadcastTXT(buffer, len);
  wsQueueStart = (wsQueueStart + 1) % WS_QUEUE_SIZE;
  wsQueueCount--;
  lastWsFlushMs = now;
}

void ensureWiFiConnected() {
  unsigned long now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiState != WiFiState::Connected) {
      wifiState = WiFiState::Connected;
      wifiConnectedLogged = false;
    }
    if (!wifiConnectedLogged) {
      wifiConnectedLogged = true;
      Serial.print("Wi-Fi connected, IP: ");
      Serial.println(WiFi.localIP());
    }
    return;
  }

  if (wifiState == WiFiState::Idle) {
    if (now - lastWifiAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
      lastWifiAttemptMs = now;
      wifiState = WiFiState::Connecting;
      wifiConnectStartMs = now;
      wifiConnectedLogged = false;
      Serial.print("Wi-Fi connecting to ");
      Serial.println(WIFI_SSID);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    return;
  }

  if (wifiState == WiFiState::Connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiState = WiFiState::Connected;
      wifiConnectedLogged = false;
      return;
    }
    if (now - wifiConnectStartMs >= 8000) {
      wifiState = WiFiState::Idle;
      Serial.println("Wi-Fi connect timed out, will retry");
    }
  }
}
