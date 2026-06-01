package api

import (
	"context"
	"encoding/json"
	"io/fs"
	"log"
	"net/http"
	"slices"
	"strings"
	"time"

	paho "github.com/eclipse/paho.mqtt.golang"

	"analytics-service/aggregator"
	"analytics-service/config"
	"analytics-service/models"
	"analytics-service/storage"
)

type Server struct {
	cfg        config.Config
	store      *storage.Store
	aggregator *aggregator.Aggregator
	mqttClient paho.Client
	staticFS   fs.FS
}

func NewServer(cfg config.Config, store *storage.Store, agg *aggregator.Aggregator, mqttClient paho.Client, staticFS fs.FS) *Server {
	return &Server{cfg: cfg, store: store, aggregator: agg, mqttClient: mqttClient, staticFS: staticFS}
}

func (s *Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", s.healthz)
	mux.HandleFunc("GET /api/devices", s.listDevices)
	mux.HandleFunc("GET /api/devices/{device_id}/current", s.deviceCurrent)
	mux.HandleFunc("GET /api/devices/{device_id}/summary", s.deviceSummary)
	mux.HandleFunc("GET /api/devices/{device_id}/timeline", s.deviceTimeline)
	mux.HandleFunc("GET /api/devices/{device_id}/angles", s.deviceAngles)
	mux.HandleFunc("GET /api/devices/{device_id}/distribution", s.deviceDistribution)
	mux.HandleFunc("POST /api/devices/{device_id}/calibrate", s.deviceCalibrate)
	mux.HandleFunc("GET /api/series", s.sensorSeries)
	mux.Handle("/", http.FileServer(http.FS(s.staticFS)))
	return loggingMiddleware(mux)
}

func (s *Server) healthz(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (s *Server) listDevices(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 5*time.Second)
	defer cancel()

	influxDevices, err := s.store.ListDevices(ctx)
	if err != nil {
		log.Printf("list devices from influx failed: %v", err)
	}

	memDevices := s.aggregator.ListDevices()
	seen := make(map[string]struct{})
	var devices []string
	for _, id := range append(influxDevices, memDevices...) {
		if id == "" {
			continue
		}
		if _, ok := seen[id]; ok {
			continue
		}
		seen[id] = struct{}{}
		devices = append(devices, id)
	}
	slices.Sort(devices)
	writeJSON(w, http.StatusOK, devices)
}

func (s *Server) deviceCurrent(w http.ResponseWriter, r *http.Request) {
	deviceID := r.PathValue("device_id")
	if deviceID == "" {
		writeJSON(w, http.StatusBadRequest, models.ErrorResponse{Error: "device_id required"})
		return
	}

	current, ok := s.aggregator.GetCurrent(deviceID)
	if !ok {
		writeJSON(w, http.StatusNotFound, models.ErrorResponse{Error: "device not found or no data yet"})
		return
	}
	writeJSON(w, http.StatusOK, current)
}

func (s *Server) deviceSummary(w http.ResponseWriter, r *http.Request) {
	deviceID := r.PathValue("device_id")
	rangeParam := strings.TrimSpace(r.URL.Query().Get("range"))
	if rangeParam == "" {
		rangeParam = "today"
	}

	timeRange, err := storage.ParseRangeParam(rangeParam)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, models.ErrorResponse{Error: err.Error()})
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 8*time.Second)
	defer cancel()

	summary, err := s.store.QuerySummary(ctx, deviceID, timeRange)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, models.ErrorResponse{Error: err.Error()})
		return
	}
	if summary.TotalSamples == 0 {
		if memSummary, ok := s.aggregator.GetInMemorySummary(deviceID); ok {
			writeJSON(w, http.StatusOK, memSummary)
			return
		}
	}
	writeJSON(w, http.StatusOK, summary)
}

func (s *Server) deviceTimeline(w http.ResponseWriter, r *http.Request) {
	deviceID := r.PathValue("device_id")
	rangeParam := strings.TrimSpace(r.URL.Query().Get("range"))
	if rangeParam == "" {
		rangeParam = "today"
	}

	timeRange, err := storage.ParseRangeParam(rangeParam)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, models.ErrorResponse{Error: err.Error()})
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 8*time.Second)
	defer cancel()

	entries, err := s.store.QueryTimeline(ctx, deviceID, timeRange)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, models.ErrorResponse{Error: err.Error()})
		return
	}
	writeJSON(w, http.StatusOK, entries)
}

func (s *Server) deviceAngles(w http.ResponseWriter, r *http.Request) {
	deviceID := r.PathValue("device_id")
	rangeParam := strings.TrimSpace(r.URL.Query().Get("range"))
	if rangeParam == "" {
		rangeParam = "1h"
	}

	timeRange, err := storage.ParseRangeParam(rangeParam)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, models.ErrorResponse{Error: err.Error()})
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 8*time.Second)
	defer cancel()

	points, err := s.store.QueryAngles(ctx, deviceID, timeRange)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, models.ErrorResponse{Error: err.Error()})
		return
	}

	writeJSON(w, http.StatusOK, models.AnglesResponse{
		DeviceID: deviceID,
		Range:    rangeParam,
		Data:     points,
	})
}

func (s *Server) deviceDistribution(w http.ResponseWriter, r *http.Request) {
	deviceID := r.PathValue("device_id")
	rangeParam := strings.TrimSpace(r.URL.Query().Get("range"))
	if rangeParam == "" {
		rangeParam = "today"
	}

	timeRange, err := storage.ParseRangeParam(rangeParam)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, models.ErrorResponse{Error: err.Error()})
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 8*time.Second)
	defer cancel()

	data, err := s.store.QueryDistribution(ctx, deviceID, timeRange)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, models.ErrorResponse{Error: err.Error()})
		return
	}

	writeJSON(w, http.StatusOK, models.DistributionResponse{
		DeviceID: deviceID,
		Range:    rangeParam,
		Data:     data,
	})
}

func (s *Server) sensorSeries(w http.ResponseWriter, r *http.Request) {
	field := strings.ToLower(strings.TrimSpace(r.URL.Query().Get("field")))
	if field == "" {
		field = "roll"
	}
	allowedFields := map[string]bool{"roll": true, "pitch": true}
	if !allowedFields[field] {
		writeJSON(w, http.StatusBadRequest, models.ErrorResponse{Error: "invalid field"})
		return
	}

	timeRange := strings.TrimSpace(r.URL.Query().Get("range"))
	if timeRange == "" {
		timeRange = "1h"
	}
	allowedRanges := map[string]bool{"15m": true, "1h": true, "6h": true, "24h": true}
	if !allowedRanges[timeRange] {
		writeJSON(w, http.StatusBadRequest, models.ErrorResponse{Error: "invalid range"})
		return
	}

	window := strings.TrimSpace(r.URL.Query().Get("window"))
	if window == "" {
		window = "2s"
	}

	ctx, cancel := context.WithTimeout(r.Context(), 8*time.Second)
	defer cancel()

	data, err := s.store.QuerySensorSeries(ctx, field, timeRange, window)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, models.ErrorResponse{Error: err.Error()})
		return
	}

	writeJSON(w, http.StatusOK, models.SensorSeriesResponse{
		Field: field,
		Range: timeRange,
		Data:  data,
	})
}

func loggingMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		next.ServeHTTP(w, r)
		log.Printf("%s %s (%s)", r.Method, r.URL.Path, time.Since(start).Round(time.Millisecond))
	})
}

func writeJSON(w http.ResponseWriter, status int, payload interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(payload)
}

func (s *Server) deviceCalibrate(w http.ResponseWriter, r *http.Request) {
	deviceID := r.PathValue("device_id")
	if deviceID == "" {
		writeJSON(w, http.StatusBadRequest, models.ErrorResponse{Error: "device_id is required"})
		return
	}

	// Publish "CALIBRATE" message to "sensors/calibrate"
	// QoS = 1 ensures delivery, Retained = false is extremely important to prevent double calibration loop on restart.
	token := s.mqttClient.Publish("sensors/calibrate", 1, false, "CALIBRATE")
	token.Wait()
	if token.Error() != nil {
		log.Printf("failed to publish calibration command to MQTT for device %s: %v", deviceID, token.Error())
		writeJSON(w, http.StatusInternalServerError, models.ErrorResponse{Error: "failed to dispatch calibration command"})
		return
	}

	log.Printf("Successfully published calibration command to MQTT for device %s", deviceID)
	writeJSON(w, http.StatusOK, map[string]string{
		"status":    "calibration command sent",
		"device_id": deviceID,
	})
}

