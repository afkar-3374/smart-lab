/*
  SMART LAB - ESP32 + MQTT Online Dashboard

  Features:
  - DHT22 temperature and humidity on GPIO4
  - MQ-2 analog reading on GPIO34 (through a 10k/20k divider)
  - Flame detection on GPIO15
  - People counter using two IR sensors on GPIO18 (A) and GPIO19 (B):
    A then B = entry, B then A = exit
  - Local buzzer alarm on GPIO25 (works without internet)
  - Sends data to a public MQTT broker for the online dashboard
  - Scans nearby Wi-Fi so the dashboard can switch networks

  Libraries (Arduino Library Manager):
  - DHT sensor library (Adafruit) + Adafruit Unified Sensor
  - PubSubClient (Nick O'Leary)
  - ArduinoJson (Benoit Blanchon)

  Fan runs independently from a separate 12V adapter.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "esp_wifi.h"

// Wi-Fi name and password live in secrets.h (not uploaded to GitHub).
// First time: copy secrets.example.h to secrets.h and fill it in.
#include "secrets.h"

// =====================================================
// MQTT SETTINGS
// =====================================================

// Must match TOPIC in the web page.
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
const char* TOPIC = "smartlab/afkar3374-lab1";

// =====================================================
// WIFI SETTINGS
// =====================================================

// Used on first boot. After that, Wi-Fi chosen on the dashboard is saved.
const char* DEFAULT_WIFI_SSID = WIFI_SSID;
const char* DEFAULT_WIFI_PASSWORD = WIFI_PASSWORD;

// If no Wi-Fi works, connect to this hotspot and open http://192.168.4.1
const char* SETUP_AP_SSID = "SMART-LAB-Setup";
const char* SETUP_AP_PASSWORD = "SmartLab123";

// =====================================================
// HARDWARE PINS
// =====================================================

#define DHT_PIN       4
#define DHT_TYPE      DHT22

#define MQ2_AO_PIN    34
#define FLAME_PIN     15

#define IR_A_PIN      18
#define IR_B_PIN      19

#define BUZZER_PIN    25

// =====================================================
// SENSOR SETTINGS
// =====================================================

// Adjust after observing safe baseline readings.
// This is an example threshold, not a calibrated gas level.
const int MQ2_THRESHOLD = 800;   // clean air ~260, lighter gas peaks 1300-2500
const int MQ2_HYSTERESIS = 150;

// MQ-2 heater needs time before readings are meaningful.
const unsigned long MQ2_WARMUP_MS = 120000;

// Many digital flame sensor modules are active LOW.
const int FLAME_ACTIVE = LOW;

// Many IR obstacle modules are active LOW.
const int IR_ACTIVE = LOW;

// =====================================================
// OBJECTS
// =====================================================

WiFiClient net;
PubSubClient mqtt(net);
WebServer server(80);
Preferences prefs;
DHT dht(DHT_PIN, DHT_TYPE);

// =====================================================
// SENSOR DATA
// =====================================================

float temperature = NAN;
float humidity = NAN;

int mq2RawValue = 0;

bool gasDetected = false;
bool flameDetected = false;
bool alarmOn = false;

int flameHits = 0;
unsigned long flamePulseCount = 0;   // diagnostic: active readings since last print
unsigned long flameWindowStart = 0;
unsigned long flameHoldUntil = 0;
int peopleInside = 0;

String alertMessage = "SMART LAB starting";

unsigned long lastSensorRead = 0;
unsigned long lastFlameSample = 0;

// =====================================================
// PEOPLE COUNTER VARIABLES
// =====================================================

// A then B = entry. B then A = exit.
struct Beam {
  int pin;
  bool counted;                 // this pass is counted; waiting for the beam to clear
  unsigned long blockedSince;   // 0 = not blocked
  unsigned long clearSince;     // 0 = not clear
};

Beam beamA = { IR_A_PIN, false, 0, 0 };
Beam beamB = { IR_B_PIN, false, 0, 0 };

// The second beam must be crossed within this time to complete a pass.
const unsigned long PASS_WINDOW_MS = 1500;

unsigned long aCrossedAt = 0;   // 0 = no half-finished pass
unsigned long bCrossedAt = 0;

unsigned long entryCount = 0;
unsigned long exitCount = 0;

// Diagnostic: how many readings each IR beam was blocked since the last print.
unsigned long irABlocked = 0;
unsigned long irBBlocked = 0;

// =====================================================
// NETWORK STATE
// =====================================================

String topicState, topicStatus, topicCmd, topicWifi, topicAck;

String savedSsid;
String savedPass;

bool scanning = false;
bool wifiChangePending = false;
String pendingSsid, pendingPass;
String pendingAck;

unsigned long lastMqttAttempt = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastPublish = 0;

// =====================================================
// SENSORS
// =====================================================

void updateAlarm() {

  alarmOn = gasDetected || flameDetected;

  if (gasDetected && flameDetected) {
    alertMessage = "Gas/smoke and flame detected";
  } else if (flameDetected) {
    alertMessage = "Flame detected";
  } else if (gasDetected) {
    alertMessage = "Gas/smoke level above threshold";
  } else if (millis() < MQ2_WARMUP_MS) {
    alertMessage = "Gas sensor warming up";
  } else {
    alertMessage = "All clear";
  }

  // Operate local buzzer through a suitable driver.
  digitalWrite(BUZZER_PIN, alarmOn ? HIGH : LOW);
}

// A real flame flickers, so many sensor modules give short pulses instead of
// a steady signal. Checked every 10 ms: 2+ active readings within 500 ms count
// as a flame (one noise spike does not). The alarm holds 1.5 s after the last one.
void sampleFlame() {

  unsigned long now = millis();

  if (now - flameWindowStart >= 500) {
    flameWindowStart = now;
    flameHits = 0;
  }

  if (digitalRead(FLAME_PIN) == FLAME_ACTIVE) {
    flamePulseCount++;
    flameHits++;
    if (flameHits >= 2) {
      flameHoldUntil = now + 1500;
    }
  }

  bool detected = now < flameHoldUntil;

  if (detected != flameDetected) {
    flameDetected = detected;
    updateAlarm();
  }
}

void readSensors() {

  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  long sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(MQ2_AO_PIN);
  }
  mq2RawValue = sum / 8;

  if (millis() < MQ2_WARMUP_MS) {
    gasDetected = false;
  } else if (gasDetected) {
    gasDetected = mq2RawValue >= MQ2_THRESHOLD - MQ2_HYSTERESIS;
  } else {
    gasDetected = mq2RawValue >= MQ2_THRESHOLD;
  }

  updateAlarm();

  Serial.printf("T: %.1f C | H: %.1f %% | MQ-2: %d | Flame: %s (pin %d, %lu pulses) | People: %d | %s\n",
                temperature, humidity, mq2RawValue,
                flameDetected ? "YES" : "no", digitalRead(FLAME_PIN), flamePulseCount,
                peopleInside, alertMessage.c_str());
  Serial.printf("IR A (G%d): %s blocked %lu | IR B (G%d): %s blocked %lu\n",
                IR_A_PIN, digitalRead(IR_A_PIN) ? "HIGH" : "LOW ", irABlocked,
                IR_B_PIN, digitalRead(IR_B_PIN) ? "HIGH" : "LOW ", irBBlocked);
  flamePulseCount = 0;
  irABlocked = 0;
  irBBlocked = 0;
}

// True once per person: the beam must be blocked for 50 ms (ignores noise),
// then clear for 300 ms before the next person can be counted.
bool beamPassed(Beam& beam) {

  bool blocked = digitalRead(beam.pin) == IR_ACTIVE;
  unsigned long now = millis();

  if (blocked) {
    beam.clearSince = 0;
    if (beam.blockedSince == 0) beam.blockedSince = now;

    if (!beam.counted && now - beam.blockedSince >= 50) {
      beam.counted = true;
      return true;
    }
  } else {
    beam.blockedSince = 0;

    if (beam.counted) {
      if (beam.clearSince == 0) beam.clearSince = now;
      if (now - beam.clearSince >= 300) beam.counted = false;
    }
  }

  return false;
}

void countPeople() {

  if (digitalRead(IR_A_PIN) == IR_ACTIVE) irABlocked++;
  if (digitalRead(IR_B_PIN) == IR_ACTIVE) irBBlocked++;

  unsigned long now = millis();

  // Forget a pass that was never completed (someone turned back).
  if (aCrossedAt && now - aCrossedAt > PASS_WINDOW_MS) aCrossedAt = 0;
  if (bCrossedAt && now - bCrossedAt > PASS_WINDOW_MS) bCrossedAt = 0;

  if (beamPassed(beamA)) {
    if (bCrossedAt) {
      // B then A = exit
      bCrossedAt = 0;
      exitCount++;
      if (peopleInside > 0) peopleInside--;
      Serial.printf("EXIT   -> entries %lu, exits %lu, inside %d\n", entryCount, exitCount, peopleInside);
    } else {
      aCrossedAt = now;
    }
  }

  if (beamPassed(beamB)) {
    if (aCrossedAt) {
      // A then B = entry
      aCrossedAt = 0;
      entryCount++;
      peopleInside++;
      Serial.printf("ENTRY  -> entries %lu, exits %lu, inside %d\n", entryCount, exitCount, peopleInside);
    } else {
      bCrossedAt = now;
    }
  }
}

// Keeps the alarm and counter running, even while connecting.
void serviceLocal() {

  unsigned long now = millis();

  if (now - lastSensorRead >= 2000) {
    lastSensorRead = now;
    readSensors();
  }

  if (now - lastFlameSample >= 10) {
    lastFlameSample = now;
    sampleFlame();
  }

  countPeople();
}

void waitWithService(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    serviceLocal();
    delay(10);
  }
}

// =====================================================
// WIFI
// =====================================================

void loadWifi() {
  prefs.begin("smartlab", true);
  savedSsid = prefs.getString("ssid", DEFAULT_WIFI_SSID);
  savedPass = prefs.getString("pass", DEFAULT_WIFI_PASSWORD);
  prefs.end();
}

void saveWifi(const String& ssid, const String& pass) {
  prefs.begin("smartlab", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  savedSsid = ssid;
  savedPass = pass;
}

bool connectWifi(const String& ssid, const String& pass) {

  if (ssid.length() == 0) return false;

  Serial.printf("Connecting to \"%s\"", ssid.c_str());

  WiFi.disconnect(true);
  waitWithService(100);
  WiFi.mode(WIFI_STA);

  // India: allow channels 1-13 (default only scans 1-11 actively).
  esp_wifi_set_country_code("IN", true);

  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    waitWithService(300);
    Serial.print(".");
  }

  bool ok = WiFi.status() == WL_CONNECTED;
  Serial.println(ok ? "\nConnected: " + WiFi.localIP().toString() : "\nFailed");
  return ok;
}

// =====================================================
// SETUP HOTSPOT (only when no Wi-Fi works)
// =====================================================

const char SETUP_PAGE[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>SMART LAB Setup</title>
<style>
body{margin:0;font-family:Arial,sans-serif;background:#0b1220;color:#fff}
main{max-width:420px;margin:auto;padding:24px 16px}
label{display:block;margin:16px 0 6px;color:#9fb0cc}
input{width:100%;box-sizing:border-box;padding:12px;border-radius:10px;border:1px solid #2a3a57;background:#151f32;color:#fff;font-size:16px}
button{margin-top:20px;width:100%;padding:14px;border:0;border-radius:10px;background:#3b82f6;color:#fff;font-size:16px;font-weight:bold}
</style></head><body><main>
<h1>SMART LAB Wi-Fi</h1>
<form method="POST" action="/save">
<label>Wi-Fi name</label><input name="ssid" maxlength="32" required>
<label>Password</label><input name="pass" type="password" maxlength="63">
<button>Save and restart</button>
</form></main></body></html>
)HTMLPAGE";

void runSetupPortal() {

  Serial.printf("Setup hotspot \"%s\" (password %s) -> http://192.168.4.1\n",
                SETUP_AP_SSID, SETUP_AP_PASSWORD);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASSWORD);

  server.on("/", [] { server.send_P(200, "text/html", SETUP_PAGE); });

  server.on("/save", HTTP_POST, [] {
    saveWifi(server.arg("ssid"), server.arg("pass"));
    server.send(200, "text/html",
                "<meta name='viewport' content='width=device-width'>"
                "<h2 style='font-family:Arial'>Saved. Restarting...</h2>");
    waitWithService(1500);
    ESP.restart();
  });

  server.onNotFound([] { server.send_P(200, "text/html", SETUP_PAGE); });
  server.begin();

  unsigned long lastClientSeen = millis();

  // If nobody uses the hotspot for 3 minutes, restart and retry the saved Wi-Fi
  // (handles a router that was switched off).
  while (true) {
    server.handleClient();
    serviceLocal();

    if (WiFi.softAPgetStationNum() > 0) lastClientSeen = millis();

    if (savedSsid.length() > 0 && millis() - lastClientSeen > 180000) {
      ESP.restart();
    }

    delay(5);
  }
}

// =====================================================
// MQTT
// =====================================================

void publishAck(bool ok, const String& msg) {

  JsonDocument doc;
  doc["ok"] = ok;
  doc["msg"] = msg;

  String out;
  serializeJson(doc, out);

  if (mqtt.connected()) {
    mqtt.publish(topicAck.c_str(), out.c_str());
  } else {
    pendingAck = out;
  }
}

void startScan() {
  if (scanning) return;
  scanning = true;
  mqtt.publish(topicWifi.c_str(), "{\"scanning\":true}", true);
  WiFi.scanNetworks(true);   // async, results checked in loop()
}

// Publishes the scan as a list, one entry per name, strongest first.
void publishScanResults(int n) {

  JsonDocument doc;
  doc["scanning"] = false;
  JsonArray list = doc["networks"].to<JsonArray>();

  bool used[64] = { false };
  n = min(n, 64);

  for (int added = 0; added < 20; added++) {

    int best = -1;

    for (int i = 0; i < n; i++) {
      if (used[i] || WiFi.SSID(i).length() == 0) continue;
      if (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best)) best = i;
    }

    if (best < 0) break;

    // Mark every entry with the same name as used.
    String name = WiFi.SSID(best);
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == name) used[i] = true;
    }

    JsonObject net = list.add<JsonObject>();
    net["ssid"] = name;
    net["rssi"] = WiFi.RSSI(best);
    net["open"] = WiFi.encryptionType(best) == WIFI_AUTH_OPEN;
  }

  String out;
  serializeJson(doc, out);
  mqtt.publish(topicWifi.c_str(), out.c_str(), true);
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {

  JsonDocument cmd;

  if (deserializeJson(cmd, payload, length)) return;

  String action = cmd["action"] | "";

  if (action == "scan") {

    startScan();

  } else if (action == "wifi") {

    String ssid = cmd["ssid"] | "";
    String pass = cmd["pass"] | "";

    if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 63) {
      publishAck(false, "Invalid Wi-Fi name or password");
      return;
    }

    // Applied from loop(), not inside this callback.
    pendingSsid = ssid;
    pendingPass = pass;
    wifiChangePending = true;

    publishAck(true, "Connecting to " + ssid + "...");
  }
}

bool connectMqtt() {

  String clientId = "smartlab-" + String((uint32_t)esp_random(), HEX);

  // If the device disappears, the broker marks it offline.
  if (!mqtt.connect(clientId.c_str(), topicStatus.c_str(), 1, true, "offline")) {
    Serial.printf("MQTT failed, state %d\n", mqtt.state());
    return false;
  }

  Serial.println("MQTT connected");

  mqtt.publish(topicStatus.c_str(), "online", true);
  mqtt.subscribe(topicCmd.c_str());

  if (pendingAck.length() > 0) {
    mqtt.publish(topicAck.c_str(), pendingAck.c_str());
    pendingAck = "";
  }

  return true;
}

void publishState() {

  JsonDocument doc;

  if (isnan(temperature)) doc["temperature"] = nullptr;
  else doc["temperature"] = roundf(temperature * 10) / 10;

  if (isnan(humidity)) doc["humidity"] = nullptr;
  else doc["humidity"] = roundf(humidity * 10) / 10;

  doc["mq2Raw"] = mq2RawValue;
  doc["mq2Threshold"] = MQ2_THRESHOLD;
  doc["warmingUp"] = millis() < MQ2_WARMUP_MS;
  doc["gas"] = gasDetected;
  doc["flame"] = flameDetected;
  doc["people"] = peopleInside;
  doc["entries"] = entryCount;
  doc["exits"] = exitCount;
  doc["alarm"] = alarmOn;
  doc["alert"] = alertMessage;
  doc["ssid"] = WiFi.SSID();
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;

  String out;
  serializeJson(doc, out);

  // Retained, so the dashboard shows data as soon as it opens.
  mqtt.publish(topicState.c_str(), out.c_str(), true);
}

// The new Wi-Fi is tested first. If it fails, the old one is restored.
void applyWifiChange() {

  wifiChangePending = false;

  mqtt.loop();
  waitWithService(300);
  mqtt.disconnect();

  bool ok = connectWifi(pendingSsid, pendingPass);

  if (ok) {
    saveWifi(pendingSsid, pendingPass);
  } else {
    connectWifi(savedSsid, savedPass);
  }

  if (WiFi.status() == WL_CONNECTED) connectMqtt();

  publishAck(ok, ok ? "Connected to " + pendingSsid
                    : "Could not connect to " + pendingSsid + ". Wrong password? Kept previous Wi-Fi.");
  pendingPass = "";
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  pinMode(MQ2_AO_PIN, INPUT);
  // Pull-ups: an unplugged active-LOW sensor reads "no trigger" instead of noise.
  pinMode(FLAME_PIN, INPUT_PULLUP);
  pinMode(IR_A_PIN, INPUT_PULLUP);
  pinMode(IR_B_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  analogReadResolution(12);

  dht.begin();

  String base = String(TOPIC) + "/";
  topicState = base + "state";
  topicStatus = base + "status";
  topicCmd = base + "cmd";
  topicWifi = base + "wifi";
  topicAck = base + "ack";

  readSensors();

  loadWifi();

  if (!connectWifi(savedSsid, savedPass)) {
    runSetupPortal();   // never returns
  }

  WiFi.setAutoReconnect(true);

  net.setConnectionTimeout(4000);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(2048);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);

  connectMqtt();
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

  // Alarm and counter first: they must work without internet.
  serviceLocal();

  unsigned long now = millis();

  if (wifiChangePending) {
    applyWifiChange();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiAttempt >= 30000) {
      lastWifiAttempt = now;
      WiFi.reconnect();
    }
    return;
  }

  if (!mqtt.connected()) {
    if (now - lastMqttAttempt >= 5000) {
      lastMqttAttempt = now;
      connectMqtt();
    }
    return;
  }

  mqtt.loop();

  if (scanning) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      publishScanResults(n);
      WiFi.scanDelete();
      scanning = false;
    } else if (n == WIFI_SCAN_FAILED) {
      mqtt.publish(topicWifi.c_str(), "{\"scanning\":false,\"networks\":[]}", true);
      scanning = false;
    }
  }

  if (now - lastPublish >= 2000) {
    lastPublish = now;
    publishState();
  }
}
