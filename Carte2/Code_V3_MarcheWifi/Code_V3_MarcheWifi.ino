/*********  Bibliothèques  *********/
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>

/*********  Paramètres Wi‑Fi / MQTT  *********/
const char* SSID         = "ProjetIOT";
const char* MOT_DE_PASSE = "Eseo2025";

const char* SERVEUR_MQTT = "10.42.0.1";     // IP du Raspberry Pi
const int   PORT_MQTT    = 1883;
const char* MQTT_USER    = "maxime";
const char* MQTT_PASS    = "Eseo2025";

WiFiClient   wifi;
PubSubClient mqtt(wifi);

/*********  Capteurs (à câbler)  *********/
BH1750 luxmetre;
#define BROCHE_DHT  2
#define TYPE_DHT    DHT22
DHT dht(BROCHE_DHT, TYPE_DHT);

const int BROCHE_SOL   = A0;
const int BROCHE_PROX1 = 5;
const int BROCHE_PROX2 = 6;
const int BROCHE_PROX3 = 7;

/*********  Calibration de la sonde de sol  *********/
const int SOL_SEC     = 300;    // à ajuster
const int SOL_HUMIDE  = 2800;   // à ajuster

float calculerHumiditeSol(int adc) {
  if (adc < SOL_SEC || adc > SOL_HUMIDE) return NAN;
  return 100.0f * (adc - SOL_SEC) / (SOL_HUMIDE - SOL_SEC);
}

/*********  Connexion Wi‑Fi  *********/
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
  WiFi.setSleep(false);               // évite les micro‑coupures
  WiFi.begin(SSID, MOT_DE_PASSE);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
    if (millis() - t0 > 15000) {      // 15 s timeout
      Serial.println("\n⚠️  Wi‑Fi KO ➜ reboot");
      ESP.restart();
    }
  }
  Serial.printf("\n✅ Wi‑Fi OK — IP %s\n", WiFi.localIP().toString().c_str());
}

/*********  Connexion MQTT *********/
void connecterMQTT() {
  Serial.print("Connexion MQTT…");
  while (!mqtt.connected()) {
    if (mqtt.connect("nano2", MQTT_USER, MQTT_PASS)) {
      Serial.println(" connecté !");
    } else {
      Serial.print('.');
      delay(500);
    }
  }
}

/*********  Setup  *********/
void setup() {
  Serial.begin(115200);
  delay(500);                         // laisse le temps d’ouvrir le moniteur

  connecterWiFi();
  mqtt.setServer(SERVEUR_MQTT, PORT_MQTT);

  Wire.begin(A4, A5);                 // bus I²C
  luxmetre.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  dht.begin();

  pinMode(BROCHE_PROX1, INPUT);
  pinMode(BROCHE_PROX2, INPUT);
  pinMode(BROCHE_PROX3, INPUT);
}

/*********  Boucle principale  *********/
void loop() {
  if (!mqtt.connected()) connecterMQTT();
  mqtt.loop();

  /* Mesures capteurs */
  float lux      = luxmetre.readLightLevel();
  float humAir   = dht.readHumidity();
  float tempAir  = dht.readTemperature();
  int   solADC   = analogRead(BROCHE_SOL);
  float humSol   = calculerHumiditeSol(solADC);
  bool  prox1    = !digitalRead(BROCHE_PROX1);
  bool  prox2    = !digitalRead(BROCHE_PROX2);
  bool  prox3    = !digitalRead(BROCHE_PROX3);

  /* Construction du JSON */
  StaticJsonDocument<192> doc;
  doc["lux"]      = lux;
  doc["temp_air"] = tempAir;
  doc["hum_air"]  = humAir;
  doc["hum_sol"]  = humSol;
  doc["prox1"]    = prox1;
  doc["prox2"]    = prox2;
  doc["prox3"]    = prox3;

  char payload[192];
  serializeJson(doc, payload);

  mqtt.publish("nano2/telemetry", payload);
  Serial.println(payload);

  delay(1000);                        // 1 s
}
