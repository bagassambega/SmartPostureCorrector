#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define MPU6050_ADDR 0x68

#define PWR_MGMT_1 0x6B
#define ACCEL_XOUT_H 0x3B

#define SDA_PIN 21
#define SCL_PIN 22

#define CALIB_BUTTON_PIN 14

const char *WIFI_SSID = "Jenong Smart";
const char *WIFI_PASS = "jenong21";

const char *MQTT_SERVER = "192.168.1.10";
const uint16_t MQTT_PORT = 1883;
const char *MQTT_TOPIC = "sensors/mpu6050";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Variabel untuk timing pengiriman data ke MQTT setiap 200 ms
unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 200; // Interval pengiriman data dalam ms

int16_t accX, accY, accZ;
int16_t gyroX, gyroY, gyroZ;

float rawRoll = 0;
float rawPitch = 0;

float rollOffset = 0;
float pitchOffset = 0;

float roll = 0;
float pitch = 0;

float gyroOffsetX = 0;
float gyroOffsetY = 0;
float gyroOffsetZ = 0;

float gx = 0;
float gy = 0;
float gz = 0;

bool lastButtonState = HIGH;

void setup()
{
    Serial.begin(115200);

    Wire.begin(SDA_PIN, SCL_PIN);

    pinMode(CALIB_BUTTON_PIN, INPUT_PULLUP);

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(PWR_MGMT_1);
    Wire.write(0x00);
    Wire.endTransmission();

    delay(100);

    kalibrasiOrientasi();
    kalibrasiGyro();
}

void loop()
{
    reconnectMQTT();

    bool currentButtonState = digitalRead(CALIB_BUTTON_PIN);

    if (lastButtonState == LOW && currentButtonState == HIGH)
    {
        kalibrasiOrientasi();
        kalibrasiGyro();
    }

    lastButtonState = currentButtonState;

    bacaSensor();

    // Kirim data hanya jika interval waktu 200 ms sudah tercapai
    if (millis() - lastPublish >= PUBLISH_INTERVAL)
    {
        lastPublish = millis();
        publishData();
    }

    mqttClient.loop();

    delay(10); // Small delay untuk stabilitas loop
}

void bacaSensor()
{
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(ACCEL_XOUT_H);
    Wire.endTransmission(false);

    Wire.requestFrom(MPU6050_ADDR, 14, true);

    accX = Wire.read() << 8 | Wire.read();
    accY = Wire.read() << 8 | Wire.read();
    accZ = Wire.read() << 8 | Wire.read();

    Wire.read();
    Wire.read();

    gyroX = Wire.read() << 8 | Wire.read();
    gyroY = Wire.read() << 8 | Wire.read();
    gyroZ = Wire.read() << 8 | Wire.read();

    float ax = accX / 16384.0;
    float ay = accY / 16384.0;
    float az = accZ / 16384.0;

    rawRoll =
        atan2(ay, az) * 180.0 / PI;

    rawPitch =
        atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;

    roll = rawRoll - rollOffset;
    pitch = rawPitch - pitchOffset;

    gx = (gyroX - gyroOffsetX) / 131.0;
    gy = (gyroY - gyroOffsetY) / 131.0;
    gz = (gyroZ - gyroOffsetZ) / 131.0;
}

void publishData()
{
    if (!mqttClient.connected())
        return;

    char payload[512];

    unsigned long timestamp = millis();

    snprintf(
        payload,
        sizeof(payload),

        "{"
        "\"timestamp\":%lu,"
        "\"rawRoll\":%.2f,"
        "\"rawPitch\":%.2f,"
        "\"rollOffset\":%.2f,"
        "\"pitchOffset\":%.2f,"
        "\"roll\":%.2f,"
        "\"pitch\":%.2f,"
        "\"gyroOffsetX\":%.2f,"
        "\"gyroOffsetY\":%.2f,"
        "\"gyroOffsetZ\":%.2f,"
        "\"gx\":%.2f,"
        "\"gy\":%.2f,"
        "\"gz\":%.2f"
        "}",

        timestamp,

        rawRoll,
        rawPitch,

        rollOffset,
        pitchOffset,

        roll,
        pitch,

        gyroOffsetX,
        gyroOffsetY,
        gyroOffsetZ,

        gx,
        gy,
        gz);

    mqttClient.publish(MQTT_TOPIC, payload);

    Serial.println(payload);
}

void kalibrasiOrientasi()
{
    bacaSensor();

    rollOffset = rawRoll;
    pitchOffset = rawPitch;

    Serial.println("Orientation calibrated");
}

void kalibrasiGyro()
{
    float sumX = 0;
    float sumY = 0;
    float sumZ = 0;

    const int sample = 200;

    for (int i = 0; i < sample; i++)
    {
        bacaSensor();

        sumX += gyroX;
        sumY += gyroY;
        sumZ += gyroZ;

        delay(5);
    }

    gyroOffsetX = sumX / sample;
    gyroOffsetY = sumY / sample;
    gyroOffsetZ = sumZ / sample;

    Serial.println("Gyro calibrated");
}

void reconnectMQTT()
{
    if (mqttClient.connected())
        return;

    while (!mqttClient.connected())
    {
        String clientId = "ESP32_";
        clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

        mqttClient.connect(clientId.c_str());

        delay(1000);
    }
}

// Syntax:
// mosquitto_sub -h localhost -t sensors/mpu6050 | jq -r '
// [
// .timestamp,
// .rawRoll,
// .rawPitch,
// .rollOffset,
// .pitchOffset,
// .roll,
// .pitch,
// .gyroOffsetX,
// .gyroOffsetY,
// .gyroOffsetZ,
// .gx,
// .gy,
// .gz
// ] | @csv' >> posture_dataset1.csv 