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
    std::map<uint32_t, int> channel_states_; // unused, kept for potential future use
    
    // Configuration parameters
    uint32_t threshold_;
    uint32_t window_timeout_;
    uint32_t transition_count_;
    
    // Event buffer
    std::vector<SeizureEvent> events_;
};

#endif // SEIZURE_DETECTOR_H
