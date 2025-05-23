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

#define SOUND_SENSOR_PIN 35 // Pin untuk sensor tepok
#define BUZZER_PIN 25       // Pin untuk buzzer
#define WIFI_LED 2          // Onboard blue LED on GPIO2 (D2)

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

// Sound detection variables
unsigned long lastSoundTime = 0;
const int soundThreshold = 3000;            // Mungkin perlu disesuaikan dengan sensitivitas sensor
unsigned long soundDetectionInterval = 300; // Interval minimal antar tepukan (ms)
unsigned long lastTepukTime = 0;
const int tepukTimeWindow = 3000; // Jendela waktu 3 detik untuk mendeteksi semua tepukan
int tepukCount = 0;

// WiFi connection stability variables
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 30000; // Try to reconnect every 30 seconds
int consecutiveFailedConnections = 0;
const int maxFailedConnections = 5;       // After 5 failed attempts, restart ESP32
unsigned long wifiConnectTimeout = 60000; // 15 second timeout for WiFi connections

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
void playTone(int frequency, int duration);
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
void printAllSchedules();
void updateWiFiLED(bool connected);
void processTepukAction(int count);

void setup()
{
    Serial.begin(115200);

    // Inisialisasi pin relay dan matikan dulu (HIGH untuk low-active relay)
    pinMode(RELAY_1, OUTPUT);
    pinMode(RELAY_2, OUTPUT);
    pinMode(RELAY_3, OUTPUT);
    pinMode(RELAY_4, OUTPUT);

    digitalWrite(RELAY_1, HIGH);
    digitalWrite(RELAY_2, HIGH);
    digitalWrite(RELAY_3, HIGH);
    digitalWrite(RELAY_4, HIGH);

    // Delay agar ESP32 stabil dulu sebelum lanjut
    delay(3000);

    // Konfigurasi pin lainnya
    pinMode(SOUND_SENSOR_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(WIFI_LED, OUTPUT);   // Initialize WiFi LED pin
    digitalWrite(WIFI_LED, LOW); // Turn OFF WiFi LED initially

    // Verifikasi status relay - tambahkan delay kecil dan cetak status
    delay(100);
    Serial.println("Relay status after initialization:");
    Serial.println("Relay 1: " + String(digitalRead(RELAY_1) == HIGH ? "OFF" : "ON"));
    Serial.println("Relay 2: " + String(digitalRead(RELAY_2) == HIGH ? "OFF" : "ON"));
    Serial.println("Relay 3: " + String(digitalRead(RELAY_3) == HIGH ? "OFF" : "ON"));
    Serial.println("Relay 4: " + String(digitalRead(RELAY_4) == HIGH ? "OFF" : "ON"));

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
        // Turn on WiFi LED as we're connected
        updateWiFiLED(true);

        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        struct tm timeinfo;
        if (getLocalTime(&timeinfo))
        {
            Serial.println("Current time: " + String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min));
        }
        else
        {
            Serial.println("Failed to obtain time! Check NTP server connection.");
        }

        // Connect to Firebase
        connectToFirebase();

        // Print all schedules after loading for debugging
        printAllSchedules();
    }
    else
    {
        isOffline = true;
        Serial.println("Starting in offline mode");
        updateWiFiLED(false); // Turn off WiFi LED as we're offline
        playErrorTone();
    }

    // Pastikan relay tetap OFF setelah semua inisialisasi
    updateRelays();
}

void loop()
{
    // Handle sound sensor for offline control
    handleSoundSensor();

    // Check and maintain WiFi connection
    bool wifiConnected = checkWiFiConnection();

    // Update WiFi LED based on connection status
    updateWiFiLED(wifiConnected);

    // Handle online functionality if WiFi is connected
    if (wifiConnected && !isOffline)
    {
        // Check schedules more frequently (every 10 seconds)
        static unsigned long lastScheduleCheck = 0;
        if (millis() - lastScheduleCheck > 10000)
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
            loadAllSchedules();
            lastStatusUpdate = millis();
        }
    }

    // Always update relays to match current state
    updateRelays();

    // Small delay to prevent CPU hogging
    delay(10);
}

// Helper function to update WiFi LED status
void updateWiFiLED(bool connected)
{
    digitalWrite(WIFI_LED, connected ? HIGH : LOW);
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
    bool wifiConnected = wifiManager.autoConnect("Smart Light", "bismillah");

    if (wifiConnected)
    {
        Serial.println("WiFi Connected Successfully!");
        Serial.println("IP: " + WiFi.localIP().toString());
        updateWiFiLED(true); // Turn ON WiFi LED
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
        updateWiFiLED(false); // Turn OFF WiFi LED
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
                updateWiFiLED(true); // Turn ON WiFi LED
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
                    updateWiFiLED(false); // Turn OFF WiFi LED
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
    Serial.println("Loading lamp schedules...");

    for (int i = 1; i <= 4; i++)
    {
        FirebaseData scheduleData;
        String onPath = "/jadwal/" + String(i) + "/on";
        String offPath = "/jadwal/" + String(i) + "/off";

        String onTime, offTime;
        bool onExists = false, offExists = false;

        // Get ON time - dengan debugging tambahan
        if (Firebase.getString(scheduleData, onPath))
        {
            onTime = scheduleData.stringData();
            Serial.println("Raw ON time data for lamp " + String(i) + ": " + onTime);

            // Hapus quotes tambahan
            onTime.replace("\"", "");
            onExists = true;
            Serial.println("Lamp " + String(i) + " ON time cleaned: " + onTime);
        }
        else
        {
            Serial.println("Failed to get ON schedule for lamp " + String(i) + ": " + scheduleData.errorReason());
        }

        // Get OFF time - dengan debugging tambahan
        if (Firebase.getString(scheduleData, offPath))
        {
            offTime = scheduleData.stringData();
            Serial.println("Raw OFF time data for lamp " + String(i) + ": " + offTime);

            // Hapus quotes tambahan
            offTime.replace("\"", "");
            offExists = true;
            Serial.println("Lamp " + String(i) + " OFF time cleaned: " + offTime);
        }
        else
        {
            Serial.println("Failed to get OFF schedule for lamp " + String(i) + ": " + scheduleData.errorReason());
        }

        // Jika keduanya ada, parsing waktu dan atur jadwal
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
                Serial.println("Failed to parse schedule times for lamp " + String(i));
                lampSchedules[i - 1].hasSchedule = false;
            }
        }
        else
        {
            Serial.println("No complete schedule found for lamp " + String(i));
            lampSchedules[i - 1].hasSchedule = false;
        }
    }
}

bool parseTimeString(String timeStr, ScheduleTime &time)
{
    // Bersihkan string dari karakter yang tidak diinginkan
    timeStr.replace("\"", "");
    timeStr.trim();

    // Penanganan format seperti "12:00" dan sebagainya
    int colonPos = timeStr.indexOf(':');
    if (colonPos > 0)
    {
        String hourStr = timeStr.substring(0, colonPos);
        String minStr = timeStr.substring(colonPos + 1);

        // Bersihkan dari quotes tambahan atau whitespace
        hourStr.trim();
        minStr.trim();

        time.hour = hourStr.toInt();
        time.minute = minStr.toInt();

        Serial.print("Parsed time: ");
        Serial.print(time.hour);
        Serial.print(":");
        Serial.println(time.minute);

        // Validasi nilai waktu
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

    Serial.print("Checking schedules at: ");
    Serial.print(currentHour);
    Serial.print(":");
    Serial.println(currentMinute);

    for (int i = 0; i < 4; i++)
    {
        if (lampSchedules[i].hasSchedule)
        {
            Serial.print("Lamp ");
            Serial.print(i + 1);
            Serial.print(" schedule - On: ");
            Serial.print(lampSchedules[i].onTime.hour);
            Serial.print(":");
            Serial.print(lampSchedules[i].onTime.minute);
            Serial.print(", Off: ");
            Serial.print(lampSchedules[i].offTime.hour);
            Serial.print(":");
            Serial.println(lampSchedules[i].offTime.minute);

            // Cek apakah waktu saat ini sesuai dengan jadwal ON
            if (currentHour == lampSchedules[i].onTime.hour &&
                currentMinute == lampSchedules[i].onTime.minute)
            {
                Serial.println("*** ACTIVATING ON schedule for lamp " + String(i + 1));
                lampStates[i] = true;

                // Update Firebase jika online
                if (!isOffline && Firebase.ready())
                {
                    if (Firebase.setBool(fbdo, "/lampu/" + String(i + 1), true))
                    {
                        Serial.println("Firebase lamp state updated successfully");
                    }
                    else
                    {
                        Serial.println("Failed to update Firebase: " + fbdo.errorReason());
                    }
                }

                // Update relay state segera
                updateRelays();
                playSuccessTone(); // Tambahkan notifikasi audio
            }

            // Cek apakah waktu saat ini sesuai dengan jadwal OFF
            if (currentHour == lampSchedules[i].offTime.hour &&
                currentMinute == lampSchedules[i].offTime.minute)
            {
                Serial.println("*** ACTIVATING OFF schedule for lamp " + String(i + 1));
                lampStates[i] = false;

                // Update Firebase jika online
                if (!isOffline && Firebase.ready())
                {
                    if (Firebase.setBool(fbdo, "/lampu/" + String(i + 1), false))
                    {
                        Serial.println("Firebase lamp state updated successfully");
                    }
                    else
                    {
                        Serial.println("Failed to update Firebase: " + fbdo.errorReason());
                    }
                }

                // Update relay state segera
                updateRelays();
                playTone(1000, 200); // Nada singkat untuk OFF
            }
        }
    }
}

void handleSoundSensor()
{
    int soundValue = analogRead(SOUND_SENSOR_PIN);

    // Jika sudah cukup tepukan dan waktu tunggu habis, proses tepukan
    if (tepukCount > 0 && millis() - lastTepukTime > tepukTimeWindow)
    {
        // Proses jumlah tepukan yang sudah terkumpul
        processTepukAction(tepukCount);
        tepukCount = 0; // Reset jumlah tepukan setelah diproses
        return;
    }

    // Deteksi suara keras (tepukan)
    if (soundValue > soundThreshold)
    {
        if (millis() - lastSoundTime > soundDetectionInterval)
        {
            Serial.println("Sound detected! Value: " + String(soundValue));
            lastSoundTime = millis();

            // Tambah jumlah tepukan
            tepukCount++;
            lastTepukTime = millis();

            // Play tone untuk konfirmasi tepukan terdeteksi
            playTone(2000, 50);

            Serial.println("Tepuk terdeteksi: " + String(tepukCount));
        }
    }
}

void processTepukAction(int count)
{
    Serial.println("Memproses " + String(count) + " tepukan");

    // Hanya di mode offline kita proses tepukan
    if (isOffline)
    {
        // Tentukan lampu mana yang akan dipengaruhi berdasarkan jumlah tepukan
        if (count >= 1 && count <= 4)
        {
            // Toggle lampu individual (1-4)
            int lampIndex = count - 1;
            lampStates[lampIndex] = !lampStates[lampIndex];

            Serial.println("Toggle lampu " + String(count) + " menjadi " +
                           (lampStates[lampIndex] ? "HIDUP" : "MATI"));

            // Mainkan nada konfirmasi
            if (lampStates[lampIndex])
            {
                playSuccessTone();
            }
            else
            {
                playTone(1000, 200);
            }
        }
        else if (count == 5)
        {
            // Toggle semua lampu bersama-sama
            bool newState = !lampStates[0]; // Gunakan kebalikan dari lampu 1 sebagai status target

            for (int i = 0; i < 4; i++)
            {
                lampStates[i] = newState;
            }

            Serial.println("Toggle SEMUA lampu menjadi " + String(newState ? "HIDUP" : "MATI"));

            // Mainkan nada konfirmasi khusus untuk semua lampu
            if (newState)
            {
                // Nada khusus untuk semua lampu hidup
                for (int i = 0; i < 3; i++)
                {
                    playTone(1200, 100);
                    delay(50);
                }
            }
            else
            {
                // Nada khusus untuk semua lampu mati
                for (int i = 0; i < 3; i++)
                {
                    playTone(800, 100);
                    delay(50);
                }
            }
        }

        // Update relay setelah perubahan status
        updateRelays();
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

// Fungsi tambahan untuk debugging jadwal
void printAllSchedules()
{
    Serial.println("\n===== CURRENT SCHEDULES =====");
    for (int i = 0; i < 4; i++)
    {
        Serial.print("Lamp ");
        Serial.print(i + 1);
        Serial.print(": ");

        if (lampSchedules[i].hasSchedule)
        {
            Serial.print("ON at ");
            Serial.print(lampSchedules[i].onTime.hour);
            Serial.print(":");
            if (lampSchedules[i].onTime.minute < 10)
                Serial.print("0");
            Serial.print(lampSchedules[i].onTime.minute);

            Serial.print(", OFF at ");
            Serial.print(lampSchedules[i].offTime.hour);
            Serial.print(":");
            if (lampSchedules[i].offTime.minute < 10)
                Serial.print("0");
            Serial.println(lampSchedules[i].offTime.minute);
        }
        else
        {
            Serial.println("No schedule");
        }
    }
    Serial.println("============================\n");
}

// FUNGSI BUZZER YANG DISEDERHANAKAN

// Fungsi dasar untuk memainkan nada
void playTone(int frequency, int duration)
{
    tone(BUZZER_PIN, frequency, duration);
    delay(duration * 1.3); // jeda sedikit setelah tiap nada
    noTone(BUZZER_PIN);
}

// Fungsi untuk nada sukses/positif
void playSuccessTone()
{
    // Nada sukses - 3 nada naik
    playTone(1000, 100);
    playTone(1500, 100);
    playTone(2000, 100);
}

// Fungsi untuk nada error/negatif
void playErrorTone()
{
    // Nada error - 2 nada rendah
    playTone(500, 200);
    playTone(300, 300);
}