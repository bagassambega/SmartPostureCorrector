#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Alamat I2C MPU6050 (0x68 jika AD0 ke GND)
#define MPU6050_ADDR 0x68

// Register MPU6050
#define PWR_MGMT_1 0x6B
#define ACCEL_XOUT_H 0x3B
#define GYRO_XOUT_H 0x43

// Variabel untuk menyimpan offset gyro
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;

// Variabel untuk data mentah
int16_t accX, accY, accZ;
int16_t gyroX, gyroY, gyroZ;

// Variabel untuk hitungan waktu (opsional untuk integrasi yaw)
unsigned long lastTime = 0;

// --- MQTT & WiFi configuration (edit these) ---
const char *WIFI_SSID = "Jenong Smart";
const char *WIFI_PASS = "jenong21";
const char *MQTT_SERVER = "192.168.1.9"; // ganti ke alamat broker Anda
const uint16_t MQTT_PORT = 1883;
const char *MQTT_USER = ""; // isi jika broker perlu auth
const char *MQTT_PASS = ""; // isi jika broker perlu auth
const char *MQTT_TOPIC = "sensors/mpu6050";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 200; // ms

void setup()
{
  Serial.begin(115200);
  Wire.begin(21, 22); // SDA=21, SCL=22

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

void loop()
{
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
  float roll = atan2(ay, az) * 180.0 / PI;
  float pitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;

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
      snprintf(payload, sizeof(payload), "{\"roll\":%.2f,\"pitch\":%.2f,\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f}", roll, pitch, gx, gy, gz);
      boolean ok = mqttClient.publish(MQTT_TOPIC, payload);
      if (!ok)
        Serial.println("Gagal publish MQTT");
    }
  }

  mqttClient.loop();

  delay(50); // baca setiap 50 ms
}

// Fungsi baca data mentah dari MPU6050
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
  float sumX = 0, sumY = 0, sumZ = 0;
  int sample = 200;
  Serial.println("Kalibrasi gyro... jangan gerakkan sensor!");
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
  Serial.println("Kalibrasi selesai.");
}