package aggregator

import (
	"testing"
	"time"

	"analytics-service/models"
)

func TestMajorityVote(t *testing.T) {
	codes := []int{2, 2, 3, 2, 3}
	got := majorityVote(codes)
	if got != 2 {
		t.Fatalf("expected 2, got %d", got)
	}
}

func TestBadPostureAlert(t *testing.T) {
	agg := New(30, 5, nil)
	deviceID := "test-device"
	payload := models.PosturePayload{
		DeviceID:     deviceID,
		Timestamp:    1000,
		Posture:      models.PostureDudukBungkuk,
		PostureCode:  2,
		IsBadPosture: true,
		Roll:         -1,
		Pitch:        -5,
	}

	start := time.Now().UTC()
	for i := 0; i < 160; i++ {
		payload.Timestamp = uint64(1000 + i*200)
		current := agg.Process(payload, start.Add(time.Duration(i)*200*time.Millisecond))
		if i >= 150 && !current.AlertActive {
			t.Fatalf("expected alert active after 30s bad posture at sample %d", i)
		}
	}
}

func TestGoodPostureResetsStreak(t *testing.T) {
	agg := New(30, 5, nil)
	bad := models.PosturePayload{
		DeviceID: deviceID("a"), Timestamp: 1,
		Posture: models.PostureDudukBungkuk, PostureCode: 2, IsBadPosture: true,
	}
	good := models.PosturePayload{
		DeviceID: deviceID("a"), Timestamp: 2,
		Posture: models.PostureDudukTegak, PostureCode: 3, IsBadPosture: false,
	}

	now := time.Now().UTC()
	agg.Process(bad, now)
	current := agg.Process(good, now.Add(time.Second))
	if current.IsBadPosture || current.BadStreakSec > 0 || current.AlertActive {
		t.Fatalf("expected streak reset after good posture")
	}
}

func TestSensorPayloadToPosturePayload(t *testing.T) {
	raw := models.SensorPayload{
		Timestamp: 1000,
		Roll:      -24.5,
		Pitch:     5.1,
	}

	posture := raw.ToPosturePayload("ESP32_MPU6050", 20, 20)
	if posture.DeviceID != "ESP32_MPU6050" {
		t.Fatalf("expected device id copied, got %q", posture.DeviceID)
	}
	if posture.Posture != models.PostureDudukBungkuk || posture.PostureCode != 2 || !posture.IsBadPosture {
		t.Fatalf("expected bad sitting posture, got %+v", posture)
	}
	if posture.Roll != raw.Roll || posture.Pitch != raw.Pitch {
		t.Fatalf("expected roll/pitch copied to posture event, got %+v", posture)
	}
}

func deviceID(s string) string { return s }
