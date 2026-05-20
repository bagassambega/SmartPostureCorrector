package aggregator

import (
	"sync"
	"time"

	"analytics-service/models"
)

const (
	defaultAlertThresholdSec = 30
	defaultSmoothingWindow   = 5
)

type DeviceState struct {
	mu sync.RWMutex

	DeviceID         string
	CurrentPosture   string
	CurrentPostureCode int
	IsBadPosture     bool
	BadStreakSec     float64
	AlertActive      bool
	LastSeen         time.Time

	recentCodes []int
	codeCounts  map[int]int

	totalSamples int
	badSamples   int
	goodSamples  int

	badDurationSec  float64
	goodDurationSec float64

	badStreakStart time.Time
	lastEventTime  time.Time
	hasLastEvent   bool

	minuteTotalSamples int
	minuteBadSamples   int
	minuteBadDuration  float64
	minuteGoodDuration float64
	minuteCodeCounts   map[int]int
	minuteStart        time.Time
}

type Aggregator struct {
	mu               sync.RWMutex
	devices          map[string]*DeviceState
	alertThreshold   float64
	smoothingWindow  int
	onMinuteSummary  func(deviceID string, summary MinuteSummary)
}

type MinuteSummary struct {
	BadPosturePct      float64
	BadDurationSec     float64
	GoodDurationSec    float64
	TotalSamples       int
	BadSamples         int
	DominantPostureCode int
	AlertActive        bool
}

func New(alertThresholdSec float64, smoothingWindow int, onMinuteSummary func(string, MinuteSummary)) *Aggregator {
	if alertThresholdSec <= 0 {
		alertThresholdSec = defaultAlertThresholdSec
	}
	if smoothingWindow <= 0 {
		smoothingWindow = defaultSmoothingWindow
	}
	return &Aggregator{
		devices:         make(map[string]*DeviceState),
		alertThreshold:  alertThresholdSec,
		smoothingWindow: smoothingWindow,
		onMinuteSummary: onMinuteSummary,
	}
}

func (a *Aggregator) getOrCreate(deviceID string) *DeviceState {
	a.mu.Lock()
	defer a.mu.Unlock()
	if st, ok := a.devices[deviceID]; ok {
		return st
	}
	now := time.Now().UTC()
	st := &DeviceState{
		DeviceID:        deviceID,
		recentCodes:     make([]int, 0, a.smoothingWindow),
		codeCounts:      make(map[int]int),
		minuteCodeCounts: make(map[int]int),
		minuteStart:     now,
	}
	a.devices[deviceID] = st
	return st
}

func (a *Aggregator) Process(payload models.PosturePayload, serverTime time.Time) models.DeviceCurrentResponse {
	st := a.getOrCreate(payload.DeviceID)
	st.mu.Lock()
	defer st.mu.Unlock()

	a.applyDuration(st, payload.IsBadPosture, serverTime)
	a.maybeFlushMinute(st, serverTime)

	st.recentCodes = append(st.recentCodes, payload.PostureCode)
	if len(st.recentCodes) > a.smoothingWindow {
		st.recentCodes = st.recentCodes[len(st.recentCodes)-a.smoothingWindow:]
	}

	stableCode := majorityVote(st.recentCodes)
	stablePosture := models.PostureFromCode(stableCode)
	if stablePosture == "" {
		stablePosture = payload.Posture
		stableCode = payload.PostureCode
	}
	stableBad := models.IsBadPosture(stablePosture)

	st.CurrentPosture = stablePosture
	st.CurrentPostureCode = stableCode
	st.IsBadPosture = stableBad
	st.LastSeen = serverTime

	st.totalSamples++
	st.minuteTotalSamples++
	st.codeCounts[stableCode]++
	st.minuteCodeCounts[stableCode]++

	if stableBad {
		st.badSamples++
		st.minuteBadSamples++
		if st.badStreakStart.IsZero() {
			st.badStreakStart = serverTime
		}
		st.BadStreakSec = serverTime.Sub(st.badStreakStart).Seconds()
	} else {
		st.goodSamples++
		st.badStreakStart = time.Time{}
		st.BadStreakSec = 0
	}

	st.AlertActive = stableBad && st.BadStreakSec >= a.alertThreshold
	st.lastEventTime = serverTime
	st.hasLastEvent = true

	return st.snapshot()
}

func (a *Aggregator) applyDuration(st *DeviceState, isBad bool, serverTime time.Time) {
	if !st.hasLastEvent {
		return
	}
	delta := serverTime.Sub(st.lastEventTime).Seconds()
	if delta <= 0 || delta > 60 {
		return
	}
	if isBad {
		st.badDurationSec += delta
		st.minuteBadDuration += delta
	} else {
		st.goodDurationSec += delta
		st.minuteGoodDuration += delta
	}
}

func (a *Aggregator) maybeFlushMinute(st *DeviceState, serverTime time.Time) {
	if st.minuteStart.IsZero() {
		st.minuteStart = serverTime
		return
	}
	if serverTime.Sub(st.minuteStart) < time.Minute {
		return
	}
	a.flushMinuteLocked(st, serverTime)
}

func (a *Aggregator) flushMinuteLocked(st *DeviceState, serverTime time.Time) {
	if a.onMinuteSummary == nil {
		st.resetMinute(serverTime)
		return
	}

	dominant := dominantCode(st.minuteCodeCounts)
	pct := 0.0
	if st.minuteTotalSamples > 0 {
		pct = float64(st.minuteBadSamples) / float64(st.minuteTotalSamples) * 100
	}

	summary := MinuteSummary{
		BadPosturePct:       pct,
		BadDurationSec:      st.minuteBadDuration,
		GoodDurationSec:     st.minuteGoodDuration,
		TotalSamples:        st.minuteTotalSamples,
		BadSamples:          st.minuteBadSamples,
		DominantPostureCode: dominant,
		AlertActive:         st.AlertActive,
	}
	a.onMinuteSummary(st.DeviceID, summary)
	st.resetMinute(serverTime)
}

func (st *DeviceState) resetMinute(serverTime time.Time) {
	st.minuteStart = serverTime
	st.minuteTotalSamples = 0
	st.minuteBadSamples = 0
	st.minuteBadDuration = 0
	st.minuteGoodDuration = 0
	st.minuteCodeCounts = make(map[int]int)
}

func (st *DeviceState) snapshot() models.DeviceCurrentResponse {
	return models.DeviceCurrentResponse{
		DeviceID:     st.DeviceID,
		Posture:      st.CurrentPosture,
		PostureCode:  st.CurrentPostureCode,
		IsBadPosture: st.IsBadPosture,
		BadStreakSec: st.BadStreakSec,
		AlertActive:  st.AlertActive,
		LastSeen:     st.LastSeen,
	}
}

func (a *Aggregator) GetCurrent(deviceID string) (models.DeviceCurrentResponse, bool) {
	a.mu.RLock()
	st, ok := a.devices[deviceID]
	a.mu.RUnlock()
	if !ok {
		return models.DeviceCurrentResponse{}, false
	}
	st.mu.RLock()
	defer st.mu.RUnlock()
	return st.snapshot(), true
}

func (a *Aggregator) ListDevices() []string {
	a.mu.RLock()
	defer a.mu.RUnlock()
	ids := make([]string, 0, len(a.devices))
	for id := range a.devices {
		ids = append(ids, id)
	}
	return ids
}

func (a *Aggregator) GetInMemorySummary(deviceID string) (models.DeviceSummaryResponse, bool) {
	a.mu.RLock()
	st, ok := a.devices[deviceID]
	a.mu.RUnlock()
	if !ok {
		return models.DeviceSummaryResponse{}, false
	}
	st.mu.RLock()
	defer st.mu.RUnlock()

	pct := 0.0
	if st.totalSamples > 0 {
		pct = float64(st.badSamples) / float64(st.totalSamples) * 100
	}
	dominant := models.PostureFromCode(dominantCode(st.codeCounts))

	return models.DeviceSummaryResponse{
		BadPosturePct:   pct,
		BadDurationSec:  st.badDurationSec,
		GoodDurationSec: st.goodDurationSec,
		DominantPosture: dominant,
		TotalSamples:    st.totalSamples,
	}, true
}

func (a *Aggregator) StartMinuteFlusher(stop <-chan struct{}) {
	ticker := time.NewTicker(time.Minute)
	go func() {
		defer ticker.Stop()
		for {
			select {
			case <-stop:
				return
			case now := <-ticker.C:
				a.flushAll(now.UTC())
			}
		}
	}()
}

func (a *Aggregator) flushAll(now time.Time) {
	a.mu.RLock()
	devices := make([]*DeviceState, 0, len(a.devices))
	for _, st := range a.devices {
		devices = append(devices, st)
	}
	a.mu.RUnlock()

	for _, st := range devices {
		st.mu.Lock()
		if !st.minuteStart.IsZero() && now.Sub(st.minuteStart) >= time.Minute {
			a.flushMinuteLocked(st, now)
		}
		st.mu.Unlock()
	}
}

func majorityVote(codes []int) int {
	if len(codes) == 0 {
		return -1
	}
	counts := make(map[int]int)
	for _, c := range codes {
		counts[c]++
	}
	bestCode := codes[len(codes)-1]
	bestCount := 0
	for code, count := range counts {
		if count > bestCount || (count == bestCount && code == codes[len(codes)-1]) {
			bestCount = count
			bestCode = code
		}
	}
	return bestCode
}

func dominantCode(counts map[int]int) int {
	bestCode := 0
	bestCount := -1
	for code, count := range counts {
		if count > bestCount {
			bestCount = count
			bestCode = code
		}
	}
	return bestCode
}
