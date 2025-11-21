//Enzo Fernandes Ramos RM:563705
//Felipe Henrique de Souza Cerazi RM:562746
//Gustavo Peaguda de Castro RM:562923

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <math.h>

// ====================== CONFIG WIFI / MQTT ======================
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// IP da VM (mesmo que você usa no Postman para Orion/STH/IoT Agent)
const char* MQTT_HOST = "102.37.18.193";  // <-- TROCAR PARA O IP DA VM
const int   MQTT_PORT = 1883;

// Dados da pulseira / dispositivo
const char* DEVICE_ID   = "brace001";         // device_id no IoT Agent
const char* APIKEY      = "TEF";              // apikey do service group
// Tópico Ultralight MQTT para o IoT Agent
// Formato: /<apikey>/<device_id>/attrs
String mqttTopic = String("/") + APIKEY + "/" + DEVICE_ID + "/attrs";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ====================== CONFIG MPU6050 ======================
const int   MPU_ADDR   = 0x68;
const float ACC_SCALE  = 16384.0;   // LSB/g para ±2g

// Amostragem e publicação
const unsigned long SAMPLE_INTERVAL_MS  = 200;   // 5 Hz
const unsigned long PUBLISH_INTERVAL_MS = 2000;  // envia a cada 2s

unsigned long lastSampleMs  = 0;
unsigned long lastPublishMs = 0;

// Zona morta perto de zero para considerar parado
const float ZERO_EPS = 0.02;

// Limiares (baseados em aMag direto, sem subtrair gravidade)
const float THRESH_PARADO      = 0.05;
const float THRESH_MOV_LEVE    = 0.30;
const float THRESH_ANDANDO     = 0.70;
const float THRESH_INTENSO     = 1.20;

// Variáveis globais de estado
float lastAccelMag = 0.0;
const float STEP_THRESHOLD = 0.6;             // pico para contar passo
bool aboveStep = false;
unsigned long lastStepTime = 0;
unsigned long stepCount = 0;
const unsigned long MIN_STEP_INTERVAL = 300;  // ms

// Ultimos valores lidos (para mandar no MQTT)
float currentAx = 0, currentAy = 0, currentAz = 0;
float currentMag = 0;
String currentState = "Parado";

// ====================== WIFI / MQTT ======================
void connectWiFi() {
  Serial.print("Conectando ao WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Conectando ao MQTT em ");
    Serial.print(MQTT_HOST);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    String clientId = "bracelet-esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("MQTT conectado!");
    } else {
      Serial.print("Falha MQTT, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" tentando novamente em 2s...");
      delay(2000);
    }
  }
}

// ====================== MPU6050 ======================
void setupMPU6050() {
  Wire.begin();
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);  // power management
  Wire.write(0);     // acorda
  Wire.endTransmission(true);
  Serial.println("MPU6050 inicializado.");
}

void readAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); // ACCEL_XOUT_H
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);

  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read();
  int16_t rawZ = (Wire.read() << 8) | Wire.read();

  ax = rawX / ACC_SCALE;
  ay = rawY / ACC_SCALE;
  az = rawZ / ACC_SCALE;
}

// Classificação baseada em aMag, com zero = parado
const char* classify(float ax, float ay, float az, float aMag) {
  if (fabs(ax) < ZERO_EPS && fabs(ay) < ZERO_EPS && fabs(az) < ZERO_EPS) {
    return "Parado";
  }
  if (aMag < THRESH_PARADO) {
    return "Parado";
  } else if (aMag < THRESH_MOV_LEVE) {
    return "Movimento leve";
  } else if (aMag < THRESH_ANDANDO) {
    return "Andando";
  } else if (aMag < THRESH_INTENSO) {
    return "Movimento intenso";
  } else {
    return "MUITO intenso";
  }
}

// Detecção simples de passos com base em picos na magnitude
void updateSteps(float aMag) {
  unsigned long now = millis();

  if (!aboveStep && aMag > STEP_THRESHOLD) {
    if (now - lastStepTime > MIN_STEP_INTERVAL) {
      stepCount++;
      lastStepTime = now;
    }
    aboveStep = true;
  }

  if (aboveStep && aMag < (STEP_THRESHOLD * 0.5f)) {
    aboveStep = false;
  }
}

// Processa uma amostra do sensor
void processSample() {
  readAccel(currentAx, currentAy, currentAz);

  currentMag = sqrt(currentAx * currentAx +
                    currentAy * currentAy +
                    currentAz * currentAz);

  const char* estado = classify(currentAx, currentAy, currentAz, currentMag);
  currentState = String(estado);

  updateSteps(currentMag);

  // Debug no Serial
  Serial.println("-------------------------------------------------");
  Serial.print("ax="); Serial.print(currentAx, 3);
  Serial.print(" ay="); Serial.print(currentAy, 3);
  Serial.print(" az="); Serial.print(currentAz, 3);
  Serial.println(" (g)");

  Serial.print("aMag="); Serial.print(currentMag, 3);
  Serial.print(" | state="); Serial.print(currentState);
  Serial.print(" | steps="); Serial.println(stepCount);
}

// Envia dados ao IoT Agent via MQTT (Ultralight)
void publishToFiware() {
  // Ultralight: a|<accelMag>|p|<steps>|s|<state>
  String payload = "a|" + String(currentMag, 3) +
                   "|p|" + String(stepCount) +
                   "|s|" + currentState;

  Serial.print("Publicando em ");
  Serial.print(mqttTopic);
  Serial.print(" => ");
  Serial.println(payload);

  mqttClient.publish(mqttTopic.c_str(), payload.c_str());
}

// ====================== SETUP & LOOP ======================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== Pulseira de Movimento com MPU6050 + MQTT (FIWARE) ===");

  connectWiFi();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  setupMPU6050();

  lastSampleMs  = millis();
  lastPublishMs = millis();
}

void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  unsigned long now = millis();

  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    processSample();
  }

  if (now - lastPublishMs >= PUBLISH_INTERVAL_MS) {
    lastPublishMs = now;
    publishToFiware();
  }
}
