package mqtt

import (
	"encoding/json"
	"log"
	"time"

	paho "github.com/eclipse/paho.mqtt.golang"

	"analytics-service/aggregator"
	"analytics-service/config"
	"analytics-service/models"
	"analytics-service/storage"
)

type Handler struct {
	cfg        config.Config
	store      *storage.Store
	aggregator *aggregator.Aggregator
}

func NewHandler(cfg config.Config, store *storage.Store, agg *aggregator.Aggregator) *Handler {
	return &Handler{cfg: cfg, store: store, aggregator: agg}
}

func (h *Handler) Connect() (paho.Client, error) {
	opts := paho.NewClientOptions().
		AddBroker(h.cfg.MQTTBroker).
		SetClientID(h.cfg.MQTTClientID).
		SetAutoReconnect(true).
		SetConnectRetry(true).
		SetConnectRetryInterval(2 * time.Second)

	opts.OnConnect = func(client paho.Client) {
		token := client.Subscribe(h.cfg.MQTTSensorTopic, 1, h.handleSensor)
		if token.Wait() && token.Error() != nil {
			log.Printf("mqtt subscribe failed for %s: %v", h.cfg.MQTTSensorTopic, token.Error())
			return
		}
		log.Printf("mqtt subscribed to sensor topic %s", h.cfg.MQTTSensorTopic)
	}

	opts.OnConnectionLost = func(_ paho.Client, err error) {
		log.Printf("mqtt connection lost: %v", err)
	}

	client := paho.NewClient(opts)
	token := client.Connect()
	if token.Wait() && token.Error() != nil {
		return nil, token.Error()
	}
	return client, nil
}

func (h *Handler) handleSensor(_ paho.Client, msg paho.Message) {
	var payload models.SensorPayload
	if err := json.Unmarshal(msg.Payload(), &payload); err != nil {
		log.Printf("invalid sensor json: %v", err)
		return
	}
	if err := payload.Validate(); err != nil {
		log.Printf("invalid sensor payload: %v", err)
		return
	}

	serverTime := time.Now().UTC()
	posturePayload := payload.ToPosturePayload(
		h.cfg.SensorDeviceID,
		h.cfg.PostureBadRollDeg,
		h.cfg.PostureBadPitchDeg,
	)
	if err := posturePayload.Validate(); err != nil {
		log.Printf("sensor payload could not be converted to posture event: %v", err)
		return
	}

	h.aggregator.Process(posturePayload, serverTime)

	if err := h.store.WritePostureEvent(posturePayload, serverTime); err != nil {
		log.Printf("failed writing posture event from sensor payload: %v", err)
	}
	if err := h.store.WriteSensor(payload, serverTime); err != nil {
		log.Printf("failed writing raw sensor point: %v", err)
	}
}

func Disconnect(client paho.Client) {
	if client != nil && client.IsConnected() {
		client.Disconnect(250)
	}
}

func WriteMinuteSummary(store *storage.Store) func(string, aggregator.MinuteSummary) {
	return func(deviceID string, summary aggregator.MinuteSummary) {
		if err := store.WritePostureSummary(deviceID, summary, time.Now().UTC()); err != nil {
			log.Printf("failed writing posture summary for %s: %v", deviceID, err)
		}
	}
}
