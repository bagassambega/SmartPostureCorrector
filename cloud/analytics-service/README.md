# EncokCok Cloud — Smart Posture Analytics

Stack cloud untuk **EncokCok Smart Posture Corrector**: ingest MQTT dari ESP32 + MPU6050, agregasi postur, alert, dan dashboard.
Pipeline ini disesuaikan dengan `hardware/sensor/sensor.ino`; `hardware/collect_dataset/collect_dataset.ino` tidak digunakan oleh cloud.

## Arsitektur

```
ESP32 + MPU6050 ──MQTT──► Mosquitto ──► analytics-service (Go)
                                                 │
                                                 ├──► InfluxDB (posture_events, posture_summary, raw_mpu6050)
                                                 └──► Dashboard (http://localhost:3000)
```

## Prasyarat

- Docker & Docker Compose
- (Opsional) `mosquitto_pub` untuk testing MQTT

## Menjalankan Sistem

```bash
cd cloud
cp .env.example .env   # sesuaikan token InfluxDB jika perlu
docker compose up --build
```

Layanan:

| Service | URL / Port |
|---------|------------|
| Dashboard | http://localhost:3000 |
| MQTT Broker | `localhost:1883` |
| InfluxDB | http://localhost:8086 |
| Health check | http://localhost:3000/healthz |

## Topik MQTT

### Sensor firmware — `sensors/mpu6050`

Firmware `hardware/sensor/sensor.ino` mengirim payload berikut setiap 200 ms:

```json
{
  "timestamp": 123456,
  "roll": -2.3,
  "pitch": 5.1
}
```

Cloud menyimpan data ini ke measurement `raw_mpu6050`, lalu mengonversinya menjadi posture event agar dashboard real-time, summary, timeline, distribusi, dan alert tetap berfungsi.

Klasifikasi default:

- `duduk_bungkuk` jika `abs(roll) >= POSTURE_BAD_ROLL_DEG` atau `abs(pitch) >= POSTURE_BAD_PITCH_DEG`
- `duduk_tegak` jika masih di bawah ambang

Default ambang adalah 20 derajat dan bisa diubah melalui `.env`.
Karena payload firmware tidak membawa informasi posisi berdiri/duduk, cloud hanya mengklasifikasikan dua label: `duduk_tegak` dan `duduk_bungkuk`.

## Testing MQTT

Simulasi payload firmware saat ini:

```bash
mosquitto_pub -h localhost -t sensors/mpu6050 -m '{"timestamp":123456,"roll":-24.5,"pitch":5.1}'
```

Simulasi postur baik:

```bash
mosquitto_pub -h localhost -t sensors/mpu6050 -m '{"timestamp":123656,"roll":2.5,"pitch":5.1}'
```

Kirim berulang untuk menguji alert (postur buruk ≥ 30 detik):

```bash
for i in $(seq 1 200); do
  mosquitto_pub -h localhost -t sensors/mpu6050 -m "{\"timestamp\":$((1000 + i * 200)),\"roll\":-24.5,\"pitch\":5.1}"
  sleep 0.2
done
```

## Endpoint API

| Method | Path | Deskripsi |
|--------|------|-----------|
| GET | `/healthz` | Health check |
| GET | `/api/devices` | Daftar device_id |
| GET | `/api/devices/{device_id}/current` | Status postur terkini + alert |
| GET | `/api/devices/{device_id}/summary?range=today` | Ringkasan (today, 1h, 24h, dll.) |
| GET | `/api/devices/{device_id}/timeline?range=today` | Timeline postur |
| GET | `/api/devices/{device_id}/angles?range=1h` | Time-series `roll` dan `pitch` |
| GET | `/api/devices/{device_id}/distribution?range=today` | Distribusi postur |
| GET | `/api/series?field=roll&range=1h` | Data sensor mentah (`roll` atau `pitch`) |

## Struktur Kode

```
analytics-service/
├── main.go
├── config/          # Konfigurasi env
├── models/          # Payload MQTT & response API
├── mqtt/            # Subscriber & handler
├── storage/         # InfluxDB read/write
├── aggregator/      # Smoothing, streak, alert, summary
├── api/             # HTTP handlers
└── web/             # Dashboard Chart.js
```

## Variabel Lingkungan

| Variable | Default | Keterangan |
|----------|---------|------------|
| `HTTP_ADDR` | `:3000` | Port HTTP |
| `MQTT_BROKER` | `tcp://localhost:1883` | Broker MQTT |
| `MQTT_SENSOR_TOPIC` | `sensors/mpu6050` | Topik firmware `sensor.ino` |
| `SENSOR_DEVICE_ID` | `ESP32_MPU6050` | Device ID untuk payload firmware yang belum membawa `device_id` |
| `POSTURE_BAD_ROLL_DEG` | `20` | Ambang roll untuk klasifikasi postur buruk |
| `POSTURE_BAD_PITCH_DEG` | `20` | Ambang pitch untuk klasifikasi postur buruk |
| `INFLUX_URL` | `http://localhost:8086` | InfluxDB |
| `INFLUX_TOKEN` | — | Token API |
| `INFLUX_ORG` | `my-org` | Org InfluxDB |
| `INFLUX_BUCKET` | `iot_data` | Bucket |
| `INFLUX_MEASUREMENT` | `raw_mpu6050` | Measurement sensor mentah |

## Menjalankan Tanpa Docker (dev lokal)

```bash
# Terminal 1: Mosquitto + InfluxDB via docker compose (hanya infra)
cd cloud && docker compose up mosquitto influxdb

# Terminal 2: analytics-service
cd cloud/analytics-service
export MQTT_BROKER=tcp://localhost:1883
export INFLUX_URL=http://localhost:8086
export INFLUX_TOKEN=change-me-to-secret-token
go run .
```

## Troubleshooting

**InfluxDB `unauthorized`:** Token di `.env` (`INFLUXDB_TOKEN`) harus sama dengan token saat InfluxDB pertama kali di-init. Jika volume lama memakai token berbeda, reset volume:

```bash
docker compose down
rm -rf influxdb/*
docker compose up --build
```


Firmware saat ini cukup publish ke **`sensors/mpu6050`** dengan payload:

- `timestamp` — `millis()` device
- `roll` — sudut roll
- `pitch` — sudut pitch
