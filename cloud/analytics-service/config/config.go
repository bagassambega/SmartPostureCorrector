package config

import (
	"os"
	"strconv"
	"strings"
)

type Config struct {
	HTTPAddr                string
	MQTTBroker              string
	MQTTSensorTopic         string
	MQTTClientID            string
	SensorDeviceID          string
	PostureBadRollDeg       float64
	PostureBadPitchDeg      float64
	InfluxURL               string
	InfluxToken             string
	InfluxOrg               string
	InfluxBucket            string
	InfluxMeasurementSensor string
	AlertThresholdSec       float64
	SmoothingWindow         int
}

func Load() Config {
	return Config{
		HTTPAddr:                envOrDefault("HTTP_ADDR", ":3000"),
		MQTTBroker:              envOrDefault("MQTT_BROKER", "tcp://localhost:1883"),
		MQTTSensorTopic:         envOrDefault("MQTT_SENSOR_TOPIC", "sensors/mpu6050"),
		MQTTClientID:            envOrDefault("MQTT_CLIENT_ID", "analytics-service"),
		SensorDeviceID:          envOrDefault("SENSOR_DEVICE_ID", "ESP32_MPU6050"),
		PostureBadRollDeg:       envPositiveFloatOrDefault("POSTURE_BAD_ROLL_DEG", 20),
		PostureBadPitchDeg:      envPositiveFloatOrDefault("POSTURE_BAD_PITCH_DEG", 20),
		InfluxURL:               envOrDefault("INFLUX_URL", "http://localhost:8086"),
		InfluxToken:             envOrDefault("INFLUX_TOKEN", "my-super-secret-auth-token"),
		InfluxOrg:               envOrDefault("INFLUX_ORG", "my-org"),
		InfluxBucket:            envOrDefault("INFLUX_BUCKET", "iot_data"),
		InfluxMeasurementSensor: envOrDefault("INFLUX_MEASUREMENT", "raw_mpu6050"),
		AlertThresholdSec:       30,
		SmoothingWindow:         5,
	}
}

func envOrDefault(key, fallback string) string {
	value := strings.TrimSpace(os.Getenv(key))
	if value == "" {
		return fallback
	}
	return value
}

func envPositiveFloatOrDefault(key string, fallback float64) float64 {
	value := strings.TrimSpace(os.Getenv(key))
	if value == "" {
		return fallback
	}
	parsed, err := strconv.ParseFloat(value, 64)
	if err != nil || parsed <= 0 {
		return fallback
	}
	return parsed
}
