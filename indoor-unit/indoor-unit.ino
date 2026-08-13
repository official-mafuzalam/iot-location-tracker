#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <LoRa.h>
#include <FirebaseClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// =====================================================
// WIFI
// =====================================================

#define WIFI_SSID       "Net Nai"
#define WIFI_PASSWORD   "netcelokobe120"

// =====================================================
// FIREBASE
// =====================================================

#define API_KEY         "AIzaSyCJ36lEQLb8y9EIXWT0K_1jB0P93rroxO8"
#define DATABASE_URL    "https://lora-location-tracker-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL      "gateway@loralocationtracker.com"
#define USER_PASSWORD   "159zaq159"

// =====================================================
// LORA - ESP8266
// =====================================================

#define LORA_SCK        D5
#define LORA_MISO       D6
#define LORA_MOSI       D7
#define LORA_SS         D4
#define LORA_RST        D1
#define LORA_DIO0       D2

#define LORA_FREQUENCY  433E6

// =====================================================
// NTP TIME CONFIG
// =====================================================

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 21600, 60000); // UTC+6 for Bangladesh

// =====================================================
// FIREBASE OBJECTS
// =====================================================

UserAuth user_auth(
  API_KEY,
  USER_EMAIL,
  USER_PASSWORD
);

FirebaseApp app;

WiFiClientSecure ssl_client;

using AsyncClient = AsyncClientClass;

AsyncClient aClient(ssl_client);

RealtimeDatabase Database;

// =====================================================
// VEHICLE DATA
// =====================================================

float latitude  = 0.0;
float longitude = 0.0;
float altitude  = 0.0;
float speed     = 0.0;

int satellites = 0;
int rssi       = 0;

bool gpsValid = false;
bool packetReceived = false;
bool timeSynced = false;

unsigned long lastPacketTime = 0;
const unsigned long PACKET_TIMEOUT = 10000; // 10 seconds timeout

// =====================================================
// TIMERS
// =====================================================

unsigned long lastFirebaseSend = 0;
const unsigned long FIREBASE_INTERVAL = 3000;

unsigned long lastTimeSync = 0;
const unsigned long TIME_SYNC_INTERVAL = 3600000; // Sync time every hour

// =====================================================
// FIREBASE CALLBACK
// =====================================================

void processData(AsyncResult &result)
{
  if (!result.isResult())
    return;

  if (result.isError())
  {
    Serial.print("[Firebase ERROR] ");
    Serial.print(result.error().message().c_str());
    Serial.print(" | Code: ");
    Serial.println(result.error().code());
    return;
  }

  Serial.println("[Firebase] Operation completed");
}

// =====================================================
// WIFI
// =====================================================

bool connectWiFi()
{
  Serial.println();
  Serial.println("[WiFi] Connecting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");

    if (millis() - startTime > 20000)
    {
      Serial.println();
      Serial.println("[WiFi] Connection timeout.");
      return false;
    }
  }

  Serial.println();
  Serial.println("[WiFi] Connected!");
  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());

  return true;
}

// =====================================================
// TIME SYNC
// =====================================================

bool syncTime()
{
  Serial.println();
  Serial.println("[NTP] Syncing time...");
  
  timeClient.begin();
  timeClient.update();
  
  if (timeClient.isTimeSet())
  {
    timeSynced = true;
    lastTimeSync = millis();
    Serial.print("[NTP] Time synced: ");
    Serial.println(timeClient.getFormattedTime());
    return true;
  }
  else
  {
    Serial.println("[NTP] Time sync failed!");
    return false;
  }
}

unsigned long getEpochMillis()
{
  if (!timeSynced)
  {
    // Fallback to millis() if NTP not synced
    return millis();
  }
  
  timeClient.update();
  return timeClient.getEpochTime() * 1000; // Convert to milliseconds
}

// =====================================================
// FIREBASE INITIALIZATION
// =====================================================

void initializeFirebase()
{
  Serial.println();
  Serial.println("[Firebase] Initializing...");

  ssl_client.setInsecure();
  ssl_client.setBufferSizes(4096, 1024);

  initializeApp(
    aClient,
    app,
    getAuth(user_auth),
    processData,
    "firebaseAuth"
  );

  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);

  Serial.println("[Firebase] Initialized");
}

// =====================================================
// LORA INITIALIZATION
// =====================================================

bool initializeLoRa()
{
  Serial.println();
  Serial.println("[LoRa] Initializing...");

  // Initialize SPI (no parameters on ESP8266)
  SPI.begin();
  
  // Manual reset for SX1278
  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_RST, LOW);
  delay(10);
  digitalWrite(LORA_RST, HIGH);
  delay(10);
  
  // Set CS pin
  pinMode(LORA_SS, OUTPUT);
  digitalWrite(LORA_SS, HIGH);

  LoRa.setPins(
    LORA_SS,
    LORA_RST,
    LORA_DIO0
  );

  if (!LoRa.begin(LORA_FREQUENCY))
  {
    Serial.println("[LoRa ERROR]");
    Serial.println("SX1278 was not detected.");
    Serial.println("Check wiring:");
    Serial.println("  - VCC to 3.3V");
    Serial.println("  - GND to GND");
    Serial.println("  - NSS to D4");
    Serial.println("  - SCK to D5");
    Serial.println("  - MOSI to D7");
    Serial.println("  - MISO to D6");
    Serial.println("  - RST to D1");
    return false;
  }

  // Must match OUTDOOR unit
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  LoRa.setTxPower(17);
  
  // Set receive mode
  LoRa.receive();

  Serial.println("[LoRa] Ready");
  Serial.println("[LoRa] Frequency : 433 MHz");
  Serial.println("[LoRa] SF        : 7");
  Serial.println("[LoRa] Bandwidth : 125 kHz");
  Serial.println("[LoRa] CR        : 4/5");
  Serial.println("[LoRa] CRC       : ENABLED");

  return true;
}

// =====================================================
// GPS PACKET PARSER
// =====================================================

bool parseGPSPacket(String packet)
{
  packet.trim();

  if (!packet.startsWith("GPS,"))
  {
    return false;
  }

  packet.remove(0, 4);

  int p1 = packet.indexOf(',');
  int p2 = packet.indexOf(',', p1 + 1);
  int p3 = packet.indexOf(',', p2 + 1);
  int p4 = packet.indexOf(',', p3 + 1);

  if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0)
  {
    return false;
  }

  latitude = packet.substring(0, p1).toFloat();
  longitude = packet.substring(p1 + 1, p2).toFloat();
  satellites = packet.substring(p2 + 1, p3).toInt();
  altitude = packet.substring(p3 + 1, p4).toFloat();
  speed = packet.substring(p4 + 1).toFloat();

  // GPS fix validation
  gpsValid = (latitude != 0.0 && longitude != 0.0 && satellites > 0);

  return true;
}

// =====================================================
// RECEIVE LORA
// =====================================================

void receiveLoRa()
{
  int packetSize = LoRa.parsePacket();

  if (!packetSize)
    return;

  String packet = "";

  while (LoRa.available())
  {
    packet += (char)LoRa.read();
  }

  rssi = LoRa.packetRssi();
  lastPacketTime = millis();

  Serial.println();
  Serial.println("==========================================");
  Serial.println(">>> LORA PACKET RECEIVED <<<");
  Serial.println("==========================================");

  Serial.print("Raw Payload : ");
  Serial.println(packet);

  Serial.print("RSSI        : ");
  Serial.print(rssi);
  Serial.println(" dBm");

  if (parseGPSPacket(packet))
  {
    packetReceived = true;

    Serial.println();
    Serial.print("Latitude    : ");
    Serial.println(latitude, 6);

    Serial.print("Longitude   : ");
    Serial.println(longitude, 6);

    Serial.print("Satellites  : ");
    Serial.println(satellites);

    Serial.print("Altitude    : ");
    Serial.print(altitude, 1);
    Serial.println(" m");

    Serial.print("Speed       : ");
    Serial.print(speed, 1);
    Serial.println(" km/h");

    Serial.print("GPS Status  : ");

    if (gpsValid)
    {
      Serial.println("VALID FIX");
    }
    else
    {
      Serial.println("NO FIX");
    }
  }
  else
  {
    Serial.println();
    Serial.println("[GPS] Invalid packet format");
  }

  Serial.println("==========================================");
}

// =====================================================
// SEND COMPLETE VEHICLE DATA TO FIREBASE
// =====================================================

void sendToFirebase()
{
  if (!app.ready())
  {
    Serial.println("[Firebase] App not ready");
    return;
  }

  if (!packetReceived)
  {
    Serial.println("[Firebase] No packet received yet");
    return;
  }

  if (!gpsValid)
  {
    Serial.println("[Firebase] GPS data invalid - skipping upload");
    return;
  }

  Serial.println();
  Serial.println("[Firebase] Uploading vehicle data...");

  // Create JSON using object_t (FirebaseClient format)
  JsonWriter writer;
  object_t json;

  // Add latitude
  object_t latObj;
  writer.create(latObj, "latitude", number_t(latitude, 6));
  
  // Add longitude
  object_t lonObj;
  writer.create(lonObj, "longitude", number_t(longitude, 6));
  
  // Add altitude
  object_t altObj;
  writer.create(altObj, "altitude", number_t(altitude, 1));
  
  // Add speed
  object_t speedObj;
  writer.create(speedObj, "speed", number_t(speed, 1));
  
  // Add satellites
  object_t satObj;
  writer.create(satObj, "satellites", satellites);
  
  // Add RSSI
  object_t rssiObj;
  writer.create(rssiObj, "rssi", rssi);
  
  // Add GPS valid status
  object_t gpsValidObj;
  writer.create(gpsValidObj, "gps_valid", gpsValid);
  
  // Add online status
  object_t onlineObj;
  writer.create(onlineObj, "online", true);
  
  // Add last update timestamp using NTP time
  object_t timestampObj;
  unsigned long epochTime = getEpochMillis();
  writer.create(timestampObj, "last_update", (long)epochTime);
  
  // Add human-readable time for debugging
  object_t timeStrObj;
  if (timeSynced) {
    timeClient.update();
    writer.create(timeStrObj, "time_string", timeClient.getFormattedTime());
  } else {
    writer.create(timeStrObj, "time_string", "Time not synced");
  }

  // Combine all fields
  writer.join(
    json,
    10,  // Increased to 10 fields
    latObj,
    lonObj,
    altObj,
    speedObj,
    satObj,
    rssiObj,
    gpsValidObj,
    onlineObj,
    timestampObj,
    timeStrObj
  );

  // Upload to Firebase using update (merges data)
  Database.update<object_t>(
    aClient,
    "/vehicles/vehicle_01",
    json,
    processData,
    "vehicleUpdate"
  );

  Serial.print("[Firebase] Update requested with timestamp: ");
  Serial.println(epochTime);
}

// =====================================================
// CHECK AND MAINTAIN CONNECTIONS
// =====================================================

void checkStatus()
{
  static unsigned long lastStatus = 0;
  
  // Check packet timeout
  if (packetReceived && (millis() - lastPacketTime > PACKET_TIMEOUT))
  {
    Serial.println("[Warning] Packet timeout - GPS data may be stale");
    packetReceived = false;
    gpsValid = false;
  }
  
  // Sync time periodically
  if (millis() - lastTimeSync > TIME_SYNC_INTERVAL && WiFi.status() == WL_CONNECTED)
  {
    syncTime();
  }
  
  // Print status every 30 seconds
  if (millis() - lastStatus > 30000)
  {
    lastStatus = millis();
    Serial.println();
    Serial.println("--- STATUS ---");
    Serial.print("WiFi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    Serial.print("Time: ");
    if (timeSynced) {
      timeClient.update();
      Serial.println(timeClient.getFormattedTime());
    } else {
      Serial.println("Not synced");
    }
    Serial.print("LoRa: ");
    Serial.println(LoRa.parsePacket() != 0 ? "Activity detected" : "Listening");
    Serial.print("GPS: ");
    Serial.println(gpsValid ? "Valid" : "Invalid");
    Serial.print("Last Packet: ");
    if (packetReceived)
    {
      Serial.print((millis() - lastPacketTime) / 1000);
      Serial.println("s ago");
    }
    else
    {
      Serial.println("None");
    }
    Serial.println("----------------");
  }
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==========================================");
  Serial.println("       LORA GPS INDOOR GATEWAY");
  Serial.println("==========================================");

  // WiFi
  if (!connectWiFi())
  {
    Serial.println("[FATAL] WiFi connection failed. Restarting...");
    ESP.restart();
  }

  // Sync time
  syncTime();

  // Firebase
  initializeFirebase();

  // LoRa
  if (!initializeLoRa())
  {
    Serial.println();
    Serial.println("SYSTEM STOPPED");
    Serial.println("Check SX1278 wiring and power.");

    while (true)
    {
      delay(1000);
      Serial.print(".");
    }
  }

  lastPacketTime = millis();

  Serial.println();
  Serial.println("==========================================");
  Serial.println("SYSTEM READY");
  Serial.println("Waiting for vehicle packets...");
  Serial.println("==========================================");
  Serial.println();
  Serial.println("Expected packet format: GPS,lat,lon,sats,alt,speed");
  Serial.println("Example: GPS,23.795760,90.270062,4,19.8,5.6");
  Serial.println();
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  // Firebase background processing
  app.loop();

  // Receive LoRa packets
  receiveLoRa();

  // Check system status
  checkStatus();

  // Upload every 3 seconds if we have valid GPS data
  if (
    packetReceived &&
    gpsValid &&
    millis() - lastFirebaseSend >= FIREBASE_INTERVAL
  )
  {
    lastFirebaseSend = millis();
    sendToFirebase();
  }

  yield();
}