/*********  Bibliothèques  *********/
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>

/*********  Paramètres Wi-Fi / MQTT  *********/
const char* SSID          = "Freeboxe";
const char* MOT_DE_PASSE  = "88888888";
const char* SERVEUR_MQTT  = "192.168.26.23";
const int   PORT_MQTT     = 1883;

WiFiClient        wifi;
PubSubClient      mqtt(wifi);

/*********  Capteurs  *********/
// — BH1750 — (luxmètre I²C 0x23)
BH1750 luxmetre;

// — DHT22 — (température et humidité de l’air)
#define BROCHE_DHT  2          // GPIO2
#define TYPE_DHT    DHT22
DHT dht(BROCHE_DHT, TYPE_DHT);

// — Sonde d’humidité du sol —
const int BROCHE_SOL = A0;     // ADC 12 bits (0-4095)

// — Capteurs de proximité —
const int BROCHE_PROX1 = 5;
const int BROCHE_PROX2 = 6;
const int BROCHE_PROX3 = 7;

/*********  Calibration de la sonde de sol  *********
 *  Mesure la valeur ADC :
 *    • à SEC (sonde dans l’air)       → SOL_SEC
 *    • à SATURÉ (sonde trempée)       → SOL_HUMIDE
 *  Mets tes valeurs réelles ici après essai.
 ****************************************************/
const int SOL_SEC     = 300;     // exemple : ~300
const int SOL_HUMIDE  = 2800;    // exemple : ~2800

// Convertit la lecture ADC en pourcentage 0–100 %
float calculerHumiditeSol(int valeurADC)
{
  // si la lecture est hors des valeurs calibrées, on renvoie un "erreur" (NaN)
  if (valeurADC < SOL_SEC || valeurADC > SOL_HUMIDE) {
    return NAN;
  }
  // sinon conversion linéaire 0–100%
  return 100.0f * (valeurADC - SOL_SEC) / (SOL_HUMIDE - SOL_SEC);
}


/*********  Connexions réseau  *********/
void connecterWiFi()
{
  WiFi.begin(SSID, MOT_DE_PASSE);
  while (WiFi.status() != WL_CONNECTED) delay(500);
}

void connecterMQTT()
{
  while (!mqtt.connected())
  {
    mqtt.connect("nano2");      // client-ID unique
    delay(500);
  }
}

/*********  Setup  *********/
void setup()
{
  Serial.begin(115200);

  connecterWiFi();
  mqtt.setServer(SERVEUR_MQTT, PORT_MQTT);

  // Bus I²C de la Nano ESP32 : SDA = 18, SCL = 19
  Wire.begin(A4, A5);
  luxmetre.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  dht.begin();

  pinMode(BROCHE_PROX1, INPUT);
  pinMode(BROCHE_PROX2, INPUT);
  pinMode(BROCHE_PROX3, INPUT);
}

/*********  Boucle principale  *********/
void loop()
{
  if (!mqtt.connected()) connecterMQTT();
  mqtt.loop();    // maintient la socket active

  /* --- Luminosité (lux) --- */
  float lux = luxmetre.readLightLevel();
  if (lux < 0) lux = NAN;       // erreur capteur

  /* --- Température & humidité de l’air --- */
  float humAir  = dht.readHumidity();
  float tempAir = dht.readTemperature();   // °C

  /* --- Humidité du sol --- */
  int   solADC     = analogRead(BROCHE_SOL);      // 0–4095
  float humSolPct  = calculerHumiditeSol(solADC); // 0–100 %

  /* --- Proximité --- */
  bool prox1 = !digitalRead(BROCHE_PROX1);        // objet présent = true
  bool prox2 = !digitalRead(BROCHE_PROX2);
  bool prox3 = !digitalRead(BROCHE_PROX3);

  /* --- Construction du message JSON --- */
  StaticJsonDocument<192> doc;
  doc["lux"]       = lux;
  doc["temp_air"]  = tempAir;
  doc["hum_air"]   = humAir;
  doc["hum_sol"]   = humSolPct;   // **nouveau champ en %**
  doc["prox1"]     = prox1;
  doc["prox2"]     = prox2;
  doc["prox3"]     = prox3;

  char payload[192];
  serializeJson(doc, payload);

  mqtt.publish("nano2/telemetry", payload);
  Serial.println(payload);

  delay(1000);  // 1 s entre deux mesures
}
