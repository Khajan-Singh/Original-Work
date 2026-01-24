#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include "secrets.h"

// -------- DHT Sensor --------
#define DHTPIN 22
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// -------- WiFi --------
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

// -------- MQTT --------
const char *mqtt_broker = MQTT_BROKER_IP;
const int mqtt_port = MQTT_PORT;

// Topics (more realistic/scalable than "app/temp")
const char *device_id = "esp32_01";
const char *telemetry_topic = "plant/esp32_01/telemetry";
const char *status_topic    = "plant/esp32_01/status";

WiFiClient espClient;
PubSubClient client(espClient);

// Publish interval
const unsigned long PUBLISH_MS = 5000;
unsigned long lastPublish = 0;

// -------- Optional: handle incoming commands --------
void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");

  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

// -------- Connect WiFi --------
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nWi-Fi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// -------- Connect MQTT
void connectMQTT() {
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);

  while (!client.connected()) {
    String client_id = String("esp32-") + WiFi.macAddress();
    Serial.print("Connecting to MQTT as ");
    Serial.println(client_id);

    if (client.connect(client_id.c_str())) {
      Serial.println("MQTT connected");

      // Publish status once on connect
      String status = String("{\"device_id\":\"") + device_id +
                      String("\",\"status\":\"online\"}");
      client.publish(status_topic, status.c_str(), true);
    } else {
      Serial.print("MQTT failed, state=");
      Serial.println(client.state());
      delay(1500);
    }
  }
}

void setup() {
  Serial.begin(9600);
  delay(200);

  dht.begin();
  connectWiFi();
  connectMQTT();
}

void loop() {
  // Keep connections alive
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!client.connected()) connectMQTT();
  client.loop();

  // Publish telemetry on interval
  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_MS) {
    lastPublish = now;

    // Read sensor
    float h = dht.readHumidity();
    float tC = dht.readTemperature();        // Celsius
    float tF = dht.readTemperature(true);    // Fahrenheit

    if (isnan(h) || isnan(tC) || isnan(tF)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    float hiF = dht.computeHeatIndex(tF, h);         // Heat index F
    float hiC = dht.computeHeatIndex(tC, h, false);  // Heat index C

    // Build JSON payload
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"ts_ms\":%lu,"
             "\"humidity\":%.2f,"
             "\"temp_c\":%.2f,"
             "\"temp_f\":%.2f,"
             "\"heat_index_c\":%.2f,"
             "\"heat_index_f\":%.2f}",
             device_id, now, h, tC, tF, hiC, hiF);

    // Publish
    bool ok = client.publish(telemetry_topic, payload);
    Serial.print("Published: ");
    Serial.println(ok ? "OK" : "FAILED");
    Serial.println(payload);
  }
}