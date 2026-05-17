#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Print specs
#include "esp_system.h"
#include "esp_spi_flash.h"

// Alamat I2C MPU6050 (0x68 jika AD0 ke GND)
#define MPU6050_ADDR 0x68

// Register MPU6050
#define PWR_MGMT_1 0x6B
#define ACCEL_XOUT_H 0x3B
#define GYRO_XOUT_H 0x43

// Push button
#define CALIB_BUTTON_PIN 14

// SDA dan SCL sensor MPU6050
#define SDA_PIN 21
#define SCL_PIN 22

// Variabel untuk menyimpan offset gyro (nilai error rata-rata gyro saat diam)
// Nilai ini dihitung dalam fungsi kalibrasiGyro() dan digunakan untuk menyesuaikan pembacaan gyro agar akurat
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;

// Variabel untuk menyimpan data mentah accelerometer dan gyroscope dari register MPU6050
// Diperbarui setiap pemanggilan fungsi bacaSensorRaw()
int16_t accX, accY, accZ;
int16_t gyroX, gyroY, gyroZ;

// Variabel untuk menyimpan nilai kalibrasi (offset) roll dan pitch
// Digunakan dengan mengurangi pembacaan raw dengan offset untuk mendapat sudut kemiringan relatif (titik 0 baru)
float rollOffset = 0;
float pitchOffset = 0;

// Variabel untuk menyimpan sudut roll (sumbu X) dan pitch (sumbu Y) asli yang didapat melalui fungsi arctan
float rawRoll = 0;
float rawPitch = 0;

// Variabel untuk menyimpan catatan waktu mikrodetik terakhir (opsional untuk perhitungan integral yaw timer)
unsigned long lastTime = 0;

// --- MQTT & WiFi configuration (edit these) ---
// Variabel penyimpan SSID dan kata sandi WiFi agar ESP32 bisa connect ke router
const char *WIFI_SSID = "Jenong Smart";
const char *WIFI_PASS = "jenong21";

// Server broker MQTT dan Port (alamat perangkat sentral untuk ESP32 mengirim data)
const char *MQTT_SERVER = "192.168.1.10"; // ganti ke alamat broker Anda
const uint16_t MQTT_PORT = 1883;

// Username dan Password untuk autentikasi keamanan MQTT (jika ada)
const char *MQTT_USER = ""; // isi jika broker perlu auth
const char *MQTT_PASS = ""; // isi jika broker perlu auth

// Topik pub/sub MQTT tempat ESP32 akan melemparkan parameter sudut (roll, pitch)
const char *MQTT_TOPIC = "sensors/mpu6050";

// Interface untuk WiFi klien dan modul PubSubClient untuk mengurus koneksi MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Variabel untuk waktu (dalam ms) terakhir kali sensor mempublikasikan data MQTT
// Digunakan dalam loop() untuk mekanisme non-blocking delay setiap interval tertentu
unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 200; // ms (interval publish ke MQTT)

// Variabel status button sebelumnya untuk mendeteksi kapan state tombol berubah (Edge Detection)
bool lastButtonState = HIGH;
// Variabel untuk debouncing hardware (mekanisme anti-bounce saklar mekanik) 
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;  // waktu tunda debounce 50ms

// Fungsi setup() dijalankan satu kali saat perangkat pertama kali dinyalakan.
// Bertugas untuk menginisialisasi Serial komunikasi, protokol I2C, menetapkan mode pin, 
// membangun koneksi dengan WiFi dan server MQTT, serta mengkonfigurasi dan melakukan proses kalibrasi awal MPU6050.
void setup()
{
  Serial.begin(115200);
  delay(2000);
  printESP32Specs();
  Wire.begin(SDA_PIN, SCL_PIN); // SDA=21, SCL=22

  pinMode(CALIB_BUTTON_PIN, INPUT_PULLUP);

  // Inisialisasi WiFi
  Serial.print("Menghubungkan ke WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    if (millis() - wifiStart > 15000)
      break; // timeout 15s
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi terhubung");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("\nGagal koneksi WiFi (lanjut tanpa WiFi)");
  }

  // Setup MQTT server
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

  // Inisialisasi MPU6050
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(PWR_MGMT_1);
  Wire.write(0x00); // Bangun dari sleep mode
  Wire.endTransmission();

  // Konfigurasi accelerometer (±2g) dan gyro (±250°/s) menggunakan register konfigurasi
  // (Opsional: set register 0x1C untuk accel, 0x1B untuk gyro, default sudah ±2g dan ±250°/s)
  delay(100);
  kalibrasiGyro();

  lastTime = micros();
  Serial.println("MPU6050 siap. Data: Roll(°), Pitch(°), GyroX(°/s), GyroY(°/s), GyroZ(°/s)");
}

// Fungsi loop() berjalan terus-menerus selama alat aktif.
// Bertujuan untuk membaca sensor, merespon input tombol untuk kalibrasi ulang orientasi,
// menghitung logik derajat kemiringan secara matematis, mengurus koneksi ulang (reconnect) MQTT jika terputus,
// dan mengirimkan payload data orientasi yang telah dikalkulasi tersebut ke server MQTT setiap PUBLISH_INTERVAL.
void loop()
{
  bool currentButtonState = digitalRead(CALIB_BUTTON_PIN);

  // Deteksi transisi HIGH -> LOW (button pressed)
  if (lastButtonState == LOW && currentButtonState == HIGH)
  {
      if (millis() - lastDebounceTime > debounceDelay)
      {
          Serial.println();
          Serial.println("=================================");
          Serial.println("Button dilepas");
          Serial.println("Memulai re-kalibrasi gyro...");
          Serial.println("Pastikan sensor diam");
          Serial.println("=================================");

          delay(300);

          rollOffset = rawRoll;
          pitchOffset = rawPitch;
          kalibrasiOrientasi();
          kalibrasiGyro();

          lastDebounceTime = millis();
      }
  }

  lastButtonState = currentButtonState;

  // Pastikan koneksi MQTT aktif
  if (!mqttClient.connected())
  {
    // coba konek singkat tanpa blocking terlalu lama
    static unsigned long lastMqttTry = 0;
    if (millis() - lastMqttTry > 2000)
    {
      lastMqttTry = millis();
      if (WiFi.status() == WL_CONNECTED)
      {
        // build clientId dari MAC untuk unik
        String clientId = "ESP32_MPU6050_";
        clientId += String((uint64_t)ESP.getEfuseMac(), HEX);
        Serial.print("Menghubungkan ke MQTT broker...");
        if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS))
        {
          Serial.println(" terkoneksi");
        }
        else
        {
          Serial.print(" gagal, state=");
          Serial.println(mqttClient.state());
        }
      }
    }
  }

  bacaSensorRaw();

  // Konversi nilai accelerometer ke g (skala 16384 untuk ±2g)
  float ax = accX / 16384.0;
  float ay = accY / 16384.0;
  float az = accZ / 16384.0;

  // Hitung roll (kemiringan sumbu X) dan pitch (kemiringan sumbu Y)
  rawRoll = atan2(ay, az) * 180.0 / PI;
  rawPitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;

  float roll = rawRoll - rollOffset;
  float pitch = rawPitch - pitchOffset;

  // Konversi gyro ke derajat per detik (skala 131 untuk ±250°/s)
  float gx = (gyroX - gyroOffsetX) / 131.0;
  float gy = (gyroY - gyroOffsetY) / 131.0;
  float gz = (gyroZ - gyroOffsetZ) / 131.0;

  // Kirim data ke serial monitor
  Serial.print("Roll: ");
  Serial.print(roll, 2);
  Serial.print("\tPitch: ");
  Serial.print(pitch, 2);
  Serial.print("\tGyro X: ");
  Serial.print(gx, 2);
  Serial.print("\tGyro Y: ");
  Serial.print(gy, 2);
  Serial.print("\tGyro Z: ");
  Serial.println(gz, 2);

  // Publish ke MQTT setiap PUBLISH_INTERVAL jika terhubung
  if (millis() - lastPublish >= PUBLISH_INTERVAL)
  {
    lastPublish = millis();
    if (mqttClient.connected())
    {
      char payload[128];
      unsigned long timestamp = millis();
      // snprintf(payload, sizeof(payload), "{\"roll\":%.2f,\"pitch\":%.2f,\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f}", roll, pitch, gx, gy, gz);
      snprintf(
          payload,
          sizeof(payload),
          "{\"timestamp\":%lu,\"roll\":%.2f,\"pitch\":%.2f}",
          timestamp,
          roll,
          pitch
      );
      boolean ok = mqttClient.publish(MQTT_TOPIC, payload);
      if (!ok)
        Serial.println("Gagal publish MQTT");
    }
  }

  mqttClient.loop();

  delay(50); // baca setiap 50 ms
}

// Fungsi bacaSensorRaw() membaca 14 byte register berurutan dari MPU6050 lewat bus I2C.
// Secara spesifik data ini memuat nilai accelerometer (X, Y, Z), sensor suhu (abaikan), dan gyroscope (X, Y, Z).
// Dipanggil setiap siklus untuk memperbarui variabel mentah di atas sebelum dikonversi dan diolah (akselerasi dan rotasi).
void bacaSensorRaw()
{
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(ACCEL_XOUT_H);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 14, true); // 14 byte: accel (6) + suhu (2) + gyro (6)

  accX = Wire.read() << 8 | Wire.read();
  accY = Wire.read() << 8 | Wire.read();
  accZ = Wire.read() << 8 | Wire.read();
  Wire.read();
  Wire.read(); // baca suhu (abaikan)
  gyroX = Wire.read() << 8 | Wire.read();
  gyroY = Wire.read() << 8 | Wire.read();
  gyroZ = Wire.read() << 8 | Wire.read();
}

// Kalibrasi gyro dengan merata-rata 200 sampel saat sensor diam
void kalibrasiGyro()
{
    float sumX = 0;
    float sumY = 0;
    float sumZ = 0;

    const int sample = 200;

    Serial.println("Calibrating...");
    
    for (int i = 0; i < sample; i++)
    {
        bacaSensorRaw();

        sumX += gyroX;
        sumY += gyroY;
        sumZ += gyroZ;

        delay(5);
    }

    gyroOffsetX = sumX / sample;
    gyroOffsetY = sumY / sample;
    gyroOffsetZ = sumZ / sample;

    Serial.println("Calibration finished");

    Serial.print("Offset X: ");
    Serial.println(gyroOffsetX);

    Serial.print("Offset Y: ");
    Serial.println(gyroOffsetY);

    Serial.print("Offset Z: ");
    Serial.println(gyroOffsetZ);

    // Validasi sederhana
    if (abs(gyroOffsetX) > 1000 ||
        abs(gyroOffsetY) > 1000 ||
        abs(gyroOffsetZ) > 1000)
    {
        Serial.println("WARNING: Calibration may have failed");
    }

    Serial.println("=================================");
}

void kalibrasiOrientasi()
{
    bacaSensorRaw();

    // Konversi accelerometer ke satuan g
    float ax = accX / 16384.0;
    float ay = accY / 16384.0;
    float az = accZ / 16384.0;

    // Simpan posisi saat ini sebagai baseline baru
    rollOffset = atan2(ay, az) * 180.0 / PI;

    pitchOffset =
        atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;

    Serial.println("=================================");
    Serial.println("Orientation calibrated");
    
    Serial.print("Roll offset: ");
    Serial.println(rollOffset);

    Serial.print("Pitch offset: ");
    Serial.println(pitchOffset);

    Serial.println("=================================");
}

void printESP32Specs()
{
    Serial.println("===== ESP32 SYSTEM INFO =====");

    // Chip model
    Serial.print("Chip Model: ");
    Serial.println(ESP.getChipModel());

    // Revision
    Serial.print("Chip Revision: ");
    Serial.println(ESP.getChipRevision());

    // Core count
    Serial.print("CPU Cores: ");
    Serial.println(ESP.getChipCores());

    // CPU frequency
    Serial.print("CPU Frequency (MHz): ");
    Serial.println(ESP.getCpuFreqMHz());

    // Flash size
    Serial.print("Flash Size (bytes): ");
    Serial.println(ESP.getFlashChipSize());

    Serial.print("Flash Speed (Hz): ");
    Serial.println(ESP.getFlashChipSpeed());

    // Heap memory
    Serial.print("Free Heap (bytes): ");
    Serial.println(ESP.getFreeHeap());

    Serial.print("Min Free Heap (bytes): ");
    Serial.println(ESP.getMinFreeHeap());

    Serial.print("Max Alloc Heap (bytes): ");
    Serial.println(ESP.getMaxAllocHeap());

    // PSRAM
    if (psramFound())
    {
        Serial.println("PSRAM: FOUND");

        Serial.print("PSRAM Size (bytes): ");
        Serial.println(ESP.getPsramSize());

        Serial.print("Free PSRAM (bytes): ");
        Serial.println(ESP.getFreePsram());
    }
    else
    {
        Serial.println("PSRAM: NOT FOUND");
    }

    Serial.println("=============================");
}