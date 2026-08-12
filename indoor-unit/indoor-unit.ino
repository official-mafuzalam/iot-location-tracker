#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <LoRa.h>
#include <FirebaseClient.h>

// =====================================================
// WIFI CONFIG
// =====================================================
#define WIFI_SSID     "Net Nai"
#define WIFI_PASSWORD "netcelokobe120"

// =====================================================
// FIREBASE CONFIG
// =====================================================
#define API_KEY       "AIzaSyCJ36lEQLb8y9EIXWT0K_1jB0P93rroxO8"
#define DATABASE_URL  "https://lora-location-tracker-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL    "gateway@loralocationtracker.com"
#define USER_PASSWORD "159zaq159"

// =====================================================
// LORA - ESP8266 PINS
// =====================================================
#define LORA_SCK   D5  // GPIO14
#define LORA_MISO  D6  // GPIO12
#define LORA_MOSI  D7  // GPIO13
#define LORA_SS    D4  // GPIO2
#define LORA_RST   D1  // GPIO5
#define LORA_DIO0  D2  // GPIO4 (Changed from D0 to fix interrupt issue)

#define LORA_FREQUENCY 433E6

// =====================================================
// OBJECTS & GLOBALS
// =====================================================
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

float latitude  = 0.0;
float longitude = 0.0;
float altitude  = 0.0;
float speed     = 0.0;
int satellites  = 0;
int rssi        = 0;
bool gpsValid   = false;

bool isNewDataAvailable = false;
unsigned long lastFirebaseSend = 0;
const unsigned long FIREBASE_INTERVAL = 3000;

// =====================================================
// FIREBASE CALLBACK (Cleaned output)
// =====================================================
void processData(AsyncResult &result) {
  if (!result.isResult()) return;

  if (result.isError()) {
    Serial.print("[Firebase Error] ");
    Serial.println(result.error().message().c_str());
  }
}

// =====================================================
// WIFI INITIALIZATION
// =====================================================
void connectWiFi() {
  Serial.println("\n[WiFi] Connecting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("\n[WiFi] Connected!");
  Serial.print("[WiFi] IP Address: ");
  Serial.println(WiFi.localIP());
}

// =====================================================
// FIREBASE INITIALIZATION
// =====================================================
void initializeFirebase() {
  Serial.println("\n[Firebase] Initializing...");

  ssl_client.setInsecure();
  ssl_client.setBufferSizes(4096, 1024);

  initializeApp(aClient, app, getAuth(user_auth), processData, "firebaseAuth");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);

  Serial.println("[Firebase] Initialized!");
}

// =====================================================
// LORA INITIALIZATION
// =====================================================
void initializeLoRa() {
  Serial.println("\n[LoRa] Initializing SPI & Chip...");

  SPI.begin();
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("[LoRa ERROR] Chip not found! Check physical wiring.");
    while (true) {
      delay(1000);
    }
  }

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  Serial.println("[LoRa] Ready on 433 MHz. Listening for packets...");
}

// =====================================================
// GPS PACKET PARSER
// =====================================================
bool parseGPSPacket(String packet) {
  packet.trim();
  if (!packet.startsWith("GPS,")) return false;

  packet.remove(0, 4);

  int p1 = packet.indexOf(',');
  int p2 = packet.indexOf(',', p1 + 1);
  int p3 = packet.indexOf(',', p2 + 1);
  int p4 = packet.indexOf(',', p3 + 1);

  if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0) return false;

  latitude   = packet.substring(0, p1).toFloat();
  longitude  = packet.substring(p1 + 1, p2).toFloat();
  satellites = packet.substring(p2 + 1, p3).toInt();
  altitude   = packet.substring(p3 + 1, p4).toFloat();
  speed      = packet.substring(p4 + 1).toFloat();

  gpsValid = (latitude != 0.0 && longitude != 0.0 && satellites > 0);
  return true;
}

// =====================================================
// RECEIVE LORA
// =====================================================
void receiveLoRa() {
  int packetSize = LoRa.parsePacket();
  if (!packetSize) return;

  String packet = "";
  while (LoRa.available()) {
    packet += (char)LoRa.read();
  }

  rssi = LoRa.packetRssi();

  Serial.println("\n==========================================");
  Serial.println(">>> LORA PACKET RECEIVED <<<");
  Serial.print("Raw Payload : "); Serial.println(packet);
  Serial.print("Signal RSSI : "); Serial.print(rssi); Serial.println(" dBm");

  if (parseGPSPacket(packet)) {
    Serial.print("Parsed Lat  : "); Serial.println(latitude, 6);
    Serial.print("Parsed Lon  : "); Serial.println(longitude, 6);
    Serial.print("Satellites  : "); Serial.println(satellites);
    Serial.print("GPS Status  : "); Serial.println(gpsValid ? "VALID FIX" : "NO FIX");
    isNewDataAvailable = true; // Mark data as ready for upload
  } else {
    Serial.println("[Warning] Packet received but invalid GPS string format!");
  }
  Serial.println("==========================================");
}

// =====================================================
// SEND TO FIREBASE
// =====================================================
void sendToFirebase() {
  if (!app.ready()) {
    Serial.println("[Firebase] App not ready...");
    return;
  }

  if (!gpsValid) {
    Serial.println("[Firebase] Skipping upload - Invalid GPS coordinates");
    return;
  }

  Serial.println("\n[Firebase] Sending payload to Realtime Database...");
  const String base = "/vehicles/vehicle_01/";

  Database.set(aClient, base + "latitude", String(latitude, 6), processData);
  Database.set(aClient, base + "longitude", String(longitude, 6), processData);
  Database.set(aClient, base + "altitude", (double)altitude, processData);
  Database.set(aClient, base + "speed", (double)speed, processData);
  Database.set(aClient, base + "satellites", (int)satellites, processData);
  Database.set(aClient, base + "rssi", (int)rssi, processData);
  Database.set(aClient, base + "gps_valid", gpsValid, processData);
  Database.set(aClient, base + "online", true, processData);

  Serial.println("[Firebase] Sent successfully!");
  isNewDataAvailable = false; // Reset flag after pushing
}

// =====================================================
// SETUP & MAIN LOOP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==========================================");
  Serial.println("    ESP8266 LoRa GPS Gateway Starting    ");
  Serial.println("==========================================");

  connectWiFi();
  initializeFirebase();
  initializeLoRa();
}

void loop() {
  app.loop();
  receiveLoRa();

  // Upload ONLY when new data arrives and interval timing is met
  if (isNewDataAvailable && (millis() - lastFirebaseSend >= FIREBASE_INTERVAL)) {
    lastFirebaseSend = millis();
    sendToFirebase();
  }

  yield();
}