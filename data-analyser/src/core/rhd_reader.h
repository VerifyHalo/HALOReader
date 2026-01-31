#ifndef RHD_READER_H
#define RHD_READER_H

#include <vector>
#include <string>
#include <cstdint>

// Intan RHD file reader
// Reads .rhd files and extracts amplifier data
class RhdReader {
public:
    struct RhdData {
        std::vector<std::vector<uint16_t>> amplifier_data;  // [channel][sample] - ADC codes
        double sample_rate;
        uint32_t num_channels;
        uint32_t num_samples;
    };
    
    // Read RHD file and extract amplifier data
    // Returns true on success
    static bool readFile(const std::string& filepath, RhdData& data);
    
    // Helper: Extract channels 2-31 (skip reference/ground channels 0-1)
    // Returns data in flat format: [ch0_sample0, ch0_sample1, ..., ch1_sample0, ...]
    static std::vector<uint16_t> extractDataChannels(const RhdData& rhd_data);
};

#endif // RHD_READER_H
