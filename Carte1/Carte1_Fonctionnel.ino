#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <BH1750.h>
#include <cmath>

// ---------- WIFI & MQTT ----------
const char* ssid       = "Freeboxe";
const char* password   = "88888888";
const char* mqttServer = "192.168.26.23";   // ← IP du Mac trouvée plus haut
const int   mqttPort   = 1883;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ---------- Capteurs ----------
const int VOLTAGE_PIN = A0;
const int CURRENT_PIN = A1;
const float VOLTAGE_SCALING_FACTOR = 5.0;
const float VOLTAGE_OFFSET_ADD     = 0.3;
const float VOLTAGE_ZERO_THRESHOLD = 0.5;
const float SENSITIVITY_V_PER_AMP  = 0.100;
const float CURRENT_ZERO_THRESHOLD = 0.06;
const float ADC_REFERENCE_VOLTAGE  = 3.3;
const int   ADC_RESOLUTION         = 4095;
const int   CALIBRATION_SAMPLES    = 50;
const int   RUNTIME_AVG_SAMPLES    = 10;

BH1750 lightMeter;

float calibratedZeroCurrentVoltage = ADC_REFERENCE_VOLTAGE / 2.0;

// ---------- Fonctions ----------
void connectWiFi() {
  Serial.print("Connexion Wi-Fi…");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" OK");
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connexion MQTT…");
    if (mqttClient.connect("nano1")) {      // ID client unique
      Serial.println(" connectée");
    } else {
      Serial.print(" échec, code=");
      Serial.println(mqttClient.state());
      delay(1000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  connectWiFi();
  mqttClient.setServer(mqttServer, mqttPort);

  // --- Calibration courante ---
  long rawCurrentSum = 0;
  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    rawCurrentSum += analogRead(CURRENT_PIN);
    delay(5);
  }
  calibratedZeroCurrentVoltage =
      (rawCurrentSum / (float)CALIBRATION_SAMPLES / ADC_RESOLUTION) * ADC_REFERENCE_VOLTAGE;

  // --- BH1750 ---
  Wire.begin();
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
}

void loop() {
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  // --- Tension ---
  float voltageRaw = analogRead(VOLTAGE_PIN) * ADC_REFERENCE_VOLTAGE / ADC_RESOLUTION;
  float voltage    = (voltageRaw < VOLTAGE_ZERO_THRESHOLD) ? 0 :
                     voltageRaw * VOLTAGE_SCALING_FACTOR + VOLTAGE_OFFSET_ADD;

  // --- Courant (moyenné) ---
  long rawSum = 0;
  for (int i = 0; i < RUNTIME_AVG_SAMPLES; i++) {
    rawSum += analogRead(CURRENT_PIN);
    delayMicroseconds(100);
  }
  float sensorV = (rawSum / (float)RUNTIME_AVG_SAMPLES / ADC_RESOLUTION) * ADC_REFERENCE_VOLTAGE;
  float current = (sensorV - calibratedZeroCurrentVoltage) / SENSITIVITY_V_PER_AMP;
  current = (fabs(current) < CURRENT_ZERO_THRESHOLD) ? 0 : current;

  // --- Lux ---
  float lux = max(lightMeter.readLightLevel(), 0.0f);

  // --- Payload JSON ---
  StaticJsonDocument<128> doc;
  doc["voltage"] = voltage;
  doc["current"] = current;
  doc["lux"]     = lux;
  char payload[128];
  serializeJson(doc, payload);

  // --- Publish ---
  mqttClient.publish("nano1/telemetry", payload);
  Serial.println(payload);

  delay(1000);   // 1 s
}
