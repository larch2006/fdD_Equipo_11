/*
 * ESP32 API para sensores con IP fija
 * Proporciona datos de sensores en formato JSON para ser consumidos por Flask
 * IP fija: 192.168.1.35
 * 
 * Sensores:
 * - pH (GPIO36/VP)
 * - TDS (GPIO39/VN)
 * - Temperatura agua DS18B20 (GPIO4)
 * - DHT22 (GPIO19)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ==================== CONFIGURACIÓN WiFi ====================
const char* ssid = "HONOR";
const char* password = "123ab45c";

// ==================== CONFIGURACIÓN IP FIJA ====================
IPAddress local_IP(192, 168, 1, 35);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

// ==================== CONFIGURACIÓN DE PINES ====================
// Sensor de pH
#define PH_PIN 36          // GPIO36 (VP)

// Sensor TDS
#define TDS_PIN 39         // GPIO39 (VN)
#define TDS_VREF 3.3       // Voltaje de referencia

// Sensor DS18B20 (temperatura agua)
#define ONE_WIRE_BUS 4     // GPIO4

// Sensor DHT22
#define DHT_PIN 19         // GPIO19
#define DHT_TYPE DHT22

// ==================== CONFIGURACIÓN ADC ====================
#define ADC_RESOLUTION 12  // 0-4095
#define ADC_ATTENUATION ADC_11db

// ==================== VARIABLES GLOBALES ====================
WebServer server(80);

// Inicializar sensores
DHT dht(DHT_PIN, DHT_TYPE);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Variables para promediado de ADC
const int NUM_READINGS = 10;
float phReadings[NUM_READINGS];
float tdsReadings[NUM_READINGS];
int readIndex = 0;

// ==================== CONFIGURACIÓN INICIAL ====================
void setup() {
  Serial.begin(115200);
  
  // Configurar ADC
  analogReadResolution(ADC_RESOLUTION);
  analogSetPinAttenuation(PH_PIN, ADC_ATTENUATION);
  analogSetPinAttenuation(TDS_PIN, ADC_ATTENUATION);
  
  // Inicializar sensores
  dht.begin();
  sensors.begin();
  
  // Inicializar arrays de promediado
  for (int i = 0; i < NUM_READINGS; i++) {
    phReadings[i] = 0;
    tdsReadings[i] = 0;
  }
  
  // Conectar a WiFi con IP fija
  connectToWiFi();
  
  // Configurar rutas del servidor
  setupRoutes();
  
  // Iniciar servidor
  server.begin();
  Serial.println("Servidor HTTP iniciado");
  Serial.println("API disponible en: http://192.168.1.35/api/sensores");
}

// ==================== FUNCIONES PRINCIPALES ====================
void loop() {
  server.handleClient();
  delay(10);  // Pequeña pausa para el watchdog
}

// ==================== CONEXIÓN WiFi CON IP FIJA ====================
void connectToWiFi() {
  // 1. Mostrar mensaje de inicio
  Serial.println("Conectando a WiFi...");
  
  // 2. Configurar modo WiFi como estación
  WiFi.mode(WIFI_STA);
  
  // 3. Configurar la IP fija
  Serial.println("Configurando IP fija: 192.168.1.35");
  bool configSuccess = WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  
  // 4. Verificar si la configuración fue exitosa
  if (!configSuccess) {
    Serial.println("Error configurando IP fija");
    Serial.println("Usando DHCP automático");
  } else {
    Serial.println("Configuración IP fija aplicada correctamente");
  }
  
  // 5. Conectarse al WiFi
  WiFi.begin(ssid, password);
  
  // 6. Esperar hasta que se conecte
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    
    // Si pasan 30 segundos sin conectar, reiniciar
    if (attempts > 60) {
      Serial.println("\nError: Tiempo de espera agotado. Reiniciando...");
      ESP.restart();
    }
  }
  
  // 7. Mostrar información de conexión
  Serial.println("");
  Serial.println("WiFi conectado");
  Serial.print("Dirección IP fija del ESP32: ");
  Serial.println(WiFi.localIP());
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("Subnet: ");
  Serial.println(WiFi.subnetMask());
  Serial.print("DNS Primario: ");
  Serial.println(WiFi.dnsIP(0));
  Serial.print("DNS Secundario: ");
  Serial.println(WiFi.dnsIP(1));
}

// ==================== CONFIGURACIÓN DE RUTAS ====================
void setupRoutes() {
  // Ruta principal
  server.on("/", HTTP_GET, handleRoot);
  
  // Ruta de sensores
  server.on("/api/sensores", HTTP_GET, handleSensores);
  
  // Ruta para CORS preflight
  server.on("/api/sensores", HTTP_OPTIONS, handleOptions);
  
  // Ruta 404
  server.onNotFound(handleNotFound);
}

// ==================== MANEJADORES DE RUTAS ====================

// Ruta principal
void handleRoot() {
  String response = "{\"mensaje\":\"API de sensores ESP32 funcionando\",";
  response += "\"endpoints\":{\"/api/sensores\":\"Datos de todos los sensores\"},";
  response += "\"ip_fija\":\"192.168.1.35\",";
  response += "\"status\":\"online\"}";
  
  server.send(200, "application/json", response);
}

// Ruta de sensores
void handleSensores() {
  // Leer y procesar todos los sensores
  float ph = readPH();
  float tds = readTDS();
  float tempAgua = readTempAgua();
  float humedad = readHumedad();
  float tempAmbiente = readTempAmbiente();
  
  // Determinar estados
  String phEstado = getEstadoPH(ph);
  String tdsEstado = getEstadoTDS(tds);
  String tempAguaEstado = getEstadoTempAgua(tempAgua);
  String humedadEstado = getEstadoHumedad(humedad);
  
  // Construir JSON
  String json = "{";
  
  // pH
  json += "\"ph\":{";
  json += "\"valor\":" + String(ph, 2) + ",";
  json += "\"unidad\":\"pH\",";
  json += "\"estado\":\"" + phEstado + "\"";
  json += "},";
  
  // TDS
  json += "\"tds\":{";
  json += "\"valor\":" + String(tds, 0) + ",";
  json += "\"unidad\":\"ppm\",";
  json += "\"estado\":\"" + tdsEstado + "\"";
  json += "},";
  
  // Temperatura agua
  json += "\"temperatura_agua\":{";
  json += "\"valor\":" + String(tempAgua, 1) + ",";
  json += "\"unidad\":\"°C\",";
  json += "\"estado\":\"" + tempAguaEstado + "\"";
  json += "},";
  
  // Humedad
  json += "\"humedad\":{";
  json += "\"valor\":" + String(humedad, 1) + ",";
  json += "\"unidad\":\"%\",";
  json += "\"estado\":\"" + humedadEstado + "\"";
  json += "},";
  
  // Temperatura ambiente
  json += "\"temperatura_ambiente\":{";
  json += "\"valor\":" + String(tempAmbiente, 1) + ",";
  json += "\"unidad\":\"°C\"";
  json += "}";
  
  json += "}";
  
  // Enviar respuesta con CORS
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(200, "application/json", json);
}

// Manejar CORS preflight
void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204);
}

// Página 404
void handleNotFound() {
  String message = "{\"error\":\"Ruta no encontrada\",\"status\":404}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(404, "application/json", message);
}

// ==================== LECTURA DE SENSORES ====================

// Leer sensor de pH con promediado
float readPH() {
  float sum = 0;
  
  for (int i = 0; i < NUM_READINGS; i++) {
    int raw = analogRead(PH_PIN);
    float voltage = (raw / 4095.0) * 3.3;
    
    // Fórmula ajustable para pH
    // 2.5V = pH 7, pendiente ajustable
    float ph = 7.0 + ((2.5 - voltage) * 1.2);  // Ajustar el factor 1.2 según calibración
    phReadings[readIndex] = ph;
    
    readIndex = (readIndex + 1) % NUM_READINGS;
    delay(50);
  }
  
  // Calcular promedio
  for (int i = 0; i < NUM_READINGS; i++) {
    sum += phReadings[i];
  }
  
  float avgPh = sum / NUM_READINGS;
  
  // Limitar valores válidos
  if (avgPh < 0 || avgPh > 14) {
    avgPh = -1;  // Valor inválido
  }
  
  return avgPh;
}

// Leer sensor TDS con compensación de temperatura
float readTDS() {
  float sum = 0;
  
  // Leer temperatura del agua para compensación
  float tempAgua = readTempAgua();
  
  for (int i = 0; i < NUM_READINGS; i++) {
    int raw = analogRead(TDS_PIN);
    float voltage = (raw / 4095.0) * TDS_VREF;
    
    // Compensación de temperatura (ecuación estándar para TDS)
    // TDS = (V / 3.3) * 1000 * factor_compensacion
    float tds = (voltage / TDS_VREF) * 1000;
    
    // Compensación por temperatura
    if (tempAgua > 0 && tempAgua < 100) {
      // Factor de compensación: 1 + 0.02 * (T - 25)
      float tempComp = 1 + 0.02 * (tempAgua - 25.0);
      tds = tds / tempComp;
    }
    
    tdsReadings[readIndex] = tds;
    readIndex = (readIndex + 1) % NUM_READINGS;
    delay(50);
  }
  
  // Calcular promedio
  for (int i = 0; i < NUM_READINGS; i++) {
    sum += tdsReadings[i];
  }
  
  float avgTds = sum / NUM_READINGS;
  
  // Limitar valores válidos (0-2000 ppm típico)
  if (avgTds < 0 || avgTds > 2000) {
    avgTds = 0;
  }
  
  return avgTds;
}

// Leer temperatura del agua (DS18B20)
float readTempAgua() {
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  
  // Verificar lectura válida
  if (temp == DEVICE_DISCONNECTED_C) {
    return -1;  // Valor inválido
  }
  
  return temp;
}

// Leer humedad del DHT22
float readHumedad() {
  float humedad = dht.readHumidity();
  
  // Verificar lectura válida
  if (isnan(humedad)) {
    return -1;  // Valor inválido
  }
  
  return humedad;
}

// Leer temperatura ambiente del DHT22
float readTempAmbiente() {
  float temp = dht.readTemperature();
  
  // Verificar lectura válida
  if (isnan(temp)) {
    return -1;  // Valor inválido
  }
  
  return temp;
}

// ==================== FUNCIONES DE ESTADO ====================

// Determinar estado del pH
String getEstadoPH(float ph) {
  if (ph < 0) return "error";
  if (ph >= 6.0 && ph <= 7.5) return "optimo";
  if ((ph >= 5.5 && ph < 6.0) || (ph > 7.5 && ph <= 8.5)) return "margen";
  return "critico";  // < 5.5 o > 8.5
}

// Determinar estado del TDS
String getEstadoTDS(float tds) {
  if (tds < 0) return "error";
  if (tds >= 300 && tds <= 700) return "optimo";
  if (tds > 700 && tds <= 1000) return "margen";
  if (tds > 1000) return "critico";
  return "margen";  // < 300
}

// Determinar estado de la temperatura del agua
String getEstadoTempAgua(float temp) {
  if (temp < 0) return "error";
  if (temp >= 18 && temp <= 28) return "optimo";
  if ((temp >= 15 && temp < 18) || (temp > 28 && temp <= 32)) return "margen";
  return "critico";  // < 15 o > 32
}

// Determinar estado de la humedad
String getEstadoHumedad(float humedad) {
  if (humedad < 0) return "error";
  if (humedad >= 40 && humedad <= 60) return "optimo";
  if (humedad > 60 && humedad <= 80) return "margen";
  if (humedad < 40) return "critico";
  return "critico";  // > 80
}