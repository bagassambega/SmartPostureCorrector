#include <Wire.h>

// Alamat I2C MPU6050 (0x68 jika AD0 ke GND)
#define MPU6050_ADDR 0x68

// Register MPU6050
#define PWR_MGMT_1   0x6B
#define ACCEL_XOUT_H 0x3B
#define GYRO_XOUT_H  0x43

// Variabel untuk menyimpan offset gyro
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;

// Variabel untuk data mentah
int16_t accX, accY, accZ;
int16_t gyroX, gyroY, gyroZ;

// Variabel untuk hitungan waktu (opsional untuk integrasi yaw)
unsigned long lastTime = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); // SDA=21, SCL=22

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

void loop() {
  bacaSensorRaw();

  // Konversi nilai accelerometer ke g (skala 16384 untuk ±2g)
  float ax = accX / 16384.0;
  float ay = accY / 16384.0;
  float az = accZ / 16384.0;

  // Hitung roll (kemiringan sumbu X) dan pitch (kemiringan sumbu Y)
  float roll  = atan2(ay, az) * 180.0 / PI;
  float pitch = atan2(-ax, sqrt(ay*ay + az*az)) * 180.0 / PI;

  // Konversi gyro ke derajat per detik (skala 131 untuk ±250°/s)
  float gx = (gyroX - gyroOffsetX) / 131.0;
  float gy = (gyroY - gyroOffsetY) / 131.0;
  float gz = (gyroZ - gyroOffsetZ) / 131.0;

  // Kirim data ke serial monitor
  Serial.print("Roll: "); Serial.print(roll, 2);
  Serial.print("\tPitch: "); Serial.print(pitch, 2);
  Serial.print("\tGyro X: "); Serial.print(gx, 2);
  Serial.print("\tGyro Y: "); Serial.print(gy, 2);
  Serial.print("\tGyro Z: "); Serial.println(gz, 2);

  delay(50); // baca setiap 50 ms
}

// Fungsi baca data mentah dari MPU6050
void bacaSensorRaw() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(ACCEL_XOUT_H);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 14, true); // 14 byte: accel (6) + suhu (2) + gyro (6)

  accX = Wire.read() << 8 | Wire.read();
  accY = Wire.read() << 8 | Wire.read();
  accZ = Wire.read() << 8 | Wire.read();
  Wire.read(); Wire.read(); // baca suhu (abaikan)
  gyroX = Wire.read() << 8 | Wire.read();
  gyroY = Wire.read() << 8 | Wire.read();
  gyroZ = Wire.read() << 8 | Wire.read();
}

// Kalibrasi gyro dengan merata-rata 200 sampel saat sensor diam
void kalibrasiGyro() {
  float sumX = 0, sumY = 0, sumZ = 0;
  int sample = 200;
  Serial.println("Kalibrasi gyro... jangan gerakkan sensor!");
  for (int i = 0; i < sample; i++) {
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