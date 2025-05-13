/*********  Librairies  *********/
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>

/*********  Wi-Fi / MQTT  *********/
const char* ssid       = "Serveur_instrumentation";
const char* password   = "Eseo2025";
const char* mqttServer = "10.42.0.1";   // IP de ton Mac
const int   mqttPort   = 1883;

WiFiClient wifi;
PubSubClient mqtt(wifi);

/*********  Capteurs  *********/
// — BH1750 —
BH1750 luxMeter;                     // addr 0x23

// — DHT22 —
#define DHTPIN 2       // D2 (GPIO2)
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// — Humidité sol —
const int SOIL_PIN = A0;             // ADC 12 bits (0-4095)

// — Proximité —
const int PROX1_PIN = 5;  // D5
const int PROX2_PIN = 6;  // D6
const int PROX3_PIN = 7;  // D7

/*********  Connexions  *********/
void connectWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
}

void connectMQTT() {
  while (!mqtt.connected()) {
    mqtt.connect("nano2");           // client-id unique
    delay(500);
  }
}

/*********  Setup  *********/
void setup() {
  Serial.begin(115200);

  connectWiFi();
  mqtt.setServer(mqttServer, mqttPort);

  // I²C sur Nano ESP32 : SDA = 18, SCL = 19
  Wire.begin(18, 19);
  luxMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  dht.begin();

  pinMode(PROX1_PIN, INPUT);
  pinMode(PROX2_PIN, INPUT);
  pinMode(PROX3_PIN, INPUT);
}

/*********  Loop  *********/
void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();                       // garde la socket active

  /* --- BH1750 --- */
  float lux = luxMeter.readLightLevel();
  if (lux < 0) lux = NAN;            // erreur

  /* --- DHT22 --- */
  float humAir = dht.readHumidity();
  float tempAir = dht.readTemperature();   // °C

  /* --- Humidité sol (brute) --- */
  int soilRaw = analogRead(SOIL_PIN);      // 0 (sec) → 4095 (humide)

  /* --- Proximité --- */
  bool p1 = !digitalRead(PROX1_PIN);       // objet = true
  bool p2 = !digitalRead(PROX2_PIN);
  bool p3 = !digitalRead(PROX3_PIN);

  /* --- JSON MQTT --- */
  StaticJsonDocument<192> doc;
  doc["lux"]      = lux;
  doc["temp_air"] = tempAir;
  doc["hum_air"]  = humAir;
  doc["soil"]     = soilRaw;
  doc["prox1"]    = p1;
  doc["prox2"]    = p2;
  doc["prox3"]    = p3;

  char payload[192];
  serializeJson(doc, payload);
  mqtt.publish("nano2/telemetry", payload);
  Serial.println(payload);

  delay(500);
}
