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


std::vector<SeizureEvent> SeizureDetector::getEvents() {
    std::vector<SeizureEvent> result = events_;
    events_.clear();
    return result;
}
