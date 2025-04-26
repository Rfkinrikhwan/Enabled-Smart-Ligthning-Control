#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <FirebaseESP32.h>
#include <time.h>

// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

// Firebase configuration
#define API_KEY "AIzaSyBDajKmo4JpXoYS7Xu0c3QBIImlanvOFFY"
#define DATABASE_URL "https://smartlightingproject-d599e-default-rtdb.asia-southeast1.firebasedatabase.app/"

// Pin Definitions
#define RELAY_1 12
#define RELAY_2 14
#define RELAY_3 27
#define RELAY_4 26
#define SOUND_SENSOR_PIN 35 // Pin untuk sensor tepok
#define BUZZER_PIN 25       // Pin untuk buzzer

// Time configuration
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200; // GMT+7 untuk Indonesia (7 * 3600)
const int daylightOffset_sec = 0;

// Firebase components
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Global variables
bool signUpOK = false;
bool isOffline = false;
bool lampStates[4] = {false, false, false, false};
unsigned long lastSoundTime = 0;
const int soundThreshold = 3000;
unsigned long soundDetectionInterval = 1000;
int soundCount = 0;
unsigned long soundCountResetTime = 0;
bool offlineControlActive = false;

// WiFi connection stability variables
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 30000; // Try to reconnect every 30 seconds
int consecutiveFailedConnections = 0;
const int maxFailedConnections = 5;       // After 5 failed attempts, restart ESP32
unsigned long wifiConnectTimeout = 15000; // 15 second timeout for WiFi connections

// Structure for time
struct ScheduleTime
{
  int hour;
  int minute;
};

// Schedule tracking - one schedule for each lamp
struct LampSchedule
{
  ScheduleTime onTime;
  ScheduleTime offTime;
  bool hasSchedule;
};

LampSchedule lampSchedules[4]; // Array to store schedules for 4 lamps

// Function Prototypes
void handleSchedules();
void playTone(int duration);
void playSuccessTone();
void playErrorTone();
void handleSoundSensor();
void updateRelays();
void connectToFirebase();
void readFirebaseData();
void loadAllSchedules();
void updateDeviceStatus();
bool parseTimeString(String timeStr, ScheduleTime &time);
bool checkWiFiConnection();
void setupWiFi();

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n===== Smart Lighting System Starting =====");

  // Initialize pins
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  pinMode(RELAY_3, OUTPUT);
  pinMode(RELAY_4, OUTPUT);
  pinMode(SOUND_SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Set initial state of relays (HIGH = OFF for active low relays)
  digitalWrite(RELAY_1, LOW);
  digitalWrite(RELAY_2, LOW);
  digitalWrite(RELAY_3, LOW);
  digitalWrite(RELAY_4, LOW);

  // Initialize schedules
  for (int i = 0; i < 4; i++)
  {
    lampSchedules[i].hasSchedule = false;
  }

  // Setup WiFi connection
  setupWiFi();

  // Configure time if WiFi is connected
  if (WiFi.status() == WL_CONNECTED)
  {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
      Serial.println("Current time: " + String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min));
    }

    // Connect to Firebase
    connectToFirebase();
  }
  else
  {
    isOffline = true;
    Serial.println("Starting in offline mode");
    playErrorTone();
  }
}

void loop()
{
  // Handle sound sensor for offline control
  handleSoundSensor();

  // Check and maintain WiFi connection
  bool wifiConnected = checkWiFiConnection();

  // Handle online functionality if WiFi is connected
  if (wifiConnected && !isOffline)
  {
    // Check schedules every minute
    static unsigned long lastScheduleCheck = 0;
    if (millis() - lastScheduleCheck > 60000)
    {
      handleSchedules();
      lastScheduleCheck = millis();
    }

    // Read data from Firebase
    readFirebaseData();

    // Update device status in Firebase
    static unsigned long lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate > 30000)
    { // Every 30 seconds
      updateDeviceStatus();
      lastStatusUpdate = millis();
    }
  }

  // Always update relays to match current state
  updateRelays();

  // Small delay to prevent CPU hogging
  delay(10);
}

void setupWiFi()
{
  // Use WiFiManager to handle connection
  WiFiManager wifiManager;
  wifiManager.setTimeout(wifiConnectTimeout / 1000); // Convert to seconds

  // Configure low power mode and connection stability
  WiFi.setSleep(false); // Disable WiFi sleep mode for better stability

  // Attempt to connect to WiFi
  Serial.println("Attempting to connect to WiFi...");

  // Add custom parameters here if needed
  bool wifiConnected = wifiManager.autoConnect("SmartLighting_AP", "smart1234");

  if (wifiConnected)
  {
    Serial.println("WiFi Connected Successfully!");
    Serial.println("IP: " + WiFi.localIP().toString());
    playSuccessTone();

    // Set maximum transmit power for better range
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    // Configure to use a static IP if needed
    // WiFi.config(staticIP, gateway, subnet, primaryDNS, secondaryDNS);
  }
  else
  {
    Serial.println("Failed to connect to WiFi. Starting in offline mode.");
    isOffline = true;
    playErrorTone();
  }
}

bool checkWiFiConnection()
{
  static unsigned long lastConnectionCheck = 0;
  static bool wasConnected = false;
  bool isConnected = (WiFi.status() == WL_CONNECTED);

  // Check WiFi status periodically to prevent excessive checking
  if (millis() - lastConnectionCheck > 5000)
  {
    lastConnectionCheck = millis();

    // If WiFi status changed, report it
    if (wasConnected != isConnected)
    {
      if (isConnected)
      {
        Serial.println("WiFi reconnected!");
        isOffline = false;
        playSuccessTone();

        // Reconnect to Firebase if we were offline
        if (signUpOK == false)
        {
          connectToFirebase();
        }

        // Reset failed connection counter
        consecutiveFailedConnections = 0;
      }
      else
      {
        Serial.println("WiFi connection lost!");
        consecutiveFailedConnections++;

        if (!isOffline)
        {
          playErrorTone();
          isOffline = true;
        }

        // If we've failed too many times in a row, restart the device
        if (consecutiveFailedConnections >= maxFailedConnections)
        {
          Serial.println("Too many failed connections. Restarting ESP32...");
          ESP.restart();
        }
      }
      wasConnected = isConnected;
    }

    // If disconnected, try to reconnect based on interval
    if (!isConnected && (millis() - lastReconnectAttempt > reconnectInterval))
    {
      Serial.println("Attempting to reconnect WiFi...");
      lastReconnectAttempt = millis();

      // Force reconnection attempt
      WiFi.disconnect();
      delay(100);
      WiFi.reconnect();
    }
  }

  return isConnected;
}

void connectToFirebase()
{
  // Clear config to start fresh
  config = FirebaseConfig();
  auth = FirebaseAuth();

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  Serial.println("Connecting to Firebase...");

  // Handle timeouts for Firebase connection
  unsigned long firebaseConnectStart = millis();
  const unsigned long firebaseTimeout = 10000; // 10 seconds timeout

  if (Firebase.signUp(&config, &auth, "", ""))
  {
    Serial.println("Firebase SignUp berhasil");
    signUpOK = true;
    playSuccessTone();
  }
  else
  {
    Serial.println("SignUp GAGAL: " + String(config.signer.signupError.message.c_str()));
    playErrorTone();
    isOffline = true;
    return;
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Enhanced error handling for stream connection
  int retries = 0;
  const int maxRetries = 3;

  while (retries < maxRetries)
  {
    if (Firebase.beginStream(fbdo, "/lampu"))
    {
      Serial.println("Firebase stream connected successfully");
      break;
    }
    else
    {
      Serial.println("Stream connection failed, attempt " + String(retries + 1) + "/" + String(maxRetries) + ": " + fbdo.errorReason());
      retries++;

      if (retries >= maxRetries)
      {
        Serial.println("Failed to establish stream after maximum retries");
        isOffline = true;
        playErrorTone();
        return;
      }

      delay(1000); // Wait before next retry
    }
  }

  // Load initial lamp states
  loadInitialLampStates();

  // Load all schedules
  loadAllSchedules();
}

void loadInitialLampStates()
{
  FirebaseData lampData;

  // Create a retry mechanism for getting lamp data
  int retries = 0;
  const int maxRetries = 3;

  while (retries < maxRetries)
  {
    if (Firebase.getJSON(lampData, "/lampu"))
    {
      String jsonStr = lampData.jsonString();
      Serial.println("Lamp data: " + jsonStr);

      // Parse lamp states - try with fallback methods
      for (int i = 1; i <= 4; i++)
      {
        bool lampState = false;

        // Try to get boolean directly first
        if (Firebase.getBool(lampData, "/lampu/" + String(i), &lampState))
        {
          lampStates[i - 1] = lampState;
        }
        else
        {
          // Fallback to parsing JSON
          String lookFor = "\"" + String(i) + "\":";
          int statePos = jsonStr.indexOf(lookFor);

          if (statePos >= 0)
          {
            statePos += lookFor.length();
            String stateStr = jsonStr.substring(statePos, jsonStr.indexOf(",", statePos));
            if (stateStr.indexOf("}") < stateStr.indexOf(",") && stateStr.indexOf("}") >= 0)
            {
              stateStr = stateStr.substring(0, stateStr.indexOf("}"));
            }
            stateStr.trim();

            lampStates[i - 1] = (stateStr == "true");
          }
        }

        Serial.println("Lamp " + String(i) + " initial state: " + String(lampStates[i - 1]));
      }

      break; // Success, exit the retry loop
    }
    else
    {
      Serial.println("Failed to get lamp data, attempt " + String(retries + 1) + "/" + String(maxRetries));
      retries++;

      if (retries >= maxRetries)
      {
        Serial.println("Failed to get lamp data after maximum retries");
        break;
      }

      delay(1000); // Wait before next retry
    }
  }
}

void loadAllSchedules()
{
  // Check all schedule entries
  FirebaseData scheduleData;

  Serial.println("Loading lamp schedules...");

  // Try to load schedules for each lamp
  for (int i = 1; i <= 4; i++)
  {
    int retries = 0;
    const int maxRetries = 3;
    bool scheduleLoaded = false;

    while (retries < maxRetries && !scheduleLoaded)
    {
      if (Firebase.getJSON(scheduleData, "/jadwal/" + String(i)))
      {
        String lampScheduleStr = scheduleData.jsonString();
        Serial.println("Schedule data for lamp " + String(i) + ": " + lampScheduleStr);

        if (lampScheduleStr.indexOf("\"on\"") > 0 &&
            lampScheduleStr.indexOf("\"off\"") > 0)
        {

          // Extract on time
          int onStart = lampScheduleStr.indexOf("\"on\"") + 6; // Skip "on":"
          String onTimeStr = lampScheduleStr.substring(onStart, lampScheduleStr.indexOf("\"", onStart));

          // Extract off time
          int offStart = lampScheduleStr.indexOf("\"off\"") + 7; // Skip "off":"
          String offTimeStr = lampScheduleStr.substring(offStart, lampScheduleStr.indexOf("\"", offStart));

          if (parseTimeString(onTimeStr, lampSchedules[i - 1].onTime) &&
              parseTimeString(offTimeStr, lampSchedules[i - 1].offTime))
          {
            lampSchedules[i - 1].hasSchedule = true;
            scheduleLoaded = true;

            Serial.println("Schedule for lamp " + String(i) + " - On: " +
                           String(lampSchedules[i - 1].onTime.hour) + ":" +
                           String(lampSchedules[i - 1].onTime.minute) +
                           ", Off: " +
                           String(lampSchedules[i - 1].offTime.hour) + ":" +
                           String(lampSchedules[i - 1].offTime.minute));
          }
        }
        else
        {
          Serial.println("No valid schedule for lamp " + String(i));
          scheduleLoaded = true; // Count as loaded even if no schedule
        }
      }
      else
      {
        Serial.println("Failed to get schedule for lamp " + String(i) + ", attempt " + String(retries + 1));
        retries++;

        if (retries >= maxRetries)
        {
          Serial.println("Failed to get schedule after maximum retries");
        }

        delay(500);
      }
    }
  }
}

bool parseTimeString(String timeStr, ScheduleTime &time)
{
  if (timeStr.length() >= 5)
  {
    time.hour = timeStr.substring(0, 2).toInt();
    time.minute = timeStr.substring(3, 5).toInt();

    // Validate time values
    if (time.hour >= 0 && time.hour <= 23 && time.minute >= 0 && time.minute <= 59)
    {
      return true;
    }
  }
  Serial.println("Invalid time format: " + timeStr);
  return false;
}

void readFirebaseData()
{
  if (Firebase.ready() && Firebase.readStream(fbdo))
  {
    if (fbdo.streamAvailable())
    {
      String path = fbdo.dataPath();
      String data = fbdo.stringData();

      Serial.print("Firebase data received - Path: ");
      Serial.print(path);
      Serial.print(", Data: ");
      Serial.println(data);

      // Update lamp states based on Firebase data
      if (path == "/1")
      {
        lampStates[0] = (data == "true");
      }
      else if (path == "/2")
      {
        lampStates[1] = (data == "true");
      }
      else if (path == "/3")
      {
        lampStates[2] = (data == "true");
      }
      else if (path == "/4")
      {
        lampStates[3] = (data == "true");
      }
    }
  }
  else if (!isOffline)
  {
    Serial.println("Firebase stream error: " + fbdo.errorReason());

    // Check if we need to restart stream
    static unsigned long lastStreamReconnect = 0;
    if (millis() - lastStreamReconnect > 60000)
    { // Try to reconnect stream every minute
      lastStreamReconnect = millis();

      Serial.println("Attempting to restart Firebase stream...");
      if (Firebase.beginStream(fbdo, "/lampu"))
      {
        Serial.println("Stream restarted successfully");
      }
      else
      {
        Serial.println("Failed to restart stream");
      }
    }
  }
}

void updateDeviceStatus()
{
  if (!Firebase.ready())
    return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }

  char timeStringBuffer[30];
  strftime(timeStringBuffer, sizeof(timeStringBuffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  String timestamp = String(timeStringBuffer);

  // Use transaction to update device status (more reliable)
  bool statusUpdated = false;

  if (Firebase.setString(fbdo, "/device_status/esp32_1/last_seen", timestamp))
  {
    if (Firebase.setBool(fbdo, "/device_status/esp32_1/online", true))
    {
      statusUpdated = true;
    }
  }

  if (!statusUpdated)
  {
    Serial.println("Failed to update device status: " + fbdo.errorReason());
  }
}

void handleSchedules()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }

  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;
  int currentTimeInMinutes = currentHour * 60 + currentMinute;

  // Check each lamp's schedule
  for (int i = 0; i < 4; i++)
  {
    if (lampSchedules[i].hasSchedule)
    {
      int onTimeInMinutes = lampSchedules[i].onTime.hour * 60 + lampSchedules[i].onTime.minute;
      int offTimeInMinutes = lampSchedules[i].offTime.hour * 60 + lampSchedules[i].offTime.minute;

      // Check if we need to turn this lamp on
      if (currentTimeInMinutes == onTimeInMinutes)
      {
        Serial.println("Schedule ON for lamp " + String(i + 1));
        lampStates[i] = true;

        // Try to update Firebase if online
        if (!isOffline && Firebase.ready())
        {
          Firebase.setBool(fbdo, "/lampu/" + String(i + 1), true);
        }
      }

      // Check if we need to turn this lamp off
      if (currentTimeInMinutes == offTimeInMinutes)
      {
        Serial.println("Schedule OFF for lamp " + String(i + 1));
        lampStates[i] = false;

        // Try to update Firebase if online
        if (!isOffline && Firebase.ready())
        {
          Firebase.setBool(fbdo, "/lampu/" + String(i + 1), false);
        }
      }
    }
  }
}

void handleSoundSensor()
{
  int soundValue = analogRead(SOUND_SENSOR_PIN);

  // Reset sound count if timeout
  if (millis() - soundCountResetTime > 2000)
  {
    soundCount = 0;
  }

  // Detect loud sound (clap)
  if (soundValue > soundThreshold)
  {
    if (millis() - lastSoundTime > soundDetectionInterval)
    {
      Serial.println("Sound detected! Value: " + String(soundValue));
      lastSoundTime = millis();
      soundCount++;
      soundCountResetTime = millis();

      // If offline mode and detected 2 claps, toggle all lights
      if (isOffline && soundCount >= 2)
      {
        soundCount = 0;
        offlineControlActive = !offlineControlActive;

        // Set all lights to same state in offline mode
        for (int i = 0; i < 4; i++)
        {
          lampStates[i] = offlineControlActive;
        }

        // Confirm with buzzer
        if (offlineControlActive)
        {
          playSuccessTone();
        }
        else
        {
          playTone(200);
        }
      }
    }
  }
}

void updateRelays()
{
  // Relays are typically active LOW
  digitalWrite(RELAY_1, lampStates[0] ? HIGH : LOW);
  digitalWrite(RELAY_2, lampStates[1] ? HIGH : LOW);
  digitalWrite(RELAY_3, lampStates[2] ? HIGH : LOW);
  digitalWrite(RELAY_4, lampStates[3] ? HIGH : LOW);
}

void playTone(int duration)
{
  tone(BUZZER_PIN, 1000, duration);
}

void playSuccessTone()
{
  tone(BUZZER_PIN, 1000, 100);
  delay(100);
  tone(BUZZER_PIN, 1500, 100);
  delay(100);
  tone(BUZZER_PIN, 2000, 100);
}

void playErrorTone()
{
  tone(BUZZER_PIN, 500, 200);
  delay(200);
  tone(BUZZER_PIN, 500, 200);
}