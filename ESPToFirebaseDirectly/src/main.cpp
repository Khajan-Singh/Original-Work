#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <time.h>
#include <sys/time.h>
#include "secrets.h"

// -------- DHT Sensor --------
#define DHTPIN 22
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// -------- WiFi --------
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

// -------- Firebase Realtime Database --------
static const char *FIREBASE_DB_URL = FIREBASE_URL;

static const char *device_id = "esp32_01";

// Database paths
static const char *READINGS_PATH = "/plant/esp32_01/readings";
static const char *STATUS_PATH   = "/plant/esp32_01/status";

// Publish interval
const unsigned long PUBLISH_MS = 5000;
unsigned long lastPublish = 0;

// ---------- Time (NTP) ----------
static const char *NTP1 = "pool.ntp.org";
static const char *NTP2 = "time.nist.gov";

// Keep timestamps in UTC (best for dashboards)
static const long GMT_OFFSET_SEC = 0;
static const int  DAYLIGHT_OFFSET_SEC = 0;

bool timeSynced = false;

// Epoch milliseconds (UTC)
uint64_t epochMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
}

// Sync time. Returns true when time looks valid.
bool syncTimeNTP(unsigned long timeoutMs = 15000) {
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP1, NTP2);

  Serial.print("Syncing time via NTP");
  unsigned long start = millis();

  time_t now;
  while (millis() - start < timeoutMs) {
    time(&now);
    // If after Jan 1, 2020, assume synced
    if (now > 1577836800) {
      Serial.println("\n Time synced (UTC)");
      struct tm t;
      gmtime_r(&now, &t);
      Serial.printf("UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                    t.tm_hour, t.tm_min, t.tm_sec);
      return true;
    }
    Serial.print(".");
    delay(500);
  }

  Serial.println("\n NTP sync timeout");
  return false;
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" Wi-Fi connect failed (timeout)");
  }
}

String baseUrl(const char *path) {
  String base = String(FIREBASE_DB_URL);
  if (base.endsWith("/")) base.remove(base.length() - 1);
  return base + String(path) + ".json";
}

bool firebasePOST(const char *path, const String &jsonBody) {
  WiFiClientSecure secure;
  secure.setInsecure(); // demo-friendly

  HTTPClient http;
  String url = baseUrl(path);

  if (!http.begin(secure, url)) {
    Serial.println("HTTP begin() failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t*)jsonBody.c_str(), jsonBody.length());

  Serial.print("POST -> ");
  Serial.println(code);

  if (code > 0) Serial.println(http.getString());
  else Serial.println(http.errorToString(code));

  http.end();
  return (code >= 200 && code < 300);
}

bool firebasePUT(const char *path, const String &jsonBody) {
  WiFiClientSecure secure;
  secure.setInsecure();

  HTTPClient http;
  String url = baseUrl(path);

  if (!http.begin(secure, url)) {
    Serial.println("HTTP begin() failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  int code = http.PUT((uint8_t*)jsonBody.c_str(), jsonBody.length());

  Serial.print("PUT -> ");
  Serial.println(code);

  if (code > 0) Serial.println(http.getString());
  else Serial.println(http.errorToString(code));

  http.end();
  return (code >= 200 && code < 300);
}

void publishStatus(const char *status) {
  // Dashboard-friendly time fields
  time_t secNow;
  time(&secNow);
  uint64_t msNow = epochMs();

  char body[200];
  snprintf(body, sizeof(body),
           "{\"device_id\":\"%s\",\"status\":\"%s\","
           "\"server_ts\":%lu,"
           "\"server_ts_ms\":%llu}",
           device_id, status,
           (unsigned long)secNow,
           (unsigned long long)msNow);

  firebasePUT(STATUS_PATH, String(body));
}

void setup() {
  Serial.begin(9600);
  delay(200);

  dht.begin();
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    timeSynced = syncTimeNTP();
    // Even if not synced, we still publish a status (but server_ts would be wrong)
    publishStatus("online");
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    delay(200);
    return;
  }

  // Retry NTP if it wasn’t synced earlier (helps when network becomes available later)
  if (!timeSynced) timeSynced = syncTimeNTP(8000);

  unsigned long now = millis();
  if (now - lastPublish < PUBLISH_MS) return;
  lastPublish = now;

  float h  = dht.readHumidity();
  float tC = dht.readTemperature();
  float tF = dht.readTemperature(true);

  if (isnan(h) || isnan(tC) || isnan(tF)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  float hiF = dht.computeHeatIndex(tF, h);
  float hiC = dht.computeHeatIndex(tC, h, false);

  // Dashboard uses server_ts for ordering
  time_t secNow;
  time(&secNow);
  uint64_t msNow = epochMs();

  char payload[360];
  snprintf(payload, sizeof(payload),
           "{\"device_id\":\"%s\","
           "\"server_ts\":%lu,"
           "\"server_ts_ms\":%llu,"
           "\"humidity\":%.2f,"
           "\"temp_c\":%.2f,"
           "\"temp_f\":%.2f,"
           "\"heat_index_c\":%.2f,"
           "\"heat_index_f\":%.2f}",
           device_id,
           (unsigned long)secNow,
           (unsigned long long)msNow,
           h, tC, tF, hiC, hiF);

  bool ok = firebasePOST(READINGS_PATH, String(payload));
  Serial.print("Firebase write: ");
  Serial.println(ok ? "OK" : "FAILED");
}