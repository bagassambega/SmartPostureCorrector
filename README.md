# Encok-Cok: Sensor MPU6050 dengan ESP32

Proyek ini mendemonstrasikan cara menggunakan sensor gyroscope dan accelerometer MPU6050 dengan ESP32 DevKit V1 untuk mengukur orientasi dan gerakan.

---

## Daftar Isi

1. [Persiapan Hardware](#persiapan-hardware)
2. [Setup dan Konfigurasi](#setup-dan-konfigurasi)
3. [Koneksi Hardware](#koneksi-hardware)
4. [Upload Kode ke ESP32](#upload-kode-ke-esp32)
5. [Menjalankan Sensor](#menjalankan-sensor)
6. [MQTT Configuration](#mqtt-configuration)
7. [Penjelasan Kode](#penjelasan-kode)
8. [Output Serial Monitor](#output-serial-monitor)
9. [Troubleshooting](#troubleshooting)

---

## Persiapan Hardware

Sebelum memulai, pastikan Anda memiliki komponen berikut:

### Komponen yang Dibutuhkan

1. **ESP32 DevKit V1** - Board microcontroller utama
2. **MPU6050** - Sensor 6-axis gyroscope dan accelerometer
3. **Kabel USB Mikro** - Untuk menghubungkan ESP32 dengan laptop
4. **Kabel Jumper** - Untuk menghubungkan ESP32 dengan MPU6050
5. **Laptop/PC** - Dengan Arduino IDE terinstal

### Software yang Dibutuhkan

1. Arduino IDE (download dari <https://www.arduino.cc/en/software>)
2. Board support untuk ESP32 (akan dijelaskan di bagian Setup)
3. Library Wire (sudah built-in di Arduino)

---

## Setup dan Konfigurasi

### 1. Instalasi Arduino IDE

- Download Arduino IDE dari <https://www.arduino.cc/en/software>
- Install sesuai dengan sistem operasi Anda

### 2. Tambahkan Board Support ESP32

- Buka Arduino IDE
- Pergi ke **File → Preferences**
- Di kolom "Additional Boards Manager URLs", tambahkan:

  ```
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
  ```

- Klik OK
- Pergi ke **Tools → Board → Boards Manager**
- Cari "esp32" dan install "ESP32 by Espressif Systems"

### 3. Konfigurasi Board Arduino IDE

Setelah terinstal, atur pengaturan berikut:

- **Tools → Board** → Pilih "ESP32 Dev Module"
- **Tools → Port** → Pilih port COM yang sesuai (cek di Device Manager atau System Report)
- **Tools → Upload Speed** → 115200

### 4. Instalasi Library Dependencies

Proyek ini memerlukan library Arduino tambahan untuk komunikasi I2C dan MQTT. Ikuti langkah berikut untuk menginstal:

#### Library yang Diperlukan

1. **Wire** - Sudah built-in (tidak perlu install)
2. **PubSubClient** - Untuk komunikasi MQTT dengan broker

#### Cara Install PubSubClient

1. Buka Arduino IDE
2. Pergi ke **Sketch → Include Library → Manage Libraries** (atau tekan `Ctrl+Shift+I`)
3. Di kolom pencarian, ketik: `PubSubClient`
4. Cari hasil dengan penulis **Nick O'Leary**
5. Klik tombol **Install**
6. Tunggu hingga instalasi selesai (biasanya 1-2 menit)

**Versi yang direkomendasikan**: 2.8.0 atau lebih baru

#### Verifikasi Instalasi

- Buka **Sketch → Include Library**
- Jika Anda melihat **PubSubClient** di daftar library, maka instalasi berhasil

---

## Koneksi Hardware

### Penjelasan I2C: SDA dan SCL

**I2C (Inter-Integrated Circuit)** adalah protokol komunikasi yang menggunakan 2 jalur data:

- **SDA (Serial Data)** - Jalur untuk data
- **SCL (Serial Clock)** - Jalur untuk sinyal clock/sinkronisasi

Protokol ini memungkinkan ESP32 berkomunikasi dengan sensor MPU6050 melalui hanya 2 kabel (plus VCC dan GND).

### Pin yang Digunakan pada ESP32 DevKit V1

- **SDA** → Pin **D21** (GPIO 21)
- **SCL** → Pin **D22** (GPIO 22)
- **VCC** → Pin **3V3** (3.3V)
- **GND** → Pin **GND**

### Langkah-Langkah Koneksi

Hubungkan ESP32 dengan MPU6050 menggunakan kabel jumper sebagai berikut:

| ESP32 Pin | MPU6050 Pin | Fungsi |
|-----------|-------------|---------|
| 3V3 | VCC | Supply Voltage (3.3V) |
| GND | GND | Ground |
| D21 (SDA) | SDA | Serial Data |
| D22 (SCL) | SCL | Serial Clock |

### Diagram Koneksi

```
ESP32 DevKit V1                    MPU6050
┌─────────────┐                ┌──────────┐
│  3V3 ───────┼────────────────┤ VCC      │
│  GND ───────┼────────────────┤ GND      │
│  D21(SDA)──┼────────────────┤ SDA      │
│  D22(SCL)──┼────────────────┤ SCL      │
└─────────────┘                └──────────┘
```

### Catatan Penting

- Pastikan koneksi **VCC ke VCC** (Power)
- Pastikan koneksi **GND ke GND** (Ground)
- Pastikan koneksi **SDA ke SDA** (Serial Data)
- Pastikan koneksi **SCL ke SCL** (Serial Clock)
- Gunakan kabel jumper berkualitas untuk koneksi yang stabil
- Pastikan semua koneksi **solid** dan tidak ada yang longgar

### Verifikasi Sensor Aktif

- **LED pada MPU6050 akan menyala** ketika sensor berhasil aktif dan mendapat power
- Jika LED tidak menyala, periksa kembali koneksi VCC dan GND

---

## Upload Kode ke ESP32

### 1. Hubungkan ESP32 ke Laptop

- Gunakan kabel USB Mikro untuk menghubungkan ESP32 dengan laptop
- Tunggu driver terdeteksi otomatis (beberapa sistem memerlukan driver CH340)

### 2. Buka File Kode

- Buka Arduino IDE
- Buka file `hardware/sensor.ino`

### 3. Verify Kode

- Klik tombol **Verify** (atau tekan Ctrl+R)
- Tunggu hingga proses selesai dan tidak ada error

### 4. Upload Kode

- Klik tombol **Upload** (atau tekan Ctrl+U)
- Tunggu hingga muncul pesan "Done uploading" di status bar bawah

### 5. Buka Serial Monitor

- Klik **Tools → Serial Monitor** (atau tekan Ctrl+Shift+M)
- Pastikan baud rate di kanan bawah adalah **115200**

---

## Menjalankan Sensor

### Prosedur Jalankan

1. **Buka Serial Monitor** (sebagaimana dijelaskan di atas)
2. Pastikan baud rate adalah **115200**
3. **Tunggu 2-3 detik** untuk inisialisasi sensor
4. Anda akan melihat pesan:

   ```
   Kalibrasi gyro... jangan gerakkan sensor!
   Kalibrasi selesai.
   MPU6050 siap. Data: Roll(°), Pitch(°), GyroX(°/s), GyroY(°/s), GyroZ(°/s)
   ```

5. **Goyangkan/putar board** untuk melihat perubahan nilai di Serial Monitor

### Verifikasi Data Berhasil Diambil

- **Jika data berhasil**, Anda akan melihat **nilai-nilai berubah** di Serial Monitor ketika Anda **goyangkan atau putar board**
- Jika nilai tidak berubah, periksa koneksi hardware dan inisialisasi ulang

---

## MQTT Configuration

### Persiapan MQTT Broker

Sebelum mengirim data ke MQTT broker, pastikan Anda memiliki:

1. **MQTT Broker** yang sedang berjalan (misalnya: Mosquitto, HiveMQ, atau layanan cloud)
2. **Alamat IP/Hostname** broker
3. **Port MQTT** (default: 1883)
4. **Username dan Password** (jika broker memerlukan autentikasi)

### Konfigurasi WiFi dan MQTT di Kode

Edit file `hardware/sensor.ino` dan ubah konfigurasi berikut di bagian atas file:

```cpp
// --- MQTT & WiFi configuration (edit these) ---
const char* WIFI_SSID = "YOUR_SSID";           // Ganti dengan nama WiFi Anda
const char* WIFI_PASS = "YOUR_PASSWORD";       // Ganti dengan password WiFi
const char* MQTT_SERVER = "192.168.1.100";     // Ganti dengan IP/hostname broker
const uint16_t MQTT_PORT = 1883;               // Port default MQTT (ubah jika perlu)
const char* MQTT_USER = "";                    // Username broker (kosongkan jika tidak perlu)
const char* MQTT_PASS = "";                    // Password broker (kosongkan jika tidak perlu)
const char* MQTT_TOPIC = "sensors/mpu6050";    // Topic untuk publish data
```

### Contoh Konfigurasi untuk Mosquitto Lokal

Jika Anda menjalankan Mosquitto di komputer lokal dengan IP `192.168.1.100`:

```cpp
const char* WIFI_SSID = "MyWiFi";
const char* WIFI_PASS = "MyPassword123";
const char* MQTT_SERVER = "192.168.1.100";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "";  // Jika auth disabled
const char* MQTT_PASS = "";
const char* MQTT_TOPIC = "sensors/mpu6050";
```

### Cara Kerja Publishing Data

Setelah konfigurasi selesai:

1. **ESP32 akan terhubung ke WiFi** saat startup
2. **ESP32 akan terhubung ke MQTT broker** jika WiFi berhasil
3. **Data sensor dikirim ke MQTT topic** setiap 200ms dalam format JSON:

```json
{"roll":2.45,"pitch":-1.23,"gx":0.20,"gy":-0.15,"gz":0.10}
```

1. Pesan akan muncul di Serial Monitor:
   - `Menghubungkan ke WiFi.` (dengan titik-titik saat menunggu)
   - `WiFi terhubung` (jika berhasil)
   - `IP: 192.168.1.XX` (IP yang diperoleh ESP32)
   - `Menghubungkan ke MQTT broker...` (setiap 2 detik jika belum terkoneksi)
   - `MQTT terkoneksi` (jika berhasil)

### Monitoring Data MQTT

#### Option 1: Menggunakan MQTT Explorer (GUI)

1. Download MQTT Explorer dari <https://github.com/thomasnordquist/MQTT-Explorer>
2. Buka aplikasi
3. Klik "+" untuk tambah koneksi baru
4. Masukkan:
   - **Host**: Alamat IP broker Anda
   - **Port**: 1883
   - **Username/Password**: Jika diperlukan
5. Klik "CONNECT"
6. Di bagian kiri, Anda akan melihat topic `sensors/mpu6050` dengan data yang streaming

#### Option 2: Menggunakan Command Line `mosquitto_sub`

```bash
mosquitto_sub -h 192.168.1.100 -t "sensors/mpu6050"
```

Anda akan melihat output data sensor real-time:

```
{"roll":2.45,"pitch":-1.23,"gx":0.20,"gy":-0.15,"gz":0.10}
{"roll":2.48,"pitch":-1.20,"gx":0.22,"gy":-0.18,"gz":0.12}
{"roll":15.67,"pitch":8.90,"gx":45.30,"gy":32.10,"gz":5.50}
```

### Troubleshooting MQTT

**Masalah**: WiFi tidak terkoneksi

- Periksa SSID dan password WiFi
- Pastikan ESP32 berada dalam jangkauan WiFi

**Masalah**: MQTT broker tidak terkoneksi

- Verifikasi IP/hostname broker benar
- Pastikan broker sedang berjalan
- Periksa username/password jika menggunakan autentikasi
- Pastikan firewall tidak memblokir port 1883

**Masalah**: Data tidak terlihat di MQTT topic

- Verifikasi ESP32 terhubung ke WiFi (cek Serial Monitor)
- Pastikan ESP32 terhubung ke MQTT broker
- Coba subscribe dengan `mosquitto_sub` untuk verifikasi broker

---

## Penjelasan Kode

### Struktur Program

#### 1. **Inisialisasi Hardware (Setup)**

```cpp
Wire.begin(21, 22); // Inisialisasi I2C dengan SDA=21, SCL=22
```

- Mengaktifkan protokol I2C pada pin D21 (SDA) dan D22 (SCL)
- Ini memungkinkan ESP32 berkomunikasi dengan MPU6050

#### 2. **Kalibrasi Gyro**

```cpp
kalibrasiGyro();
```

- Membaca 200 sampel data gyro saat sensor diam
- Menghitung rata-rata sebagai offset
- Digunakan untuk menghilangkan *noise* dan *drift* dari gyro

#### 3. **Pembacaan Data Sensor (Loop)**

Setiap 50ms, program membaca data mentah dari MPU6050 dan mengkonversinya ke satuan yang berguna.

---

### Output Data yang Ditampilkan

Program mengirim 5 nilai data ke Serial Monitor setiap 50ms:

#### **1. Roll (derajat)**

- **Definisi**: Kemiringan pada sumbu X (rotation around X-axis)
- **Nilai normal**: -90° sampai +90°
- **Pengukuran menggunakan**: Accelerometer (sensor percepatan)
- **Perubahan**: Berubah ketika Anda memiringkan board ke kanan/kiri

#### **2. Pitch (derajat)**

- **Definisi**: Kemiringan pada sumbu Y (rotation around Y-axis)
- **Nilai normal**: -90° sampai +90°
- **Pengukuran menggunakan**: Accelerometer (sensor percepatan)
- **Perubahan**: Berubah ketika Anda memiringkan board ke depan/belakang

#### **3. GyroX (°/s - derajat per detik)**

- **Definisi**: Kecepatan rotasi pada sumbu X
- **Pengukuran menggunakan**: Gyroscope (sensor kecepatan rotasi)
- **Range**: -250°/s sampai +250°/s
- **Arti**:
  - 0 = tidak berputar
  - Positif = berputar searah jarum jam
  - Negatif = berputar berlawanan arah jarum jam
- **Perubahan**: Muncul nilainya ketika Anda **memutar board** di sekitar sumbu X

#### **4. GyroY (°/s - derajat per detik)**

- Sama seperti GyroX, tapi untuk rotasi sumbu Y
- **Perubahan**: Muncul nilainya ketika Anda **memutar board** di sekitar sumbu Y

#### **5. GyroZ (°/s - derajat per detik)**

- Sama seperti GyroX dan GyroY, tapi untuk rotasi sumbu Z
- **Perubahan**: Muncul nilainya ketika Anda **memutar board** di sekitar sumbu Z (rotasi pada bidang horizontal)

### Contoh Output Serial Monitor

```
Kalibrasi gyro... jangan gerakkan sensor!
Kalibrasi selesai.
MPU6050 siap. Data: Roll(°), Pitch(°), GyroX(°/s), GyroY(°/s), GyroZ(°/s)
Roll: 2.45    Pitch: -1.23   Gyro X: 0.20   Gyro Y: -0.15  Gyro Z: 0.10
Roll: 2.40    Pitch: -1.25   Gyro X: 0.18   Gyro Y: -0.12  Gyro Z: 0.08
Roll: 15.67   Pitch: 8.90    Gyro X: 45.30  Gyro Y: 32.10  Gyro Z: 5.50
Roll: 20.45   Pitch: 12.30   Gyro X: 52.10  Gyro Y: 38.50  Gyro Z: 8.20
```

---

### Penjelasan Teknis Konversi Data

#### **Accelerometer**

- Data mentah dari sensor: integer 16-bit (-32768 sampai +32767)
- Dikonversi dengan rumus: `nilai_g = nilai_mentah / 16384.0`
- Hasil dalam satuan **g** (gravitasi)
- Digunakan untuk menghitung Roll dan Pitch

#### **Gyroscope**

- Data mentah dari sensor: integer 16-bit (-32768 sampai +32767)
- Dikonversi dengan rumus: `nilai_dps = nilai_mentah / 131.0`
- Hasil dalam satuan **°/s** (derajat per detik)
- Offset dikurangi untuk menghilangkan noise statis

---

## Output Serial Monitor

### Format Output

Setiap baris menampilkan:

```
Roll: [nilai]   Pitch: [nilai]   Gyro X: [nilai]   Gyro Y: [nilai]   Gyro Z: [nilai]
```

### Interpretasi Nilai

| Parameter | Saat Diam | Saat Goyangan | Saat Putar |
|-----------|-----------|--------------|-----------|
| Roll | Mendekati 0° | Berubah -90° sampai +90° | Berubah |
| Pitch | Mendekati 0° | Berubah -90° sampai +90° | Berubah |
| Gyro X | Mendekati 0°/s | 0°/s (jika statis) | Nilainya berubah |
| Gyro Y | Mendekati 0°/s | 0°/s (jika statis) | Nilainya berubah |
| Gyro Z | Mendekati 0°/s | 0°/s (jika statis) | Nilainya berubah |

### Contoh Aksi dan Hasil

1. **Board diam horizontal**: Roll ≈ 0°, Pitch ≈ 0°, semua Gyro ≈ 0°/s
2. **Kemiringkan board ke kanan**: Roll naik positif
3. **Kemiringkan board ke depan**: Pitch naik positif
4. **Putar board**: Nilai Gyro akan menunjukkan kecepatan rotasi

---

## Troubleshooting

### Problem 1: Serial Monitor Menampilkan Karakter Aneh/Gibberish

**Penyebab**: Baud rate tidak sesuai
**Solusi**:

- Pastikan baud rate di Serial Monitor adalah **115200**
- Buka **Tools → Serial Monitor** dan cek dropdown di kanan bawah

### Problem 2: Serial Monitor Kosong / Tidak Ada Output

**Penyebab**:

- Port COM salah
- Board tidak ter-upload dengan benar
- Kabel USB rusak

**Solusi**:

1. Periksa **Tools → Port** dan pastikan port yang benar terpilih
2. Coba **upload ulang** kode
3. Coba **kabel USB yang berbeda**
4. Restart Arduino IDE dan ESP32

### Problem 3: "Failed to connect to ESP32" saat Upload

**Penyebab**: Driver CH340 tidak terinstal atau board tidak terdeteksi
**Solusi**:

1. Install driver CH340 (cari "CP210x VCP Drivers" atau "CH340 drivers")
2. Gunakan kabel USB yang support data transfer (bukan hanya charging)
3. Tekan tombol **BOOT** pada ESP32 sebelum upload

### Problem 4: LED MPU6050 Tidak Menyala

**Penyebab**: Koneksi VCC atau GND putus/lepas
**Solusi**:

1. Periksa koneksi kabel jumper VCC dan GND ke MPU6050
2. Pastikan kabel terhubung dengan solid
3. Coba koneksi ulang dengan kabel jumper yang berbeda
4. Gunakan multimeter untuk cek continuity/koneksi

### Problem 5: Nilai Sensor Tidak Berubah Saat Goyangan

**Penyebab**:

- Koneksi SDA/SCL lepas
- Alamat I2C salah
- MPU6050 tidak terdeteksi

**Solusi**:

1. Periksa koneksi SDA (D21) dan SCL (D22)
2. Buka Serial Monitor dan lihat apakah muncul pesan "Kalibrasi selesai"
3. Jika tidak ada pesan sama sekali, ada masalah komunikasi I2C
4. Coba I2C Scanner untuk verifikasi alamat MPU6050

### Problem 6: Nilai Selalu Sama Setiap Kalibrasi Ulang

**Penyebab**: Sensor mungkin sudah drift atau ada masalah hardware
**Solusi**:

1. Jangan gerakkan sensor saat kalibrasi (tunggu sampai muncul "Kalibrasi selesai")
2. Tunggu 5 detik setelah upload sebelum menggoyangkan
3. Coba reset ESP32 dengan menekan tombol RESET

---

## Tips & Trik

1. **Kalibrasi Optimal**: Pastikan sensor benar-benar diam saat kalibrasi (jangan disentuh)
2. **Pembacaan Akurat**: Tunggu 2-3 detik setelah inisialisasi untuk data yang stabil
3. **Performa**: Program membaca sensor setiap 50ms (20 Hz), cukup untuk aplikasi rata-rata
4. **Debugging**: Jika ada masalah, tambahkan print statement di Serial Monitor untuk debugging
5. **Loop Kontinu**: Program akan terus membaca sensor secara kontinu hingga Anda menekan reset atau lepas power

---

## Referensi

- **MPU6050 Datasheet**: <https://invensense.tdk.com/>
- **ESP32 Documentation**: <https://docs.espressif.com/projects/esp32-arduino/>
- **Arduino Wire Library**: <https://www.arduino.cc/en/reference/wire>

---

## Catatan Pengembang

Kode ini menggunakan:

- **Protocol I2C** untuk komunikasi dengan MPU6050
- **Accelerometer** untuk mengukur orientasi (Roll, Pitch)
- **Gyroscope** untuk mengukur kecepatan rotasi (GyroX, GyroY, GyroZ)
- **Kalibrasi offset gyro** untuk meningkatkan akurasi

Untuk pengembangan lebih lanjut, pertimbangkan:

- Menambahkan **Complementary Filter** atau **Kalman Filter** untuk data yang lebih akurat
- Menambahkan **SD Card** untuk logging data
- Menambahkan **LCD/OLED Display** untuk tampilan real-time
- Integrasi dengan **MQTT** untuk monitoring remote

---

**Selamat mencoba! Jika ada pertanyaan, silakan cek section Troubleshooting atau kontrol koneksi hardware.**
