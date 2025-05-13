/*********  Bibliothèques  *********/
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <BH1750.h>
#include <cmath>

/*********  Paramètres Wi‑Fi / MQTT  *********/
const char* SSID         = "ProjetIOT";
const char* MOT_DE_PASSE = "Eseo2025";

const char* SERVEUR_MQTT = "10.42.0.1";
const int   PORT_MQTT    = 1883;
const char* MQTT_USER    = "maxime";
const char* MQTT_PASS    = "Eseo2025";

WiFiClient   wifi;
PubSubClient mqtt(wifi);

/*********  Capteurs  *********/
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
float   zeroCurrentVoltage = ADC_REFERENCE_VOLTAGE / 2.0;

/*********  Connexion Wi‑Fi  *********/
void connecterWiFi() {
  Serial.println("\n--- Scan Wi‑Fi ---");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    Serial.printf("%2d) %s (%ddBm)  %s\n", i + 1,
                  WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                  (WiFi.encryptionType(i) == WIFI_AUTH_WPA2_PSK) ? "WPA2" : "?");
  }
  Serial.println("------------------");

  Serial.printf("Connexion à %s", SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(SSID, MOT_DE_PASSE);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
    if (millis() - t0 > 15000) {
      Serial.println("\n⚠️  Wi‑Fi KO ➜ reboot");
      ESP.restart();
    }
  }
  Serial.printf("\n✅ Wi‑Fi OK — IP %s\n", WiFi.localIP().toString().c_str());
}

/*********  Connexion MQTT  *********/
void connecterMQTT() {
  Serial.print("MQTT…");
  while (!mqtt.connected()) {
    if (mqtt.connect("nano1", MQTT_USER, MQTT_PASS)) {
      Serial.println(" connecté !");
    } else {
      Serial.printf(" échec (state %d)\n", mqtt.state());
      delay(1000);
    }
  }
}

/*********  Setup  *********/
void setup() {
  Serial.begin(115200);
  delay(500);

  connecterWiFi();
  mqtt.setServer(SERVEUR_MQTT, PORT_MQTT);

  /* --- Calibration capteur courant --- */
  long rawSum = 0;
  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    rawSum += analogRead(CURRENT_PIN);
    delay(5);
  }
  zeroCurrentVoltage =
    (rawSum / (float)CALIBRATION_SAMPLES / ADC_RESOLUTION) * ADC_REFERENCE_VOLTAGE;

  /* --- BH1750 --- */
  Wire.begin(A4, A5);                           // bus I²C
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
}

/*********  Boucle principale  *********/
void loop() {
  if (!mqtt.connected()) connecterMQTT();
  mqtt.loop();

  /* --- Mesure tension --- */
  float vRaw = analogRead(VOLTAGE_PIN) * ADC_REFERENCE_VOLTAGE / ADC_RESOLUTION;
  float tension = (vRaw < VOLTAGE_ZERO_THRESHOLD) ? 0
                 : vRaw * VOLTAGE_SCALING_FACTOR + VOLTAGE_OFFSET_ADD;

  /* --- Mesure courant --- */
  long sum = 0;
  for (int i = 0; i < RUNTIME_AVG_SAMPLES; i++) {
    sum += analogRead(CURRENT_PIN);
    delayMicroseconds(100);
  }
  float sensorV = (sum / (float)RUNTIME_AVG_SAMPLES / ADC_RESOLUTION) * ADC_REFERENCE_VOLTAGE;
  float courant = (sensorV - zeroCurrentVoltage) / SENSITIVITY_V_PER_AMP;
  if (fabs(courant) < CURRENT_ZERO_THRESHOLD) courant = 0;

  /* --- Lux --- */
  float lux = fmax(lightMeter.readLightLevel(), 0.0f);

  /* --- JSON --- */
  StaticJsonDocument<128> doc;
  doc["voltage"] = tension;
  doc["current"] = courant;
  doc["lux"]     = lux;

  char payload[128];
  serializeJson(doc, payload);

  mqtt.publish("nano1/telemetry", payload);
  Serial.println(payload);

  delay(1000);                                  // 1 s
}