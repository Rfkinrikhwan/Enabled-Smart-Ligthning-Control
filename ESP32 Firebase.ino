#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <time.h>

// === Firebase ===
#define API_KEY "AIzaSyBDajKmo4JpXoYS7Xu0c3QBIImlanvOFFY"                                                    // Ganti dengan API Key kamu
#define DATABASE_URL "https://smartlightingproject-d599e-default-rtdb.asia-southeast1.firebasedatabase.app/" // Ganti dengan Realtime DB URL kamu

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// === Pin Konfigurasi ===
const int relayPins[4] = {23, 22, 21, 19};
const int clapPin = 34;
const int buzzerPin = 18;

bool offlineMode = false;
unsigned long lastHeartbeat = 0;

// === Tepukan ===
unsigned long lastClapTime = 0;
int clapCount = 0;
bool previousClap = LOW;
const unsigned long clapTimeout = 1000;

// === Setup Awal ===
void setup()
{
    Serial.begin(115200);
    for (int i = 0; i < 4; i++)
        pinMode(relayPins[i], OUTPUT);
    pinMode(clapPin, INPUT);
    pinMode(buzzerPin, OUTPUT);

    WiFiManager wm;
    if (!wm.autoConnect("SmartLamp-Setup"))
    {
        Serial.println("Gagal konek WiFi!");
        playFailTone();
        offlineMode = true;
    }
    else
    {
        Serial.println("Tersambung ke WiFi!");
        playSuccessTone();

        configTime(0, 0, "pool.ntp.org", "time.nist.gov");

        config.api_key = API_KEY;
        config.database_url = DATABASE_URL;
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);

        if (!Firebase.RTDB.beginStream(&fbdo, "/lampu"))
        {
            Serial.println("Stream Firebase gagal");
            playFailTone();
            offlineMode = true;
        }
        else
        {
            Firebase.RTDB.setStreamCallback(&fbdo, streamCallback, streamTimeoutCallback);
            updateDeviceStatus(true);
        }
    }
}

// === Loop utama ===
void loop()
{
    if (offlineMode)
    {
        handleClapFallback();
    }
    else
    {
        if (millis() - lastHeartbeat > 30000)
        {
            updateDeviceStatus(true);
            lastHeartbeat = millis();
        }
    }
}

// === Firebase Listener ===
void streamCallback(FirebaseStream data)
{
    String path = data.dataPath();
    int lampuIndex = path.substring(1).toInt() - 1;

    if (lampuIndex >= 0 && lampuIndex < 4)
    {
        bool status = data.boolData();
        digitalWrite(relayPins[lampuIndex], status ? HIGH : LOW);
        Serial.printf("Lampu %d %s\n", lampuIndex + 1, status ? "HIDUP" : "MATI");
    }
}

void streamTimeoutCallback(bool timeout)
{
    if (timeout)
    {
        Serial.println("Stream timeout, masuk offline mode.");
        offlineMode = true;
        playFailTone();
    }
}

// === Mode Fallback Sensor Tepuk ===
void handleClapFallback()
{
    bool currentClap = digitalRead(clapPin);
    unsigned long currentTime = millis();

    if (currentClap == HIGH && previousClap == LOW && (currentTime - lastClapTime > 50))
    {
        clapCount++;
        lastClapTime = currentTime;
    }

    if (clapCount > 0 && (currentTime - lastClapTime > clapTimeout))
    {
        if (clapCount >= 1 && clapCount <= 4)
        {
            int index = clapCount - 1;
            bool currentState = digitalRead(relayPins[index]);
            digitalWrite(relayPins[index], !currentState);
            Serial.printf("Lampu %d %s (tepuk %d)\n", index + 1, !currentState ? "HIDUP" : "MATI", clapCount);
        }
        clapCount = 0;
    }

    previousClap = currentClap;
}

// === Status Device ===
void updateDeviceStatus(bool online)
{
    Firebase.RTDB.setBool(&fbdo, "/device_status/esp32_1/online", online);
    Firebase.RTDB.setString(&fbdo, "/device_status/esp32_1/last_seen", getISOTime());
}

String getISOTime()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        return "unknown";
    }
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return String(buf);
}

// === Nada Buzzer ===
void playSuccessTone()
{
    tone(buzzerPin, 1000, 200);
    delay(250);
    tone(buzzerPin, 1500, 200);
    delay(250);
    tone(buzzerPin, 2000, 200);
    delay(250);
    noTone(buzzerPin);
}

void playFailTone()
{
    tone(buzzerPin, 500, 400);
    delay(500);
    tone(buzzerPin, 300, 400);
    delay(500);
    noTone(buzzerPin);
}
