#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <FirebaseESP32.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <TimeLib.h>

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

// NTP Client for time sync
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200); // GMT+7 for Indonesia

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

// Schedule tracking
struct ScheduleTime
{
    int hour;
    int minute;
};

ScheduleTime onTime = {0, 0};
ScheduleTime offTime = {0, 0};
bool hasSchedule = false;

// Function Prototypes
void handleSchedule();
void playTone(int duration);
void playSuccessTone();
void playErrorTone();
void handleSoundSensor();
void updateRelays();
void connectToFirebase();
void readFirebaseData();
void updateDeviceStatus();

void setup()
{
    Serial.begin(115200);

    // Initialize pins
    pinMode(RELAY_1, OUTPUT);
    pinMode(RELAY_2, OUTPUT);
    pinMode(RELAY_3, OUTPUT);
    pinMode(RELAY_4, OUTPUT);
    pinMode(SOUND_SENSOR_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    // Set initial state of relays (HIGH = OFF for active low relays)
    digitalWrite(RELAY_1, HIGH);
    digitalWrite(RELAY_2, HIGH);
    digitalWrite(RELAY_3, HIGH);
    digitalWrite(RELAY_4, HIGH);

    // WiFiManager setup
    WiFiManager wifiManager;

    // Uncomment to reset WiFi settings for testing
    // wifiManager.resetSettings();

    // Custom parameters for WiFiManager
    // WiFiManagerParameter custom_text("Note that this device requires internet connection to control lights remotely");

    // Attempt to connect to WiFi
    Serial.println("Trying to connect to WiFi...");
    bool wifiConnected = wifiManager.autoConnect("SmartLighting_AP", "smart1234");

    if (wifiConnected)
    {
        Serial.println("WiFi Connected!");
        Serial.println("IP: " + WiFi.localIP().toString());

        // Initialize NTP client
        timeClient.begin();
        timeClient.update();

        // Connect to Firebase
        connectToFirebase();
    }
    else
    {
        Serial.println("Failed to connect to WiFi");
        isOffline = true;
        playErrorTone();
    }
}

void loop()
{
    // Handle sound sensor for offline control
    handleSoundSensor();

    // Handle online functionality if WiFi is connected
    if (WiFi.status() == WL_CONNECTED && !isOffline)
    {
        // Update time from NTP server every minute
        static unsigned long lastTimeUpdate = 0;
        if (millis() - lastTimeUpdate > 60000)
        {
            timeClient.update();
            lastTimeUpdate = millis();

            // Check schedule
            handleSchedule();
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
    else if (WiFi.status() != WL_CONNECTED && !isOffline)
    {
        // Lost connection
        Serial.println("Lost WiFi connection, going to offline mode");
        isOffline = true;
        playErrorTone();
    }

    // Always update relays to match current state
    updateRelays();

    delay(100);
}

void connectToFirebase()
{
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;

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

    // Start listening for changes
    if (!Firebase.beginStream(fbdo, "/lampu"))
    {
        Serial.println("Gagal memulai stream:");
        Serial.println(fbdo.errorReason());
        playErrorTone();
        isOffline = true;
    }

    // Also check for schedule information
    FirebaseData scheduleData;
    if (Firebase.getJSON(scheduleData, "/jadwal/2"))
    {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, scheduleData.jsonString());

        if (doc.containsKey("on") && doc.containsKey("off"))
        {
            String onTimeStr = doc["on"].as<String>();
            String offTimeStr = doc["off"].as<String>();

            if (onTimeStr.length() >= 5 && offTimeStr.length() >= 5)
            {
                onTime.hour = onTimeStr.substring(0, 2).toInt();
                onTime.minute = onTimeStr.substring(3, 5).toInt();

                offTime.hour = offTimeStr.substring(0, 2).toInt();
                offTime.minute = offTimeStr.substring(3, 5).toInt();

                hasSchedule = true;
                Serial.println("Schedule loaded - On: " + String(onTime.hour) + ":" + String(onTime.minute) +
                               ", Off: " + String(offTime.hour) + ":" + String(offTime.minute));
            }
        }
    }
    else
    {
        Serial.println("Failed to get schedule data");
    }
}

void readFirebaseData()
{
    if (Firebase.readStream(fbdo))
    {
        if (fbdo.streamAvailable())
        {
            String path = fbdo.dataPath();
            Serial.print("Path: ");
            Serial.println(path);
            Serial.print("Data: ");
            Serial.println(fbdo.stringData());

            // Update lamp states based on Firebase data
            if (path == "/1")
            {
                lampStates[0] = (fbdo.stringData() == "true");
            }
            else if (path == "/2")
            {
                lampStates[1] = (fbdo.stringData() == "true");
            }
            else if (path == "/3")
            {
                lampStates[2] = (fbdo.stringData() == "true");
            }
            else if (path == "/4")
            {
                lampStates[3] = (fbdo.stringData() == "true");
            }
        }
    }
    else
    {
        Serial.println("Firebase stream error: " + fbdo.errorReason());
    }
}

void updateDeviceStatus()
{
    String timestamp = String(timeClient.getYear()) + "-" +
                       String(timeClient.getMonth()) + "-" +
                       String(timeClient.getDay()) + "T" +
                       String(timeClient.getHours()) + ":" +
                       String(timeClient.getMinutes()) + ":" +
                       String(timeClient.getSeconds()) + "Z";

    Firebase.setString(fbdo, "/device_status/esp32_1/last_seen", timestamp);
    Firebase.setBool(fbdo, "/device_status/esp32_1/online", true);
}

void handleSchedule()
{
    if (!hasSchedule)
        return;

    int currentHour = timeClient.getHours();
    int currentMinute = timeClient.getMinutes();

    // Convert to total minutes for easier comparison
    int currentTimeInMinutes = currentHour * 60 + currentMinute;
    int onTimeInMinutes = onTime.hour * 60 + onTime.minute;
    int offTimeInMinutes = offTime.hour * 60 + offTime.minute;

    // Check if we need to turn all lights on
    if (currentTimeInMinutes == onTimeInMinutes)
    {
        Serial.println("Scheduled ON time reached");
        for (int i = 0; i < 4; i++)
        {
            lampStates[i] = true;
            String lampPath = "/" + String(i + 1);
            Firebase.setBool(fbdo, "/lampu" + lampPath, true);
        }
    }

    // Check if we need to turn all lights off
    if (currentTimeInMinutes == offTimeInMinutes)
    {
        Serial.println("Scheduled OFF time reached");
        for (int i = 0; i < 4; i++)
        {
            lampStates[i] = false;
            String lampPath = "/" + String(i + 1);
            Firebase.setBool(fbdo, "/lampu" + lampPath, false);
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
    digitalWrite(RELAY_1, lampStates[0] ? LOW : HIGH);
    digitalWrite(RELAY_2, lampStates[1] ? LOW : HIGH);
    digitalWrite(RELAY_3, lampStates[2] ? LOW : HIGH);
    digitalWrite(RELAY_4, lampStates[3] ? LOW : HIGH);
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