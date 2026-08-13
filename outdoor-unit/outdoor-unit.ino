#include <SoftwareSerial.h>
#include <TinyGPS++.h>

#define GPS_RX D3
#define GPS_TX D4

SoftwareSerial gpsSerial(GPS_RX, GPS_TX);
TinyGPSPlus gps;

unsigned long lastPrint = 0;

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600);

  Serial.println();
  Serial.println("================================");
  Serial.println("      OUTDOOR GPS TEST");
  Serial.println("================================");
  Serial.println("Waiting for GPS fix...");
}

void loop() {

  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  if (millis() - lastPrint >= 2000) {
    lastPrint = millis();

    Serial.println();
    Serial.println("-------------------------------");

    Serial.print("GPS characters : ");
    Serial.println(gps.charsProcessed());

    Serial.print("Satellites     : ");
    
    if (gps.satellites.isValid())
      Serial.println(gps.satellites.value());
    else
      Serial.println("INVALID");

    Serial.print("Location       : ");

    if (gps.location.isValid()) {
      Serial.print(gps.location.lat(), 6);
      Serial.print(", ");
      Serial.println(gps.location.lng());
    } else {
      Serial.println("INVALID");
    }

    Serial.print("Altitude       : ");

    if (gps.altitude.isValid()) {
      Serial.print(gps.altitude.meters(), 1);
      Serial.println(" m");
    } else {
      Serial.println("INVALID");
    }

    Serial.print("Speed          : ");

    if (gps.speed.isValid()) {
      Serial.print(gps.speed.kmph(), 2);
      Serial.println(" km/h");
    } else {
      Serial.println("INVALID");
    }

    Serial.println("-------------------------------");
  }

  yield();
}