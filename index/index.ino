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
#define RELAY_1 4
#define RELAY_2 5
#define RELAY_3 18
#define RELAY_4 19
#define SOUND_SENSOR_PIN 35 // ADC1_7
#define WIFI_LED 2          // Onboard LED

// RGB LED Pin definitions
#define LED_RED_PIN 25
#define LED_GREEN_PIN 26
#define LED_BLUE_PIN 27

// Time configuration
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200; // GMT+7
const int daylightOffset_sec = 0;

// Firebase components
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Global variables
bool signUpOK = false;
bool isOffline = false;
bool lampStates[4] = {false, false, false, false};

// RGB blinking variables for online mode
unsigned long lastLedToggleTime = 0;
bool ledIsGreen = true;
const unsigned long onlineBlinkInterval = 2000; // 2 detik

// Sound detection variables
unsigned long lastSoundTime = 0;
const int soundThreshold = 2000;                  // Sesuaikan ambang
const unsigned long soundDetectionInterval = 300; // ms
unsigned long lastTepukTime = 0;
const int tepukTimeWindow = 3000; // ms
int tepukCount = 0;

// WiFi reconnection vars
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 30000;
int consecutiveFailedConnections = 0;
const int maxFailedConnections = 5;
unsigned long wifiConnectTimeout = 30000;

// Scheduling structs
struct ScheduleTime
{
  int hour;
  int minute;
};
struct LampSchedule
{
  ScheduleTime onTime, offTime;
  bool hasSchedule;
};
LampSchedule lampSchedules[4];

// Prototypes…
void handleSchedules();
void showRGBColor(int r, int g, int b);
void updateRGBLed();
void handleSoundSensor();
void updateRelays();
void connectToFirebase();
void readFirebaseData();
void loadInitialLampStates();
void loadAllSchedules();
void updateDeviceStatus();
bool parseTimeString(String s, ScheduleTime &t);
bool checkWiFiConnection();
void setupWiFi();
void printAllSchedules();
void updateWiFiLED(bool connected);
void processTepukAction(int count);

void setup()
{
  Serial.begin(115200);
  delay(3000);

  // Relay
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  pinMode(RELAY_3, OUTPUT);
  pinMode(RELAY_4, OUTPUT);
  digitalWrite(RELAY_1, LOW);
  digitalWrite(RELAY_2, LOW);
  digitalWrite(RELAY_3, LOW);
  digitalWrite(RELAY_4, LOW);

  // Sensor suara
  pinMode(SOUND_SENSOR_PIN, INPUT);
  analogReadResolution(12);       // 12-bit ADC (0–4095)
  analogSetAttenuation(ADC_11db); // Rentang ~0–3.6V

  // LED
  pinMode(WIFI_LED, OUTPUT);
  digitalWrite(WIFI_LED, LOW);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  showRGBColor(0, 0, 0);

  // Inisialisasi jadwal
  for (int i = 0; i < 4; i++)
    lampSchedules[i].hasSchedule = false;

  // WiFi & Firebase
  setupWiFi();
  if (WiFi.status() == WL_CONNECTED)
  {
    isOffline = false;
    updateWiFiLED(true);
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    connectToFirebase();
    printAllSchedules();
  }
  else
  {
    isOffline = true;
    updateWiFiLED(false);
  }

  updateRelays();
}

void loop()
{
  handleSoundSensor();

  bool wifiConnected = checkWiFiConnection();
  updateWiFiLED(wifiConnected);
  updateRGBLed();

  if (wifiConnected && !isOffline)
  {
    static unsigned long lastScheduleCheck = 0;
    if (millis() - lastScheduleCheck > 10000)
    {
      handleSchedules();
      lastScheduleCheck = millis();
    }
    readFirebaseData();
    static unsigned long lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate > 30000)
    {
      updateDeviceStatus();
      loadAllSchedules();
      lastStatusUpdate = millis();
    }
  }

  updateRelays();
  delay(10);
}

void updateWiFiLED(bool connected)
{
  digitalWrite(WIFI_LED, connected ? HIGH : LOW);
}

void showRGBColor(int red, int green, int blue)
{
  analogWrite(LED_RED_PIN, red);
  analogWrite(LED_GREEN_PIN, green);
  analogWrite(LED_BLUE_PIN, blue);
}

void updateRGBLed()
{
  if (isOffline)
  {
    // Offline → merah solid
    showRGBColor(255, 0, 0);
  }
  else
  {
    // Online → toggle hijau ↔ biru setiap 2 detik
    if (millis() - lastLedToggleTime > onlineBlinkInterval)
    {
      ledIsGreen = !ledIsGreen;
      lastLedToggleTime = millis();
    }
    if (ledIsGreen)
    {
      showRGBColor(0, 255, 0);
    }
    else
    {
      showRGBColor(0, 0, 255);
    }
  }
}

void setupWiFi()
{
  WiFiManager wifiManager;
  wifiManager.setTimeout(wifiConnectTimeout / 1000);
  WiFi.setSleep(false);

  Serial.println("Attempting to connect to WiFi...");
  if (wifiManager.autoConnect("Smart Light", "bismillah"))
  {
    Serial.println("WiFi Connected: " + WiFi.localIP().toString());
    updateWiFiLED(true);
  }
  else
  {
    Serial.println("Failed to connect to WiFi.");
    isOffline = true;
    updateWiFiLED(false);
  }
}

bool checkWiFiConnection()
{
  static unsigned long lastCheck = 0;
  static bool wasConnected = false;
  bool isConnected = (WiFi.status() == WL_CONNECTED);

  if (millis() - lastCheck > 5000)
  {
    lastCheck = millis();
    if (wasConnected != isConnected)
    {
      if (isConnected)
      {
        Serial.println("WiFi reconnected!");
        isOffline = false;
        consecutiveFailedConnections = 0;
      }
      else
      {
        Serial.println("WiFi connection lost!");
        isOffline = true;
        consecutiveFailedConnections++;
        if (consecutiveFailedConnections >= maxFailedConnections)
        {
          Serial.println("Too many fails. Restarting...");
          ESP.restart();
        }
      }
      wasConnected = isConnected;
    }
    if (!isConnected && millis() - lastReconnectAttempt > reconnectInterval)
    {
      Serial.println("Reconnecting WiFi...");
      lastReconnectAttempt = millis();
      WiFi.disconnect();
      delay(100);
      WiFi.reconnect();
    }
  }
  return isConnected;
}

void connectToFirebase()
{
  // Reset config & auth
  config = FirebaseConfig();
  auth = FirebaseAuth();
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  Serial.println("Connecting to Firebase...");
  if (Firebase.signUp(&config, &auth, "", ""))
  {
    Serial.println("Firebase SignUp berhasil");
    signUpOK = true;
  }
  else
  {
    Serial.println("SignUp GAGAL: " + String(config.signer.signupError.message.c_str()));
    isOffline = true;
    return;
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Attempt stream
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
      Serial.println("Stream failed " + String(retries + 1) + "/" + String(maxRetries) + ": " + fbdo.errorReason());
      retries++;
      delay(1000);
    }
  }
  if (retries >= maxRetries)
  {
    Serial.println("Failed to establish stream after max retries");
    isOffline = true;
  }

  // Load initial lamp states & schedules
  loadInitialLampStates();
  loadAllSchedules();
}

void loadInitialLampStates()
{
  FirebaseData lampData;
  int retries = 0;
  const int maxRetries = 3;
  while (retries < maxRetries)
  {
    if (Firebase.getJSON(lampData, "/lampu"))
    {
      String jsonStr = lampData.jsonString();
      for (int i = 1; i <= 4; i++)
      {
        bool state = false;
        if (Firebase.getBool(lampData, "/lampu/" + String(i), &state))
        {
          lampStates[i - 1] = state;
        }
        else
        {
          String lookFor = "\"" + String(i) + "\":";
          int pos = jsonStr.indexOf(lookFor);
          if (pos >= 0)
          {
            pos += lookFor.length();
            String s = jsonStr.substring(pos, jsonStr.indexOf(',', pos));
            s.replace("}", "");
            s.trim();
            lampStates[i - 1] = (s == "true");
          }
        }
        Serial.println("Lamp " + String(i) + " initial state: " + (lampStates[i - 1] ? "ON" : "OFF"));
      }
      break;
    }
    else
    {
      Serial.println("Failed to get lamp data, attempt " + String(retries + 1));
      retries++;
      delay(1000);
    }
  }
}

void readFirebaseData()
{
  if (Firebase.ready() && Firebase.readStream(fbdo))
  {
    if (fbdo.streamAvailable())
    {
      String path = fbdo.dataPath();
      String data = fbdo.stringData();
      Serial.println("Stream update - " + path + ": " + data);
      int idx = path.substring(1).toInt() - 1;
      if (idx >= 0 && idx < 4)
      {
        lampStates[idx] = (data == "true");
      }
    }
  }
  else if (!isOffline)
  {
    Serial.println("Stream error: " + fbdo.errorReason());
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry > 60000)
    {
      lastRetry = millis();
      Serial.println("Reconnecting stream...");
      Firebase.beginStream(fbdo, "/lampu");
    }
  }
}

void loadAllSchedules()
{
  Serial.println("Loading lamp schedules...");
  for (int i = 1; i <= 4; i++)
  {
    FirebaseData sd;
    String onPath = "/jadwal/" + String(i) + "/on";
    String offPath = "/jadwal/" + String(i) + "/off";
    String onTime, offTime;
    bool onOk = Firebase.getString(sd, onPath);
    if (onOk)
    {
      onTime = sd.stringData();
      onTime.replace("\"", "");
      onTime.trim();
    }
    else
    {
      Serial.println("No ON for lamp " + String(i));
    }
    bool offOk = Firebase.getString(sd, offPath);
    if (offOk)
    {
      offTime = sd.stringData();
      offTime.replace("\"", "");
      offTime.trim();
    }
    else
    {
      Serial.println("No OFF for lamp " + String(i));
    }

    if (onOk && offOk &&
        parseTimeString(onTime, lampSchedules[i - 1].onTime) &&
        parseTimeString(offTime, lampSchedules[i - 1].offTime))
    {
      lampSchedules[i - 1].hasSchedule = true;
      Serial.printf("Lamp %d schedule: ON %s OFF %s\n",
                    i, onTime.c_str(), offTime.c_str());
    }
    else
    {
      lampSchedules[i - 1].hasSchedule = false;
    }
  }
}

void updateDeviceStatus()
{
  if (!Firebase.ready())
    return;
  struct tm ti;
  if (!getLocalTime(&ti))
    return;
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &ti);
  String ts(buf);
  if (!Firebase.setString(fbdo, "/device_status/esp32_1/last_seen", ts))
  {
    Serial.println("Status TS fail");
  }
  if (!Firebase.setBool(fbdo, "/device_status/esp32_1/online", true))
  {
    Serial.println("Status ONLINE fail");
  }
}

bool parseTimeString(String timeStr, ScheduleTime &time)
{
  timeStr.replace("\"", "");
  timeStr.trim();
  int c = timeStr.indexOf(':');
  if (c > 0)
  {
    time.hour = timeStr.substring(0, c).toInt();
    time.minute = timeStr.substring(c + 1).toInt();
    return (time.hour >= 0 && time.hour < 24 && time.minute >= 0 && time.minute < 60);
  }
  return false;
}

void handleSchedules()
{
  struct tm ti;
  if (!getLocalTime(&ti))
    return;
  int curH = ti.tm_hour, curM = ti.tm_min;
  for (int i = 0; i < 4; i++)
  {
    if (!lampSchedules[i].hasSchedule)
      continue;
    if (curH == lampSchedules[i].onTime.hour && curM == lampSchedules[i].onTime.minute)
    {
      lampStates[i] = true;
      if (!isOffline && Firebase.ready())
        Firebase.setBool(fbdo, "/lampu/" + String(i + 1), true);
      updateRelays();
    }
    if (curH == lampSchedules[i].offTime.hour && curM == lampSchedules[i].offTime.minute)
    {
      lampStates[i] = false;
      if (!isOffline && Firebase.ready())
        Firebase.setBool(fbdo, "/lampu/" + String(i + 1), false);
      updateRelays();
    }
  }
}

void printAllSchedules()
{
  Serial.println("===== CURRENT SCHEDULES =====");
  for (int i = 0; i < 4; i++)
  {
    Serial.print("Lamp ");
    Serial.print(i + 1);
    Serial.print(": ");
    if (lampSchedules[i].hasSchedule)
    {
      char buf[6];
      snprintf(buf, sizeof(buf), "%02d:%02d",
               lampSchedules[i].onTime.hour, lampSchedules[i].onTime.minute);
      Serial.print("ON ");
      Serial.print(buf);
      snprintf(buf, sizeof(buf), "%02d:%02d",
               lampSchedules[i].offTime.hour, lampSchedules[i].offTime.minute);
      Serial.print(" OFF ");
      Serial.println(buf);
    }
    else
    {
      Serial.println("No schedule");
    }
  }
  Serial.println("=============================");
}

void handleSoundSensor()
{
  // Hanya aktif saat offline
  if (!isOffline)
  {
    tepukCount = 0;
    return;
  }
  int val = analogRead(SOUND_SENSOR_PIN);
  // Tampilkan nilai analog dari sensor suara (opsional)
  //  Serial.print("Sensor Value: "); Serial.println(val);

  if (val > soundThreshold && millis() - lastSoundTime > soundDetectionInterval)
  {
    lastSoundTime = millis();
    tepukCount++;
    lastTepukTime = millis();
    // Serial print untuk jumlah tepukan terkini
    Serial.print("Tepuk detected! Count = ");
    Serial.println(tepukCount);
  }

  if (tepukCount > 0 && millis() - lastTepukTime > tepukTimeWindow)
  {
    // Saat window habis, tunjukkan total tepukan sebelum diproses
    Serial.print("Final tepuk count in window: ");
    Serial.println(tepukCount);
    processTepukAction(tepukCount);
    tepukCount = 0;
  }
}

void processTepukAction(int count)
{
  Serial.print("Processing tepuk action for count = ");
  Serial.println(count);

  if (!isOffline)
    return;

  if (count >= 1 && count <= 4)
  {
    int idx = count - 1;
    lampStates[idx] = !lampStates[idx];
  }
  else if (count == 5)
  {
    bool newSt = !lampStates[0];
    for (int i = 0; i < 4; i++)
      lampStates[i] = newSt;
  }
  updateRelays();
}

void updateRelays()
{
  digitalWrite(RELAY_1, lampStates[0] ? LOW : HIGH);
  digitalWrite(RELAY_2, lampStates[1] ? LOW : HIGH);
  digitalWrite(RELAY_3, lampStates[2] ? LOW : HIGH);
  digitalWrite(RELAY_4, lampStates[3] ? LOW : HIGH);
}