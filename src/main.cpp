#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// ─── Konfiguration ────────────────────────────────────────────────────────────
// WiFi
const char* WIFI_SSID     = "WiFimodem-F5E6";
const char* WIFI_PASSWORD = "qwzxytm4gz";

// Statisk IP (NodeMCU Amica kræver dette før WiFi.begin)
IPAddress STATIC_IP(192, 168, 73, 151);
IPAddress GATEWAY(192, 168, 73, 1);
IPAddress SUBNET(255, 255, 255, 0);
IPAddress DNS(192, 168, 73, 1);

// MQTT – Mosquitto på din Pi
const char* MQTT_BROKER   = "192.168.73.232";   // IP på din Raspberry Pi
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "cbm";
const char* MQTT_PASSWORD = "1973";
const char* MQTT_CLIENT_ID = "nodemcu_relay_01";

// MQTT topics
const char* TOPIC_COMMAND  = "home/nodemcu01/relay/set";    // HA skriver hertil
const char* TOPIC_STATE    = "home/nodemcu01/relay/state";  // NodeMCU rapporterer her
const char* TOPIC_AVAIL    = "home/nodemcu01/status";       // Online/offline

// Hardware
const int RELAY_PIN = D4;   // GPIO5 – skift til D2/D5 etc. efter behov
const int LED_PIN   = LED_BUILTIN;

// Reconnect timing
const unsigned long RECONNECT_INTERVAL = 5000;
// ─────────────────────────────────────────────────────────────────────────────

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

unsigned long lastReconnectAttempt = 0;
bool relayState = false;

// ─── Hjælpefunktioner ─────────────────────────────────────────────────────────
void setRelay(bool on) {
  relayState = on;
  digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  digitalWrite(LED_PIN, on ? HIGH : LOW);  // Builtin LED er inverteret
  mqtt.publish(TOPIC_STATE, on ? "ON" : "OFF", true);  // retain = true
  Serial.printf("[RELAY] -> %s\n", on ? "ON" : "OFF");
}

// ─── MQTT callback (indgående beskeder) ───────────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();
  Serial.printf("[MQTT] %s -> %s\n", topic, message.c_str());

  if (String(topic) == TOPIC_COMMAND) {
    if (message == "ON")  setRelay(true);
    if (message == "OFF") setRelay(false);
  }
}

// ─── WiFi forbindelse ─────────────────────────────────────────────────────────
void connectWiFi() {
  Serial.printf("\n[WiFi] Forbinder til %s...\n", WIFI_SSID);

  // Statisk IP SKAL sættes før WiFi.begin på NodeMCU Amica
  WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Forbundet! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.printf("\n[WiFi] Fejl – status: %d. Genstarter...\n", WiFi.status());
    ESP.restart();
  }
}

// ─── MQTT forbindelse ─────────────────────────────────────────────────────────
bool connectMQTT() {
  Serial.printf("[MQTT] Forbinder til %s:%d...\n", MQTT_BROKER, MQTT_PORT);

  // Last Will: sæt status til "offline" hvis forbindelsen mistes
  if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                   TOPIC_AVAIL, 0, true, "offline")) {
    Serial.println("[MQTT] Forbundet!");

    mqtt.publish(TOPIC_AVAIL, "online", true);
    mqtt.subscribe(TOPIC_COMMAND);

    // Synkronisér state ved reconnect
    mqtt.publish(TOPIC_STATE, relayState ? "ON" : "OFF", true);

    Serial.printf("[MQTT] Lytter på: %s\n", TOPIC_COMMAND);
    return true;
  }

  Serial.printf("[MQTT] Fejl, rc=%d\n", mqtt.state());
  return false;
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, HIGH);  // LED slukket (inverteret)

  connectWiFi();

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(30);

  connectMQTT();
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
  // Genopret WiFi hvis tabt
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Forbindelse tabt – genforbinder...");
    connectWiFi();
  }

  // Genopret MQTT med non-blocking retry
  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = now;
      if (connectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    mqtt.loop();
  }
}