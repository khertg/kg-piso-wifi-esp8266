#include <SPI.h>
#include <Ethernet.h>
#include <ArduinoJson.h>

// Network Configuration
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 0, 50);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);

EthernetServer server(80);  // HTTP server on port 80

// Pin Definitions
constexpr int COIN_PIN = D4;
constexpr int INSERT_COIN_LED_PIN = D3;

// LED State Management
bool ledState = false;
unsigned long ledOnTime = 0;
constexpr unsigned long LED_TIMEOUT = 60000;

// Coin Slot Variables
volatile int pulseCount = 0;
volatile int totalCoins = 0;
unsigned long lastPulseTime = 0;
constexpr unsigned long DEBOUNCE_TIME = 200;

// Global Variable for Insert Coin State
bool INSERT_COIN = false;


void IRAM_ATTR pulseDetected() {
  pulseCount++;
  lastPulseTime = millis();  // Record the time of the last pulse
}

void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;

  Serial.println("Starting Ethernet...");
  Ethernet.begin(mac, ip, dns, gateway, subnet);

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("Ethernet shield not found");
    while (true)
      ;
  }

  Serial.print("IP Address: ");
  Serial.println(Ethernet.localIP());

  server.begin();
  Serial.println("Server started...");

  // Initialize LED Pin
  pinMode(INSERT_COIN_LED_PIN, OUTPUT);
  digitalWrite(INSERT_COIN_LED_PIN, LOW);

  // Initialize Pins
  pinMode(INSERT_COIN_LED_PIN, OUTPUT);
  digitalWrite(INSERT_COIN_LED_PIN, LOW);

  pinMode(COIN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(COIN_PIN), pulseDetected, FALLING);
}

void loop() {
  handleClientConnections();
  processCoinInsertion();
  manageInsertCoinLedState();
}

void processCoinInsertion() {
  if (pulseCount > 0 && (millis() - lastPulseTime > DEBOUNCE_TIME)) {
    ledOnTime = millis();
    totalCoins += pulseCount;
    pulseCount = 0;

    Serial.printf("Coin inserted: %d\n", pulseCount);
    Serial.printf("Total coins: %d\n", totalCoins);
  }
}

void manageInsertCoinLedState() {
  if (ledState && (millis() - ledOnTime >= LED_TIMEOUT)) {
    digitalWrite(INSERT_COIN_LED_PIN, LOW);
    ledState = false;
    Serial.println("LED turned off after timeout");
  }
}

void handleClientConnections() {
  EthernetClient client = server.available();
  if (client) {
    Serial.println("New client connected");
    processClientRequest(client);
    client.stop();
    Serial.println("Client disconnected");
  }
}

void processClientRequest(EthernetClient &client) {
  String request = readClientRequest(client);
  if (request.isEmpty()) return;

  String path = extractRequestPath(request);
  if (path == "/api/insertCoin" && request.startsWith("POST")) {
    handleInsertCoin(client, request);
  } else if (path == "/api/createVoucher" && request.startsWith("POST")) {
    handleCreateVoucher(client, request);
  } else {
    sendErrorResponse(client, "Endpoint not found", 404);
  }
}

void handleCreateVoucher(EthernetClient &client, const String &request) {

  int contentLength = extractContentLength(request);
  String postData = readPostData(client, contentLength);
  StaticJsonDocument<200> jsonDoc;
  if (deserializeJson(jsonDoc, postData)) {
    sendErrorResponse(client, "Invalid JSON", 400);
  }

  // Extract Data
  // @TODO: double check with global variable
  const char *macAddress = jsonDoc["macAddress"];
  const char *ipAddress = jsonDoc["ipAddress"];

  // Respond
  StaticJsonDocument<200> responseDoc;
  responseDoc["status"] = "success";
  responseDoc["message"] = "Voucher created successfully";
  responseDoc["voucherCode"] = "Te2sd34sdvlt";

  String response;
  serializeJson(responseDoc, response);
  sendJsonResponse(client, response, 200);
}

void handleInsertCoin(EthernetClient &client, const String &request) {
  INSERT_COIN = true;

  int contentLength = extractContentLength(request);
  String postData = readPostData(client, contentLength);
  StaticJsonDocument<200> jsonDoc;
  if (deserializeJson(jsonDoc, postData)) {
    sendErrorResponse(client, "Invalid JSON", 400);
  }

  // Extract Data
  // @TODO: set global variable
  const char *macAddress = jsonDoc["macAddress"];
  const char *ipAddress = jsonDoc["ipAddress"];

  sendJsonResponse(client, "", 204);

  // Turn on LED
  digitalWrite(INSERT_COIN_LED_PIN, HIGH);
  ledOnTime = millis();
  ledState = true;
}

String readClientRequest(EthernetClient &client) {
  String request;
  unsigned long timeout = millis();
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      request += c;
      if (request.endsWith("\r\n\r\n")) break;
    }
    if (millis() - timeout > 5000) {
      Serial.println("Client request timeout");
      return "";
    }
  }
  return request;
}

String extractRequestPath(const String &request) {
  int start = request.indexOf(' ') + 1;
  int end = request.indexOf(' ', start);
  return (start > 0 && end > start) ? request.substring(start, end) : "";
}

int extractContentLength(const String &request) {
  int index = request.indexOf("Content-Length: ");
  if (index < 0) return 0;
  int end = request.indexOf("\r\n", index);
  return request.substring(index + 16, end).toInt();
}

String readPostData(EthernetClient &client, int contentLength) {
  String data;
  unsigned long timeout = millis();
  while (client.available() < contentLength) {
    if (millis() - timeout > 5000) return "";
  }
  while (client.available()) {
    data += static_cast<char>(client.read());
    if (data.length() >= contentLength) break;
  }
  return data;
}

void sendErrorResponse(EthernetClient &client, const char *errorMessage, int statusCode) {
  StaticJsonDocument<100> jsonDoc;
  jsonDoc["error"] = errorMessage;
  String response;
  serializeJson(jsonDoc, response);
  sendJsonResponse(client, response, statusCode);
}

void sendJsonResponse(EthernetClient &client, const String &content, int statusCode) {
  client.printf("HTTP/1.1 %d OK\r\n", statusCode);
  client.println("Content-Type: application/json\r\nConnection: close\r\n");
  client.println(content);
}
