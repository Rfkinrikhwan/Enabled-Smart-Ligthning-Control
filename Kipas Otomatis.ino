#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Konstanta
#define DHTPIN 2      // Pin data sensor DHT
#define DHTTYPE DHT11 // Tipe sensor DHT
#define POWER_PIN 3   // Pin untuk suplai daya ke sensor
#define KIPAS_PIN 4   // Pin output untuk kipas

// Karakter khusus
byte thermometerIcon[8] = {
    B00100,
    B01010,
    B01010,
    B01010,
    B01010,
    B10001,
    B10001,
    B01110};

byte dropletIcon[8] = {
    B00100,
    B00100,
    B01010,
    B01010,
    B10001,
    B10001,
    B10001,
    B01110};

byte fanIcon[8] = {
    B00000,
    B11001,
    B01011,
    B00100,
    B11010,
    B10011,
    B00000,
    B00000};

// Inisialisasi objek
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2); // Alamat I2C untuk LCD

// Variabel untuk animasi
int animationState = 0;
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 500; // Interval animasi (ms)

void setup()
{
    lcd.init();
    lcd.backlight();

    // Buat karakter khusus
    lcd.createChar(0, thermometerIcon);
    lcd.createChar(1, dropletIcon);
    lcd.createChar(2, fanIcon);

    pinMode(POWER_PIN, OUTPUT);
    digitalWrite(POWER_PIN, HIGH); // Aktifkan sensor dari awal

    pinMode(KIPAS_PIN, OUTPUT);
    digitalWrite(KIPAS_PIN, HIGH); // Pastikan kipas mati saat startup (asumsi active-LOW)

    Serial.begin(9600);
    Serial.println("Sistem Kipas Otomatis dengan DHT11");

    // Tampilan awal
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" SISTEM MONITOR ");
    lcd.setCursor(0, 1);
    lcd.print("  SUHU & KELEMBAPAN  ");

    // Animasi teks berjalan untuk judul
    for (int i = 0; i < 4; i++)
    {
        lcd.scrollDisplayLeft();
        delay(400);
    }

    // Delay untuk memastikan sensor siap
    delay(2000);

    dht.begin();
}

void loop()
{
    // Baca suhu dan kelembapan dengan delay sesuai spesifikasi DHT11
    float suhu = dht.readTemperature();
    float kelembapan = dht.readHumidity();

    unsigned long currentTime = millis();

    // Cek jika pembacaan valid
    if (isnan(suhu) || isnan(kelembapan))
    {
        Serial.println("Gagal membaca sensor DHT11!");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Error Sensor!");
        lcd.setCursor(0, 1);
        lcd.print("Cek Koneksi");
        delay(2000);
        return;
    }

    // Tampilkan data ke Serial Monitor
    Serial.print("Suhu: ");
    Serial.print(suhu, 1);
    Serial.print(" °C, Kelembapan: ");
    Serial.print(kelembapan, 1);
    Serial.println(" %");

    // Tampilkan ke LCD dengan animasi bergantian
    if (currentTime - lastUpdateTime >= updateInterval)
    {
        lastUpdateTime = currentTime;

        if (animationState == 0)
        {
            // Tampilan suhu
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.write(byte(0)); // Ikon termometer
            lcd.print(" SUHU: ");
            lcd.print(suhu, 1);
            lcd.print((char)223); // Simbol derajat
            lcd.print("C");

            // Status kipas di baris kedua
            lcd.setCursor(0, 1);
            if (suhu > 35.0)
            {
                lcd.write(byte(2)); // Ikon kipas
                lcd.print(" KIPAS: ON ");
                // Animasi kipas berputar
                lcd.print(">>>>>");
            }
            else
            {
                lcd.write(byte(2)); // Ikon kipas
                lcd.print(" KIPAS: OFF");
            }
        }
        else
        {
            // Tampilan kelembapan
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.write(byte(1)); // Ikon tetesan air
            lcd.print(" KELEMBAPAN:");
            lcd.setCursor(0, 1);
            lcd.print("    ");
            lcd.print(kelembapan, 1);
            lcd.print(" %");

            // Evaluasi kelembapan
            if (kelembapan < 40)
            {
                lcd.print(" KERING");
            }
            else if (kelembapan > 70)
            {
                lcd.print(" LEMBAP");
            }
            else
            {
                lcd.print(" NORMAL");
            }
        }

        // Ganti status animasi untuk tampilan bergantian
        animationState = 1 - animationState;
    }

    // Kontrol kipas berdasarkan suhu
    if (suhu > 35.0)
    {
        digitalWrite(KIPAS_PIN, LOW); // Kipas menyala (active-LOW)
        Serial.println("Kipas: ON");
    }
    else
    {
        digitalWrite(KIPAS_PIN, HIGH); // Kipas mati (inactive-HIGH)
        Serial.println("Kipas: OFF");
    }

    // Delay pembacaan berikutnya (lebih singkat untuk animasi yang mulus)
    delay(1000);
}