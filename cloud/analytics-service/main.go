package main

import (
	"context"
	"embed"
	"io/fs"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	paho "github.com/eclipse/paho.mqtt.golang"
	influxdb2 "github.com/influxdata/influxdb-client-go/v2"

	"analytics-service/aggregator"
	"analytics-service/api"
	"analytics-service/config"
	mqtthandler "analytics-service/mqtt"
	"analytics-service/storage"
)

//go:embed web/*
var webFS embed.FS

func main() {
	cfg := config.Load()

	influxClient := influxdb2.NewClient(cfg.InfluxURL, cfg.InfluxToken)
	defer influxClient.Close()

	store := storage.New(cfg, influxClient)

	stopAgg := make(chan struct{})
	defer close(stopAgg)

	agg := aggregator.New(cfg.AlertThresholdSec, cfg.SmoothingWindow, mqtthandler.WriteMinuteSummary(store))
	agg.StartMinuteFlusher(stopAgg)

	mqttHandler := mqtthandler.NewHandler(cfg, store, agg)
	mqttClient, err := mqttHandler.Connect()
	if err != nil {
		log.Fatalf("failed to start mqtt client: %v", err)
	}
	defer mqtthandler.Disconnect(mqttClient)

	staticFS, err := fs.Sub(webFS, "web")
	if err != nil {
		log.Fatalf("failed to load embedded web assets: %v", err)
	}

	srv := api.NewServer(cfg, store, agg, staticFS)
	httpSrv := &http.Server{
		Addr:    cfg.HTTPAddr,
		Handler: srv.Handler(),
	}

	go func() {
		log.Printf("analytics-service listening on %s", cfg.HTTPAddr)
		log.Printf("mqtt sensor topic: %s", cfg.MQTTSensorTopic)
		if err := httpSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("http server failed: %v", err)
		}
	}()

	waitForShutdown(httpSrv, mqttClient)
}

func waitForShutdown(httpSrv *http.Server, mqttClient paho.Client) {
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh
	log.Println("shutdown signal received")

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if err := httpSrv.Shutdown(ctx); err != nil {
		log.Printf("http shutdown error: %v", err)
	}
	mqtthandler.Disconnect(mqttClient)
}
