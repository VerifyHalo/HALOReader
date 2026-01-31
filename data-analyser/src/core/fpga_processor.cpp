#include "fpga_processor.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <chrono>
#include <thread>

FpgaProcessor::FpgaProcessor(uint32_t threshold,
                            uint32_t window_timeout,
                            uint32_t transition_count)
    : device_(nullptr)
    , initialized_(false)
    , threshold_(threshold)
    , window_timeout_(window_timeout)
    , transition_count_(transition_count)
    , reading_(false)
    , read_complete_(false)
{
}

FpgaProcessor::~FpgaProcessor() {
    reading_ = false;
    read_cv_.notify_all();
    if (read_thread_.joinable()) {
        read_thread_.join();
    }
    delete device_;
}

bool FpgaProcessor::initialize(const std::string& bitfile_path) {
    if (initialized_) {
        return true;
    }
    
    try {
        device_ = new OkFrontPanel();
    } catch (const std::exception& e) {
        last_error_ = "Failed to create FrontPanel: " + std::string(e.what());
        return false;
    }
    
    // Check for devices
    int count = device_->getDeviceCount();
    if (count <= 0) {
        last_error_ = "No Opal Kelly devices found";
        delete device_;
        device_ = nullptr;
        return false;
    }
    
    // Try to open first available device
    std::string serial;
    bool opened = false;
    for (int i = 0; i < count; ++i) {
        serial = device_->getDeviceListSerial(i);
        int rc = device_->openBySerial(serial);
        if (rc == OkFrontPanel::NoError) {
            opened = true;
            std::cerr << "[FPGA] Opened device: " << serial << std::endl;
            break;
        }
    }
    
    if (!opened) {
        last_error_ = "Could not open any Opal Kelly device";
        delete device_;
        device_ = nullptr;
        return false;
    }
    
    // Configure FPGA
    int rc = device_->configureFPGA(bitfile_path);
    if (rc != OkFrontPanel::NoError) {
        last_error_ = "ConfigureFPGA failed: " + OkFrontPanel::getErrorString(rc);
        delete device_;
        device_ = nullptr;
        return false;
    }
    
    initialized_ = true;
    return true;
}

std::vector<std::vector<uint8_t>> FpgaProcessor::generateChunks(
    const std::vector<uint16_t>& data_channels,
    uint32_t num_channels,
    uint32_t num_samples) {
    
    std::vector<std::vector<uint8_t>> chunks;
    uint32_t num_chunks = (num_samples + SAMPLES_PER_CHUNK - 1) / SAMPLES_PER_CHUNK;
    chunks.reserve(num_chunks);
    
    for (uint32_t chunk_id = 0; chunk_id < num_chunks; ++chunk_id) {
        std::vector<uint8_t> chunk_bytes;
        chunk_bytes.reserve(num_channels * SAMPLES_PER_CHUNK * SAMPLE_SIZE_BYTES);
        
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            for (uint32_t sample_in_chunk = 0; sample_in_chunk < SAMPLES_PER_CHUNK; ++sample_in_chunk) {
                uint32_t sample_idx = chunk_id * SAMPLES_PER_CHUNK + sample_in_chunk;
                uint32_t data_idx = ch * num_samples + sample_idx;
                
                uint16_t code16 = 32768; // Mid-point default
                if (data_idx < data_channels.size()) {
                    code16 = data_channels[data_idx];
                }
                
                // Format: [31:16] = channel_id, [15:0] = ADC code
                uint32_t word32 = (ch << 16) | code16;
                
                // Little-endian bytes
                chunk_bytes.push_back(static_cast<uint8_t>(word32 & 0xFF));
                chunk_bytes.push_back(static_cast<uint8_t>((word32 >> 8) & 0xFF));
                chunk_bytes.push_back(static_cast<uint8_t>((word32 >> 16) & 0xFF));
                chunk_bytes.push_back(static_cast<uint8_t>((word32 >> 24) & 0xFF));
            }
        }
        
        // Pad to multiple of 16 bytes
        size_t rem = chunk_bytes.size() % 16;
        if (rem != 0) {
            chunk_bytes.resize(chunk_bytes.size() + (16 - rem), 0);
        }
        
        chunks.push_back(chunk_bytes);
    }
    
    return chunks;
}

void FpgaProcessor::parseEvents(const std::vector<uint8_t>& output_data,
                                uint32_t num_channels,
                                std::vector<SeizureEvent>& events) {
    events.clear();
    
    if (output_data.size() < 4) {
        return;
    }
    
    // Parse 32-bit words: [31:30]=event_code, [29:25]=channel_id, [24:0]=timestamp
    for (size_t i = 0; i + 4 <= output_data.size(); i += 4) {
        uint32_t word = static_cast<uint32_t>(output_data[i]) |
                       (static_cast<uint32_t>(output_data[i+1]) << 8) |
                       (static_cast<uint32_t>(output_data[i+2]) << 16) |
                       (static_cast<uint32_t>(output_data[i+3]) << 24);
        
        uint8_t event_code = (word >> 30) & 0x3;
        uint8_t channel_id = (word >> 25) & 0x1F;
        uint32_t timestamp25 = word & 0x01FFFFFF;
        
        // Only process data channels (0-29, which are RHD channels 2-31)
        if (channel_id >= num_channels) {
            continue;
        }
        
        if (event_code == 0x1) { // START
            SeizureEvent event;
            event.type = SeizureEvent::Type::START;
            event.channel = channel_id;
            event.timestamp = timestamp25;
            event.sample_index = timestamp25; // At 1kHz, timestamp = sample index
            events.push_back(event);
        } else if (event_code == 0x2) { // END
            SeizureEvent event;
            event.type = SeizureEvent::Type::END;
            event.channel = channel_id;
            event.timestamp = timestamp25;
            event.sample_index = timestamp25;
            events.push_back(event);
        }
    }
}

void FpgaProcessor::readWorker() {
    std::vector<uint8_t> accumulated_data;
    
    while (reading_) {
        // Read from FPGA continuously
        std::vector<uint8_t> data = device_->readFromPipeOut(PIPE_OUT_ADDR, EVENT_BUFFER_SIZE);
        if (!data.empty()) {
            accumulated_data.insert(accumulated_data.end(), data.begin(), data.end());
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Store accumulated data
    {
        std::lock_guard<std::mutex> lock(read_mutex_);
        read_buffer_ = accumulated_data;
        read_complete_ = true;
    }
}

bool FpgaProcessor::processData(const std::vector<uint16_t>& data_channels,
                               uint32_t num_channels,
                               uint32_t num_samples,
                               std::vector<SeizureEvent>& events) {
    if (!initialized_ || !device_) {
        last_error_ = "FPGA not initialized";
        return false;
    }
    
    // Set parameters
    device_->setWireInValue(WIREIN_THRESHOLD, threshold_ & 0xFFFFFFFF, 0xFFFFFFFF);
    device_->setWireInValue(WIREIN_WINDOW_TIMEOUT, window_timeout_ & 0xFFFFFFFF, 0xFFFFFFFF);
    device_->setWireInValue(WIREIN_TRANSITION_COUNT, transition_count_ & 0xFFFFFFFF, 0xFFFFFFFF);
    
    // Set timestamp (use random seed)
    std::random_device rd;
    std::mt19937 gen(rd());
    uint64_t ts = gen();
    device_->setWireInValue(WIREIN_TS_LO, ts & 0xFFFFFFFF, 0xFFFFFFFF);
    device_->setWireInValue(WIREIN_TS_HI, (ts >> 32) & 0xFFFFFFFF, 0xFFFFFFFF);
    
    // Reset FPGA
    device_->setWireInValue(WIREIN_CTRL, 0x80000000, 0xFFFFFFFF);
    device_->updateWireIns();
    device_->setWireInValue(WIREIN_CTRL, 0x00000000, 0xFFFFFFFF);
    device_->updateWireIns();
    
    // Generate chunks
    std::vector<std::vector<uint8_t>> chunks = generateChunks(data_channels, num_channels, num_samples);
    
    // Start reading thread
    read_complete_ = false;
    read_buffer_.clear();
    reading_ = true;
    read_thread_ = std::thread(&FpgaProcessor::readWorker, this);
    
    // Send chunks (parallel with reading)
    for (size_t chunk_idx = 0; chunk_idx < chunks.size(); ++chunk_idx) {
        int status = device_->writeToPipeIn(PIPE_IN_ADDR, chunks[chunk_idx]);
        if (status < 0) {
            reading_ = false;
            if (read_thread_.joinable()) {
                read_thread_.join();
            }
            last_error_ = "WriteToPipeIn failed for chunk " + std::to_string(chunk_idx);
            return false;
        }
        
        // Small delay to allow FPGA to process
        if ((chunk_idx + 1) % 50 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    // Wait a bit for FPGA to finish processing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Stop reading thread
    reading_ = false;
    if (read_thread_.joinable()) {
        read_thread_.join();
    }
    
    // Get accumulated data from reading thread
    std::vector<uint8_t> output_data;
    {
        std::lock_guard<std::mutex> lock(read_mutex_);
        output_data = read_buffer_;
    }
    
    // Final read to get any remaining data
    std::vector<uint8_t> final_data = device_->readFromPipeOut(PIPE_OUT_ADDR, EVENT_BUFFER_SIZE);
    if (!final_data.empty()) {
        output_data.insert(output_data.end(), final_data.begin(), final_data.end());
    }
    
    // Parse events
    parseEvents(output_data, num_channels, events);
    
    return true;
}
