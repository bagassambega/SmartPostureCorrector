#include <Wire.h>
#include <WiFi.h>
#include <Arduino.h>
#include <PubSubClient.h>

#define MPU6050_ADDR 0x68

#define PWR_MGMT_1 0x6B
#define ACCEL_XOUT_H 0x3B

#define SDA_PIN 21
#define SCL_PIN 22

#define CALIB_BUTTON_PIN 14

const char *WIFI_SSID = "A15";
const char *WIFI_PASS = "saynotoclankers";

const char *MQTT_SERVER = "10.29.236.51";
const uint16_t MQTT_PORT = 1883;

const char *MQTT_TOPIC = "sensors/posture";
const char *MQTT_CALIB_TOPIC = "sensors/calibrate";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 200;

// =========================
// RAW SENSOR VALUES
// =========================

int16_t rawAccX, rawAccY, rawAccZ;
int16_t rawGyroX, rawGyroY, rawGyroZ;

// =========================
// KEMIRINGAN
// =========================

float rawKemiringanX = 0;
float rawKemiringanY = 0;

float offsetKemiringanX = 0;
float offsetKemiringanY = 0;

float kemiringanX = 0;
float kemiringanY = 0;

// =========================
// GYRO OFFSET
// =========================

float gyroOffsetX = 0;
float gyroOffsetY = 0;
float gyroOffsetZ = 0;

// =========================
// KECEPATAN ROTASI
// =========================

float kecepatanRotasiX = 0;
float kecepatanRotasiY = 0;
float kecepatanRotasiZ = 0;

bool lastButtonState = HIGH;

void bacaSensor();
void publishData();
void kalibrasiOrientasi();
void kalibrasiGyro();
void reconnectMQTT();

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    String message;

    for (unsigned int i = 0; i < length; i++)
    {
        message += (char)payload[i];
    }

    Serial.print("MQTT Message [");
    Serial.print(topic);
    Serial.print("]: ");
    Serial.println(message);

    if (
        strcmp(topic, MQTT_CALIB_TOPIC) == 0 &&
        message == "CALIBRATE")
    {
        Serial.println("=================================");
        Serial.println("Remote calibration requested");
        Serial.println("=================================");

        kalibrasiOrientasi();
        kalibrasiGyro();
    }
}

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

    Serial.println("\nWiFi Connected");

    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(1024);

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

    bool currentButtonState =
        digitalRead(CALIB_BUTTON_PIN);

    if (lastButtonState == LOW &&
        currentButtonState == HIGH)
    {
        kalibrasiOrientasi();
        kalibrasiGyro();
    }

    lastButtonState = currentButtonState;

    bacaSensor();

    if (millis() - lastPublish >= PUBLISH_INTERVAL)
    {
        lastPublish = millis();

        publishData();
    }

    mqttClient.loop();

    delay(10);
}

void bacaSensor()
{
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(ACCEL_XOUT_H);
    Wire.endTransmission(false);

    Wire.requestFrom((uint16_t)MPU6050_ADDR, (size_t)14, true);

    rawAccX =
        Wire.read() << 8 | Wire.read();

    rawAccY =
        Wire.read() << 8 | Wire.read();

    rawAccZ =
        Wire.read() << 8 | Wire.read();

    Wire.read();
    Wire.read();

    rawGyroX =
        Wire.read() << 8 | Wire.read();

    rawGyroY =
        Wire.read() << 8 | Wire.read();

    rawGyroZ =
        Wire.read() << 8 | Wire.read();

    // =========================
    // CONVERT ACCEL
    // =========================

    float ax = rawAccX / 16384.0;
    float ay = rawAccY / 16384.0;
    float az = rawAccZ / 16384.0;

    // =========================
    // RAW KEMIRINGAN
    // =========================

    rawKemiringanX =
        atan2(ay, az) * 180.0 / PI;

    rawKemiringanY =
        atan2(
            -ax,
            sqrt(ay * ay + az * az)) *
        180.0 / PI;

    // =========================
    // FINAL KEMIRINGAN
    // =========================

    kemiringanX =
        rawKemiringanX -
        offsetKemiringanX;

    kemiringanY =
        rawKemiringanY -
        offsetKemiringanY;

    // =========================
    // GYRO ROTATION SPEED
    // =========================

    kecepatanRotasiX =
        (rawGyroX - gyroOffsetX) / 131.0;

    kecepatanRotasiY =
        (rawGyroY - gyroOffsetY) / 131.0;

    kecepatanRotasiZ =
        (rawGyroZ - gyroOffsetZ) / 131.0;
}

void publishData()
{
    if (!mqttClient.connected())
        return;

    char payload[1024];

    unsigned long timestamp = millis();

    int payloadLen = snprintf(
        payload,
        sizeof(payload),

        "{"

        "\"timestamp\":%lu,"

        "\"rawAccX\":%d,"
        "\"rawAccY\":%d,"
        "\"rawAccZ\":%d,"

        "\"rawGyroX\":%d,"
        "\"rawGyroY\":%d,"
        "\"rawGyroZ\":%d,"

        "\"rawKemiringanX\":%.2f,"
        "\"rawKemiringanY\":%.2f,"

        "\"offsetKemiringanX\":%.2f,"
        "\"offsetKemiringanY\":%.2f,"

        "\"kemiringanX\":%.2f,"
        "\"kemiringanY\":%.2f,"

        "\"gyroOffsetX\":%.2f,"
        "\"gyroOffsetY\":%.2f,"
        "\"gyroOffsetZ\":%.2f,"

        "\"kecepatanRotasiX\":%.2f,"
        "\"kecepatanRotasiY\":%.2f,"
        "\"kecepatanRotasiZ\":%.2f"

        "}",

        timestamp,

        rawAccX,
        rawAccY,
        rawAccZ,

        rawGyroX,
        rawGyroY,
        rawGyroZ,

        rawKemiringanX,
        rawKemiringanY,

        offsetKemiringanX,
        offsetKemiringanY,

        kemiringanX,
        kemiringanY,

        gyroOffsetX,
        gyroOffsetY,
        gyroOffsetZ,

        kecepatanRotasiX,
        kecepatanRotasiY,
        kecepatanRotasiZ);

    if (payloadLen < 0 || payloadLen >= (int)sizeof(payload))
    {
        Serial.println("Payload formatting error/truncated");
        return;
    }

    bool res = mqttClient.publish(MQTT_TOPIC, payload);

    if (!res)
    {
        Serial.print("Error sending, payload bytes: ");
        Serial.println(payloadLen);
    }

    Serial.println(payload);
}

void kalibrasiOrientasi()
{
    bacaSensor();

    offsetKemiringanX =
        rawKemiringanX;

    offsetKemiringanY =
        rawKemiringanY;

    Serial.println(
        "Orientation calibrated");
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

        sumX += rawGyroX;
        sumY += rawGyroY;
        sumZ += rawGyroZ;

        // Keep MQTT and WiFi alive during calibration
        mqttClient.loop();
        yield();

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

        clientId +=
            String(
                (uint32_t)ESP.getEfuseMac(),
                HEX);

        if (mqttClient.connect(clientId.c_str()))
        {
            Serial.println("MQTT Connected");

            mqttClient.subscribe(
                MQTT_CALIB_TOPIC);

            Serial.print(
                "Subscribed: ");

            Serial.println(
                MQTT_CALIB_TOPIC);
        }
        else
        {
            Serial.print(
                "MQTT connect failed, state=");

            Serial.println(
                mqttClient.state());

            delay(1000);
        }
    }
}