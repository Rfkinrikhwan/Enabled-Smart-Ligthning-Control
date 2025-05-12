#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <FirebaseESP32.h>
#include <time.h>

// Firebase helper includes
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// Firebase configuration
#define API_KEY "AIzaSyBDajKmo4JpXoYS7Xu0c3QBIImlanvOFFY"
#define DATABASE_URL "https://smartlightingproject-d599e-default-rtdb.asia-southeast1.firebasedatabase.app/"

// Pin Definitions
const uint8_t RELAY_PINS[4] = {12, 14, 27, 26}; // RELAY_1-4
#define SOUND_SENSOR_PIN 35
#define BUZZER_PIN 25
#define WIFI_LED 2

// Time configuration
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200; // GMT+7 for Indonesia

// Firebase components
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Global variables
bool signUpOK = false;
bool isOffline = false;
bool lampStates[4] = {false};

// Sound detection variables
unsigned long lastSoundTime = 0;
const int soundThreshold = 3000;
unsigned long soundDetectionInterval = 300; // ms between claps
unsigned long lastTepukTime = 0;
const int tepukTimeWindow = 3000; // 3-second window for clap detection
int tepukCount = 0;

// WiFi connection variables
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 30000; // 30 seconds between reconnect attempts
int consecutiveFailedConnections = 0;
const int maxFailedConnections = 5;
unsigned long wifiConnectTimeout = 60000; // 60 seconds timeout

// Schedule structure
struct ScheduleTime
{
    int hour;
    int minute;
};

struct LampSchedule
{
    ScheduleTime onTime;
    ScheduleTime offTime;
    bool hasSchedule;
};

LampSchedule lampSchedules[4];

// Function Prototypes
void setupPins();
void setupWiFi();
bool checkWiFiConnection();
void connectToFirebase();
void updateWiFiLED(bool connected);
void updateRelays();
void playTone(int frequency, int duration);
void playSuccessTone();
void playErrorTone();
void handleSoundSensor();
void processTepukAction(int count);
void handleSchedules();
void readFirebaseData();
bool parseTimeString(String timeStr, ScheduleTime &time);
void loadAllSchedules();
void updateDeviceStatus();

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n===== Smart Lighting System Starting =====");

    // Setup pins and initial states
    setupPins();

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
        updateWiFiLED(true);
        configTime(gmtOffset_sec, 0, ntpServer);

        // Connect to Firebase
        connectToFirebase();
    }
    else
    {
        isOffline = true;
        Serial.println("Starting in offline mode");
        updateWiFiLED(false);
        playErrorTone();
    }

    // Ensure relays are in correct state
    updateRelays();
}

void loop()
{
    // Handle sound sensor for offline control
    handleSoundSensor();

    // Check and maintain WiFi connection
    bool wifiConnected = checkWiFiConnection();
    updateWiFiLED(wifiConnected);

    // Handle online functionality if WiFi is connected
    if (wifiConnected && !isOffline)
    {
        // Check schedules every 10 seconds
        static unsigned long lastScheduleCheck = 0;
        if (millis() - lastScheduleCheck > 10000)
        {
            handleSchedules();
            lastScheduleCheck = millis();
        }

        // Read data from Firebase
        readFirebaseData();

        // Update device status in Firebase every 30 seconds
        static unsigned long lastStatusUpdate = 0;
        if (millis() - lastStatusUpdate > 30000)
        {
            updateDeviceStatus();
            loadAllSchedules();
            lastStatusUpdate = millis();
        }
    }

    // Always update relays to match current state
    updateRelays();

    // Small delay to prevent CPU hogging
    delay(10);
}

void setupPins()
{
    // Configure relay pins as OUTPUT and set to OFF state
    for (int i = 0; i < 4; i++)
    {
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], HIGH); // HIGH is OFF for active LOW relays
    }

    // Configure other pins
    pinMode(SOUND_SENSOR_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(WIFI_LED, OUTPUT);
    digitalWrite(WIFI_LED, LOW);
}

void setupWiFi()
{
    WiFiManager wifiManager;
    wifiManager.setTimeout(wifiConnectTimeout / 1000);
    WiFi.setSleep(false); // Disable WiFi sleep mode for better stability

    Serial.println("Attempting to connect to WiFi...");
    bool wifiConnected = wifiManager.autoConnect("SmartLighting_AP", "smart1234");

    if (wifiConnected)
    {
        Serial.println("WiFi Connected: " + WiFi.localIP().toString());
        updateWiFiLED(true);
        playSuccessTone();
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
    }
    else
    {
        Serial.println("Failed to connect to WiFi. Starting in offline mode.");
        isOffline = true;
        updateWiFiLED(false);
        playErrorTone();
    }
}

bool checkWiFiConnection()
{
    static unsigned long lastConnectionCheck = 0;
    static bool wasConnected = false;
    bool isConnected = (WiFi.status() == WL_CONNECTED);

    // Check WiFi status every 5 seconds
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
                updateWiFiLED(true);
                playSuccessTone();

                // Reconnect to Firebase if needed
                if (signUpOK == false)
                {
                    connectToFirebase();
                }

                consecutiveFailedConnections = 0;
            }
            else
            {
                Serial.println("WiFi connection lost!");
                consecutiveFailedConnections++;

                if (!isOffline)
                {
                    playErrorTone();
                    updateWiFiLED(false);
                    isOffline = true;
                }

                // Restart device after too many failures
                if (consecutiveFailedConnections >= maxFailedConnections)
                {
                    Serial.println("Too many failed connections. Restarting ESP32...");
                    ESP.restart();
                }
            }
            wasConnected = isConnected;
        }

        // Try to reconnect if disconnected
        if (!isConnected && (millis() - lastReconnectAttempt > reconnectInterval))
        {
            Serial.println("Attempting to reconnect WiFi...");
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
    config = FirebaseConfig();
    auth = FirebaseAuth();

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;

    Serial.println("Connecting to Firebase...");

    if (Firebase.signUp(&config, &auth, "", ""))
    {
        Serial.println("Firebase SignUp successful");
        signUpOK = true;
        playSuccessTone();
    }
    else
    {
        Serial.println("SignUp FAILED: " + String(config.signer.signupError.message.c_str()));
        playErrorTone();
        isOffline = true;
        return;
    }

    config.token_status_callback = tokenStatusCallback;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    // Connect to stream with retries
    for (int retries = 0; retries < 3; retries++)
    {
        if (Firebase.beginStream(fbdo, "/lampu"))
        {
            Serial.println("Firebase stream connected successfully");
            break;
        }
        else
        {
            Serial.println("Stream connection attempt " + String(retries + 1) + ": " + fbdo.errorReason());
            if (retries >= 2)
            {
                Serial.println("Failed to establish stream after maximum retries");
                isOffline = true;
                playErrorTone();
                return;
            }
            delay(1000);
        }
    }

    // Load initial lamp states and schedules
    loadInitialLampStates();
    loadAllSchedules();
}

void loadInitialLampStates()
{
    FirebaseData lampData;

    for (int retries = 0; retries < 3; retries++)
    {
        if (Firebase.getJSON(lampData, "/lampu"))
        {
            String jsonStr = lampData.jsonString();

            // Parse lamp states
            for (int i = 1; i <= 4; i++)
            {
                bool state = false;

                if (Firebase.getBool(lampData, "/lampu/" + String(i), &state))
                {
                    lampStates[i - 1] = state;
                }
                else
                {
                    // Fallback to JSON parsing
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

            break;
        }
        else
        {
            Serial.println("Failed to get lamp data, attempt " + String(retries + 1));
            if (retries >= 2)
                break;
            delay(1000);
        }
    }
}

void loadAllSchedules()
{
    Serial.println("Loading lamp schedules...");

    for (int i = 1; i <= 4; i++)
    {
        FirebaseData scheduleData;
        String onTime, offTime;
        bool onExists = false, offExists = false;

        // Get ON time
        if (Firebase.getString(scheduleData, "/jadwal/" + String(i) + "/on"))
        {
            onTime = scheduleData.stringData();
            onTime.replace("\"", "");
            onExists = true;
        }

        // Get OFF time
        if (Firebase.getString(scheduleData, "/jadwal/" + String(i) + "/off"))
        {
            offTime = scheduleData.stringData();
            offTime.replace("\"", "");
            offExists = true;
        }

        // Parse times and set schedule
        if (onExists && offExists)
        {
            if (parseTimeString(onTime, lampSchedules[i - 1].onTime) &&
                parseTimeString(offTime, lampSchedules[i - 1].offTime))
            {
                lampSchedules[i - 1].hasSchedule = true;
                Serial.println("Schedule set for lamp " + String(i) +
                               " - On: " + String(lampSchedules[i - 1].onTime.hour) + ":" +
                               String(lampSchedules[i - 1].onTime.minute) +
                               ", Off: " + String(lampSchedules[i - 1].offTime.hour) + ":" +
                               String(lampSchedules[i - 1].offTime.minute));
            }
            else
            {
                lampSchedules[i - 1].hasSchedule = false;
            }
        }
        else
        {
            lampSchedules[i - 1].hasSchedule = false;
        }
    }
}

bool parseTimeString(String timeStr, ScheduleTime &time)
{
    timeStr.replace("\"", "");
    timeStr.trim();

    int colonPos = timeStr.indexOf(':');
    if (colonPos > 0)
    {
        String hourStr = timeStr.substring(0, colonPos);
        String minStr = timeStr.substring(colonPos + 1);

        hourStr.trim();
        minStr.trim();

        time.hour = hourStr.toInt();
        time.minute = minStr.toInt();

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

            // Update lamp states based on Firebase data
            for (int i = 1; i <= 4; i++)
            {
                if (path == "/" + String(i))
                {
                    lampStates[i - 1] = (data == "true");
                    break;
                }
            }
        }
    }
    else if (!isOffline)
    {
        // Check if we need to restart stream
        static unsigned long lastStreamReconnect = 0;
        if (millis() - lastStreamReconnect > 60000)
        { // Every minute
            lastStreamReconnect = millis();

            if (Firebase.beginStream(fbdo, "/lampu"))
            {
                Serial.println("Stream restarted successfully");
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

    Firebase.setString(fbdo, "/device_status/esp32_1/last_seen", timestamp);
    Firebase.setBool(fbdo, "/device_status/esp32_1/online", true);
}

void handleSchedules()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
        return;

    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;

    for (int i = 0; i < 4; i++)
    {
        if (lampSchedules[i].hasSchedule)
        {
            // Check if current time matches ON schedule
            if (currentHour == lampSchedules[i].onTime.hour &&
                currentMinute == lampSchedules[i].onTime.minute)
            {
                lampStates[i] = true;

                // Update Firebase if online
                if (!isOffline && Firebase.ready())
                {
                    Firebase.setBool(fbdo, "/lampu/" + String(i + 1), true);
                }

                updateRelays();
                playSuccessTone();
            }

            // Check if current time matches OFF schedule
            if (currentHour == lampSchedules[i].offTime.hour &&
                currentMinute == lampSchedules[i].offTime.minute)
            {
                lampStates[i] = false;

                // Update Firebase if online
                if (!isOffline && Firebase.ready())
                {
                    Firebase.setBool(fbdo, "/lampu/" + String(i + 1), false);
                }

                updateRelays();
                playTone(1000, 200);
            }
        }
    }
}

void handleSoundSensor()
{
    int soundValue = analogRead(SOUND_SENSOR_PIN);

    // Process claps after time window expires
    if (tepukCount > 0 && millis() - lastTepukTime > tepukTimeWindow)
    {
        processTepukAction(tepukCount);
        tepukCount = 0;
        return;
    }

    // Detect loud sound (clap)
    if (soundValue > soundThreshold && millis() - lastSoundTime > soundDetectionInterval)
    {
        lastSoundTime = millis();
        tepukCount++;
        lastTepukTime = millis();
        playTone(2000, 50); // Confirmation tone
        Serial.println("Clap detected: " + String(tepukCount));
    }
}

void processTepukAction(int count)
{
    Serial.println("Processing " + String(count) + " claps");

    // Only process claps in offline mode
    if (isOffline)
    {
        if (count >= 1 && count <= 4)
        {
            // Toggle individual lamp (1-4)
            int lampIndex = count - 1;
            lampStates[lampIndex] = !lampStates[lampIndex];

            // Play confirmation tone
            lampStates[lampIndex] ? playSuccessTone() : playTone(1000, 200);
        }
        else if (count == 5)
        {
            // Toggle all lamps together
            bool newState = !lampStates[0];

            for (int i = 0; i < 4; i++)
            {
                lampStates[i] = newState;
            }

            // Play special confirmation tone
            if (newState)
            {
                for (int i = 0; i < 3; i++)
                {
                    playTone(1200, 100);
                    delay(50);
                }
            }
            else
            {
                for (int i = 0; i < 3; i++)
                {
                    playTone(800, 100);
                    delay(50);
                }
            }
        }

        // Update relays after state change
        updateRelays();
    }
}

void updateRelays()
{
    for (int i = 0; i < 4; i++)
    {
        digitalWrite(RELAY_PINS[i], lampStates[i] ? LOW : HIGH);
    }
}

void updateWiFiLED(bool connected)
{
    digitalWrite(WIFI_LED, connected ? HIGH : LOW);
}

// Buzzer functions
void playTone(int frequency, int duration)
{
    tone(BUZZER_PIN, frequency, duration);
    delay(duration * 1.3);
    noTone(BUZZER_PIN);
}

void playSuccessTone()
{
    playTone(1000, 100);
    playTone(1500, 100);
    playTone(2000, 100);
}

void playErrorTone()
{
    playTone(500, 200);
    playTone(300, 300);
}