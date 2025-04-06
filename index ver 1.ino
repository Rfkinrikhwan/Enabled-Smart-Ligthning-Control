#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// WiFi Configuration
const char *ssid = "Redmi Note 11";
const char *password = "rifki123";

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

void setup()
{
    Serial.begin(115200);

    // Inisialisasi pin lampu
    for (int i = 0; i < LAMP_COUNT; i++)
    {
        pinMode(lampPins[i].redPin, OUTPUT);
        pinMode(lampPins[i].greenPin, OUTPUT);
        pinMode(lampPins[i].bluePin, OUTPUT);

        // Pastikan lampu mati saat awal
        turnOffLamp(i);
    }

    // Koneksi WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi terhubung");
    Serial.print("Alamat IP: ");
    Serial.println(WiFi.localIP());

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

        // [Implementasi sebelumnya untuk mematikan lampu]
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
}

void loop()
{
    // Tangani permintaan klien
    server.handleClient();
}