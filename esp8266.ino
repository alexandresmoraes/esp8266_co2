/*
  Sistema de Controle de CO2 para Fotossíntese
  Hardware:
  - NodeMCU ESP8266
  - Sensor MH-Z19E (RX: GPIO13/D7, TX: GPIO15/D8)
  - Relé para controle do solenóide (GPIO12/D6)
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <MHZ19.h>
#include <EEPROM.h>
#include <LittleFS.h>

// ==================== DEFINIÇÕES E CONSTANTES ====================

// Definição dos pinos
#define MH_Z19_RX 13       // GPIO13
#define MH_Z19_TX 15       // GPIO15
#define RELAY_PIN 12       // GPIO12 (D6) - Para o relé

// Configurações de rede
const char* AP_SSID = "ESP8266_CO2";   // Nome da rede WiFi no modo AP
const char* AP_PASSWORD = "12345678";  // Senha da rede WiFi no modo AP

// Estrutura para armazenar configurações
struct SystemConfig {
  char wifi_ssid[32];
  char wifi_pass[64];
  int min_co2;
  int max_co2;
  int reading_interval;
  bool operation_mode;
  char smartlife_id[64];
  char smartlife_key[64];
  bool enable_notifications;
  int magic_number;  // Para validar se as configurações foram salvas
};

// ==================== VARIÁVEIS GLOBAIS ====================

// Parâmetros de operação
int minCO2 = 800;          // Nível mínimo de CO2 (ppm)
int maxCO2 = 1500;         // Nível máximo de CO2 (ppm)
int readingInterval = 30;  // Intervalo de leitura em segundos
bool operationMode = true; // true = automático, false = manual
bool solenoidState = false;// Estado do solenóide (false = desligado, true = ligado)

// Armazenamento de leituras
#define MAX_HISTORY 288    // 24 horas com leituras a cada 5 minutos
int co2History[MAX_HISTORY];
float tempHistory[MAX_HISTORY];
unsigned long timeHistory[MAX_HISTORY];
int historyIndex = 0;
int historyCount = 0;

// Timestamp de controle
unsigned long lastReadingTime = 0;
unsigned long lastHistorySaveTime = 0;

// Variáveis de leitura atuais
int currentCO2 = 0;
float currentTemp = 0;

// Servidor web na porta 80
ESP8266WebServer server(80);

// DNS Server para o portal cativo no modo AP
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Software Serial para o sensor MH-Z19E
SoftwareSerial mhZ19Serial(MH_Z19_RX, MH_Z19_TX);

// Instância do sensor MH-Z19
MHZ19 mhz19;

// Instância da configuração
SystemConfig sysConfig;

// ==================== DECLARAÇÃO DE FUNÇÕES ====================

// Funções de configuração e inicialização
void loadConfig();
void saveConfig();
void setupWiFi();
void setupWebServer();

// Funções de controle e leitura
void readSensorData();
void controlSolenoid();
void setSolenoidState(bool state);
void calibrateMHZ19();
void saveToHistory();

// Funções de manipulação HTTP
void handleRoot();
void handleCSS();
bool handleFileRead(String path);
void handleNotFound();
void handleGetData();
void handleGetHistory();
void handleGetConfig();
void handleSaveConfig();
void handleSolenoidControl();
void handleCalibrate();
void handlePairDevice();

// ==================== FUNÇÕES DE CONFIGURAÇÃO E INICIALIZAÇÃO ====================

// Inicialização
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Iniciando Sistema de Controle de CO2");
  
  // Inicializa os pinos
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Inicia com o relé desligado
  
  // Inicializa a comunicação com o sensor MH-Z19E
  mhZ19Serial.begin(9600);
  mhz19.begin(mhZ19Serial);
  
  // Configura o sensor
  mhz19.autoCalibration(false);  // Desativa a auto-calibração para controle mais preciso
  
  // Inicializa o sistema de arquivos
  if (LittleFS.begin()) {
    Serial.println("Sistema de arquivos LittleFS iniciado");
    
    // Listar arquivos
    Dir dir = LittleFS.openDir("/");
    while (dir.next()) {
      Serial.print("Arquivo: ");
      Serial.print(dir.fileName());
      Serial.print(" - Tamanho: ");
      Serial.println(dir.fileSize());
    }
  } else {
    Serial.println("Falha ao iniciar o sistema de arquivos LittleFS");
  }
  
  // Carrega as configurações
  loadConfig();
  
  // Inicializa o WiFi
  setupWiFi();
  
  // Configura as rotas do servidor web
  setupWebServer();
  
  // Inicia o servidor web
  server.begin();
  Serial.println("Servidor HTTP iniciado");
  
  // Inicia o mDNS para facilitar o acesso
  if (MDNS.begin("co2control")) {
    Serial.println("mDNS iniciado - Acesse pelo endereço: http://co2control.local");
  }
  
  // Inicializa o array de histórico
  for (int i = 0; i < MAX_HISTORY; i++) {
    co2History[i] = 0;
    tempHistory[i] = 0;
    timeHistory[i] = 0;
  }
  
  // Primeira leitura
  readSensorData();
}

// Loop principal
void loop() {
  // Gerencia conexões WiFi e servidor DNS (no modo AP)
  if (WiFi.getMode() == WIFI_AP) {
    dnsServer.processNextRequest();
  }
  
  // Gerencia o servidor web
  server.handleClient();
  
  // Atualiza o mDNS
  MDNS.update();
  
  // Verifica se é hora de fazer uma nova leitura
  unsigned long currentTime = millis();
  if (currentTime - lastReadingTime > (readingInterval * 1000)) {
    readSensorData();
    controlSolenoid();
    lastReadingTime = currentTime;
  }
  
  // Salva no histórico a cada 5 minutos
  if (currentTime - lastHistorySaveTime > (5 * 60 * 1000)) {
    saveToHistory();
    lastHistorySaveTime = currentTime;
  }
  
  // Evita overflow do millis()
  if (currentTime < lastReadingTime || currentTime < lastHistorySaveTime) {
    lastReadingTime = currentTime;
    lastHistorySaveTime = currentTime;
  }
}

// Carrega as configurações da EEPROM
void loadConfig() {
  // Inicializa a EEPROM
  EEPROM.begin(sizeof(SystemConfig));
  
  // Lê a configuração da EEPROM
  EEPROM.get(0, sysConfig);
  EEPROM.end();
  
  // Verifica se as configurações são válidas checando o magic number
  if (sysConfig.magic_number != 0xC02C02) {
    // Configurações inválidas, inicializa com os valores padrão
    memset(&sysConfig, 0, sizeof(SystemConfig));
    sysConfig.min_co2 = minCO2;
    sysConfig.max_co2 = maxCO2;
    sysConfig.reading_interval = readingInterval;
    sysConfig.operation_mode = operationMode;
    sysConfig.enable_notifications = false;
    sysConfig.magic_number = 0xC02C02;
    
    Serial.println("Configurações inválidas, usando valores padrão");
  } else {
    // Configurações são válidas, atualiza as variáveis globais
    minCO2 = sysConfig.min_co2;
    maxCO2 = sysConfig.max_co2;
    readingInterval = sysConfig.reading_interval;
    operationMode = sysConfig.operation_mode;
    
    Serial.println("Configurações carregadas com sucesso");
  }
  
  // Log das configurações
  Serial.print("WiFi SSID: ");
  Serial.println(sysConfig.wifi_ssid);
  Serial.print("Min CO2: ");
  Serial.println(minCO2);
  Serial.print("Max CO2: ");
  Serial.println(maxCO2);
  Serial.print("Intervalo: ");
  Serial.println(readingInterval);
  Serial.print("Modo: ");
  Serial.println(operationMode ? "Automático" : "Manual");
}

// Salva as configurações na EEPROM
void saveConfig() {
  // Inicializa a EEPROM
  EEPROM.begin(sizeof(SystemConfig));
  
  // Salva a configuração na EEPROM
  EEPROM.put(0, sysConfig);
  EEPROM.commit();
  EEPROM.end();
  
  Serial.println("Configurações salvas na EEPROM");
}

// Configuração do WiFi
void setupWiFi() {
  // Verifica se já temos configurações WiFi salvas
  if (strlen(sysConfig.wifi_ssid) > 0) {
    // Tenta conectar à rede WiFi configurada
    WiFi.mode(WIFI_STA);
    WiFi.begin(sysConfig.wifi_ssid, sysConfig.wifi_pass);
    
    Serial.print("Conectando à rede WiFi ");
    Serial.print(sysConfig.wifi_ssid);
    
    // Aguarda até 20 segundos pela conexão
    int timeout = 20;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
      delay(1000);
      Serial.print(".");
      timeout--;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println();
      Serial.print("Conectado! IP: ");
      Serial.println(WiFi.localIP());
      return;
    }
    
    Serial.println();
    Serial.println("Falha na conexão. Iniciando modo AP...");
  }
  
  // Se não temos configurações ou a conexão falhou, inicia o modo AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  // Configura o DNS para o modo portal cativo
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  
  Serial.print("Modo AP iniciado. SSID: ");
  Serial.println(AP_SSID);
  Serial.print("IP do AP: ");
  Serial.println(WiFi.softAPIP());
}

// Configuração do servidor web
void setupWebServer() {
  // Rota para a página principal e APIs
  server.on("/", HTTP_GET, handleRoot);
  
  // Arquivos estáticos importantes que precisam ser tratados separadamente
  server.on("/chart.js", HTTP_GET, [](){ handleFileRead("/chart.js"); });
  
  // APIs para dados
  server.on("/api/data", HTTP_GET, handleGetData);
  server.on("/api/history", HTTP_GET, handleGetHistory);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handleSaveConfig);
  server.on("/api/solenoid", HTTP_POST, handleSolenoidControl);
  server.on("/api/calibrate", HTTP_POST, handleCalibrate);
  server.on("/api/pair", HTTP_POST, handlePairDevice);
  
  // Rota para resolver arquivos estáticos ou páginas não encontradas
  server.onNotFound(handleNotFound);
}

// ==================== FUNÇÕES DE CONTROLE E LEITURA ====================

// Função para ler dados do sensor MH-Z19E usando a biblioteca
void readSensorData() {
  // Lê o CO2 usando a biblioteca MHZ19
  currentCO2 = mhz19.getCO2();
  
  // Lê a temperatura se disponível (alguns modelos não fornecem)
  currentTemp = mhz19.getTemperature();
  
  // Apenas atualiza se a leitura for válida
  if (currentCO2 > 0) {
    Serial.print("CO2: ");
    Serial.print(currentCO2);
    Serial.print(" ppm, Temp: ");
    Serial.print(currentTemp);
    Serial.println(" °C");
  } else {
    Serial.println("Erro na leitura do sensor - valor inválido");
  }
}

// Função para controlar o solenóide baseado nos níveis de CO2
void controlSolenoid() {
  if (operationMode) {  // Somente controla automaticamente no modo automático
    if (currentCO2 < minCO2 && !solenoidState) {
      // CO2 abaixo do mínimo, liga o solenóide
      setSolenoidState(true);
    } else if (currentCO2 > maxCO2 && solenoidState) {
      // CO2 acima do máximo, desliga o solenóide
      setSolenoidState(false);
    }
  }
}

// Define o estado do solenóide
void setSolenoidState(bool state) {
  solenoidState = state;
  digitalWrite(RELAY_PIN, state ? HIGH : LOW);
  
  Serial.print("Solenóide: ");
  Serial.println(state ? "LIGADO" : "DESLIGADO");
  
  // Aqui você adicionaria a lógica para enviar o estado para o Smart Life
  // se a integração estiver ativa e as notificações habilitadas
  if (strlen(sysConfig.smartlife_id) > 0 && sysConfig.enable_notifications) {
    // Código para enviar o estado via BLE para o Smart Life Hub
  }
}

// Função para calibrar o sensor MH-Z19E usando a biblioteca
void calibrateMHZ19() {
  // Calibração do ponto zero (400ppm)
  mhz19.calibrateZero();
  
  Serial.println("Calibração de ponto zero (400ppm) iniciada");
  Serial.println("Certifique-se de que o sensor está em ambiente com ar fresco!");
}

// Adiciona os valores atuais ao histórico
void saveToHistory() {
  if (currentCO2 > 0) {  // Só salva se for uma leitura válida
    co2History[historyIndex] = currentCO2;
    tempHistory[historyIndex] = currentTemp;
    timeHistory[historyIndex] = millis() / 60000;  // Timestamp em minutos
    
    historyIndex = (historyIndex + 1) % MAX_HISTORY;
    if (historyCount < MAX_HISTORY) {
      historyCount++;
    }
    
    Serial.println("Adicionado ao histórico: CO2=" + String(currentCO2) + "ppm, Temp=" + String(currentTemp) + "°C");
  }
}

// ==================== FUNÇÕES DE MANIPULAÇÃO HTTP ====================

// Gerenciamento da página principal
void handleRoot() {
  if (handleFileRead("/index.html")) {
    return;  // O arquivo foi encontrado e enviado
  }
  
  // Se o arquivo não existir, enviamos o HTML simplificado
  server.send(200, "text/html", 
    "<html><head><title>CO2 Control</title></head><body>"
    "<h1>Sistema de Controle de CO2</h1>"
    "<p>Versão simplificada - carregue o dashboard completo via LittleFS</p>"
    "<p>CO2: <span id='co2'>...</span> ppm</p>"
    "<p>Solenóide: <span id='solenoid'>...</span></p>"
    "<script>"
    "setInterval(function() {"
    "fetch('/api/data').then(r=>r.json()).then(d=>{"
    "document.getElementById('co2').textContent = d.co2;"
    "document.getElementById('solenoid').textContent = d.solenoid ? 'LIGADO' : 'DESLIGADO';"
    "});"
    "}, 2000);"
    "</script></body></html>"
  );
}

// Gerenciamento do arquivo CSS
void handleCSS() {
  if (LittleFS.exists("/style.css")) {
    File file = LittleFS.open("/style.css", "r");
    if (file) {
      server.streamFile(file, "text/css");
      file.close();
      return;
    }
  }
  
  // CSS básico se o arquivo não existir
  server.send(200, "text/css", "body{font-family:Arial;margin:20px}");
}

// Função otimizada para servir arquivos estáticos
bool handleFileRead(String path) {
  Serial.println("handleFileRead: " + path);
  
  // Se o caminho não especificar um arquivo, carregue o index.html
  if (path.endsWith("/")) {
    path += "index.html";
  }
  
  // Determina o tipo de conteúdo baseado na extensão do arquivo
  String contentType;
  if (path.endsWith(".html")) {
    contentType = "text/html";
  } else if (path.endsWith(".css")) {
    contentType = "text/css";
  } else if (path.endsWith(".js")) {
    contentType = "application/javascript";
  } else if (path.endsWith(".json")) {
    contentType = "application/json";
  } else if (path.endsWith(".ico")) {
    contentType = "image/x-icon";
  } else {
    contentType = "text/plain";
  }
  
  // Verifica se o arquivo existe
  if (LittleFS.exists(path)) {
    File file = LittleFS.open(path, "r");
    if (file) {
      // Configurar para chunked transfer encoding
      server.sendHeader("Transfer-Encoding", "chunked");
      server.sendHeader("Cache-Control", "max-age=3600");
      
      // Enviar o conteúdo em chunks
      const size_t bufferSize = 1024;
      uint8_t buffer[bufferSize];
      size_t totalSent = 0;
      size_t chunkSize;
      
      WiFiClient client = server.client();
      
      // Envia cabeçalhos HTTP
      client.print("HTTP/1.1 200 OK\r\n");
      client.print("Content-Type: " + contentType + "\r\n");
      client.print("Connection: close\r\n");
      client.print("Cache-Control: max-age=3600\r\n"); // Habilita cache por 1 hora
      client.print("Transfer-Encoding: chunked\r\n");
      client.print("\r\n");
      
      // Enviar o arquivo em pedaços
      while (file.available()) {
        chunkSize = file.read(buffer, bufferSize);
        if (chunkSize > 0) {
          client.printf("%x\r\n", chunkSize); // Tamanho do chunk em hex
          client.write(buffer, chunkSize);
          client.print("\r\n");
          totalSent += chunkSize;
          // Pequeno delay para evitar problemas de buffer
          delay(1);
        }
      }
      
      // Finalizar chunked encoding
      client.print("0\r\n\r\n");
      
      file.close();
      Serial.printf("Arquivo enviado: %s (%u bytes)\n", path.c_str(), totalSent);
      return true;
    }
  }
  
  Serial.println("Arquivo não encontrado: " + path);
  return false;
}

// Função para lidar com solicitações de arquivos não encontrados
void handleNotFound() {
  // Verifica se a requisição é para um arquivo estático
  if (handleFileRead(server.uri())) {
    return;  // O arquivo foi encontrado e enviado
  }
  
  // Se estiver no modo AP, redireciona para a página principal
  if (WiFi.getMode() == WIFI_AP) {
    handleRoot();
  } else {
    // Página não encontrada
    server.send(404, "text/plain", "Página não encontrada");
  }
}

// API para obter dados atuais
void handleGetData() {
  DynamicJsonDocument doc(256);
  
  doc["co2"] = currentCO2;
  doc["temperature"] = currentTemp;
  doc["solenoid"] = solenoidState;
  doc["min_co2"] = minCO2;
  doc["max_co2"] = maxCO2;
  doc["auto_mode"] = operationMode;
  doc["wifi_mode"] = (WiFi.getMode() == WIFI_STA) ? "client" : "ap";
  
  if (WiFi.getMode() == WIFI_STA) {
    doc["wifi_ssid"] = WiFi.SSID();
    doc["wifi_strength"] = WiFi.RSSI();
  } else {
    doc["wifi_ssid"] = AP_SSID;
    doc["wifi_strength"] = 0;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API para obter histórico
void handleGetHistory() {
  DynamicJsonDocument doc(8192);  // Tamanho maior para o histórico
  
  JsonArray co2Array = doc.createNestedArray("co2");
  JsonArray tempArray = doc.createNestedArray("temp");
  JsonArray timeArray = doc.createNestedArray("time");
  
  // Calculamos o índice inicial para obter os últimos dados
  unsigned long currentTimestamp = millis() / 60000; // Tempo atual em minutos
  
  for (int i = 0; i < historyCount; i++) {
    int index = (historyIndex - historyCount + i + MAX_HISTORY) % MAX_HISTORY;
    
    co2Array.add(co2History[index]);
    tempArray.add(tempHistory[index]);
    
    // Calcula o timestamp relativo (minutos no passado)
    long relativeTime = 0;
    if (timeHistory[index] > 0) {
      relativeTime = (timeHistory[index] - currentTimestamp);
    }
    timeArray.add(relativeTime);
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API para obter configurações
void handleGetConfig() {
  DynamicJsonDocument doc(512);
  
  doc["min_co2"] = minCO2;
  doc["max_co2"] = maxCO2;
  doc["reading_interval"] = readingInterval;
  doc["auto_mode"] = operationMode;
  
  // Não enviamos a senha, mas enviamos o SSID
  if (strlen(sysConfig.wifi_ssid) > 0) {
    doc["wifi_ssid"] = sysConfig.wifi_ssid;
  }
  
  // Configurações do Smart Life
  if (strlen(sysConfig.smartlife_id) > 0) {
    doc["smartlife_id"] = sysConfig.smartlife_id;
    doc["enable_notifications"] = sysConfig.enable_notifications;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API para salvar configurações
void handleSaveConfig() {
  if (server.hasArg("plain")) {
    String json = server.arg("plain");
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);
    
    if (!error) {
      // Atualiza as configurações WiFi somente se forem fornecidas
      if (doc.containsKey("wifi_ssid") && doc.containsKey("wifi_pass")) {
        // Somente atualiza se houve mudança ou a senha não está vazia
        if (strcmp(sysConfig.wifi_ssid, doc["wifi_ssid"]) != 0 || 
            (doc.containsKey("wifi_pass") && strlen(doc["wifi_pass"]) > 0)) {
          
          strlcpy(sysConfig.wifi_ssid, doc["wifi_ssid"], sizeof(sysConfig.wifi_ssid));
          strlcpy(sysConfig.wifi_pass, doc["wifi_pass"], sizeof(sysConfig.wifi_pass));
        }
      }
      
      // Atualiza as configurações do sistema
      if (doc.containsKey("min_co2")) {
        sysConfig.min_co2 = doc["min_co2"];
        minCO2 = sysConfig.min_co2;
      }
      
      if (doc.containsKey("max_co2")) {
        sysConfig.max_co2 = doc["max_co2"];
        maxCO2 = sysConfig.max_co2;
      }
      
      if (doc.containsKey("reading_interval")) {
        sysConfig.reading_interval = doc["reading_interval"];
        readingInterval = sysConfig.reading_interval;
      }
      
      if (doc.containsKey("operation_mode")) {
        sysConfig.operation_mode = doc["operation_mode"];
        operationMode = sysConfig.operation_mode;
      }
      
      // Configurações do Smart Life
      if (doc.containsKey("smartlife_id")) {
        strlcpy(sysConfig.smartlife_id, doc["smartlife_id"], sizeof(sysConfig.smartlife_id));
      }
      
      if (doc.containsKey("smartlife_key")) {
        strlcpy(sysConfig.smartlife_key, doc["smartlife_key"], sizeof(sysConfig.smartlife_key));
      }
      
      if (doc.containsKey("enable_notifications")) {
        sysConfig.enable_notifications = doc["enable_notifications"];
      }
      
      // Define um magic number para validar a configuração
      sysConfig.magic_number = 0xC02C02;
      
      // Salva as configurações na EEPROM
      saveConfig();
      
      // Se as configurações de WiFi foram alteradas, reinicia a conexão
      if (doc.containsKey("wifi_ssid") && doc.containsKey("wifi_pass")) {
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configurações salvas. Reiniciando WiFi...\"}");
        delay(1000);
        ESP.restart();  // Reinicia o ESP para aplicar as novas configurações WiFi
      } else {
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configurações salvas com sucesso\"}");
      }
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Erro no formato JSON\"}");
    }
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Dados não fornecidos\"}");
  }
}

// API para controle manual do solenóide
void handleSolenoidControl() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Dados não fornecidos\"}");
    return;
  }
  
  String json = server.arg("plain");
  
  DynamicJsonDocument doc(64);
  DeserializationError error = deserializeJson(doc, json);
  
  if (error || !doc.containsKey("state")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Dados inválidos\"}");
    return;
  }
  
  // Se estiver no modo automático, rejeita o comando
  if (operationMode) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Solenóide em modo automático\"}");
    return;
  }
  
  // Atualiza o estado do solenóide
  bool newState = doc["state"];
  setSolenoidState(newState);
  
  // Responde com o novo estado
  server.send(200, "application/json", 
    "{\"status\":\"success\",\"state\":" + String(solenoidState ? "true" : "false") + "}");
}

// API para calibrar o sensor
void handleCalibrate() {
  // Envia o comando de calibração para o sensor MH-Z19E usando a biblioteca
  calibrateMHZ19();
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Calibração iniciada\"}");
}

// API para emparelhar com o Smart Life
void handlePairDevice() {
  // Esta função é um placeholder para a integração Smart Life
  // Na implementação real, aqui seria iniciado o procedimento de emparelhamento Bluetooth
  
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Função de emparelhamento em desenvolvimento\"}");
}