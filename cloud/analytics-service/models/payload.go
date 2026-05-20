package models

import (
	"fmt"
	"math"
	"strings"
)

const (
	PostureDudukBungkuk = "duduk_bungkuk"
	PostureDudukTegak   = "duduk_tegak"
)

var ValidPostures = map[string]int{
	PostureDudukBungkuk: 2,
	PostureDudukTegak:   3,
}

func IsBadPosture(posture string) bool {
	return posture == PostureDudukBungkuk
}

func PostureFromCode(code int) string {
	for label, c := range ValidPostures {
		if c == code {
			return label
		}
	}
	return ""
}

// PosturePayload is the internal posture event derived from the ESP32 roll/pitch payload.
type PosturePayload struct {
	DeviceID     string  `json:"device_id"`
	Timestamp    uint64  `json:"timestamp"`
	Posture      string  `json:"posture"`
	PostureCode  int     `json:"posture_code"`
	IsBadPosture bool    `json:"is_bad_posture"`
	Roll         float64 `json:"roll"`
	Pitch        float64 `json:"pitch"`
}

func (p *PosturePayload) Validate() error {
	if strings.TrimSpace(p.DeviceID) == "" {
		return fmt.Errorf("device_id is required")
	}
	if p.Timestamp == 0 {
		return fmt.Errorf("timestamp is required")
	}
	if strings.TrimSpace(p.Posture) == "" {
		return fmt.Errorf("posture is required")
	}
	expectedCode, ok := ValidPostures[p.Posture]
	if !ok {
		return fmt.Errorf("invalid posture label: %q", p.Posture)
	}
	if p.PostureCode != expectedCode {
		return fmt.Errorf("posture_code %d does not match posture %q (expected %d)", p.PostureCode, p.Posture, expectedCode)
	}
	expectedBad := IsBadPosture(p.Posture)
	if p.IsBadPosture != expectedBad {
		return fmt.Errorf("is_bad_posture=%v inconsistent with posture %q", p.IsBadPosture, p.Posture)
	}
	if !isFinite(p.Roll) || !isFinite(p.Pitch) {
		return fmt.Errorf("sensor values must be finite")
	}
	return nil
}

// SensorPayload matches hardware/sensor/sensor.ino on sensors/mpu6050.
type SensorPayload struct {
	Timestamp uint64  `json:"timestamp"`
	Roll      float64 `json:"roll"`
	Pitch     float64 `json:"pitch"`
}

func (p *SensorPayload) Validate() error {
	if p.Timestamp == 0 {
		return fmt.Errorf("timestamp is required")
	}
	if !isFinite(p.Roll) || !isFinite(p.Pitch) {
		return fmt.Errorf("sensor values must be finite")
	}
	return nil
}

func (p SensorPayload) ToPosturePayload(deviceID string, badRollDeg, badPitchDeg float64) PosturePayload {
	isBad := math.Abs(p.Roll) >= badRollDeg || math.Abs(p.Pitch) >= badPitchDeg
	posture := PostureDudukTegak
	if isBad {
		posture = PostureDudukBungkuk
	}

	return PosturePayload{
		DeviceID:     strings.TrimSpace(deviceID),
		Timestamp:    p.Timestamp,
		Posture:      posture,
		PostureCode:  ValidPostures[posture],
		IsBadPosture: isBad,
		Roll:         p.Roll,
		Pitch:        p.Pitch,
	}
}

func isFinite(v float64) bool {
	return !math.IsNaN(v) && !math.IsInf(v, 0)
}
