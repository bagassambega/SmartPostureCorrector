package storage

import (
	"context"
	"fmt"
	"strconv"
	"time"

	influxdb2 "github.com/influxdata/influxdb-client-go/v2"
	"github.com/influxdata/influxdb-client-go/v2/api"
	"github.com/influxdata/influxdb-client-go/v2/api/write"

	"analytics-service/aggregator"
	"analytics-service/config"
	"analytics-service/models"
)

const (
	measurementPostureEvents  = "posture_events"
	measurementPostureSummary = "posture_summary"
)

type Store struct {
	cfg      config.Config
	writeAPI api.WriteAPIBlocking
	queryAPI api.QueryAPI
}

func New(cfg config.Config, client influxdb2.Client) *Store {
	return &Store{
		cfg:      cfg,
		writeAPI: client.WriteAPIBlocking(cfg.InfluxOrg, cfg.InfluxBucket),
		queryAPI: client.QueryAPI(cfg.InfluxOrg),
	}
}

func (s *Store) WritePostureEvent(payload models.PosturePayload, serverTime time.Time) error {
	isBadStr := "false"
	if payload.IsBadPosture {
		isBadStr = "true"
	}

	fields := map[string]interface{}{
		"posture_code": payload.PostureCode,
		"device_ts":    float64(payload.Timestamp),
		"roll":         payload.Roll,
		"pitch":        payload.Pitch,
	}

	tags := map[string]string{
		"device_id":     payload.DeviceID,
		"posture_label": payload.Posture,
		"is_bad":        isBadStr,
	}

	point := write.NewPoint(measurementPostureEvents, tags, fields, serverTime)
	return s.writeAPI.WritePoint(context.Background(), point)
}

func (s *Store) WritePostureSummary(deviceID string, summary aggregator.MinuteSummary, serverTime time.Time) error {
	fields := map[string]interface{}{
		"bad_posture_pct":       summary.BadPosturePct,
		"bad_duration_sec":      summary.BadDurationSec,
		"good_duration_sec":     summary.GoodDurationSec,
		"total_samples":         summary.TotalSamples,
		"bad_samples":           summary.BadSamples,
		"dominant_posture_code": summary.DominantPostureCode,
		"alert_active":          summary.AlertActive,
	}

	tags := map[string]string{
		"device_id": deviceID,
	}

	point := write.NewPoint(measurementPostureSummary, tags, fields, serverTime)
	return s.writeAPI.WritePoint(context.Background(), point)
}

func (s *Store) WriteSensor(payload models.SensorPayload, serverTime time.Time) error {
	fields := map[string]interface{}{
		"roll":      payload.Roll,
		"pitch":     payload.Pitch,
		"device_ts": float64(payload.Timestamp),
	}

	tags := map[string]string{
		"device_id": s.cfg.SensorDeviceID,
	}

	point := write.NewPoint(s.cfg.InfluxMeasurementSensor, tags, fields, serverTime)
	return s.writeAPI.WritePoint(context.Background(), point)
}

func (s *Store) ListDevices(ctx context.Context) ([]string, error) {
	flux := fmt.Sprintf(`import "influxdata/influxdb/schema"
schema.tagValues(
  bucket: %q,
  tag: "device_id",
  predicate: (r) => r._measurement == %q
)`, s.cfg.InfluxBucket, measurementPostureEvents)

	result, err := s.queryAPI.Query(ctx, flux)
	if err != nil {
		return nil, err
	}
	defer result.Close()

	seen := make(map[string]struct{})
	var devices []string
	for result.Next() {
		v := result.Record().Value()
		if v == nil {
			continue
		}
		id := fmt.Sprint(v)
		if _, ok := seen[id]; ok {
			continue
		}
		seen[id] = struct{}{}
		devices = append(devices, id)
	}
	if result.Err() != nil {
		return nil, result.Err()
	}
	return devices, nil
}

func (s *Store) QueryTimeline(ctx context.Context, deviceID, timeRange string) ([]models.TimelineEntry, error) {
	flux := fmt.Sprintf(`from(bucket: %q)
  |> range(start: %s)
  |> filter(fn: (r) => r._measurement == %q and r.device_id == %q and r._field == "posture_code")
  |> keep(columns: ["_time", "_value", "posture_label", "is_bad"])
  |> sort(columns: ["_time"])`, s.cfg.InfluxBucket, timeRange, measurementPostureEvents, deviceID)

	result, err := s.queryAPI.Query(ctx, flux)
	if err != nil {
		return nil, err
	}
	defer result.Close()

	entries := make([]models.TimelineEntry, 0, 256)
	for result.Next() {
		rec := result.Record()
		posture := rec.ValueByKey("posture_label")
		isBadStr := rec.ValueByKey("is_bad")
		postureStr := fmt.Sprint(posture)
		isBad := fmt.Sprint(isBadStr) == "true"

		entries = append(entries, models.TimelineEntry{
			Time:         rec.Time(),
			Posture:      postureStr,
			IsBadPosture: isBad,
		})
	}
	return entries, result.Err()
}

func (s *Store) QueryAngles(ctx context.Context, deviceID, timeRange string) ([]models.AnglePoint, error) {
	flux := fmt.Sprintf(`from(bucket: %q)
  |> range(start: %s)
  |> filter(fn: (r) => r._measurement == %q and r.device_id == %q)
  |> filter(fn: (r) => r._field == "roll" or r._field == "pitch")
  |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
  |> sort(columns: ["_time"])`, s.cfg.InfluxBucket, timeRange, measurementPostureEvents, deviceID)

	result, err := s.queryAPI.Query(ctx, flux)
	if err != nil {
		return nil, err
	}
	defer result.Close()

	points := make([]models.AnglePoint, 0, 256)
	for result.Next() {
		rec := result.Record()
		roll := toFloat64(rec.ValueByKey("roll"))
		pitch := toFloat64(rec.ValueByKey("pitch"))
		points = append(points, models.AnglePoint{
			Time:  rec.Time(),
			Roll:  roll,
			Pitch: pitch,
		})
	}
	return points, result.Err()
}

func (s *Store) QuerySummary(ctx context.Context, deviceID, timeRange string) (models.DeviceSummaryResponse, error) {
	// Prefer aggregated summary points when available.
	summaryFlux := fmt.Sprintf(`from(bucket: %q)
  |> range(start: %s)
  |> filter(fn: (r) => r._measurement == %q and r.device_id == %q)
  |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")`, s.cfg.InfluxBucket, timeRange, measurementPostureSummary, deviceID)

	result, err := s.queryAPI.Query(ctx, summaryFlux)
	if err != nil {
		return models.DeviceSummaryResponse{}, err
	}

	var (
		totalSamples  int
		badSamples    int
		badDur        float64
		goodDur       float64
		dominantCode  int
		dominantCount int
		codeCounts    = make(map[int]int)
	)

	for result.Next() {
		rec := result.Record()
		totalSamples += int(toFloat64(rec.ValueByKey("total_samples")))
		badSamples += int(toFloat64(rec.ValueByKey("bad_samples")))
		badDur += toFloat64(rec.ValueByKey("bad_duration_sec"))
		goodDur += toFloat64(rec.ValueByKey("good_duration_sec"))
		dc := int(toFloat64(rec.ValueByKey("dominant_posture_code")))
		ts := int(toFloat64(rec.ValueByKey("total_samples")))
		codeCounts[dc] += ts
		if codeCounts[dc] > dominantCount {
			dominantCount = codeCounts[dc]
			dominantCode = dc
		}
	}
	result.Close()
	if result.Err() != nil {
		return models.DeviceSummaryResponse{}, result.Err()
	}

	if totalSamples == 0 {
		return s.querySummaryFromEvents(ctx, deviceID, timeRange)
	}

	pct := 0.0
	if totalSamples > 0 {
		pct = float64(badSamples) / float64(totalSamples) * 100
	}

	return models.DeviceSummaryResponse{
		BadPosturePct:   pct,
		BadDurationSec:  badDur,
		GoodDurationSec: goodDur,
		DominantPosture: models.PostureFromCode(dominantCode),
		TotalSamples:    totalSamples,
	}, nil
}

func (s *Store) querySummaryFromEvents(ctx context.Context, deviceID, timeRange string) (models.DeviceSummaryResponse, error) {
	flux := fmt.Sprintf(`from(bucket: %q)
  |> range(start: %s)
  |> filter(fn: (r) => r._measurement == %q and r.device_id == %q and r._field == "posture_code")
  |> keep(columns: ["_time", "_value", "is_bad", "posture_label"])`, s.cfg.InfluxBucket, timeRange, measurementPostureEvents, deviceID)

	result, err := s.queryAPI.Query(ctx, flux)
	if err != nil {
		return models.DeviceSummaryResponse{}, err
	}
	defer result.Close()

	var (
		totalSamples int
		badSamples   int
		badDur       float64
		goodDur      float64
		codeCounts   = make(map[string]int)
		lastTime     time.Time
		lastBad      bool
		hasLast      bool
	)

	for result.Next() {
		rec := result.Record()
		totalSamples++
		isBad := fmt.Sprint(rec.ValueByKey("is_bad")) == "true"
		posture := fmt.Sprint(rec.ValueByKey("posture_label"))
		codeCounts[posture]++

		if isBad {
			badSamples++
		}

		t := rec.Time()
		if hasLast {
			delta := t.Sub(lastTime).Seconds()
			if delta > 0 && delta <= 60 {
				if lastBad {
					badDur += delta
				} else {
					goodDur += delta
				}
			}
		}
		lastTime = t
		lastBad = isBad
		hasLast = true
	}
	if result.Err() != nil {
		return models.DeviceSummaryResponse{}, result.Err()
	}

	dominant := ""
	best := -1
	for p, c := range codeCounts {
		if c > best {
			best = c
			dominant = p
		}
	}

	pct := 0.0
	if totalSamples > 0 {
		pct = float64(badSamples) / float64(totalSamples) * 100
	}

	return models.DeviceSummaryResponse{
		BadPosturePct:   pct,
		BadDurationSec:  badDur,
		GoodDurationSec: goodDur,
		DominantPosture: dominant,
		TotalSamples:    totalSamples,
	}, nil
}

func (s *Store) QueryDistribution(ctx context.Context, deviceID, timeRange string) ([]models.DistributionEntry, error) {
	flux := fmt.Sprintf(`from(bucket: %q)
  |> range(start: %s)
  |> filter(fn: (r) => r._measurement == %q and r.device_id == %q and r._field == "posture_code")
  |> group(columns: ["posture_label"])
  |> count()`, s.cfg.InfluxBucket, timeRange, measurementPostureEvents, deviceID)

	result, err := s.queryAPI.Query(ctx, flux)
	if err != nil {
		return nil, err
	}
	defer result.Close()

	counts := make(map[string]int)
	total := 0
	for result.Next() {
		posture := fmt.Sprint(result.Record().ValueByKey("posture_label"))
		count := int(toFloat64(result.Record().Value()))
		counts[posture] = count
		total += count
	}
	if result.Err() != nil {
		return nil, result.Err()
	}

	entries := make([]models.DistributionEntry, 0, len(models.ValidPostures))
	for posture := range models.ValidPostures {
		count := counts[posture]
		pct := 0.0
		if total > 0 {
			pct = float64(count) / float64(total) * 100
		}
		entries = append(entries, models.DistributionEntry{
			Posture: posture,
			Count:   count,
			Pct:     pct,
		})
	}
	return entries, nil
}

func (s *Store) QuerySensorSeries(ctx context.Context, field, timeRange, window string) ([]models.SensorSeriesPoint, error) {
	flux := fmt.Sprintf(`from(bucket: %q)
  |> range(start: -%s)
  |> filter(fn: (r) => r._measurement == %q and r._field == %q)
  |> aggregateWindow(every: %s, fn: mean, createEmpty: false)
  |> yield(name: "mean")`, s.cfg.InfluxBucket, timeRange, s.cfg.InfluxMeasurementSensor, field, window)

	result, err := s.queryAPI.Query(ctx, flux)
	if err != nil {
		return nil, err
	}
	defer result.Close()

	series := make([]models.SensorSeriesPoint, 0, 256)
	for result.Next() {
		value, ok := result.Record().Value().(float64)
		if !ok {
			value = toFloat64(result.Record().Value())
		}
		series = append(series, models.SensorSeriesPoint{
			Time:  result.Record().Time().Format(time.RFC3339),
			Value: value,
		})
	}
	return series, result.Err()
}

func ParseRangeParam(rangeParam string) (string, error) {
	switch rangeParam {
	case "", "today":
		now := time.Now().UTC()
		start := time.Date(now.Year(), now.Month(), now.Day(), 0, 0, 0, 0, time.UTC)
		return fmt.Sprintf("time(v: %q)", start.Format(time.RFC3339)), nil
	case "15m", "1h", "6h", "24h":
		return fmt.Sprintf("-%s", rangeParam), nil
	default:
		return "", fmt.Errorf("invalid range")
	}
}

func toFloat64(v interface{}) float64 {
	switch n := v.(type) {
	case float64:
		return n
	case float32:
		return float64(n)
	case int64:
		return float64(n)
	case int:
		return float64(n)
	case uint64:
		return float64(n)
	case string:
		f, _ := strconv.ParseFloat(n, 64)
		return f
	default:
		return 0
	}
}
