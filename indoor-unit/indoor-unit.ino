#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <LoRa.h>
#include <FirebaseClient.h>

// =====================================================
// WIFI
// =====================================================

#define WIFI_SSID "Net Nai"
#define WIFI_PASSWORD "netcelokobe120"

// =====================================================
// FIREBASE
// =====================================================

#define API_KEY "AIzaSyCJ36lEQLb8y9EIXWT0K_1jB0P93rroxO8"

#define DATABASE_URL "https://lora-location-tracker-default-rtdb.asia-southeast1.firebasedatabase.app/"

#define USER_EMAIL "gateway@loralocationtracker.com"
#define USER_PASSWORD "159zaq159" 

// =====================================================
// LORA PINS
// =====================================================

#define LORA_SCK D5
#define LORA_MISO D6
#define LORA_MOSI D7
#define LORA_SS D8
#define LORA_RST D1
#define LORA_DIO0 D2

#define LORA_FREQUENCY 433E6

// =====================================================
// FIREBASE OBJECTS
// =====================================================

UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);

FirebaseApp app;

WiFiClientSecure ssl_client;

using AsyncClient = AsyncClientClass;

AsyncClient aClient(ssl_client);

RealtimeDatabase Database;

// =====================================================
// VEHICLE DATA
// =====================================================

float latitude = 0.0;
float longitude = 0.0;
float altitude = 0.0;
float speed = 0.0;

int satellites = 0;
int rssi = 0;

bool gpsValid = false;
bool gpsModuleDetected = false;

unsigned long lastPacketTime = 0;
unsigned long lastFirebaseSend = 0;

const unsigned long FIREBASE_INTERVAL = 3000;

// =====================================================
// FIREBASE CALLBACK
// =====================================================

void processData(AsyncResult &result) {
  if (!result.isResult())
    return;

  if (result.isError()) {
    Serial.print("[Firebase ERROR] ");
    Serial.println(result.error().message().c_str());
  }
}

// =====================================================
// WIFI
// =====================================================

void connectWiFi() {
  Serial.println();
  Serial.println("[WiFi] Connecting...");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD);

  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);

    Serial.print(".");

    attempts++;

    if (attempts >= 60) {
      Serial.println();
      Serial.println("[WiFi] Connection timeout.");
      return;
    }
  }

  Serial.println();
  Serial.println("[WiFi] Connected");

  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("[WiFi] RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

// =====================================================
// FIREBASE INITIALIZATION
// =====================================================

void initializeFirebase() {
  Serial.println();
  Serial.println("[Firebase] Initializing...");

  ssl_client.setInsecure();

  ssl_client.setBufferSizes(
    4096,
    1024);

  initializeApp(
    aClient,
    app,
    getAuth(user_auth),
    processData,
    "firebaseAuth");

  app.getApp<RealtimeDatabase>(Database);

  Database.url(DATABASE_URL);

  Serial.println("[Firebase] Initialized");
}

// =====================================================
// LORA INITIALIZATION
// =====================================================

void initializeLoRa() {
  Serial.println();
  Serial.println("[LoRa] Initializing...");

  SPI.begin();

  LoRa.setPins(
    LORA_SS,
    LORA_RST,
    LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println();
    Serial.println("[LoRa ERROR]");
    Serial.println("SX1278 was not detected.");

    while (true) {
      delay(1000);
    }
  }

  // Must match outdoor transmitter

  LoRa.setSpreadingFactor(7);

  LoRa.setSignalBandwidth(
    125E3);

  LoRa.setCodingRate4(5);

  LoRa.enableCrc();

  Serial.println("[LoRa] Ready");

  Serial.println("[LoRa] Frequency : 433 MHz");
  Serial.println("[LoRa] SF        : 7");
  Serial.println("[LoRa] Bandwidth : 125 kHz");
  Serial.println("[LoRa] CR        : 4/5");
  Serial.println("[LoRa] CRC       : ENABLED");
}

// =====================================================
// PARSE GPS PACKET
// =====================================================
//
// New packet:
//
// GPS,VALID,23.795760,90.270062,8,19.8,5.6
//
// GPS,INVALID,0,0,0,0,0
//
// Format:
//
// GPS
// STATUS
// LATITUDE
// LONGITUDE
// SATELLITES
// ALTITUDE
// SPEED
// =====================================================

bool parseGPSPacket(String packet) {
  packet.trim();

  if (!packet.startsWith("GPS,")) {
    return false;
  }

  // Remove "GPS,"
  packet.remove(0, 4);

  // -----------------------------
  // STATUS
  // -----------------------------

  int p1 = packet.indexOf(',');

  if (p1 < 0)
    return false;

  String status =
    packet.substring(
      0,
      p1);

  gpsModuleDetected = true;

  // -----------------------------
  // Latitude
  // -----------------------------

  int p2 =
    packet.indexOf(
      ',',
      p1 + 1);

  if (p2 < 0)
    return false;

  latitude =
    packet.substring(
            p1 + 1,
            p2)
      .toFloat();

  // -----------------------------
  // Longitude
  // -----------------------------

  int p3 =
    packet.indexOf(
      ',',
      p2 + 1);

  if (p3 < 0)
    return false;

  longitude =
    packet.substring(
            p2 + 1,
            p3)
      .toFloat();

  // -----------------------------
  // Satellites
  // -----------------------------

  int p4 =
    packet.indexOf(
      ',',
      p3 + 1);

  if (p4 < 0)
    return false;

  satellites =
    packet.substring(
            p3 + 1,
            p4)
      .toInt();

  // -----------------------------
  // Altitude
  // -----------------------------

  int p5 =
    packet.indexOf(
      ',',
      p4 + 1);

  if (p5 < 0)
    return false;

  altitude =
    packet.substring(
            p4 + 1,
            p5)
      .toFloat();

  // -----------------------------
  // Speed
  // -----------------------------

  speed =
    packet.substring(
            p5 + 1)
      .toFloat();

  // -----------------------------
  // GPS status
  // -----------------------------

  if (status == "VALID") {
    gpsValid =
      (latitude != 0.0 && longitude != 0.0 && satellites > 0);
  } else {
    gpsValid = false;
  }

  return true;
}

// =====================================================
// RECEIVE LORA
// =====================================================

void receiveLoRa() {
  int packetSize =
    LoRa.parsePacket();

  if (!packetSize)
    return;

  String packet = "";

  while (LoRa.available()) {
    packet +=
      (char)LoRa.read();
  }

  rssi =
    LoRa.packetRssi();

  lastPacketTime =
    millis();

  Serial.println();
  Serial.println(
    "==========================================");

  Serial.println(
    "          LORA PACKET RECEIVED");

  Serial.println(
    "==========================================");

  Serial.print(
    "Raw Payload : ");

  Serial.println(packet);

  Serial.print(
    "RSSI        : ");

  Serial.print(rssi);

  Serial.println(" dBm");

  // -----------------------------------------
  // Parse
  // -----------------------------------------

  if (!parseGPSPacket(packet)) {
    Serial.println(
      "[ERROR] Invalid GPS packet format");

    Serial.println(
      "==========================================");

    return;
  }

  // -----------------------------------------
  // GPS MODULE
  // -----------------------------------------

  Serial.print(
    "GPS Module  : ");

  Serial.println(
    gpsModuleDetected
      ? "CONNECTED"
      : "NOT DETECTED");

  // -----------------------------------------
  // GPS FIX
  // -----------------------------------------

  Serial.print(
    "GPS Fix     : ");

  Serial.println(
    gpsValid
      ? "VALID"
      : "NO FIX");

  // -----------------------------------------
  // Satellites
  // -----------------------------------------

  Serial.print(
    "Satellites  : ");

  Serial.println(
    satellites);

  // -----------------------------------------
  // Location
  // -----------------------------------------

  if (gpsValid) {
    Serial.print(
      "Latitude    : ");

    Serial.println(
      latitude,
      6);

    Serial.print(
      "Longitude   : ");

    Serial.println(
      longitude,
      6);

    Serial.print(
      "Altitude    : ");

    Serial.print(
      altitude,
      1);

    Serial.println(" m");

    Serial.print(
      "Speed       : ");

    Serial.print(
      speed,
      1);

    Serial.println(" km/h");
  }

  // -----------------------------------------
  // Connection
  // -----------------------------------------

  Serial.print(
    "Last Packet : ");

  Serial.println(
    "JUST NOW");

  Serial.println(
    "==========================================");
}

// =====================================================
// FIREBASE UPDATE
// =====================================================

void sendToFirebase() {

  if (!app.ready()) {
    Serial.println("[Firebase] App not ready");
    return;
  }

  Serial.println();
  Serial.println("[Firebase] Uploading vehicle data...");

  const String base = "/vehicles/vehicle_01";

  // Send one value at a time, but wait for the
  // Firebase client to process the previous request.

  Database.set(
    aClient,
    base + "/latitude",
    String(latitude, 6),
    processData
  );

  app.loop();

  Database.set(
    aClient,
    base + "/longitude",
    String(longitude, 6),
    processData
  );

  app.loop();

  Database.set(
    aClient,
    base + "/altitude",
    String(altitude, 1),
    processData
  );

  app.loop();

  Database.set(
    aClient,
    base + "/speed",
    String(speed, 1),
    processData
  );

  app.loop();

  Database.set(
    aClient,
    base + "/satellites",
    String(satellites),
    processData
  );

  app.loop();

  Database.set(
    aClient,
    base + "/rssi",
    String(rssi),
    processData
  );

  app.loop();

  Database.set(
    aClient,
    base + "/gps_valid",
    gpsValid,
    processData
  );

  app.loop();

  Database.set(
    aClient,
    base + "/online",
    true,
    processData
  );

  Serial.println("[Firebase] Update requested");
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println(
    "==========================================");

  Serial.println(
    "      LORA GPS INDOOR GATEWAY");

  Serial.println(
    "==========================================");

  connectWiFi();

  initializeFirebase();

  initializeLoRa();

  Serial.println();
  Serial.println(
    "SYSTEM READY");

  Serial.println(
    "Waiting for vehicle...");
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  // Firebase background processing

  app.loop();

  // Receive LoRa

  receiveLoRa();

  // -----------------------------------------
  // Firebase periodic update
  // -----------------------------------------

  if (
    millis() - lastFirebaseSend
    >= FIREBASE_INTERVAL) {
    lastFirebaseSend =
      millis();

    sendToFirebase();
  }

  // -----------------------------------------
  // Vehicle timeout
  // -----------------------------------------

  if (
    lastPacketTime > 0 && millis() - lastPacketTime > 15000) {
    // No packet for 15 seconds

    gpsModuleDetected = false;

    gpsValid = false;

    Serial.println(
      "[WARNING] Vehicle packet timeout");
  }

  yield();
}