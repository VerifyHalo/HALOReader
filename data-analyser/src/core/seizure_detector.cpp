#include "seizure_detector.h"
#include <algorithm>
#include <cmath>
#include <map>

SeizureDetector::SeizureDetector(uint32_t threshold,
                                 uint32_t window_timeout,
                                 uint32_t transition_count)
    : threshold_(threshold)
    , window_timeout_(window_timeout)
    , transition_count_(transition_count)
{
}

void SeizureDetector::reset() {
    channel_states_.clear();
    events_.clear();
}

int64_t SeizureDetector::computeNEO(uint16_t x_prev, uint16_t x_curr, uint16_t x_next) {
    // Center samples around zero (subtract 32768)
    int32_t x_prev_centered = static_cast<int32_t>(x_prev) - 32768;
    int32_t x_curr_centered = static_cast<int32_t>(x_curr) - 32768;
    int32_t x_next_centered = static_cast<int32_t>(x_next) - 32768;
    
    // NEO: ψ[n] = x[n]² - x[n-1] × x[n+1]
    int64_t curr_sq = static_cast<int64_t>(x_curr_centered) * static_cast<int64_t>(x_curr_centered);
    int64_t neigh_mul = static_cast<int64_t>(x_prev_centered) * static_cast<int64_t>(x_next_centered);
    int64_t neo = curr_sq - neigh_mul;
    
    // Return absolute value
    return neo < 0 ? -neo : neo;
}

bool SeizureDetector::processSample(uint16_t adc_code, uint32_t channel_id, uint64_t sample_index) {
    // Get or create channel state
    ChannelState& ch_state = channel_states_[channel_id];
    
    // Initialize state if needed
    if (ch_state.sample_history.empty()) {
        ch_state.state = ChannelState::NORMAL;
        ch_state.detection_counter = 0;
        ch_state.timeout_counter = 0;
        ch_state.seizure_start_sample = 0;
    }
    
    // Add sample to history (keep only last 3)
    ch_state.sample_history.push_back(adc_code);
    if (ch_state.sample_history.size() > 3) {
        ch_state.sample_history.erase(ch_state.sample_history.begin());
    }
    
    // Need at least 3 samples for NEO computation
    if (ch_state.sample_history.size() < 3) {
        return false;
    }
    
    // Compute NEO
    uint16_t x_prev = ch_state.sample_history[0];
    uint16_t x_curr = ch_state.sample_history[1];
    uint16_t x_next = ch_state.sample_history[2];
    int64_t neo_abs = computeNEO(x_prev, x_curr, x_next);
    
    // Threshold comparison
    bool detected = (neo_abs > static_cast<int64_t>(threshold_));
    
    // State machine
    if (ch_state.state == ChannelState::NORMAL) {
        if (detected) {
            ch_state.detection_counter++;
            ch_state.timeout_counter = 0;
            
            // Transition to SEIZURE if we have enough consecutive detections
            if (ch_state.detection_counter >= transition_count_) {
                ch_state.state = ChannelState::SEIZURE;
                ch_state.seizure_start_sample = sample_index - (transition_count_ - 1);
                
                // Generate START event
                SeizureEvent event;
                event.type = SeizureEvent::START;
                event.channel = channel_id;
                event.timestamp = static_cast<uint32_t>(ch_state.seizure_start_sample); // Assuming 1kHz: sample = ms
                event.sample_index = ch_state.seizure_start_sample;
                events_.push_back(event);
                
                ch_state.detection_counter = 0;
                return true;
            }
        } else {
            ch_state.detection_counter = 0;
        }
    } else { // SEIZURE state
        if (detected) {
            ch_state.timeout_counter = 0;
        } else {
            ch_state.timeout_counter++;
            
            // Transition back to NORMAL if timeout exceeded
            if (ch_state.timeout_counter >= window_timeout_) {
                ch_state.state = ChannelState::NORMAL;
                
                // Generate END event
                SeizureEvent event;
                event.type = SeizureEvent::END;
                event.channel = channel_id;
                event.timestamp = static_cast<uint32_t>(sample_index - window_timeout_);
                event.sample_index = sample_index - window_timeout_;
                events_.push_back(event);
                
                ch_state.timeout_counter = 0;
                return true;
            }
        }
    }
    
    return false;
}

std::vector<SeizureEvent> SeizureDetector::getEvents() {
    std::vector<SeizureEvent> result = events_;
    events_.clear();
    return result;
}
