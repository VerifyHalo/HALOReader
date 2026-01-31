#ifndef FPGA_PROCESSOR_H
#define FPGA_PROCESSOR_H

#include "ok_frontpanel.h"
#include "seizure_detector.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

// FPGA-based seizure detection processor
class FpgaProcessor {
public:
    FpgaProcessor(uint32_t threshold = 25000,
                 uint32_t window_timeout = 200,
                 uint32_t transition_count = 30);
    
    ~FpgaProcessor();
    
    // Initialize FPGA (must be called before processing)
    bool initialize(const std::string& bitfile_path);
    
    // Process RHD data using FPGA
    // Returns true on success
    bool processData(const std::vector<uint16_t>& data_channels,
                    uint32_t num_channels,
                    uint32_t num_samples,
                    std::vector<SeizureEvent>& events);
    
    // Update parameters (will be applied on next processData call)
    void setThreshold(uint32_t threshold) { threshold_ = threshold; }
    void setWindowTimeout(uint32_t timeout) { window_timeout_ = timeout; }
    void setTransitionCount(uint32_t count) { transition_count_ = count; }
    
    // Get last error message
    std::string getLastError() const { return last_error_; }
    
    // Check if FPGA is initialized
    bool isInitialized() const { return initialized_; }

private:
    // Constants matching Python code
    static constexpr uint32_t PIPE_IN_ADDR = 0x80;
    static constexpr uint32_t PIPE_OUT_ADDR = 0xA0;
    static constexpr uint32_t WIREIN_CTRL = 0x00;
    static constexpr uint32_t WIREIN_TS_LO = 0x01;
    static constexpr uint32_t WIREIN_TS_HI = 0x02;
    static constexpr uint32_t WIREIN_THRESHOLD = 0x03;
    static constexpr uint32_t WIREIN_WINDOW_TIMEOUT = 0x04;
    static constexpr uint32_t WIREIN_TRANSITION_COUNT = 0x05;
    static constexpr uint32_t SAMPLES_PER_CHUNK = 128;
    static constexpr uint32_t SAMPLE_SIZE_BYTES = 4;
    static constexpr size_t MAX_EVENTS = 10000;
    static constexpr size_t EVENT_BUFFER_SIZE = MAX_EVENTS * SAMPLE_SIZE_BYTES;
    
    // Generate FPGA chunks from data
    std::vector<std::vector<uint8_t>> generateChunks(const std::vector<uint16_t>& data_channels,
                                                     uint32_t num_channels,
                                                     uint32_t num_samples);
    
    // Parse events from FPGA output
    void parseEvents(const std::vector<uint8_t>& output_data,
                    uint32_t num_channels,
                    std::vector<SeizureEvent>& events);
    
    // Worker thread for reading FPGA output (parallel with sending)
    void readWorker();
    
    OkFrontPanel* device_;
    bool initialized_;
    uint32_t threshold_;
    uint32_t window_timeout_;
    uint32_t transition_count_;
    std::string last_error_;
    
    // Parallel read/write support
    std::thread read_thread_;
    std::atomic<bool> reading_;
    std::mutex read_mutex_;
    std::condition_variable read_cv_;
    std::vector<uint8_t> read_buffer_;
    bool read_complete_;
};

#endif // FPGA_PROCESSOR_H
