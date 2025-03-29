#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <DNSServer.h>

// Port server (umumnya 80 untuk HTTP atau 8080 untuk alternatif)
const int SERVER_PORT = 80;

// Konfigurasi lampu RGB
const int LAMP_COUNT = 2;
struct RGBLamp
{
    int redPin;
    int greenPin;
    int bluePin;
    String name;
    int currentRed;
    int currentGreen;
    int currentBlue;
};

// Warna default saat menyalakan lampu
const int DEFAULT_RED = 255;
const int DEFAULT_GREEN = 100;
const int DEFAULT_BLUE = 50;

RGBLamp lampPins[LAMP_COUNT] = {
    {14, 12, 13, "Lamp 1", 0, 0, 0}, // Lampu 1: Red=14, Green=12, Blue=13
    {25, 26, 27, "Lamp 2", 0, 0, 0}  // Lampu 2: Red=25, Green=26, Blue=27
};

// Membuat instance server web pada port yang ditentukan
WebServer server(SERVER_PORT);

// WiFiManager
WiFiManager wifiManager;

// Variabel untuk menyimpan IP Address
String ipAddress = "";

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
        analogWrite(lampPins[lampId].redPin, 0);
        analogWrite(lampPins[lampId].greenPin, 0);
        analogWrite(lampPins[lampId].bluePin, 0);

        // Reset warna saat ini
        lampPins[lampId].currentRed = 0;
        lampPins[lampId].currentGreen = 0;
        lampPins[lampId].currentBlue = 0;

        Serial.println(lampPins[lampId].name + " dimatikan");
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

// Fungsi untuk menyalakan lampu dengan warna default
void turnOnLampDefault(int lampId)
{
    if (lampId >= 0 && lampId < LAMP_COUNT)
    {
        // Set warna default
        analogWrite(lampPins[lampId].redPin, DEFAULT_RED);
        analogWrite(lampPins[lampId].greenPin, DEFAULT_GREEN);
        analogWrite(lampPins[lampId].bluePin, DEFAULT_BLUE);

        // Simpan warna saat ini
        lampPins[lampId].currentRed = DEFAULT_RED;
        lampPins[lampId].currentGreen = DEFAULT_GREEN;
        lampPins[lampId].currentBlue = DEFAULT_BLUE;

        Serial.println(lampPins[lampId].name + " dinyalakan dengan warna default");
    }
}

// Fungsi Untuk Menghidupkan Semua Lampu
void turnOnAllLamps()
{
    for (int i = 0; i < LAMP_COUNT; i++)
    {
        turnOnLampDefault(i);
    }
    Serial.println("Semua lampu dinyalakan");
}

// Callback setelah WiFi terhubung
void configModeCallback(WiFiManager *myWiFiManager)
{
    Serial.println("Mode Konfigurasi Aktif");
    Serial.println(WiFi.softAPIP());
    Serial.println(myWiFiManager->getConfigPortalSSID());
}

// Endpoint untuk halaman utama
void handleRoot()
{
    String html = "<html><head>";
    html += "<title>ESP32 RGB Lamp Control</title>";
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
    html += "<h1>ESP32 RGB Lamp Controller</h1>";
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
    html += "</body></html>";
    server.send(200, "text/html", html);
}

// Endpoint untuk melihat daftar API
void handleAPI()
{
    String html = "<html><head>";
    html += "<title>ESP32 RGB Lamp API</title>";
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
    html += "<h1>ESP32 RGB Lamp API Endpoints</h1>";
    html += "<a href='/'>&larr; Back to Home</a>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method get'>GET</span> /lamp/status</h2>";
    html += "<p>Get status of all lamps</p>";
    html += "<pre>{\"lamps\":[{\"id\":0,\"name\":\"Lamp 1\",\"status\":\"ON/OFF\",\"currentColor\":{\"r\":255,\"g\":100,\"b\":50}}, ...]}</pre>";
    html += "</div>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method post'>POST</span> /lamp/on</h2>";
    html += "<p>Turn on a specific lamp with default color</p>";
    html += "<p>Request body: <code>{\"id\": 0}</code></p>";
    html += "</div>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method post'>POST</span> /lamp/off</h2>";
    html += "<p>Turn off a specific lamp</p>";
    html += "<p>Request body: <code>{\"id\": 0}</code></p>";
    html += "</div>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method get'>GET</span> /lamp/all/on</h2>";
    html += "<p>Turn on all lamps with default color</p>";
    html += "</div>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method get'>GET</span> /lamp/all/off</h2>";
    html += "<p>Turn off all lamps</p>";
    html += "</div>";

    html += "<div class='endpoint'>";
    html += "<h2><span class='method post'>POST</span> /lamp/color</h2>";
    html += "<p>Set color for a specific lamp</p>";
    html += "<p>Request body: <code>{\"id\": 0, \"color\": {\"r\": 255, \"g\": 100, \"b\": 50}}</code></p>";
    html += "</div>";

    html += "</body></html>";
    server.send(200, "text/html", html);
}

// Reset pengaturan WiFi dan masuk ke mode konfigurasi
void handleResetWiFi()
{
    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='5;url=/'></head><body>"
                                  "<h3>WiFi settings reset. ESP32 will restart.</h3>"
                                  "<p>The device will create an access point named 'ESP32-RGB-Setup'.</p>"
                                  "<p>Connect to it and navigate to 192.168.4.1 to configure WiFi.</p>"
                                  "</body></html>");
    delay(3000);
    // Reset pengaturan WiFi
    wifiManager.resetSettings();
    delay(1000);
    ESP.restart();
}

void setup()
{
    Serial.begin(115200);
    Serial.println("\nMemulai ESP32 RGB Lamp Controller");

    // Inisialisasi pin lampu
    for (int i = 0; i < LAMP_COUNT; i++)
    {
        pinMode(lampPins[i].redPin, OUTPUT);
        pinMode(lampPins[i].greenPin, OUTPUT);
        pinMode(lampPins[i].bluePin, OUTPUT);

        // Pastikan lampu mati saat awal
        turnOffLamp(i);
    }

    // Konfigurasi WiFiManager
    wifiManager.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    wifiManager.setAPCallback(configModeCallback);

    // Set nama AP dan password jika tidak dapat terhubung ke jaringan tersimpan
    bool connected = wifiManager.autoConnect("ESP32-RGB-Setup", "password123");

    if (!connected)
    {
        Serial.println("Gagal terhubung ke WiFi, restart ESP32...");
        ESP.restart();
    }

    // Simpan IP Address
    ipAddress = WiFi.localIP().toString();

    Serial.println("\nWiFi terhubung");
    Serial.print("Alamat IP: ");
    Serial.println(ipAddress);

    // Tambahkan handler untuk halaman web
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api", HTTP_GET, handleAPI);
    server.on("/resetwifi", HTTP_GET, handleResetWiFi);

    // Tambahkan handler untuk preflight request
    server.on("/lamp/status", HTTP_OPTIONS, handleCORS);
    server.on("/lamp/on", HTTP_OPTIONS, handleCORS);
    server.on("/lamp/off", HTTP_OPTIONS, handleCORS);
    server.on("/lamp/all/off", HTTP_OPTIONS, handleCORS);
    server.on("/lamp/all/on", HTTP_OPTIONS, handleCORS);
    server.on("/lamp/color", HTTP_OPTIONS, handleCORS);

    // Endpoint untuk mendapatkan status lampu
    server.on("/lamp/status", HTTP_GET, []()
              {
        sendCORSHeaders();

        StaticJsonDocument<500> jsonDoc;
        JsonArray lampStatus = jsonDoc.createNestedArray("lamps");

        for (int i = 0; i < LAMP_COUNT; i++) {
            JsonObject lamp = lampStatus.createNestedObject();
            lamp["id"] = i;
            lamp["name"] = lampPins[i].name;
            
            // Cek apakah lampu menyala
            bool isOn = (lampPins[i].currentRed > 0 || 
                         lampPins[i].currentGreen > 0 || 
                         lampPins[i].currentBlue > 0);
            
            lamp["status"] = isOn ? "ON" : "OFF";
            
            // Tambahkan informasi warna saat ini
            JsonObject currentColor = lamp.createNestedObject("currentColor");
            currentColor["r"] = lampPins[i].currentRed;
            currentColor["g"] = lampPins[i].currentGreen;
            currentColor["b"] = lampPins[i].currentBlue;
        }

        String response;
        serializeJson(jsonDoc, response);
        server.send(200, "application/json", response); });

    // Endpoint untuk menyalakan lampu dengan warna default
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
            // Nyalakan lampu dengan warna default
            turnOnLampDefault(lampId);

            // Kirim respons sukses
            String response = "{\"id\":" + String(lampId) + 
                              ", \"name\":\"" + lampPins[lampId].name + 
                              "\", \"status\":\"ON\"" +
                              ", \"defaultColor\":{\"r\":" + String(DEFAULT_RED) + 
                              ", \"g\":" + String(DEFAULT_GREEN) + 
                              ", \"b\":" + String(DEFAULT_BLUE) + "}}";
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
            // Matikan lampu
            turnOffLamp(lampId);

            // Kirim respons sukses
            String response = "{\"id\":" + String(lampId) + 
                              ", \"name\":\"" + lampPins[lampId].name + 
                              "\", \"status\":\"OFF\"}";
            server.send(200, "application/json", response);
        } else {
            server.send(400, "application/json", "{\"error\":\"ID lampu tidak valid\"}");
        } });

    // Endpoint untuk mematikan semua lampu
    server.on("/lamp/all/off", HTTP_GET, []()
              {
        sendCORSHeaders();

        // Matikan semua lampu
        turnOffAllLamps();
        
        // Kirim respons
        server.send(200, "application/json", 
            "{\"status\":\"ALL_OFF\", \"message\":\"Semua lampu dimatikan\"}"); });

    // Endpoint untuk menghidupkan semua lampu
    server.on("/lamp/all/on", HTTP_GET, []()
              {
        sendCORSHeaders();

        // Hidupkan semua lampu
        turnOnAllLamps();
        
        // Kirim respons
        server.send(200, "application/json", 
            "{\"status\":\"ALL_ON\", \"message\":\"Semua lampu dihidupkan\"}"); });

    // Endpoint untuk mengatur warna lampu
    server.on("/lamp/color", HTTP_POST, []()
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
        int lampId = doc["id"] | -1;
        int red = doc["color"]["r"] | -1;
        int green = doc["color"]["g"] | -1;
        int blue = doc["color"]["b"] | -1;

        // Validasi parameter
        if (lampId >= 0 && lampId < LAMP_COUNT &&
            red >= 0 && red <= 255 &&
            green >= 0 && green <= 255 &&
            blue >= 0 && blue <= 255) 
        {
            // Set warna lampu
            analogWrite(lampPins[lampId].redPin, red);
            analogWrite(lampPins[lampId].greenPin, green);
            analogWrite(lampPins[lampId].bluePin, blue);

            // Simpan warna saat ini
            lampPins[lampId].currentRed = red;
            lampPins[lampId].currentGreen = green;
            lampPins[lampId].currentBlue = blue;

            // Kirim respons sukses
            String response = "{\"id\":" + String(lampId) + 
                              ", \"name\":\"" + lampPins[lampId].name + 
                              "\", \"color\":{\"r\":" + String(red) + 
                              ", \"g\":" + String(green) + 
                              ", \"b\":" + String(blue) + "}}";
            server.send(200, "application/json", response);
            
            Serial.println(lampPins[lampId].name + " diubah menjadi warna R:" + 
                           String(red) + " G:" + String(green) + " B:" + String(blue));
        } 
        else 
        {
            server.send(400, "application/json", "{\"error\":\"Parameter tidak valid\"}");
        } });

    // Mulai server
    server.begin();
    Serial.println("Server HTTP dimulai");

    // Kedipkan lampu untuk menunjukkan bahwa sistem siap
    for (int i = 0; i < LAMP_COUNT; i++)
    {
        analogWrite(lampPins[i].redPin, 255);
        delay(200);
        analogWrite(lampPins[i].redPin, 0);
        analogWrite(lampPins[i].greenPin, 255);
        delay(200);
        analogWrite(lampPins[i].greenPin, 0);
        analogWrite(lampPins[i].bluePin, 255);
        delay(200);
        analogWrite(lampPins[i].bluePin, 0);
    }
}

void loop()
{
    // Tangani permintaan klien
    server.handleClient();
}