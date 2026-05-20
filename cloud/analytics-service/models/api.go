package models

import "time"

type DeviceCurrentResponse struct {
	DeviceID     string    `json:"device_id"`
	Posture      string    `json:"posture"`
	PostureCode  int       `json:"posture_code"`
	IsBadPosture bool      `json:"is_bad_posture"`
	BadStreakSec float64   `json:"bad_streak_sec"`
	AlertActive  bool      `json:"alert_active"`
	LastSeen     time.Time `json:"last_seen"`
}

type DeviceSummaryResponse struct {
	BadPosturePct   float64 `json:"bad_posture_pct"`
	BadDurationSec  float64 `json:"bad_duration_sec"`
	GoodDurationSec float64 `json:"good_duration_sec"`
	DominantPosture string  `json:"dominant_posture"`
	TotalSamples    int     `json:"total_samples"`
}

type TimelineEntry struct {
	Time         time.Time `json:"time"`
	Posture      string    `json:"posture"`
	IsBadPosture bool      `json:"is_bad_posture"`
}

type AnglePoint struct {
	Time  time.Time `json:"time"`
	Roll  float64   `json:"roll"`
	Pitch float64   `json:"pitch"`
}

type AnglesResponse struct {
	DeviceID string       `json:"device_id"`
	Range    string       `json:"range"`
	Data     []AnglePoint `json:"data"`
}

type DistributionEntry struct {
	Posture string  `json:"posture"`
	Count   int     `json:"count"`
	Pct     float64 `json:"pct"`
}

type DistributionResponse struct {
	DeviceID string              `json:"device_id"`
	Range    string              `json:"range"`
	Data     []DistributionEntry `json:"data"`
}

type SensorSeriesPoint struct {
	Time  string  `json:"time"`
	Value float64 `json:"value"`
}

type SensorSeriesResponse struct {
	Field string              `json:"field"`
	Range string              `json:"range"`
	Data  []SensorSeriesPoint `json:"data"`
}

type ErrorResponse struct {
	Error string `json:"error"`
}
