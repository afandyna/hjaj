#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <FirebaseESP8266.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

// WiFi credentials (fixed)
const char* wifiSSID = "eng_afandyna";
const char* wifiPassword = "M#201677MM#m";

// Firebase details
#define API_KEY "AIzaSyBNlL_8Y0dNsL1X02w8M-a_ApiizR-d3wM"
#define DATABASE_URL "https://data-292e2-default-rtdb.firebaseio.com/"

// GPS Pins
static const int RXPin = 13, TXPin = 15;
static const uint32_t GPSBaud = 9600;

// Push button pin (D5/GPIO14)
const int buttonPin = 14;

// Built-in LED pin (GPIO2, active-low)
const int ledPin = 2;

// NTP for accurate time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);  // UTC, update every 60s

SoftwareSerial gpsSerial(RXPin, TXPin);
TinyGPSPlus gps;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long lastUploadTime = 0;
const unsigned long uploadInterval = 2000;  // Upload every 2 seconds
String macAddress;
bool buttonPressed = false;
unsigned long lastButtonCheck = 0;
const unsigned long buttonDebounce = 50;  // Fast debounce (50ms)
unsigned long ledOnTime = 0;
bool ledActive = false;

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(GPSBaud);
  pinMode(buttonPin, INPUT_PULLUP);  // Button with pull-up
  pinMode(ledPin, OUTPUT);           // Built-in LED
  digitalWrite(ledPin, HIGH);        // LED off (active-low)

  // Get MAC address as unique ID
  macAddress = WiFi.macAddress();
  macAddress.replace(":", "");  // Remove colons for Firebase path

  // Connect to WiFi
  connectToWiFi();

  // Initialize NTP
  timeClient.begin();

  // Firebase setup
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  Firebase.signUp(&config, &auth, "", "");  // Anonymous auth
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  // Update GPS data
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // Update time
  timeClient.update();

  // Check button with fast debounce
  if (millis() - lastButtonCheck > buttonDebounce) {
    lastButtonCheck = millis();
    if (digitalRead(buttonPin) == LOW) {  // Button pressed (active low)
      buttonPressed = true;
      uploadButtonPress();
    }
  }

  // Upload GPS data every 2 seconds
  if (millis() - lastUploadTime > uploadInterval) {
    lastUploadTime = millis();
    uploadGPSData();
  }

  // Turn off LED after 2 seconds
  if (ledActive && millis() - ledOnTime > 2000) {
    digitalWrite(ledPin, HIGH);  // LED off
    ledActive = false;
  }
}

void connectToWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(wifiSSID, wifiPassword);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected.");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi.");
  }
}

void uploadGPSData() {
  if (WiFi.status() != WL_CONNECTED || !gps.location.isValid()) return;

  String path = "/devices/" + macAddress + "/gps";
  Firebase.setFloat(fbdo, path + "/latitude", gps.location.lat());
  Firebase.setFloat(fbdo, path + "/longitude", gps.location.lng());
  Firebase.setString(fbdo, path + "/mac", macAddress);
  Firebase.setString(fbdo, path + "/date", String(timeClient.getEpochTime() / 86400));
  Firebase.setString(fbdo, path + "/time", timeClient.getFormattedTime());
}

void uploadButtonPress() {
  if (WiFi.status() != WL_CONNECTED || !buttonPressed) return;

  String path = "/devices/" + macAddress + "/button";
  bool success = true;

  // Upload button state and data
  success &= Firebase.setBool(fbdo, path + "/state", true);
  if (gps.location.isValid()) {
    success &= Firebase.setFloat(fbdo, path + "/press_latitude", gps.location.lat());
    success &= Firebase.setFloat(fbdo, path + "/press_longitude", gps.location.lng());
  } else {
    success &= Firebase.setFloat(fbdo, path + "/press_latitude", 0.0);
    success &= Firebase.setFloat(fbdo, path + "/press_longitude", 0.0);
  }
  success &= Firebase.setString(fbdo, path + "/press_date", String(timeClient.getEpochTime() / 86400));
  success &= Firebase.setString(fbdo, path + "/press_time", timeClient.getFormattedTime());

  // Light LED for 2 seconds on successful upload
  if (success && fbdo.errorReason() == "") {
    digitalWrite(ledPin, LOW);  // LED on (active-low)
    ledOnTime = millis();
    ledActive = true;
  }

  buttonPressed = false;  // Reset after upload
}