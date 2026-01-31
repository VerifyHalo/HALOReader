#ifndef SEIZURE_DETECTOR_H
#define SEIZURE_DETECTOR_H

#include <vector>
#include <cstdint>
#include <string>
#include <map>

// Seizure detection event
struct SeizureEvent {
    enum Type {
        START = 1,
        END = 2
    };
    
    Type type;
    uint32_t channel;       // Channel ID (0-31)
    uint32_t timestamp;     // Sample timestamp (milliseconds from start)
    uint64_t sample_index;  // Sample index in the recording
};

// Seizure detection processor implementing NEO algorithm + state machine
class SeizureDetector {
public:
    SeizureDetector(uint32_t threshold = 25000,
                   uint32_t window_timeout = 200,
                   uint32_t transition_count = 30);
    
    // Process a single sample
    // Returns true if an event was generated (check getEvents())
    bool processSample(uint16_t adc_code, uint32_t channel_id, uint64_t sample_index);
    
    // Get all events generated since last call (clears internal buffer)
    std::vector<SeizureEvent> getEvents();
    
    // Reset detector state (useful when starting new file)
    void reset();
    
    // Configuration
    void setThreshold(uint32_t threshold) { threshold_ = threshold; }
    void setWindowTimeout(uint32_t timeout) { window_timeout_ = timeout; }
    void setTransitionCount(uint32_t count) { transition_count_ = count; }
    
    uint32_t getThreshold() const { return threshold_; }
    uint32_t getWindowTimeout() const { return window_timeout_; }
    uint32_t getTransitionCount() const { return transition_count_; }

private:
    // NEO computation: ψ[n] = x[n]² - x[n-1] × x[n+1]
    int64_t computeNEO(uint16_t x_prev, uint16_t x_curr, uint16_t x_next);
    
    // Per-channel state
    struct ChannelState {
        enum State {
            NORMAL = 0,
            SEIZURE = 1
        };
        
        State state;
        std::vector<uint16_t> sample_history;  // Need 3 samples for NEO
        uint32_t detection_counter;             // Consecutive detections
        uint32_t timeout_counter;               // Samples without detection
        uint64_t seizure_start_sample;          // Sample index when seizure started
    };
    
    std::map<uint32_t, ChannelState> channel_states_;
    
    // Configuration parameters
    uint32_t threshold_;
    uint32_t window_timeout_;
    uint32_t transition_count_;
    
    // Event buffer
    std::vector<SeizureEvent> events_;
};

#endif // SEIZURE_DETECTOR_H
