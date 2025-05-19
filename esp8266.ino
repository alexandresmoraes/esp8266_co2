/*
  Sistema de Controle de CO2 para Fotossíntese
  Hardware:
  - NodeMCU ESP8266
  - Sensor MH-Z19E (RX: GPIO13/D7, TX: GPIO15/D8)
  - Relé para controle do solenóide (GPIO12/D6)
  
  Modificações:
  - Adicionado suporte à calibração span do MH-Z19E
  - Integração com Hub Smart Life via API WiFi
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
#include <ESP8266HTTPClient.h>

// ==================== DEFINIÇÕES E CONSTANTES ====================

// Definição dos pinos
#define MH_Z19_RX 13       // GPIO13
#define MH_Z19_TX 15       // GPIO15
#define RELAY_PIN 12       // GPIO12 (D6) - Para o relé

// Configurações de rede
const char* AP_SSID = "ESP8266_CO2";   // Nome da rede WiFi no modo AP
const char* AP_PASSWORD = "12345678";  // Senha da rede WiFi no modo AP

// Valores padrão de calibração e operação
const int DEFAULT_CO2_SPAN = 2000;      // Valor padrão para calibração span (2000ppm)

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
  char smartlife_ip[16];      // Armazena o IP do Hub Smart Life
  int smartlife_port;         // Porta do Hub Smart Life (normalmente 80)
  bool enable_notifications;
  int co2_span_value;         // Valor de calibração span
  int magic_number;           // Para validar se as configurações foram salvas
};

// ==================== VARIÁVEIS GLOBAIS ====================

// Parâmetros de operação
int minCO2 = 800;          // Nível mínimo de CO2 (ppm)
int maxCO2 = 1500;         // Nível máximo de CO2 (ppm)
int readingInterval = 30;  // Intervalo de leitura em segundos
bool operationMode = true; // true = automático, false = manual
bool solenoidState = false;// Estado do solenóide (false = desligado, true = ligado)
int co2SpanValue = DEFAULT_CO2_SPAN; // Valor para calibração span

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
unsigned long lastSmartLifeUpdateTime = 0;

// Variáveis de leitura atuais
int currentCO2 = 0;
float currentTemp = 0;

// Timestamp da última ativação
unsigned long lastSolenoidActivation = 0;
unsigned long solenoidTotalActiveTime = 0;
unsigned long solenoidActivationCount = 0;

// Variáveis para conexão com o Smart Life Hub
bool smartLifeConnected = false;
unsigned long lastSmartLifeConnectionAttempt = 0;

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
void calibrateMHZ19Span(int spanValue);
void saveToHistory();

// Funções de Smart Life Hub
void connectToSmartLifeHub();
void updateSmartLifeHub();
void sendStateToSmartLifeHub(bool state);

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
void handleCalibrateSpan();
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
  
  // Atualiza o Smart Life Hub a cada 30 segundos (se ativado)
  if (sysConfig.enable_notifications && 
      strlen(sysConfig.smartlife_ip) > 0 && 
      currentTime - lastSmartLifeUpdateTime > (30 * 1000)) {
    updateSmartLifeHub();
    lastSmartLifeUpdateTime = currentTime;
  }
  
  // Tenta reconectar ao Smart Life Hub a cada 5 minutos se necessário
  if (sysConfig.enable_notifications && 
      strlen(sysConfig.smartlife_ip) > 0 && 
      !smartLifeConnected && 
      currentTime - lastSmartLifeConnectionAttempt > (5 * 60 * 1000)) {
    connectToSmartLifeHub();
    lastSmartLifeConnectionAttempt = currentTime;
  }
  
  // Evita overflow do millis()
  if (currentTime < lastReadingTime || currentTime < lastHistorySaveTime || 
      currentTime < lastSmartLifeUpdateTime || currentTime < lastSmartLifeConnectionAttempt) {
    lastReadingTime = currentTime;
    lastHistorySaveTime = currentTime;
    lastSmartLifeUpdateTime = currentTime;
    lastSmartLifeConnectionAttempt = currentTime;
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
    sysConfig.co2_span_value = DEFAULT_CO2_SPAN;
    sysConfig.smartlife_port = 80;
    sysConfig.magic_number = 0xC02C02;
    
    Serial.println("Configurações inválidas, usando valores padrão");
  } else {
    // Configurações são válidas, atualiza as variáveis globais
    minCO2 = sysConfig.min_co2;
    maxCO2 = sysConfig.max_co2;
    readingInterval = sysConfig.reading_interval;
    operationMode = sysConfig.operation_mode;
    co2SpanValue = sysConfig.co2_span_value;
    
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
  Serial.print("Valor Span CO2: ");
  Serial.println(co2SpanValue);
  
  if (strlen(sysConfig.smartlife_ip) > 0) {
    Serial.print("Smart Life Hub IP: ");
    Serial.println(sysConfig.smartlife_ip);
    Serial.print("Smart Life Hub Porta: ");
    Serial.println(sysConfig.smartlife_port);
  }
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
      
      // Tenta conectar ao Smart Life Hub se configurado
      if (strlen(sysConfig.smartlife_ip) > 0 && sysConfig.enable_notifications) {
        connectToSmartLifeHub();
      }
      
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
  server.on("/api/calibrate_span", HTTP_POST, handleCalibrateSpan); // Nova rota para calibração span
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
  unsigned long currentTime = millis();
  
  // Se estiver mudando de estado
  if (solenoidState != state) {
    // Se estava ligado e agora vai desligar, registra o tempo que ficou ativo
    if (solenoidState && !state && lastSolenoidActivation > 0) {
      solenoidTotalActiveTime += (currentTime - lastSolenoidActivation);
      solenoidActivationCount++;
    }
    
    // Se estiver ligando, registra o timestamp
    if (!solenoidState && state) {
      lastSolenoidActivation = currentTime;
    }
  }
  
  solenoidState = state;
  digitalWrite(RELAY_PIN, state ? HIGH : LOW);
  
  Serial.print("Solenóide: ");
  Serial.println(state ? "LIGADO" : "DESLIGADO");
  
  // Envia o estado para o Smart Life Hub se habilitado
  if (strlen(sysConfig.smartlife_ip) > 0 && sysConfig.enable_notifications) {
    sendStateToSmartLifeHub(state);
  }
}

// Função para calibrar o sensor MH-Z19E em zero point (400ppm)
void calibrateMHZ19() {
  // Calibração do ponto zero (400ppm)
  mhz19.calibrateZero();
  
  Serial.println("Calibração de ponto zero (400ppm) iniciada");
  Serial.println("Certifique-se de que o sensor está em ambiente com ar fresco!");
}

// NOVA FUNÇÃO: Calibração span do sensor MH-Z19E
void calibrateMHZ19Span(int spanValue) {
  // Verifica se o valor é válido (normalmente entre 1000 e 5000 ppm)
  if (spanValue >= 1000 && spanValue <= 5000) {
    // Aplica a calibração span    
    byte cmd[9] = {0xFF, 0x01, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    cmd[3] = (spanValue >> 8) & 0xFF;
    cmd[4] = spanValue & 0xFF;
    
    // Calcula o checksum
    byte checksum = 0;
    for (int i = 1; i < 8; i++) {
        checksum += cmd[i];
    }
    checksum = 0xFF - checksum + 1;
    cmd[8] = checksum;

    // Envia o comando
    mhZ19Serial.write(cmd, 9);
    
    // Salva o valor de span nas configurações
    sysConfig.co2_span_value = spanValue;
    saveConfig();
    
    Serial.print("Calibração SPAN com valor ");
    Serial.print(spanValue);
    Serial.println(" ppm iniciada");
    Serial.println("Certifique-se de que o sensor está exposto a uma concentração conhecida de CO2!");
  } else {
    Serial.print("Valor de span inválido: ");
    Serial.println(spanValue);
  }
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

// ==================== FUNÇÕES DE SMART LIFE HUB ====================

// Conecta ao Smart Life Hub
void connectToSmartLifeHub() {
  if (WiFi.status() != WL_CONNECTED || strlen(sysConfig.smartlife_ip) == 0) {
    smartLifeConnected = false;
    return;
  }
  
  // Aqui usamos comunicação HTTP para verificar se o Hub está acessível
  WiFiClient client;
  HTTPClient http;
  
  String url = "http://" + String(sysConfig.smartlife_ip) + ":" + String(sysConfig.smartlife_port) + "/api/status";
  
  Serial.print("Conectando ao Smart Life Hub via WiFi: ");
  Serial.println(url);
  
  http.begin(client, url);
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  if (httpCode > 0) {
    Serial.print("Resposta HTTP: ");
    Serial.println(httpCode);
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("Resposta do Hub: " + payload);
      smartLifeConnected = true;
    } else {
      smartLifeConnected = false;
    }
  } else {
    Serial.print("Falha na conexão com Hub. Erro: ");
    Serial.println(http.errorToString(httpCode));
    smartLifeConnected = false;
  }
  
  http.end();
  lastSmartLifeConnectionAttempt = millis();
}

// Atualiza o Smart Life Hub com os dados atuais
void updateSmartLifeHub() {
  if (WiFi.status() != WL_CONNECTED || strlen(sysConfig.smartlife_ip) == 0) {
    return;
  }
  
  WiFiClient client;
  HTTPClient http;
  
  String url = "http://" + String(sysConfig.smartlife_ip) + ":" + String(sysConfig.smartlife_port) + "/api/update";
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  // Cria um JSON com os dados atuais
  DynamicJsonDocument doc(256);
  doc["device_id"] = sysConfig.smartlife_id;
  doc["api_key"] = sysConfig.smartlife_key;
  doc["co2"] = currentCO2;
  doc["temperature"] = currentTemp;
  doc["solenoid"] = solenoidState;
  
  String jsonData;
  serializeJson(doc, jsonData);
  
  int httpCode = http.POST(jsonData);
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("Atualização do Hub bem-sucedida");
      smartLifeConnected = true;
    } else {
      Serial.print("Falha na atualização do Hub. Código: ");
      Serial.println(httpCode);
    }
  } else {
    Serial.print("Erro na conexão HTTP: ");
    Serial.println(http.errorToString(httpCode));
    smartLifeConnected = false;
  }
  
  http.end();
}

// Envia o estado do solenóide para o Smart Life Hub
void sendStateToSmartLifeHub(bool state) {
  if (WiFi.status() != WL_CONNECTED || strlen(sysConfig.smartlife_ip) == 0) {
    return;
  }
  
  WiFiClient client;
  HTTPClient http;
  
  String url = "http://" + String(sysConfig.smartlife_ip) + ":" + String(sysConfig.smartlife_port) + "/api/control";
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  // Cria um JSON com os dados atuais
  DynamicJsonDocument doc(256);
  doc["device_id"] = sysConfig.smartlife_id;
  doc["api_key"] = sysConfig.smartlife_key;
  doc["solenoid"] = state;
  
  String jsonData;
  serializeJson(doc, jsonData);
  
  int httpCode = http.POST(jsonData);
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      Serial.println("Comando enviado ao Hub com sucesso");
    } else {
      Serial.print("Falha no envio do comando ao Hub. Código: ");
      Serial.println(httpCode);
    }
  } else {
    Serial.print("Erro na conexão HTTP: ");
    Serial.println(http.errorToString(httpCode));
  }
  
  http.end();
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
  DynamicJsonDocument doc(512);
  
  doc["co2"] = currentCO2;
  doc["temperature"] = currentTemp;
  doc["solenoid"] = solenoidState;
  doc["min_co2"] = minCO2;
  doc["max_co2"] = maxCO2;
  doc["auto_mode"] = operationMode;
  doc["wifi_mode"] = (WiFi.getMode() == WIFI_STA) ? "client" : "ap";
  doc["co2_span_value"] = co2SpanValue;
  
  // Adiciona informações do Smart Life Hub
  doc["smartlife_connected"] = smartLifeConnected;
  
  if (WiFi.getMode() == WIFI_STA) {
    doc["wifi_ssid"] = WiFi.SSID();
    doc["wifi_strength"] = WiFi.RSSI();
  } else {
    doc["wifi_ssid"] = AP_SSID;
    doc["wifi_strength"] = 0;
  }
  
  // Adiciona informações de estatísticas de ativação
  doc["solenoid_total_active_time"] = solenoidTotalActiveTime / 60000; // Converte para minutos
  doc["solenoid_activation_count"] = solenoidActivationCount;
  
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
  JsonArray solenoidArray = doc.createNestedArray("solenoid"); // Para histórico de ativações
  
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
    
    // Adiciona informações simuladas de ativação do solenóide
    // Em uma implementação real, isso viria de um array de histórico de ativações
    solenoidArray.add(random(0, 30)); // Exemplo - mostrando minutos ativos por período
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
  doc["co2_span_value"] = co2SpanValue;
  
  // Não enviamos a senha, mas enviamos o SSID
  if (strlen(sysConfig.wifi_ssid) > 0) {
    doc["wifi_ssid"] = sysConfig.wifi_ssid;
  }
  
  // Configurações do Smart Life
  if (strlen(sysConfig.smartlife_id) > 0) {
    doc["smartlife_id"] = sysConfig.smartlife_id;
    doc["smartlife_ip"] = sysConfig.smartlife_ip;
    doc["smartlife_port"] = sysConfig.smartlife_port;
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
      
      if (doc.containsKey("co2_span_value")) {
        sysConfig.co2_span_value = doc["co2_span_value"];
        co2SpanValue = sysConfig.co2_span_value;
      }
      
      // Configurações do Smart Life
      if (doc.containsKey("smartlife_id")) {
        strlcpy(sysConfig.smartlife_id, doc["smartlife_id"], sizeof(sysConfig.smartlife_id));
      }
      
      if (doc.containsKey("smartlife_key")) {
        strlcpy(sysConfig.smartlife_key, doc["smartlife_key"], sizeof(sysConfig.smartlife_key));
      }
      
      if (doc.containsKey("smartlife_ip")) {
        strlcpy(sysConfig.smartlife_ip, doc["smartlife_ip"], sizeof(sysConfig.smartlife_ip));
      }
      
      if (doc.containsKey("smartlife_port")) {
        sysConfig.smartlife_port = doc["smartlife_port"];
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

// API para calibrar o sensor (zero point)
void handleCalibrate() {
  // Envia o comando de calibração para o sensor MH-Z19E usando a biblioteca
  calibrateMHZ19();
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Calibração zero point iniciada\"}");
}

// NOVA API para calibrar o sensor (span)
void handleCalibrateSpan() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Dados não fornecidos\"}");
    return;
  }
  
  String json = server.arg("plain");
  
  DynamicJsonDocument doc(64);
  DeserializationError error = deserializeJson(doc, json);
  
  if (error || !doc.containsKey("span_value")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Valor de span não fornecido\"}");
    return;
  }
  
  int spanValue = doc["span_value"];
  
  if (spanValue < 1000 || spanValue > 5000) {
    server.send(400, "application/json", 
      "{\"status\":\"error\",\"message\":\"Valor de span inválido. Use entre 1000 e 5000 ppm\"}");
    return;
  }
  
  // Realiza a calibração span
  calibrateMHZ19Span(spanValue);
  
  server.send(200, "application/json", 
    "{\"status\":\"success\",\"message\":\"Calibração span iniciada com valor " + String(spanValue) + " ppm\"}");
}

// API para emparelhar com o Smart Life Hub
void handlePairDevice() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Dados não fornecidos\"}");
    return;
  }
  
  String json = server.arg("plain");
  
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Dados inválidos\"}");
    return;
  }
  
  // Verifica se temos o IP do Hub (necessário para comunicação direta)
  if (!doc.containsKey("smartlife_ip")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"IP do Hub não fornecido\"}");
    return;
  }
  
  // Armazena as configurações do Smart Life
  strlcpy(sysConfig.smartlife_ip, doc["smartlife_ip"], sizeof(sysConfig.smartlife_ip));
  
  if (doc.containsKey("smartlife_port")) {
    sysConfig.smartlife_port = doc["smartlife_port"];
  } else {
    sysConfig.smartlife_port = 80; // Porta padrão
  }
  
  if (doc.containsKey("smartlife_id")) {
    strlcpy(sysConfig.smartlife_id, doc["smartlife_id"], sizeof(sysConfig.smartlife_id));
  }
  
  if (doc.containsKey("smartlife_key")) {
    strlcpy(sysConfig.smartlife_key, doc["smartlife_key"], sizeof(sysConfig.smartlife_key));
  }
  
  if (doc.containsKey("enable_notifications")) {
    sysConfig.enable_notifications = doc["enable_notifications"];
  } else {
    sysConfig.enable_notifications = true;
  }
  
  // Salva as configurações
  saveConfig();
  
  // Tenta conectar ao Hub
  connectToSmartLifeHub();
  
  if (smartLifeConnected) {
    server.send(200, "application/json", 
      "{\"status\":\"success\",\"message\":\"Conectado com sucesso ao Hub Smart Life\"}");
  } else {
    server.send(200, "application/json", 
      "{\"status\":\"warning\",\"message\":\"Configurações salvas, mas não foi possível conectar ao Hub. Verifique se o IP está correto.\"}");
  }
}