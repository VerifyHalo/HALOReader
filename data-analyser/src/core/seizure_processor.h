#ifndef SEIZURE_PROCESSOR_H
#define SEIZURE_PROCESSOR_H

#include "seizure_detector.h"
#include "rhd_reader.h"
#include "fpga_processor.h"
#include <string>
#include <vector>
#include <memory>

// High-level processor that handles RHD files and generates result files
class SeizureProcessor {
public:
    SeizureProcessor(uint32_t threshold = 25000,
                    uint32_t window_timeout = 200,
                    uint32_t transition_count = 30);
    
    // Process an RHD file and save results
    // Returns true on success
    bool processFile(const std::string& rhd_filepath);
    
    // Get last error message
    std::string getLastError() const { return last_error_; }
    
    // Configuration
    void setThreshold(uint32_t threshold);
    void setWindowTimeout(uint32_t timeout);
    void setTransitionCount(uint32_t count);

private:
    // Write results to res_*.txt file
    bool writeResults(const std::string& rhd_filepath, 
                     const std::vector<SeizureEvent>& events,
                     uint32_t num_channels);
    
    // Try to initialize FPGA (returns true if successful)
    bool initializeFpga();
    
    // Process using FPGA (returns true if successful)
    bool processWithFpga(const std::vector<uint16_t>& data_channels,
                        uint32_t num_channels,
                        uint32_t num_samples,
                        std::vector<SeizureEvent>& events);
    
    // Process using software detector (fallback)
    bool processWithSoftware(const std::vector<uint16_t>& data_channels,
                            uint32_t num_channels,
                            uint32_t num_samples,
                            std::vector<SeizureEvent>& events);
    
    std::unique_ptr<FpgaProcessor> fpga_processor_;
    SeizureDetector detector_;
    std::string last_error_;
    bool fpga_initialized_;
    static const std::string BITFILE_PATH;
};

#endif // SEIZURE_PROCESSOR_H
