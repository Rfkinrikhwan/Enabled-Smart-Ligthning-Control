#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

// Port server (umumnya 80 untuk HTTP atau 8080 untuk alternatif)
const int SERVER_PORT = 80;

// Konfigurasi buzzer
const int BUZZER_PIN = 23; // Gunakan pin yang tersedia pada ESP32 Anda

// Konfigurasi sensor tepok (clap sensor)
const int CLAP_SENSOR_PIN = 34;         // Gunakan pin analog yang tersedia pada ESP32 Anda
const int CLAP_THRESHOLD = 1000;        // Nilai threshold untuk deteksi tepok
const unsigned long CLAP_TIMEOUT = 800; // Timeout antara tepok (ms)
unsigned long lastClapTime = 0;
bool isFirstClap = false;
int currentLightToggle = 0; // Lampu yang akan diaktifkan dengan tepok

// Konfigurasi lampu dengan relay
const int LAMP_COUNT = 4;

// Konstanta untuk nama mDNS
const char *MDNS_NAME = "smartlighting";

struct Lamp
{
    int relayPin;
    String name;
    bool isOn;
};

Lamp lamps[LAMP_COUNT] = {
    {14, "Lamp 1", false}, // Lampu 1: Relay pin 14
    {27, "Lamp 2", false}, // Lampu 2: Relay pin 27
    {33, "Lamp 3", false}, // Lampu 3: Relay pin 33
    {19, "Lamp 4", false}  // Lampu 4: Relay pin 19
};

// Membuat instance server web pada port yang ditentukan
WebServer server(SERVER_PORT);

// WiFiManager
WiFiManager wifiManager;

// Variabel untuk menyimpan IP Address
String ipAddress = "";

// Variabel untuk mode running
bool isRunningMode = false;
unsigned long lastRunningUpdate = 0;
int RUNNING_INTERVAL = 500; // Interval perpindahan lampu (ms)
int currentRunningLamp = 0;

// Fungsi untuk membunyikan buzzer sesuai dengan pola
void playBuzzerTone(int frequency, int duration)
{
    tone(BUZZER_PIN, frequency, duration);
}

// Fungsi untuk membunyikan notifikasi WiFi terhubung dengan sukses
void playSuccessSound()
{
    // Memainkan nada sukses (ascending)
    playBuzzerTone(1000, 100);
    delay(100);
    playBuzzerTone(2000, 100);
    delay(100);
    playBuzzerTone(3000, 100);
    delay(100);
    playBuzzerTone(4000, 300);
    delay(300);
    noTone(BUZZER_PIN);
}

// Fungsi untuk membunyikan notifikasi WiFi gagal terhubung
void playFailSound()
{
    // Memainkan nada gagal (descending)
    playBuzzerTone(4000, 100);
    delay(100);
    playBuzzerTone(3000, 100);
    delay(100);
    playBuzzerTone(2000, 100);
    delay(100);
    playBuzzerTone(1000, 300);
    delay(300);
    noTone(BUZZER_PIN);
}

// Fungsi untuk menambahkan header CORS
void sendCORSHeaders()
{
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// Fungsi untuk menangani preflight request OPTIONS
void handleCORS()
{
    sendCORSHeaders();
    server.send(204);
}

// Fungsi untuk mematikan satu lampu
void turnOffLamp(int lampId)
{
    if (lampId >= 0 && lampId < LAMP_COUNT)
    {
        // Aktifkan relay untuk mematikan lampu (relay aktif LOW)
        digitalWrite(lamps[lampId].relayPin, HIGH);
        lamps[lampId].isOn = false;
        Serial.println(lamps[lampId].name + " dimatikan");
    }
}

// Fungsi untuk mematikan semua lampu
void turnOffAllLamps()
{
    for (int i = 0; i < LAMP_COUNT; i++)
    {
        turnOffLamp(i);
    }
    Serial.println("Semua lampu dimatikan");
}

// Fungsi untuk menyalakan lampu
void turnOnLamp(int lampId)
{
    if (lampId >= 0 && lampId < LAMP_COUNT)
    {
        // Aktifkan relay untuk menyalakan lampu (relay aktif LOW)
        digitalWrite(lamps[lampId].relayPin, LOW);
        lamps[lampId].isOn = true;
        Serial.println(lamps[lampId].name + " dinyalakan");
    }
}

// Fungsi Untuk Menghidupkan Semua Lampu
void turnOnAllLamps()
{
    for (int i = 0; i < LAMP_COUNT; i++)
    {
        turnOnLamp(i);
    }
    Serial.println("Semua lampu dinyalakan");
}

// Toggle status lampu
void toggleLamp(int lampId)
{
    if (lampId >= 0 && lampId < LAMP_COUNT)
    {
        if (lamps[lampId].isOn)
        {
            turnOffLamp(lampId);
        }
        else
        {
            turnOnLamp(lampId);
        }
    }
}

// Fungsi untuk mengatur mode running
void setRunningMode(bool enable)
{
    // Matikan semua lampu terlebih dahulu
    turnOffAllLamps();

    isRunningMode = enable;
    currentRunningLamp = 0;

    // Jika diaktifkan, nyalakan lampu pertama
    if (enable)
    {
        turnOnLamp(currentRunningLamp);
        lastRunningUpdate = millis();
    }

    Serial.println(enable ? "Mode running diaktifkan" : "Mode running dinonaktifkan");
}

// Fungsi untuk update mode running di loop utama
void updateRunningMode()
{
    if (isRunningMode && (millis() - lastRunningUpdate > RUNNING_INTERVAL))
    {
        // Matikan lampu saat ini
        turnOffLamp(currentRunningLamp);

        // Pindah ke lampu berikutnya
        currentRunningLamp = (currentRunningLamp + 1) % LAMP_COUNT;

        // Nyalakan lampu berikutnya
        turnOnLamp(currentRunningLamp);

        lastRunningUpdate = millis();
    }
}

// Fungsi untuk memproses tepok tangan (clap)
void processClap()
{
    unsigned long currentTime = millis();

    // Baca nilai dari sensor
    int sensorValue = analogRead(CLAP_SENSOR_PIN);

    // Debug sensor value if needed
    // Serial.println("Sensor value: " + String(sensorValue));

    // Jika terdeteksi tepok (nilai lebih besar dari threshold)
    if (sensorValue > CLAP_THRESHOLD)
    {
        // Setelah jeda cukup lama dari tepok terakhir untuk menghindari bouncing
        if (currentTime - lastClapTime > CLAP_TIMEOUT)
        {
            // Jika ini adalah tepok pertama
            if (!isFirstClap)
            {
                isFirstClap = true;
                lastClapTime = currentTime;
                // Beri umpan balik suara pendek
                playBuzzerTone(2000, 50);
            }
            // Jika ini adalah tepok kedua dalam waktu yang cukup dekat
            else if (currentTime - lastClapTime < CLAP_TIMEOUT * 2)
            {
                isFirstClap = false;

                // Matikan mode running jika aktif
                if (isRunningMode)
                {
                    setRunningMode(false);
                }

                // Toggle lampu saat ini
                toggleLamp(currentLightToggle);

                // Setelah toggle, pindah ke lampu berikutnya untuk tepok selanjutnya
                currentLightToggle = (currentLightToggle + 1) % LAMP_COUNT;

                // Beri umpan balik berupa suara
                playBuzzerTone(3000, 100);
            }
            lastClapTime = currentTime;
        }
    }

    // Reset tepok pertama jika timeout
    if (isFirstClap && (currentTime - lastClapTime > CLAP_TIMEOUT * 2))
    {
        isFirstClap = false;
    }
}

// Callback setelah WiFi terhubung
void configModeCallback(WiFiManager *myWiFiManager)
{
    Serial.println("Mode Konfigurasi Aktif");
    Serial.println(WiFi.softAPIP());
    Serial.println(myWiFiManager->getConfigPortalSSID());

    // Buzzer memberikan notifikasi masuk mode konfigurasi (tiga beep pendek)
    for (int i = 0; i < 3; i++)
    {
        playBuzzerTone(2000, 100);
        delay(200);
    }
    noTone(BUZZER_PIN);
}

// Endpoint untuk halaman utama
void handleRoot()
{
    String html = "<html><head>";
    html += "<title>ESP32 Smart Lighting Control</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; }";
    html += "h1 { color: #333; }";
    html += ".info { background-color: #f0f0f0; padding: 15px; border-radius: 5px; margin-bottom: 20px; }";
    html += ".btn { display: inline-block; background-color: #4CAF50; color: white; padding: 10px 20px; ";
    html += "margin: 10px 5px; border: none; border-radius: 4px; cursor: pointer; text-decoration: none; }";
    html += ".btn-reset { background-color: #f44336; }";
    html += "</style>";
    html += "</head><body>";
    html += "<h1>ESP32 Smart Lighting Controller</h1>";
    html += "<div class='info'>";
    html += "<p><strong>Status:</strong> Connected to WiFi</p>";
    html += "<p><strong>Network SSID:</strong> " + WiFi.SSID() + "</p>";
    html += "<p><strong>IP Address:</strong> " + ipAddress + "</p>";
    html += "<p><strong>Signal Strength:</strong> " + String(WiFi.RSSI()) + " dBm</p>";
    html += "</div>";
    html += "<p>Use this IP address to control your lamps from your application.</p>";
    html += "<p>To reset WiFi settings and configure a new network:</p>";
    html += "<a href='/resetwifi' class='btn btn-reset'>Reset WiFi Settings</a>";
    html += "<a href='/api' class='btn'>API Endpoints</a>";
    html += "<a href='/test-buzzer' class='btn'>Test Buzzer</a>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

// Handler untuk test buzzer
void handleTestBuzzer()
{
    playSuccessSound();
    delay(500);
    playFailSound();

    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='2;url=/'></head><body>"
                                  "<h3>Testing buzzer completed</h3>"
                                  "<p>Redirecting back to home page...</p>"
                                  "</body></html>");
}

// Endpoint untuk melihat daftar API
void handleAPI()
{
    String html = "<html><head>";
    html += "<title>ESP32 Smart Lighting API</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; }";
    html += "h1, h2 { color: #333; }";
    html += "code { background-color: #f0f0f0; padding: 2px 5px; border-radius: 3px; }";
    html += ".endpoint { background-color: #f9f9f9; padding: 15px; margin-bottom: 15px; border-left: 4px solid #4CAF50; }";
    html += "pre { background-color: #f0f0f0; padding: 10px; border-radius: 5px; overflow-x: auto; }";
    html += ".method { display: inline-block; padding: 3px 8px; border-radius: 3px; color: white; margin-right: 10px; }";
    html += ".get { background-color: #61affe; }";
    html += ".post { background-color: #49cc90; }";
    html += "</style>";
    html += "</head><body>";
    html += "<h1>ESP32 Smart Lighting API Endpoints</h1>";
    html += "<a href='/'>&larr; Back to Home</a>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method get'>GET</span> /lamp/status</h2>";
    html += "<p>Get status of all lamps</p>";
    html += "<pre>{\"lamps\":[{\"id\":0,\"name\":\"Lamp 1\",\"status\":\"ON/OFF\"}, ...]}</pre>";
    html += "</div>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method post'>POST</span> /lamp/on</h2>";
    html += "<p>Turn on a specific lamp</p>";
    html += "<p>Request body: <code>{\"id\": 0}</code></p>";
    html += "</div>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method post'>POST</span> /lamp/off</h2>";
    html += "<p>Turn off a specific lamp</p>";
    html += "<p>Request body: <code>{\"id\": 0}</code></p>";
    html += "</div>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method get'>GET</span> /lamp/all/on</h2>";
    html += "<p>Turn on all lamps</p>";
    html += "</div>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method get'>GET</span> /lamp/all/off</h2>";
    html += "<p>Turn off all lamps</p>";
    html += "</div>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method post'>POST</span> /lamp/toggle</h2>";
    html += "<p>Toggle a specific lamp</p>";
    html += "<p>Request body: <code>{\"id\": 0}</code></p>";
    html += "</div>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method post'>POST</span> /lamp/running</h2>";
    html += "<p>Mengaktifkan/mematikan mode running (lampu menyala bergantian)</p>";
    html += "<p>Request body: <code>{\"enable\": true, \"interval\": 500}</code></p>";
    html += "</div>";

    html += "</body></html>";
    server.send(200, "text/html", html);
}

// Reset pengaturan WiFi dan masuk ke mode konfigurasi
void handleResetWiFi()
{
    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='5;url=/'></head><body>"
                                  "<h3>WiFi settings reset. ESP32 will restart.</h3>"
                                  "<p>The device will create an access point named 'ESP32-Smart-Lighting'.</p>"
                                  "<p>Connect to it and navigate to 192.168.4.1 to configure WiFi.</p>"
                                  "</body></html>");

    // Mainkan suara peringatan untuk reset WiFi
    playBuzzerTone(2000, 1000);
    delay(1000);

    // Reset pengaturan WiFi
    wifiManager.resetSettings();
    delay(1000);
    ESP.restart();
}

void setup()
{
    Serial.begin(115200);
    Serial.println("\nMemulai ESP32 Smart Lighting Controller");

    // Inisialisasi pin buzzer
    pinMode(BUZZER_PIN, OUTPUT);

    // Inisialisasi pin sensor tepok
    pinMode(CLAP_SENSOR_PIN, INPUT);

    // Mainkan tone startup
    playBuzzerTone(1500, 200);
    delay(200);

    // Inisialisasi pin relay untuk lampu
    for (int i = 0; i < LAMP_COUNT; i++)
    {
        pinMode(lamps[i].relayPin, OUTPUT);

        // Pastikan relay mati (HIGH) saat awal - relay aktif LOW
        digitalWrite(lamps[i].relayPin, HIGH);
        lamps[i].isOn = false;
    }

    // Konfigurasi WiFiManager
    wifiManager.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    wifiManager.setAPCallback(configModeCallback);

    // Set nama AP dan password jika tidak dapat terhubung ke jaringan tersimpan
    bool connected = wifiManager.autoConnect("ESP32-Smart-Lighting", "password123");

    if (connected)
    {
        // Mainkan nada sukses terhubung ke WiFi
        playSuccessSound();

        // Simpan IP Address
        ipAddress = WiFi.localIP().toString();

        Serial.println("\nWiFi terhubung");
        Serial.print("Alamat IP: ");
        Serial.println(ipAddress);

        // Inisialisasi mDNS responder
        if (MDNS.begin(MDNS_NAME))
        {
            Serial.print("mDNS responder dimulai dengan nama: ");
            Serial.println(MDNS_NAME);
            Serial.print("Anda dapat mengakses perangkat melalui http://");
            Serial.print(MDNS_NAME);
            Serial.println(".local");

            // Daftarkan layanan HTTP
            MDNS.addService("http", "tcp", SERVER_PORT);

            // Mainkan nada khusus untuk konfirmasi mDNS berhasil
            playBuzzerTone(1500, 100);
            delay(100);
            playBuzzerTone(2000, 100);
            delay(100);
            playBuzzerTone(2500, 200);
        }
        else
        {
            Serial.println("Gagal memulai mDNS responder!");

            // Mainkan nada error untuk mDNS
            playBuzzerTone(2000, 100);
            delay(100);
            playBuzzerTone(1500, 200);
        }
    }
    else
    {
        // Jika tidak terhubung, tetap jalankan program untuk fungsi offline
        Serial.println("Tidak terhubung ke WiFi. Menjalankan mode offline.");
        // Mainkan nada pemberitahuan mode offline
        playBuzzerTone(1000, 300);
        delay(300);
        playBuzzerTone(1000, 300);
    }

    // Tambahkan handler untuk halaman web (hanya berfungsi jika WiFi terhubung)
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api", HTTP_GET, handleAPI);
    server.on("/test-buzzer", HTTP_GET, handleTestBuzzer);
    server.on("/resetwifi", HTTP_GET, handleResetWiFi);

    // Tambahkan handler untuk preflight request
    server.on("/lamp/status", HTTP_OPTIONS, handleCORS);
    server.on("/lamp/on", HTTP_OPTIONS, handleCORS);
    server.on("/lamp/off", HTTP_OPTIONS, handleCORS);
    server.on("/lamp/all/off", HTTP_OPTIONS, handleCORS);
    server.on("/lamp/all/on", HTTP_OPTIONS, handleCORS);
    server.on("/lamp/toggle", HTTP_OPTIONS, handleCORS);
    server.on("/lamp/running", HTTP_OPTIONS, handleCORS);

    // Endpoint untuk mendapatkan status lampu
    server.on("/lamp/status", HTTP_GET, []()
              {
        sendCORSHeaders();

        StaticJsonDocument<500> jsonDoc;
        JsonArray lampStatus = jsonDoc.createNestedArray("lamps");

        for (int i = 0; i < LAMP_COUNT; i++) {
            JsonObject lamp = lampStatus.createNestedObject();
            lamp["id"] = i;
            lamp["name"] = lamps[i].name;
            lamp["status"] = lamps[i].isOn ? "ON" : "OFF";
        }

        // Tambahkan status mode running
        jsonDoc["runningMode"] = isRunningMode;
        if (isRunningMode) {
            jsonDoc["runningInterval"] = RUNNING_INTERVAL;
        }

        String response;
        serializeJson(jsonDoc, response);
        server.send(200, "application/json", response); });

    // Endpoint untuk menyalakan lampu
    server.on("/lamp/on", HTTP_POST, []()
              {
        sendCORSHeaders();

        // Pastikan request adalah POST
        if (server.method() != HTTP_POST) {
            server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
            return;
        }

        // Parse JSON body
        StaticJsonDocument<100> doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));

        if (error) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        // Ambil ID lampu dari JSON
        int lampId = doc["id"] | -1;

        // Validasi ID lampu
        if (lampId >= 0 && lampId < LAMP_COUNT) {
            // Nonaktifkan mode running jika aktif
            if (isRunningMode) {
                setRunningMode(false);
            }
            
            // Nyalakan lampu
            turnOnLamp(lampId);

            // Kirim respons sukses
            String response = "{\"id\":" + String(lampId) + 
                             ", \"name\":\"" + lamps[lampId].name + 
                             "\", \"status\":\"ON\"}";
            server.send(200, "application/json", response);
        } else {
            server.send(400, "application/json", "{\"error\":\"ID lampu tidak valid\"}");
        } });

    // Endpoint untuk mematikan satu lampu
    server.on("/lamp/off", HTTP_POST, []()
              {
        sendCORSHeaders();

        // Pastikan request adalah POST
        if (server.method() != HTTP_POST) {
            server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
            return;
        }

        // Parse JSON body
        StaticJsonDocument<100> doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));

        if (error) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        // Ambil ID lampu dari JSON
        int lampId = doc["id"] | -1;

        // Validasi ID lampu
        if (lampId >= 0 && lampId < LAMP_COUNT) {
            // Nonaktifkan mode running jika aktif
            if (isRunningMode) {
                setRunningMode(false);
            }
            
            // Matikan lampu
            turnOffLamp(lampId);

            // Kirim respons sukses
            String response = "{\"id\":" + String(lampId) + 
                             ", \"name\":\"" + lamps[lampId].name + 
                             "\", \"status\":\"OFF\"}";
            server.send(200, "application/json", response);
        } else {
            server.send(400, "application/json", "{\"error\":\"ID lampu tidak valid\"}");
        } });

    // Endpoint untuk toggle lampu
    server.on("/lamp/toggle", HTTP_POST, []()
              {
        sendCORSHeaders();

        // Pastikan request adalah POST
        if (server.method() != HTTP_POST) {
            server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
            return;
        }

        // Parse JSON body
        StaticJsonDocument<100> doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));

        if (error) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        // Ambil ID lampu dari JSON
        int lampId = doc["id"] | -1;

        // Validasi ID lampu
        if (lampId >= 0 && lampId < LAMP_COUNT) {
            // Nonaktifkan mode running jika aktif
            if (isRunningMode) {
                setRunningMode(false);
            }
            
            // Toggle lampu
            toggleLamp(lampId);

            // Kirim respons sukses
            String response = "{\"id\":" + String(lampId) + 
                             ", \"name\":\"" + lamps[lampId].name + 
                             "\", \"status\":\"" + (lamps[lampId].isOn ? "ON" : "OFF") + "\"}";
            server.send(200, "application/json", response);
        } else {
            server.send(400, "application/json", "{\"error\":\"ID lampu tidak valid\"}");
        } });

    // Endpoint untuk mematikan semua lampu
    server.on("/lamp/all/off", HTTP_GET, []()
              {
        sendCORSHeaders();

        // Nonaktifkan mode running jika aktif
        if (isRunningMode) {
            setRunningMode(false);
        } else {
            // Matikan semua lampu
            turnOffAllLamps();
        }
        
        // Kirim respons
        server.send(200, "application/json", 
            "{\"status\":\"ALL_OFF\", \"message\":\"Semua lampu dimatikan\"}"); });

    // Endpoint untuk menghidupkan semua lampu
    server.on("/lamp/all/on", HTTP_GET, []()
              {
        sendCORSHeaders();

        // Nonaktifkan mode running jika aktif
        if (isRunningMode) {
            setRunningMode(false);
        }
        
        // Hidupkan semua lampu
        turnOnAllLamps();
        
        // Kirim respons
        server.send(200, "application/json", 
            "{\"status\":\"ALL_ON\", \"message\":\"Semua lampu dihidupkan\"}"); });

    // Endpoint untuk mode running
    server.on("/lamp/running", HTTP_POST, []()
              {
        sendCORSHeaders();

        // Pastikan request adalah POST
        if (server.method() != HTTP_POST) {
            server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
            return;
        }

        // Parse JSON body
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));

        if (error) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        // Ambil parameter dari JSON
        bool enable = doc["enable"] | false;
        int interval = doc["interval"] | 500;

        // Validasi interval (minimal 100ms, maksimal 5000ms)
        if (interval < 100 || interval > 5000) {
            interval = 500; // Gunakan default jika tidak valid
        }

        // Set interval running
        RUNNING_INTERVAL = interval;

        // Atur mode running
        setRunningMode(enable);

        // Kirim respons sukses
        String response = "{\"status\":\"" + String(enable ? "RUNNING_ON" : "RUNNING_OFF") + 
                        "\", \"interval\":" + String(interval) + "}";

        server.send(200, "application/json", response); });

    // Mulai server
    server.begin();
    Serial.println("Server HTTP dimulai");

    // Buzzer untuk menandakan sistem siap
    playBuzzerTone(2000, 200);
    delay(200);
    playBuzzerTone(3000, 200);
}

void loop()
{
    // Selalu memproses sensor tepok (clap) terlepas dari status WiFi
    processClap();

    // Jika terhubung WiFi, tangani permintaan HTTP
    if (WiFi.status() == WL_CONNECTED)
    {
        // Tangani permintaan klien
        server.handleClient();
    }

    // Update mode running jika diaktifkan
    updateRunningMode();

    // Monitor koneksi WiFi dan berikan notifikasi jika terputus
    static bool previouslyConnected = (WiFi.status() == WL_CONNECTED);

    if (WiFi.status() != WL_CONNECTED && previouslyConnected)
    {
        // WiFi terputus, mainkan nada error
        Serial.println("WiFi terputus, mencoba menghubungkan kembali");
        playFailSound();
        previouslyConnected = false;
    }
    else if (WiFi.status() == WL_CONNECTED && !previouslyConnected)
    {
        // WiFi terhubung kembali, mainkan nada sukses
        Serial.println("WiFi terhubung kembali");
        ipAddress = WiFi.localIP().toString();
        playSuccessSound();
        previouslyConnected = true;
    }

    // Delay kecil untuk stabilitas
    delay(10);
}