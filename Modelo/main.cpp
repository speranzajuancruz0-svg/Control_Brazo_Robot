#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <AccelStepper.h>
#include <MultiStepper.h>
#include <ArduinoJson.h>
#include <WebServer.h> 
#include <SPIFFS.h> 
#include <Wire.h>
#include <AS5600.h>

// --- CONFIGURACIÓN DE RED Y MQTT ---
const char* ssid = "Telecentro-0325";          
const char* password = "EET67YH6AXHG";  

const char* mqtt_server_A = "192.168.0.184";
const char* mqtt_server_B = "192.168.0.189";
const char* mqtt_server_actual = mqtt_server_A; 

// --- PINES I2C PARA ESP32-S3 ---
#define SDA_PIN 8
#define SCL_PIN 9

// --- CONFIGURACIÓN DE MOTORES ---
#define MOTOR_INTERFACE_TYPE 1

AccelStepper stepperX(MOTOR_INTERFACE_TYPE, 4, 16);
AccelStepper stepperY(MOTOR_INTERFACE_TYPE, 17, 5);
AccelStepper stepperZ(MOTOR_INTERFACE_TYPE, 18, 19);

MultiStepper grupoMotores; 

// --- OBJETOS DE RED Y ENCODER ---
WiFiClient espClient;
PubSubClient client(espClient);
WebServer server(80); 
AS5600 encoderX; // Instancia del encoder AS5600 (para eje X)

// --- MÁQUINAS Y REDUCCIONES ---
const int pasosPorVueltaMotor1 = 200;
const int pasosPorVueltaMotor2 = 200;
const int pasosPorVueltaMotor3 = 200;

const float relacionReduccion = 20.0; 
const float relacionReduccion2 = 20.0;
const float relacionReduccion3 = 8.0;

const float pasosPorVueltaSalida1 = pasosPorVueltaMotor1 * relacionReduccion;  // 4000
const float pasosPorVueltaSalida2 = pasosPorVueltaMotor2 * relacionReduccion2; // 4000
const float pasosPorVueltaSalida3 = pasosPorVueltaMotor3 * relacionReduccion3; // 200

bool continuoX = false; bool continuoY = false; bool continuoZ = false;
float velocidadConstante = 500; 

long posicionesDestino[3] = {0, 0, 0};
float anguloRealX = 0.0;

// --- FUNCIONES CONVERSIÓN GRADOS -> PASOS ---
long gradosAPasos1(float grados) { return (grados * pasosPorVueltaSalida1) / 360.0; }
long gradosAPasos2(float grados) { return (grados * pasosPorVueltaSalida2) / 360.0; }
long gradosAPasos3(float grados) { return (grados * pasosPorVueltaSalida3) / 360.0; }

// --- WEBSERVER SPIFFS ---
void enviarPaginaWeb() {
  if (SPIFFS.exists("/index.html")) {
    File archivo = SPIFFS.open("/index.html", "r");
    server.streamFile(archivo, "text/html");
    archivo.close();
  } else {
    server.send(404, "text/plain", "❌ Error al cargar index.html desde SPIFFS.");
  }
}

// --- TAREA FREERTOS 1: CONTROL DE MOTORES ---
void MotoresTask(void * pvParameters) {
  for(;;) {
    if (continuoX) stepperX.runSpeed();
    if (continuoY) stepperY.runSpeed();
    if (continuoZ) stepperZ.runSpeed();
    
    if (!continuoX && !continuoY && !continuoZ) {
      grupoMotores.run(); // Ejecución coordinada MultiStepper
    }
    vTaskDelay(pdMS_TO_TICKS(1)); 
  }
}

// --- TAREA FREERTOS 2: LECTURA DE ENCODER ---
void EncodersTask(void * pvParameters) {
  for(;;) {
    if (encoderX.isConnected()) {
      uint16_t rawAngle = encoderX.readAngle();
      anguloRealX = rawAngle * (360.0 / 4096.0);
      
      // Opcional: Descomentar si se quiere monitorear la lectura por consola
      // Serial.printf("Encoder X: %.2f°\n", anguloRealX);
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Muestreo cada 100 ms
  }
}

// --- CONEXIÓN WIFI ---
void setup_wifi() {
  delay(10);
  Serial.print("Conectando a WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print(".");
  }
  Serial.println("\n✓ WiFi Conectado.");
  Serial.print("👉 IP WebServer: http://");
  Serial.println(WiFi.localIP());
}

// --- RECEPCIÓN MQTT ---
void callback(char* topic, byte* payload, unsigned int length) {
  String mensaje = "";
  for (unsigned int i = 0; i < length; i++) { mensaje += (char)payload[i]; }
  
  // CASO A: COORDENADAS COORDINADAS (JSON)
  if (strcmp(topic, "motores/punto") == 0) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, mensaje);

    if (error) {
      Serial.print("❌ Error JSON: ");
      Serial.println(error.c_str());
      return;
    }

    continuoX = false; continuoY = false; continuoZ = false;

    posicionesDestino[0] = gradosAPasos1(doc["x"].as<float>());
    posicionesDestino[1] = gradosAPasos2(doc["y"].as<float>());
    posicionesDestino[2] = gradosAPasos3(doc["z"].as<float>());

    grupoMotores.moveTo(posicionesDestino);
    Serial.printf("🚀 Nueva trayectoria: X:%ld, Y:%ld, Z:%ld\n", posicionesDestino[0], posicionesDestino[1], posicionesDestino[2]);
  }
  // CASO B: CONTROL INDIVIDUAL / GIROS CONTINUOS
  else if (strcmp(topic, "motores/ejeX") == 0) {
    if (mensaje == "CON") { continuoX = true; stepperX.setSpeed(velocidadConstante); }
    else if (mensaje == "STP") { continuoX = false; stepperX.stop(); }
  }
  else if (strcmp(topic, "motores/ejeY") == 0) {
    if (mensaje == "CON") { continuoY = true; stepperY.setSpeed(velocidadConstante); }
    else if (mensaje == "STP") { continuoY = false; stepperY.stop(); }
  }
  else if (strcmp(topic, "motores/ejeZ") == 0) {
    if (mensaje == "CON") { continuoZ = true; stepperZ.setSpeed(velocidadConstante); }
    else if (mensaje == "STP") { continuoZ = false; stepperZ.stop(); }
  }
}

// --- CONEXIÓN Y SUSCRIPCIÓN MQTT ---
void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP32S3_Robot_";
    clientId += String(random(0, 0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      client.subscribe("motores/punto");
      client.subscribe("motores/ejeX");
      client.subscribe("motores/ejeY");
      client.subscribe("motores/ejeZ");
      Serial.println("✓ Conectado al Broker MQTT.");
    } else {
      delay(3000);
    }
  }
}

// --- SETUP PRINCIPAL ---
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000); 

  SPIFFS.begin(true);

  // Inicializar bus I2C para el AS5600
  Wire.begin(SDA_PIN, SCL_PIN);
  if (encoderX.begin()) {
    Serial.println("✓ Encoder AS5600 detectado en I2C.");
  } else {
    Serial.println("❌ ERROR: AS5600 no detectado en I2C.");
  }

  // Configuración de los Steppers
  stepperX.setMaxSpeed(150);
  stepperY.setMaxSpeed(150);
  stepperZ.setMaxSpeed(50);

  grupoMotores.addStepper(stepperX);
  grupoMotores.addStepper(stepperY);
  grupoMotores.addStepper(stepperZ);

  // Tareas en Paralelo (Core 0 para motores y lectura de encoders)
  xTaskCreatePinnedToCore(MotoresTask, "TareaMotores", 8192, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(EncodersTask, "TareaEncoders", 4096, NULL, 1, NULL, 0);

  setup_wifi();
  client.setServer(mqtt_server_actual, 1883);
  client.setCallback(callback);

  server.on("/", enviarPaginaWeb);
  server.begin();
}

// --- LOOP PRINCIPAL (Gobernado por Servidor y MQTT) ---
void loop() {
  if (!client.connected()) reconnect();
  client.loop();
  server.handleClient(); 

  // Publicar la posición del Encoder por MQTT de forma periódica
  static unsigned long lastMqttPub = 0;
  if (millis() - lastMqttPub > 200) { // Publica cada 200 ms
    lastMqttPub = millis();
    if (client.connected() && encoderX.isConnected()) {
      char msgBuf[16];
      dtostrf(anguloRealX, 4, 2, msgBuf);
      client.publish("motores/encoder/X", msgBuf);
    }
  }
}