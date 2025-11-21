//Enzo Fernandes Ramos RM:563705
//Felipe Henrique de Souza Cerazi RM:562746
//Gustavo Peaguda de Castro RM:562923
// ================== INCLUDES ==================
#include <WiFi.h>
#include <PubSubClient.h>
#include "DHTesp.h"
#include <math.h>
 
// ================== CONFIGURAÇÕES EDITÁVEIS ==================
 
// Wi-Fi (Wokwi)
const char* default_SSID       = "Wokwi-GUEST";
const char* default_PASSWORD   = "";
 
// Broker MQTT (IoT Agent MQTT na VM)
const char* default_BROKER_MQTT = "102.37.18.193"; // IP da VM / Broker
const int   default_BROKER_PORT = 1883;
 
// Device no FIWARE (IoT Agent)
const char* default_SERVICE_PATH      = "/TEF";
const char* default_DEVICE_ID         = "env001";  // novo dispositivo
const char* default_TOPICO_SUBSCRIBE  = "/TEF/env001/cmd";     // comandos (opcional)
const char* default_TOPICO_PUBLISH    = "/TEF/env001/attrs";   // atributos Ultralight
 
const char* default_ID_MQTT     = "fiware_env001";
 
// Pinos
const int MIC_PIN  = 34; // wokwi-microphone → esp:34
const int DHT_PIN  = 15; // dht1:SDA → esp:15 (DHT22)
const int LED_PIN  = 2;  // LED onboard
 
// ================== VARIÁVEIS GLOBAIS ==================
char* SSID             = const_cast<char*>(default_SSID);
char* PASSWORD         = const_cast<char*>(default_PASSWORD);
char* BROKER_MQTT      = const_cast<char*>(default_BROKER_MQTT);
int   BROKER_PORT      = default_BROKER_PORT;
char* TOPICO_SUBSCRIBE = const_cast<char*>(default_TOPICO_SUBSCRIBE);
char* TOPICO_PUBLISH   = const_cast<char*>(default_TOPICO_PUBLISH);
char* ID_MQTT          = const_cast<char*>(default_ID_MQTT);
 
WiFiClient    espClient;
PubSubClient  MQTT(espClient);
DHTesp        dht;
 
unsigned long ultimoEnvio = 0;
// ✅ Mais rápido: antes 5000ms, agora 2000ms
const unsigned long intervaloEnvio = 2000; // envia a cada 2 segundos
 
// ================== FUNÇÕES AUXILIARES ==================
 
void initSerial() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("===== Monitor de Ambiente - FIWARE =====");
}
 
void initWiFi() {
  delay(10);
  Serial.println("------ Conexao WI-FI ------");
  Serial.print("Conectando-se na rede: ");
  Serial.println(SSID);
 
  WiFi.begin(SSID, PASSWORD);
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
 
  Serial.println();
  Serial.println("Wi-Fi conectado com sucesso!");
  Serial.print("IP obtido: ");
  Serial.println(WiFi.localIP());
}
 
void reconectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
 
  Serial.println("Wi-Fi desconectado, tentando reconectar...");
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Wi-Fi reconectado. IP: ");
  Serial.println(WiFi.localIP());
}
 
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  // Debug de comandos, se usar /cmd
  Serial.print("Mensagem recebida no topico [");
  Serial.print(topic);
  Serial.print("]: ");
 
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.println(msg);
}
 
void initMQTT() {
  MQTT.setServer(BROKER_MQTT, BROKER_PORT);
  MQTT.setCallback(mqtt_callback);
}
 
void reconnectMQTT() {
  while (!MQTT.connected()) {
    Serial.print("Tentando se conectar ao Broker MQTT: ");
    Serial.print(BROKER_MQTT);
    Serial.print(":");
    Serial.println(BROKER_PORT);
 
    if (MQTT.connect(ID_MQTT)) {
      Serial.println("Conectado com sucesso ao broker MQTT!");
      MQTT.subscribe(TOPICO_SUBSCRIBE); // se for usar comandos
    } else {
      Serial.print("Falha ao conectar. Codigo de retorno = ");
      Serial.println(MQTT.state());
      Serial.println("Nova tentativa em 2 segundos...");
      delay(2000);
    }
  }
}
 
void verificaConexoesWiFiEMQTT() {
  reconectWiFi();
  if (!MQTT.connected()) {
    reconnectMQTT();
  }
}
 
// Pisca o LED no boot só para mostrar que está vivo
void initOutput() {
  pinMode(LED_PIN, OUTPUT);
  bool toggle = false;
 
  for (int i = 0; i < 10; i++) {
    toggle = !toggle;
    digitalWrite(LED_PIN, toggle);
    delay(120);
  }
  digitalWrite(LED_PIN, LOW);
}
 
// ================== LEITURA DOS SENSORES ==================
 
// ✅ Versão mais rápida: menos amostras, sem delay extra
float lerSomEmDecibeis() {
  int minVal = 4095;
  int maxVal = 0;
 
  // Janela de amostragem mais curta para agilizar (antes 200, agora 60)
  const int numAmostras = 60;
 
  for (int i = 0; i < numAmostras; i++) {
    int val = analogRead(MIC_PIN);
    if (val < minVal) minVal = val;
    if (val > maxVal) maxVal = val;
    // delay(0) praticamente não atrasa nada, só garante yield
    delay(0);
  }
 
  int amplitude = maxVal - minVal;
  if (amplitude < 1) amplitude = 1;  // evita log10(0)
 
  // Conversão simplificada para dB (didática, não calibrada)
  float dB = 20.0 * log10((float)amplitude) + 30.0;
 
  Serial.print("Amplitude: ");
  Serial.print(amplitude);
  Serial.print(" -> Ruido (dB): ");
  Serial.println(dB);
 
  return dB;
}
 
bool lerTemperaturaUmidade(float& tempC, float& umid) {
  TempAndHumidity data = dht.getTempAndHumidity();
  if (isnan(data.temperature) || isnan(data.humidity)) {
    Serial.println("Falha ao ler DHT22");
    return false;
  }
  tempC = data.temperature;
  umid  = data.humidity;
 
  Serial.print("Temperatura (C): ");
  Serial.print(tempC);
  Serial.print("  Umidade (%): ");
  Serial.println(umid);
 
  return true;
}
 
// ================== PUBLICAÇÃO NO FIWARE ==================
 
void enviaLeiturasMQTT() {
  float ruidoDB   = lerSomEmDecibeis();
  float tempC     = 0.0;
  float umidade   = 0.0;
 
  if (!lerTemperaturaUmidade(tempC, umidade)) {
    // Não envia nada se o DHT falhar nessa leitura
    return;
  }
 
  // Ultralight: n|<dB>|t|<temp>|h|<umid>
  char payload[100];
  snprintf(payload, sizeof(payload),
           "n|%.1f|t|%.1f|h|%.1f",
           ruidoDB, tempC, umidade);
 
  Serial.print("Enviando para topico ");
  Serial.print(TOPICO_PUBLISH);
  Serial.print(" -> ");
  Serial.println(payload);
 
  MQTT.publish(TOPICO_PUBLISH, payload);
}
 
// ================== SETUP & LOOP ==================
 
void setup() {
  initSerial();
  initOutput();
 
  // Sensores
  dht.setup(DHT_PIN, DHTesp::DHT22);
  pinMode(MIC_PIN, INPUT);
 
  // Conexões
  initWiFi();
  initMQTT();
  reconnectMQTT();
 
  // Opcional: força um primeiro envio rápido
  ultimoEnvio = millis() - intervaloEnvio;
}
 
void loop() {
  verificaConexoesWiFiEMQTT();
  MQTT.loop(); // mantém a conexão MQTT viva
 
  unsigned long agora = millis();
  if (agora - ultimoEnvio >= intervaloEnvio) {
    ultimoEnvio = agora;
    enviaLeiturasMQTT();
  }
}
 
