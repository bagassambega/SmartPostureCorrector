// ============================================================
// sensor.cpp  – ESP32 Posture Detection with Edge AI (MPU6050)
//
// Publishes every PUBLISH_INTERVAL ms to MQTT:
//   { timestamp, roll, pitch, class }
//
// Edge AI: emlearn-exported Random Forest (randomforest_model.h).
// Edge AI: LightGBM (lightgbm_model.h) via C shim (lgbm_shim.c).
// Model takes double features[8]; see feature vector section below.
// Output: softmax probabilities over 6 posture classes (argmax → class index).
// ============================================================

#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>

// ESP32 system info headers
#include "esp_system.h"
#include "esp_spi_flash.h"

// lightgbm_model.h is NOT included here directly.
// It uses C99 compound literals (double[]){...} which are invalid in C++ mode.
// Instead, lgbm_shim.c compiles the model header as C, and we declare the
// entry point here with C linkage to suppress C++ name-mangling.
// See src/lgbm_shim.c for the full rationale.
extern "C" void score(double *input, double *output);

// ============================================================
// TIMING
// ============================================================

// Interval antara setiap pembacaan sensor, prediksi, dan publish (ms)
// Diganti dari 200ms (Arduino asal) ke 5000ms sesuai requirement baru
#define PUBLISH_INTERVAL 5000UL

// ============================================================
// HARDWARE PINS & I2C ADDRESS
// ============================================================

// Alamat I2C default MPU6050 (pin AD0 ke GND)
#define MPU6050_ADDR 0x68

// Register MPU6050 yang diperlukan
#define PWR_MGMT_1   0x6B   // Power management – tulis 0x00 untuk bangun dari sleep
#define ACCEL_XOUT_H 0x3B   // Register pertama blok data accelerometer
#define GYRO_XOUT_H  0x43   // Register pertama blok data gyroscope (tidak dipakai langsung, baca burst dari ACCEL_XOUT_H)

// Tombol push untuk re-kalibrasi manual
#define CALIB_BUTTON_PIN 14

// Pin I2C kustom (default ESP32: SDA=21, SCL=22)
#define SDA_PIN 21
#define SCL_PIN 22

// Motor vibrasi – D5 pada board ESP32 DevKit = GPIO5
// Modul motor menggunakan transistor driver internal:
//   HIGH → motor ON, LOW → motor OFF
#define VIBRATION_MOTOR_PIN 5

// Label encoding yang digunakan saat training model (LightGBM, 6 kelas):
//   0 = berdiri_bungkuk (buruk)  ← motor ON
//   1 = berdiri_tegak   (baik)   ← motor OFF
//   2 = duduk_bungkuk   (buruk)  ← motor ON
//   3 = duduk_tegak     (baik)   ← motor OFF
//   4 = jalan_bungkuk   (buruk)  ← motor ON
//   5 = jalan_tegak     (baik)   ← motor OFF
//
// Macro mengevaluasi ke true untuk semua kelas postur buruk (0, 2, 4).
// Digunakan di loop() untuk mengontrol pin motor vibrasi.
#define IS_BAD_POSTURE(cls) ((cls) == 0 || (cls) == 2 || (cls) == 4)

// Jumlah fitur yang dimasukkan ke model saat ini.
// Harus sesuai dengan jumlah kolom yang dipilih saat training.
// Ubah nilai ini dan blok modelInput[] jika model baru menggunakan
// jumlah fitur yang berbeda.
#define MODEL_FEATURES_LENGTH 8

// Jumlah kelas output model saat ini (lightgbm_model: 6 kelas).
// Digunakan untuk mengalokasikan output buffer dan loop argmax.
// Ubah jika model baru memiliki jumlah kelas yang berbeda.
#define MODEL_NUM_CLASSES 6

// ============================================================
// WIFI & MQTT CONFIGURATION
// ============================================================

const char *WIFI_SSID = "A15";
const char *WIFI_PASS = "saynotoclankers";

const char *MQTT_SERVER = "10.14.131.51"; // Ganti ke alamat broker Anda
const uint16_t MQTT_PORT = 1883;

// Username/password MQTT – kosongkan jika broker tidak butuh auth
const char *MQTT_USER = "";
const char *MQTT_PASS = "";

// Topik MQTT tempat ESP32 mempublikasikan data orientasi + prediksi kelas
const char *MQTT_TOPIC = "sensors/mpu6050";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// emlearn model is a set of plain C functions; no class instance needed.
// Entry point: int32_t randomforest_model_predict(const int16_t*, int32_t)

// ============================================================
// RAW SENSOR VALUES (int16_t = 16-bit signed, sesuai register MPU6050)
// ============================================================

// Nilai mentah accelerometer dan gyroscope langsung dari register MPU6050
// Digunakan baik untuk kalkulasi sudut maupun sebagai fitur model (x[0]–x[5])
int16_t rawAccX, rawAccY, rawAccZ;
int16_t rawGyroX, rawGyroY, rawGyroZ;

// ============================================================
// TILT / KEMIRINGAN (dalam derajat)
// ============================================================

// Sudut mentah yang dihitung dari atan2 accelerometer
float rawKemiringanX = 0.0f;  // Roll mentah (°)
float rawKemiringanY = 0.0f;  // Pitch mentah (°)

// Nilai offset orientasi saat tombol kalibrasi ditekan
// Digunakan untuk menetapkan "titik nol" posisi referensi
float offsetKemiringanX = 0.0f;
float offsetKemiringanY = 0.0f;

// Sudut akhir relatif terhadap posisi kalibrasi
float kemiringanX = 0.0f;  // Roll (°) → x[6] pada model
float kemiringanY = 0.0f;  // Pitch (°) → x[7] pada model

// ============================================================
// GYRO OFFSET (bias error gyro saat diam)
// ============================================================

// Rata-rata pembacaan gyro saat sensor diam, dihitung dalam kalibrasiGyro()
// Dikurangkan dari setiap pembacaan gyro untuk menghilangkan drift bias
float gyroOffsetX = 0.0f;
float gyroOffsetY = 0.0f;
float gyroOffsetZ = 0.0f;

// ============================================================
// ANGULAR VELOCITY / KECEPATAN ROTASI (°/s)
// ============================================================

// Gyro yang sudah dikonversi ke derajat per detik (skala 131 untuk ±250°/s)
// setelah dikurangi offset → x[8], x[9], x[10] pada model
float kecepatanRotasiX = 0.0f;
float kecepatanRotasiY = 0.0f;
float kecepatanRotasiZ = 0.0f;

// ============================================================
// TIMING & DEBOUNCE STATE
// ============================================================

// Timestamp terakhir kali data dipublikasikan ke MQTT
unsigned long lastPublish = 0;

// State tombol kalibrasi sebelumnya – digunakan untuk deteksi rising edge
bool lastButtonState = HIGH;

// Timestamp terakhir transisi tombol untuk debounce
unsigned long lastDebounceTime = 0;

// Waktu tunggu debounce (50ms cukup untuk menekan noise mekanikal)
const unsigned long DEBOUNCE_DELAY = 50;

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

void bacaSensor();
void kalibrasiOrientasi();
void kalibrasiGyro();
void printESP32Specs();

// ============================================================
// SETUP
// ============================================================

// Dijalankan satu kali saat power-on: inisialisasi komunikasi serial,
// I2C, WiFi, MQTT, MPU6050, lalu lakukan kalibrasi awal.
void setup()
{
    Serial.begin(115200);
    delay(2000);

    printESP32Specs();

    // Inisialisasi bus I2C dengan pin kustom (SDA=21, SCL=22)
    Wire.begin(SDA_PIN, SCL_PIN);

    // Tombol kalibrasi menggunakan pull-up internal; LOW = ditekan
    pinMode(CALIB_BUTTON_PIN, INPUT_PULLUP);

    // Motor vibrasi: output digital, default OFF (LOW) saat boot
    // Penting: set LOW sebelum pinMode agar pin tidak menghasilkan glitch HIGH
    digitalWrite(VIBRATION_MOTOR_PIN, LOW);
    pinMode(VIBRATION_MOTOR_PIN, OUTPUT);

    // --- Koneksi WiFi ---
    Serial.print("Menghubungkan ke WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
        if (millis() - wifiStart > 15000)
            break; // timeout 15 detik, lanjut tanpa WiFi
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

    // --- Konfigurasi MQTT broker ---
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

    // --- Inisialisasi MPU6050 ---
    // Tulis 0x00 ke register PWR_MGMT_1 untuk membangunkan sensor dari sleep mode
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(PWR_MGMT_1);
    Wire.write(0x00);
    Wire.endTransmission();

    delay(100); // Beri waktu sensor stable setelah wake-up

    // Kalibrasi awal: tentukan titik nol orientasi dan hitung bias gyro
    kalibrasiOrientasi();
    kalibrasiGyro();

    Serial.println("Sistem siap. Publish setiap " + String(PUBLISH_INTERVAL) + "ms");
}

// ============================================================
// LOOP
// ============================================================

// Berjalan terus-menerus: baca sensor, deteksi tombol kalibrasi,
// reconnect MQTT jika perlu, dan setiap PUBLISH_INTERVAL jalankan
// inferensi model lalu kirim payload ke MQTT.
void loop()
{
    // --- Deteksi rising edge tombol kalibrasi (LOW→HIGH = dilepas) ---
    bool currentButtonState = digitalRead(CALIB_BUTTON_PIN);

    if (lastButtonState == LOW && currentButtonState == HIGH)
    {
        if (millis() - lastDebounceTime > DEBOUNCE_DELAY)
        {
            Serial.println("=================================");
            Serial.println("Button dilepas – re-kalibrasi...");
            Serial.println("Pastikan sensor diam");
            Serial.println("=================================");

            delay(300);

            kalibrasiOrientasi();
            kalibrasiGyro();

            lastDebounceTime = millis();
        }
    }

    lastButtonState = currentButtonState;

    // --- MQTT reconnect (non-blocking, coba setiap 2 detik) ---
    if (!mqttClient.connected())
    {
        static unsigned long lastMqttTry = 0;
        if (millis() - lastMqttTry > 2000)
        {
            lastMqttTry = millis();
            if (WiFi.status() == WL_CONNECTED)
            {
                // Bangun clientId unik dari MAC address
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

    // --- Baca sensor – update semua variabel global ---
    bacaSensor();

    // --- Publish + inferensi setiap PUBLISH_INTERVAL ---
    if (millis() - lastPublish >= PUBLISH_INTERVAL)
    {
        lastPublish = millis();

        // --------------------------------------------------------
        // [1/2] SENSOR DATA SNAPSHOT – 11 fitur lengkap
        //
        // Seluruh data sensor dikumpulkan dan disimpan di sini,
        // terlepas dari berapa fitur yang digunakan model saat ini.
        // Ini memastikan semua data tersedia jika model berikutnya
        // memerlukan fitur yang berbeda – tidak ada perubahan pada
        // logika pembacaan sensor yang diperlukan.
        //
        //   sensorData[0]  = rawAccX          (int16, raw accel X)
        //   sensorData[1]  = rawAccY          (int16, raw accel Y)
        //   sensorData[2]  = rawAccZ          (int16, raw accel Z)
        //   sensorData[3]  = rawGyroX         (int16, raw gyro X)
        //   sensorData[4]  = rawGyroY         (int16, raw gyro Y)
        //   sensorData[5]  = rawGyroZ         (int16, raw gyro Z)
        //   sensorData[6]  = kemiringanX      (roll °, relatif)
        //   sensorData[7]  = kemiringanY      (pitch °, relatif)
        //   sensorData[8]  = kecepatanRotasiX (gx °/s setelah offset)
        //   sensorData[9]  = kecepatanRotasiY (gy °/s setelah offset)
        //   sensorData[10] = kecepatanRotasiZ (gz °/s setelah offset)
        // --------------------------------------------------------
        float sensorData[11];
        sensorData[0]  = (float)rawAccX;
        sensorData[1]  = (float)rawAccY;
        sensorData[2]  = (float)rawAccZ;
        sensorData[3]  = (float)rawGyroX;
        sensorData[4]  = (float)rawGyroY;
        sensorData[5]  = (float)rawGyroZ;
        sensorData[6]  = kemiringanX;
        sensorData[7]  = kemiringanY;
        sensorData[8]  = kecepatanRotasiX;
        sensorData[9]  = kecepatanRotasiY;
        sensorData[10] = kecepatanRotasiZ;

        // --------------------------------------------------------
        // [2/2] MODEL INPUT VECTOR – subset dari sensorData
        //
        // LightGBM model mengharapkan double*.
        // Setiap nilai float/int dari sensorData di-cast ke double.
        //
        // !! PENTING !! Urutan indeks sensorData di bawah HARUS
        // sesuai dengan urutan kolom X = df[[...]] di training script.
        // Jika urutan salah, prediksi noise meskipun build sukses.
        //
        // Model saat ini: lightgbm_model (8 fitur)
        //   modelInput[0] ← sensorData[0]  (rawAccX,          raw accel X)
        //   modelInput[1] ← sensorData[1]  (rawAccY,          raw accel Y)
        //   modelInput[2] ← sensorData[2]  (rawAccZ,          raw accel Z)
        //   modelInput[3] ← sensorData[3]  (rawGyroX,         raw gyro X)
        //   modelInput[4] ← sensorData[8]  (kecepatanRotasiX, gx °/s)
        //   modelInput[5] ← sensorData[9]  (kecepatanRotasiY, gy °/s)
        //   modelInput[6] ← sensorData[6]  (kemiringanX,      roll °)
        //   modelInput[7] ← sensorData[7]  (kemiringanY,      pitch °)
        // --------------------------------------------------------
        double modelInput[MODEL_FEATURES_LENGTH];
        modelInput[0] = (double)sensorData[0];   // rawAccX
        modelInput[1] = (double)sensorData[1];   // rawAccY
        modelInput[2] = (double)sensorData[2];   // rawAccZ
        modelInput[3] = (double)sensorData[3];   // rawGyroX
        modelInput[4] = (double)sensorData[8];   // kecepatanRotasiX (gx °/s)
        modelInput[5] = (double)sensorData[9];   // kecepatanRotasiY (gy °/s)
        modelInput[6] = (double)sensorData[6];   // kemiringanX (roll °)
        modelInput[7] = (double)sensorData[7];   // kemiringanY (pitch °)

        // Jalankan inferensi.
        // score() menulis softmax probabilities ke lgbmOutput[].
        // Tidak ada built-in argmax – cari indeks dengan nilai tertinggi.
        //
        // Catatan performa: score() menggunakan double (64-bit float).
        // Xtensa LX6 tidak memiliki FPU untuk double – operasi dilakukan
        // oleh software runtime GCC. Untuk interval 5 detik ini tidak
        // bermasalah, tetapi jangan gunakan model ini di loop cepat (<100ms).
        double lgbmOutput[MODEL_NUM_CLASSES];
        score(modelInput, lgbmOutput);

        int predictedClass = 0;
        for (int i = 1; i < MODEL_NUM_CLASSES; i++) {
            if (lgbmOutput[i] > lgbmOutput[predictedClass])
                predictedClass = i;
        }

        unsigned long timestamp = millis();

        // Cetak ke serial monitor untuk debugging
        Serial.print("Roll: ");        Serial.print(kemiringanX, 2);
        Serial.print("\tPitch: ");     Serial.print(kemiringanY, 2);
        Serial.print("\tClass: ");     Serial.print(predictedClass);

        // Kontrol motor vibrasi berdasarkan hasil prediksi kelas.
        // Kelas 0 → postur yang memerlukan alert → motor ON.
        // Kelas lain (1, 2, 3) → motor OFF.
        // digitalWrite dilakukan setiap siklus PUBLISH_INTERVAL sehingga
        // state motor selalu sinkron dengan prediksi terbaru.
        // Aktifkan motor untuk kelas postur buruk (0=berdiri_bungkuk, 2=duduk_bungkuk)
        if (IS_BAD_POSTURE(predictedClass))
        {
            digitalWrite(VIBRATION_MOTOR_PIN, HIGH);
            Serial.println(" [MOTOR ON]");
        }
        else
        {
            digitalWrite(VIBRATION_MOTOR_PIN, LOW);
            Serial.println(" [motor off]");
        }

        // Kirim payload JSON ke MQTT jika terhubung
        if (mqttClient.connected())
        {
            char payload[128];

            // Payload mencakup timestamp, roll, pitch, dan kelas prediksi
            // "class" adalah integer (0–3); classLabel adalah representasi string-nya
            snprintf(
                payload,
                sizeof(payload),
                "{\"timestamp\":%lu,\"roll\":%.2f,\"pitch\":%.2f,\"class\":%d}",
                timestamp,
                kemiringanX,
                kemiringanY,
                predictedClass
            );

            boolean ok = mqttClient.publish(MQTT_TOPIC, payload);
            if (!ok)
                Serial.println("Gagal publish MQTT");
        }
    }

    mqttClient.loop();

    // Polling singkat; bacaSensor() dipanggil tiap iterasi loop
    // untuk menjaga data internal tetap segar (navigasi debounce dll.)
    delay(10);
}

// ============================================================
// bacaSensor()
//
// Membaca 14 byte register berurutan dari MPU6050 melalui I2C
// (6 byte accel + 2 byte suhu (dibuang) + 6 byte gyro = 14 byte).
// Menghitung sudut kemiringan (roll/pitch) dan kecepatan rotasi
// yang siap dipakai sebagai fitur model maupun dikirim ke MQTT.
// ============================================================
void bacaSensor()
{
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(ACCEL_XOUT_H);
    Wire.endTransmission(false); // repeated-start: pertahankan bus I2C aktif

    // requestFrom meminta 14 byte sekaligus (burst read)
    Wire.requestFrom((uint16_t)MPU6050_ADDR, (size_t)14, true);

    // Setiap register 16-bit disusun big-endian oleh MPU6050:
    // byte tinggi datang dulu, geser 8 bit ke kiri lalu OR dengan byte rendah
    rawAccX  = Wire.read() << 8 | Wire.read();
    rawAccY  = Wire.read() << 8 | Wire.read();
    rawAccZ  = Wire.read() << 8 | Wire.read();
    Wire.read(); Wire.read(); // 2 byte suhu – tidak digunakan
    rawGyroX = Wire.read() << 8 | Wire.read();
    rawGyroY = Wire.read() << 8 | Wire.read();
    rawGyroZ = Wire.read() << 8 | Wire.read();

    // Konversi akselerometer ke satuan g (skala ±2g → LSB/g = 16384)
    float ax = rawAccX / 16384.0f;
    float ay = rawAccY / 16384.0f;
    float az = rawAccZ / 16384.0f;

    // Hitung sudut mentah menggunakan atan2 (tangent inverse 2-argumen)
    // Roll  = rotasi terhadap sumbu X (kanan-kiri)
    // Pitch = rotasi terhadap sumbu Y (maju-mundur)
    rawKemiringanX = atan2(ay, az) * 180.0f / PI;
    rawKemiringanY = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0f / PI;

    // Terapkan offset kalibrasi agar posisi referensi menjadi 0°
    kemiringanX = rawKemiringanX - offsetKemiringanX;
    kemiringanY = rawKemiringanY - offsetKemiringanY;

    // Konversi gyro ke °/s (skala ±250°/s → LSB/(°/s) = 131)
    // setelah dikurangi bias offset yang dihitung saat kalibrasi
    kecepatanRotasiX = (rawGyroX - gyroOffsetX) / 131.0f;
    kecepatanRotasiY = (rawGyroY - gyroOffsetY) / 131.0f;
    kecepatanRotasiZ = (rawGyroZ - gyroOffsetZ) / 131.0f;
}

// ============================================================
// kalibrasiGyro()
//
// Rata-rata 200 sampel gyro saat sensor diam untuk mengestimasi
// bias (drift) gyro. Nilainya disimpan di gyroOffsetX/Y/Z dan
// dikurangkan dari setiap pembacaan pada bacaSensor().
// ============================================================
void kalibrasiGyro()
{
    float sumX = 0, sumY = 0, sumZ = 0;
    const int SAMPLE_COUNT = 200;

    Serial.println("Kalibrasi gyro... (pastikan sensor diam)");

    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        bacaSensor();

        sumX += rawGyroX;
        sumY += rawGyroY;
        sumZ += rawGyroZ;

        delay(5);
    }

    gyroOffsetX = sumX / SAMPLE_COUNT;
    gyroOffsetY = sumY / SAMPLE_COUNT;
    gyroOffsetZ = sumZ / SAMPLE_COUNT;

    Serial.println("Kalibrasi gyro selesai");
    Serial.print("Offset X: "); Serial.println(gyroOffsetX);
    Serial.print("Offset Y: "); Serial.println(gyroOffsetY);
    Serial.print("Offset Z: "); Serial.println(gyroOffsetZ);

    // Peringatan jika offset terlalu besar (sensor bergerak saat kalibrasi)
    if (abs(gyroOffsetX) > 1000 ||
        abs(gyroOffsetY) > 1000 ||
        abs(gyroOffsetZ) > 1000)
    {
        Serial.println("WARNING: Kalibrasi gyro mungkin gagal (sensor bergerak?)");
    }

    Serial.println("=================================");
}

// ============================================================
// kalibrasiOrientasi()
//
// Membaca satu sampel sensor dan menyimpan sudut saat ini sebagai
// offset orientasi. Setelah ini, kemiringanX/Y akan bernilai 0°
// pada posisi sekarang (titik referensi baru).
// ============================================================
void kalibrasiOrientasi()
{
    bacaSensor();

    // Simpan posisi saat ini sebagai baseline baru
    offsetKemiringanX = rawKemiringanX;
    offsetKemiringanY = rawKemiringanY;

    Serial.println("=================================");
    Serial.println("Orientasi dikalibrasi");
    Serial.print("Roll offset: ");  Serial.println(offsetKemiringanX);
    Serial.print("Pitch offset: "); Serial.println(offsetKemiringanY);
    Serial.println("=================================");
}

// ============================================================
// printESP32Specs()
//
// Mencetak spesifikasi chip ESP32 ke serial monitor saat startup.
// Berguna untuk verifikasi platform dan kapasitas memori yang tersedia.
// ============================================================
void printESP32Specs()
{
    Serial.println("===== ESP32 SYSTEM INFO =====");

    Serial.print("Chip Model: ");
    Serial.println(ESP.getChipModel());

    Serial.print("Chip Revision: ");
    Serial.println(ESP.getChipRevision());

    Serial.print("CPU Cores: ");
    Serial.println(ESP.getChipCores());

    Serial.print("CPU Frequency (MHz): ");
    Serial.println(ESP.getCpuFreqMHz());

    Serial.print("Flash Size (bytes): ");
    Serial.println(ESP.getFlashChipSize());

    Serial.print("Flash Speed (Hz): ");
    Serial.println(ESP.getFlashChipSpeed());

    Serial.print("Free Heap (bytes): ");
    Serial.println(ESP.getFreeHeap());

    Serial.print("Min Free Heap (bytes): ");
    Serial.println(ESP.getMinFreeHeap());

    Serial.print("Max Alloc Heap (bytes): ");
    Serial.println(ESP.getMaxAllocHeap());

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