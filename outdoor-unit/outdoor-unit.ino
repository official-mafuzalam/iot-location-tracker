#include <SPI.h>
#include <LoRa.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>

// =========================
// GPS
// =========================
#define GPS_RX D3   // NodeMCU receives from GPS TX
#define GPS_TX D4   // NodeMCU sends to GPS RX

SoftwareSerial gpsSerial(GPS_RX, GPS_TX);
TinyGPSPlus gps;

// =========================
// LoRa SX1278
// =========================
#define LORA_SS   D8
#define LORA_RST  D1
#define LORA_DIO0 D2

// =========================
// LoRa frequency
// =========================
// Bangladesh commonly uses 433 MHz LoRa modules.
// Your SX1278 module must actually be a 433 MHz version.
#define LORA_FREQUENCY 433E6

unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println("GPS + LoRa VEHICLE UNIT");
  Serial.println("==============================");

  // GPS
  gpsSerial.begin(9600);

  // LoRa
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  // ESP8266 hardware SPI
  SPI.begin();

  Serial.println("Starting LoRa...");

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("ERROR: LoRa initialization failed!");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("LoRa initialized successfully.");
  Serial.println("Waiting for GPS...");
  Serial.println();
}

void loop() {

  // =========================
  // Continuously read GPS
  // =========================
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  // =========================
  // Send every 2 seconds
  // =========================
  if (millis() - lastSend >= 2000) {
    lastSend = millis();

    Serial.println("------------------------------");

    if (gps.location.isValid()) {

      double latitude = gps.location.lat();
      double longitude = gps.location.lng();

      int satellites = gps.satellites.value();

      double altitude = gps.altitude.meters();
      double speed = gps.speed.kmph();

      Serial.println("GPS DATA:");
      Serial.print("Latitude  : ");
      Serial.println(latitude, 6);

      Serial.print("Longitude : ");
      Serial.println(longitude, 6);

      Serial.print("Satellites: ");
      Serial.println(satellites);

      Serial.print("Altitude  : ");
      Serial.print(altitude);
      Serial.println(" m");

      Serial.print("Speed     : ");
      Serial.print(speed);
      Serial.println(" km/h");

      // =========================
      // Create LoRa packet
      // =========================
      LoRa.beginPacket();

      LoRa.print("GPS,");

      LoRa.print(latitude, 6);
      LoRa.print(",");

      LoRa.print(longitude, 6);
      LoRa.print(",");

      LoRa.print(satellites);
      LoRa.print(",");

      LoRa.print(altitude, 1);
      LoRa.print(",");

      LoRa.print(speed, 1);

      LoRa.endPacket();

      Serial.println("LoRa: DATA SENT");

    } else {

      Serial.println("GPS: Location INVALID");
      Serial.print("Satellites: ");
      Serial.println(gps.satellites.value());

      // Optional status packet
      LoRa.beginPacket();
      LoRa.print("GPS,INVALID");
      LoRa.endPacket();

      Serial.println("LoRa: GPS INVALID SENT");
    }

    Serial.println("------------------------------");
  }
}