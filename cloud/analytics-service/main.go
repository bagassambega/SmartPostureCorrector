package main

import (
	"context"
	"embed"
	"encoding/json"
	"fmt"
	"io/fs"
	"log"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	influxdb2 "github.com/influxdata/influxdb-client-go/v2"
	"github.com/influxdata/influxdb-client-go/v2/api"
)

//go:embed web/*
var webFS embed.FS

type sensorPayload struct {
	Roll  float64 `json:"roll"`
	Pitch float64 `json:"pitch"`
	Gx    float64 `json:"gx"`
	Gy    float64 `json:"gy"`
	Gz    float64 `json:"gz"`
}

type config struct {
	HTTPAddr          string
	MQTTBroker        string
	MQTTTopic         string
	MQTTClientID      string
	InfluxURL         string
	InfluxToken       string
	InfluxOrg         string
	InfluxBucket      string
	InfluxMeasurement string
}

type app struct {
	cfg       config
	queryAPI  api.QueryAPI
	writeAPI  api.WriteAPIBlocking
	influx    influxdb2.Client
	mqtt      mqtt.Client
	httpSrv   *http.Server
}

func main() {
	cfg := config{
		HTTPAddr:          envOrDefault("HTTP_ADDR", ":3000"),
		MQTTBroker:        envOrDefault("MQTT_BROKER", "tcp://localhost:1883"),
		MQTTTopic:         envOrDefault("MQTT_TOPIC", "sensors/mpu6050"),
		MQTTClientID:      envOrDefault("MQTT_CLIENT_ID", "analytics-service"),
		InfluxURL:         envOrDefault("INFLUX_URL", "http://localhost:8086"),
		InfluxToken:       envOrDefault("INFLUX_TOKEN", "my-super-secret-auth-token"),
		InfluxOrg:         envOrDefault("INFLUX_ORG", "my-org"),
		InfluxBucket:      envOrDefault("INFLUX_BUCKET", "iot_data"),
		InfluxMeasurement: envOrDefault("INFLUX_MEASUREMENT", "mpu6050"),
	}

	influx := influxdb2.NewClient(cfg.InfluxURL, cfg.InfluxToken)
	defer influx.Close()

	a := &app{
		cfg:      cfg,
		queryAPI: influx.QueryAPI(cfg.InfluxOrg),
		writeAPI: influx.WriteAPIBlocking(cfg.InfluxOrg, cfg.InfluxBucket),
		influx:   influx,
	}

	if err := a.startMQTT(); err != nil {
		log.Fatalf("failed to start mqtt client: %v", err)
	}
	defer a.stopMQTT()

	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", a.healthzHandler)
	mux.HandleFunc("/api/series", a.seriesHandler)
	staticFS, err := fs.Sub(webFS, "web")
	if err != nil {
		log.Fatalf("failed to load embedded web assets: %v", err)
	}
	mux.Handle("/", http.FileServer(http.FS(staticFS)))

	a.httpSrv = &http.Server{
		Addr:    cfg.HTTPAddr,
		Handler: loggingMiddleware(mux),
	}

	go func() {
		log.Printf("analytics-service listening on %s", cfg.HTTPAddr)
		if err := a.httpSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("http server failed: %v", err)
		}
	}()

	waitForShutdown(a)
}

func envOrDefault(key, fallback string) string {
	value := strings.TrimSpace(os.Getenv(key))
	if value == "" {
		return fallback
	}
	return value
}

func (a *app) startMQTT() error {
	opts := mqtt.NewClientOptions().
		AddBroker(a.cfg.MQTTBroker).
		SetClientID(a.cfg.MQTTClientID).
		SetAutoReconnect(true).
		SetConnectRetry(true).
		SetConnectRetryInterval(2 * time.Second)

	opts.OnConnect = func(client mqtt.Client) {
		token := client.Subscribe(a.cfg.MQTTTopic, 1, a.handleMQTTMessage)
		if token.Wait() && token.Error() != nil {
			log.Printf("mqtt subscribe failed: %v", token.Error())
			return
		}
		log.Printf("mqtt subscribed to topic %s", a.cfg.MQTTTopic)
	}

	opts.OnConnectionLost = func(_ mqtt.Client, err error) {
		log.Printf("mqtt connection lost: %v", err)
	}

	client := mqtt.NewClient(opts)
	token := client.Connect()
	if token.Wait() && token.Error() != nil {
		return token.Error()
	}

	a.mqtt = client
	return nil
}

func (a *app) stopMQTT() {
	if a.mqtt != nil && a.mqtt.IsConnected() {
		a.mqtt.Disconnect(250)
	}
}

func (a *app) handleMQTTMessage(_ mqtt.Client, msg mqtt.Message) {
	var payload sensorPayload
	if err := json.Unmarshal(msg.Payload(), &payload); err != nil {
		log.Printf("invalid mqtt json payload: %v", err)
		return
	}

	fields := map[string]interface{}{
		"roll":  payload.Roll,
		"pitch": payload.Pitch,
		"gx":    payload.Gx,
		"gy":    payload.Gy,
		"gz":    payload.Gz,
	}

	point := influxdb2.NewPoint(a.cfg.InfluxMeasurement, nil, fields, time.Now().UTC())
	if err := a.writeAPI.WritePoint(context.Background(), point); err != nil {
		log.Printf("failed writing influx point: %v", err)
	}
}

func (a *app) healthzHandler(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

type seriesPoint struct {
	Time  string  `json:"time"`
	Value float64 `json:"value"`
}

type seriesResponse struct {
	Field string       `json:"field"`
	Range string       `json:"range"`
	Data  []seriesPoint `json:"data"`
}

func (a *app) seriesHandler(w http.ResponseWriter, r *http.Request) {
	field := strings.ToLower(strings.TrimSpace(r.URL.Query().Get("field")))
	if field == "" {
		field = "roll"
	}

	allowedFields := map[string]bool{"roll": true, "pitch": true, "gx": true, "gy": true, "gz": true}
	if !allowedFields[field] {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid field"})
		return
	}

	timeRange := strings.TrimSpace(r.URL.Query().Get("range"))
	if timeRange == "" {
		timeRange = "1h"
	}

	allowedRanges := map[string]bool{"15m": true, "1h": true, "6h": true, "24h": true}
	if !allowedRanges[timeRange] {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid range"})
		return
	}

	window := strings.TrimSpace(r.URL.Query().Get("window"))
	if window == "" {
		window = "2s"
	}

	flux := fmt.Sprintf(`from(bucket: %q)
  |> range(start: -%s)
  |> filter(fn: (r) => r._measurement == %q and r._field == %q)
  |> aggregateWindow(every: %s, fn: mean, createEmpty: false)
  |> yield(name: "mean")`, a.cfg.InfluxBucket, timeRange, a.cfg.InfluxMeasurement, field, window)

	ctx, cancel := context.WithTimeout(r.Context(), 8*time.Second)
	defer cancel()

	result, err := a.queryAPI.Query(ctx, flux)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
		return
	}
	defer result.Close()

	series := make([]seriesPoint, 0, 256)
	for result.Next() {
		record := result.Record()
		value, ok := record.Value().(float64)
		if !ok {
			continue
		}
		series = append(series, seriesPoint{
			Time:  record.Time().Format(time.RFC3339),
			Value: value,
		})
	}

	if result.Err() != nil {
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": result.Err().Error()})
		return
	}

	writeJSON(w, http.StatusOK, seriesResponse{Field: field, Range: timeRange, Data: series})
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

func waitForShutdown(a *app) {
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh
	log.Println("shutdown signal received")

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if err := a.httpSrv.Shutdown(ctx); err != nil {
		log.Printf("http shutdown error: %v", err)
	}
}
