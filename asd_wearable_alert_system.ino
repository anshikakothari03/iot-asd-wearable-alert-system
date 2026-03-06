#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include "MPU9250_asukiaaa.h"
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// === Wi-Fi ===
const char* ssid = "_____";
const char* password = "_____";

// === Telegram Bot ===
const char* botToken = "YOUR_BOT_TOKEN";
const char* chatID   = "YOUR_CHAT_ID";

// === ThingSpeak ===
const char* thingSpeakHost = "http://api.thingspeak.com/update";
const char* thingSpeakApiKey = "YOUR_THINGSPEAK_API_KEY";

// === Pins ===
#define I2C_SDA 21
#define I2C_SCL 22
#define GPS_RX 16
#define GPS_TX 17

MAX30105 maxSensor;
MPU9250_asukiaaa mpuSensor;
HardwareSerial gpsSerial(2);
TinyGPSPlus gps;

uint32_t irBuffer[100];
uint32_t redBuffer[100];
const float g_to_m_s2 = 9.80665;
const float WEIGHT_MAX = 0.4;
const float WEIGHT_MPU = 0.6;
const int AGE_MAX_BPM = 105;
const float GEOFENCE_RADIUS = 100.0;
unsigned long lastAlertTime = 0;
const unsigned long ALERT_INTERVAL = 30000;
bool originSet = false;
double originLat = 0.0, originLon = 0.0;

// URL encoding
String urlEncode(String str) {
  String encoded = "";
  char c;
  char code0, code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) encoded += c;
    else {
      encoded += '%';
      code0 = (c >> 4) & 0xF;
      code1 = c & 0xF;
      encoded += char(code0 > 9 ? code0 - 10 + 'A' : code0 + '0');
      encoded += char(code1 > 9 ? code1 - 10 + 'A' : code1 + '0');
    }
  }
  return encoded;
}

void sendTelegramAlert(String message) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(botToken) +
               "/sendMessage?chat_id=" + String(chatID) +
               "&text=" + urlEncode(message);
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode > 0) Serial.println("📨 Telegram alert sent");
  else {
    Serial.print("❌ Telegram alert failed: ");
    Serial.println(httpCode);
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✔ Wi-Fi connected.");

  if (!maxSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("❌ MAX30102 not found."); while (1);
  }
  maxSensor.setup();
  maxSensor.setPulseAmplitudeRed(0x0A);
  maxSensor.setPulseAmplitudeGreen(0);

  mpuSensor.setWire(&Wire);
  mpuSensor.beginAccel();
  mpuSensor.beginGyro();
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
}

void loop() {
  for (int i = 0; i < 100; i++) {
    while (!maxSensor.available()) maxSensor.check();
    redBuffer[i] = maxSensor.getRed();
    irBuffer[i] = maxSensor.getIR();
    maxSensor.nextSample();
  }

  int32_t spo2, heartRate;
  int8_t validSpO2, validHeartRate;
  maxim_heart_rate_and_oxygen_saturation(irBuffer, 100, redBuffer, &spo2, &validSpO2, &heartRate, &validHeartRate);
  if (heartRate < 0) heartRate = 0;
  if (spo2 < 0) spo2 = 0;

  mpuSensor.accelUpdate();
  mpuSensor.gyroUpdate();
  float ax = mpuSensor.accelX() * g_to_m_s2;
  float ay = mpuSensor.accelY() * g_to_m_s2;
  float az = mpuSensor.accelZ() * g_to_m_s2;
  float accelVector = abs(sqrt(ax * ax + ay * ay + az * az) - 9.8);

  float gx = mpuSensor.gyroX(), gy = mpuSensor.gyroY(), gz = mpuSensor.gyroZ();
  if (abs(gx) < 0.5) gx = 0; if (abs(gy) < 0.5) gy = 0; if (abs(gz) < 0.5) gz = 0;
  float gyroVector = sqrt(gx * gx + gy * gy + gz * gz);

  float norm_bpm = constrain((float)heartRate / 150.0, 0.0, 1.0);
  float norm_spo2 = constrain((float)spo2 / 100.0, 0.0, 1.0);
  float norm_accel = constrain(accelVector / 20.0, 0.0, 1.0);
  float norm_gyro = constrain(gyroVector / 250.0, 0.0, 1.0);
  float combined_score_percent = ((WEIGHT_MAX * ((norm_bpm + norm_spo2)/2.0) + WEIGHT_MPU * ((norm_accel + norm_gyro)/2.0)) * 100.0);

  // === New Condition Logic ===
  int conditionCode = 0;
  String condition = "Normal";
  String emoji = "❌";

  bool isHighHR = heartRate >= AGE_MAX_BPM;
  bool isFastAccel = accelVector >= 3.0;
  bool isHighGyro = gyroVector >= 4.0;

  if (!isHighHR && !isFastAccel && !isHighGyro) {
    condition = "Normal"; conditionCode = 0; emoji = "❌";
  } else if (!isHighHR && isFastAccel && isHighGyro) {
    condition = "Restlessness"; conditionCode = 1; emoji = "🌀";
  } else if (isHighHR && !isFastAccel && !isHighGyro) {
    condition = "Anxiety"; conditionCode = 2; emoji = "😟";
  } else if (isHighHR && isFastAccel && isHighGyro) {
    condition = "Anxiety + Restlessness"; conditionCode = 3; emoji = "😟🌀";
  }

  // === GPS Check ===
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  double distance = 0.0;
  if (gps.location.isUpdated()) {
    double lat = gps.location.lat();
    double lon = gps.location.lng();
    if (!originSet) {
      originLat = lat; originLon = lon; originSet = true;
      Serial.println("📍 GPS reference set.");
    }
    distance = TinyGPSPlus::distanceBetween(originLat, originLon, lat, lon);
    Serial.printf("📡 GPS → Lat: %.6f | Lon: %.6f | Distance: %.2f m\n", lat, lon, distance);
  }

  // === Output Debug ===
  Serial.println("--------------------------------------------------");
  Serial.printf("BPM: %d | SpO2: %d%% | Accel: %.2f m/s² | Gyro: %.2f °/s\n", heartRate, spo2, accelVector, gyroVector);
  Serial.printf("Score: %.1f%% | Condition: %s (%d)\n", combined_score_percent, condition.c_str(), conditionCode);

  if ((conditionCode > 0 || distance > GEOFENCE_RADIUS) && (millis() - lastAlertTime > ALERT_INTERVAL)) {
    String message = "[ALERT]\n";
    message += "Condition: " + emoji + " " + condition + "\nScore: " + String(combined_score_percent, 1) + "%\n";
    if (distance > GEOFENCE_RADIUS) {
      message += "Geo Alert: Out of 100m zone!\nDist: " + String(distance, 1) + " m";
    }
    sendTelegramAlert(message);
    lastAlertTime = millis();
  }

  // === ThingSpeak Upload ===
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(thingSpeakHost) +
                 "?api_key=" + thingSpeakApiKey +
                 "&field1=" + String(heartRate) +
                 "&field2=" + String(spo2) +
                 "&field3=" + String(accelVector, 2) +
                 "&field4=" + String(gyroVector, 2) +
                 "&field5=" + String(combined_score_percent, 1) +
                 "&field6=" + String(conditionCode);
    http.begin(url);
    int code = http.GET();
    if (code > 0) Serial.println("✔ Data sent to ThingSpeak");
    else Serial.println("❌ ThingSpeak upload failed");
    http.end();
  }

  delay(1000);
}





