#ifndef SEIZURE_PROCESSOR_H
#define SEIZURE_PROCESSOR_H

#include "rhd_reader.h"
#include "fpga_processor.h"
#include <string>
#include <vector>
#include <memory>

// High-level processor that handles RHD files and generates result files
class SeizureProcessor {
public:
    SeizureProcessor(uint32_t threshold = 100000,
                    uint32_t window_timeout = 300,
                    uint32_t transition_count = 50);

    // Construct with an already-initialized external FpgaProcessor (non-owning).
    // Skips device open/program — the caller is responsible for keeping fpga alive.
    SeizureProcessor(FpgaProcessor* externalFpga,
                    uint32_t threshold,
                    uint32_t window_timeout,
                    uint32_t transition_count);
    
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
    
    FpgaProcessor* external_fpga_ = nullptr;           // non-owning; set by external-FPGA constructor
    std::unique_ptr<FpgaProcessor> fpga_processor_;    // owned; only used by default constructor path
    std::string last_error_;
    bool fpga_initialized_;
    uint32_t threshold_;
    uint32_t window_timeout_;
    uint32_t transition_count_;
    static const std::string BITFILE_PATH;
};

#endif // SEIZURE_PROCESSOR_H
