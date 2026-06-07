package models

import (
	"fmt"
	"math"
	"strings"
)

// Posture label constants – must match the label encoding used during model training:
//   {'berdiri_bungkuk': 0, 'berdiri_tegak': 1, 'duduk_bungkuk': 2, 'duduk_tegak': 3}
const (
	PostureBerdiirBungkuk = "berdiri_bungkuk" // class 0 – bad
	PostureBerdiriTegak   = "berdiri_tegak"   // class 1 – good
	PostureDudukBungkuk   = "duduk_bungkuk"   // class 2 – bad
	PostureDudukTegak     = "duduk_tegak"     // class 3 – good
)

// ValidPostures maps every posture label string to its integer class code.
// Both "bungkuk" variants (0, 2) are bad postures; both "tegak" variants (1, 3) are good.
var ValidPostures = map[string]int{
	PostureBerdiirBungkuk: 0,
	PostureBerdiriTegak:   1,
	PostureDudukBungkuk:   2,
	PostureDudukTegak:     3,
}

// IsBadPosture returns true for any "bungkuk" (slouched) posture class.
func IsBadPosture(posture string) bool {
	return posture == PostureBerdiirBungkuk || posture == PostureDudukBungkuk
}

// PostureFromCode resolves a numeric RF class output to its label string.
// Returns empty string if the code is not in the ValidPostures map.
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

// SensorPayload matches the JSON published by the ESP32 firmware on sensors/mpu6050:
//   {"timestamp":<ms>, "roll":<deg>, "pitch":<deg>, "class":<0-3>}
// The "class" field carries the on-device Random Forest prediction.
type SensorPayload struct {
	Timestamp uint64  `json:"timestamp"`
	Roll      float64 `json:"roll"`
	Pitch     float64 `json:"pitch"`
	Class     int     `json:"class"` // emlearn RF output: 0=berdiri_bungkuk 1=berdiri_tegak 2=duduk_bungkuk 3=duduk_tegak
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

// ToPosturePayload converts a raw sensor reading into a classified posture event.
// Priority order:
//  1. Trust the edge AI "class" field from the ESP32 Random Forest (primary).
//  2. If the class code is unrecognised (e.g. future model version), fall back
//     to the server-side angle threshold classification.
func (p SensorPayload) ToPosturePayload(deviceID string, badRollDeg, badPitchDeg float64) PosturePayload {
	// Attempt to resolve posture from the on-device RF class output.
	posture := PostureFromCode(p.Class)

	if posture == "" {
		// Class is not in ValidPostures – degrade gracefully to angle-threshold fallback.
		isBadAngle := math.Abs(p.Roll) >= badRollDeg || math.Abs(p.Pitch) >= badPitchDeg
		if isBadAngle {
			posture = PostureDudukBungkuk
		} else {
			posture = PostureDudukTegak
		}
	}

	isBad := IsBadPosture(posture)
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
