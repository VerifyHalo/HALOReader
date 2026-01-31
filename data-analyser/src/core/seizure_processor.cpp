#include "seizure_processor.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <map>
#include <iostream>

// Bitfile path - try multiple locations
const std::string SeizureProcessor::BITFILE_PATH = []() {
    const char* paths[] = {
        "lib/detection.bit",
        "../lib/detection.bit",
        "../../lib/detection.bit",
        "/Users/antonmelnychuk/workspace/pipeline/data-analyser/lib/detection.bit"
    };
    
    for (const char* path : paths) {
        if (std::filesystem::exists(path)) {
            return std::string(path);
        }
    }
    return std::string("lib/detection.bit"); // Default
}();

SeizureProcessor::SeizureProcessor(uint32_t threshold,
                                   uint32_t window_timeout,
                                   uint32_t transition_count)
    : detector_(threshold, window_timeout, transition_count)
    , fpga_initialized_(false)
{
    // Try to initialize FPGA (non-blocking, will try again on first use)
    initializeFpga();
}

void SeizureProcessor::setThreshold(uint32_t threshold) {
    detector_.setThreshold(threshold);
    if (fpga_processor_) {
        fpga_processor_->setThreshold(threshold);
    }
}

void SeizureProcessor::setWindowTimeout(uint32_t timeout) {
    detector_.setWindowTimeout(timeout);
    if (fpga_processor_) {
        fpga_processor_->setWindowTimeout(timeout);
    }
}

void SeizureProcessor::setTransitionCount(uint32_t count) {
    detector_.setTransitionCount(count);
    if (fpga_processor_) {
        fpga_processor_->setTransitionCount(count);
    }
}

bool SeizureProcessor::initializeFpga() {
    if (fpga_initialized_) {
        return true;
    }
    
    if (!std::filesystem::exists(BITFILE_PATH)) {
        std::cerr << "[FPGA] Bitfile not found at: " << BITFILE_PATH << std::endl;
        return false;
    }
    
    try {
        fpga_processor_ = std::make_unique<FpgaProcessor>(
            detector_.getThreshold(),
            detector_.getWindowTimeout(),
            detector_.getTransitionCount()
        );
        
        if (fpga_processor_->initialize(BITFILE_PATH)) {
            fpga_initialized_ = true;
            std::cerr << "[FPGA] Successfully initialized FPGA" << std::endl;
            return true;
        } else {
            std::cerr << "[FPGA] Failed to initialize: " << fpga_processor_->getLastError() << std::endl;
            fpga_processor_.reset();
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[FPGA] Exception during initialization: " << e.what() << std::endl;
        fpga_processor_.reset();
        return false;
    }
}

bool SeizureProcessor::processWithFpga(const std::vector<uint16_t>& data_channels,
                                     uint32_t num_channels,
                                     uint32_t num_samples,
                                     std::vector<SeizureEvent>& events) {
    if (!fpga_processor_ || !fpga_initialized_) {
        return false;
    }
    
    // Note: FPGA parameters are set during initialization
    // If parameters changed, we'd need to reinitialize, but for now we assume they're constant
    
    return fpga_processor_->processData(data_channels, num_channels, num_samples, events);
}

bool SeizureProcessor::processWithSoftware(const std::vector<uint16_t>& data_channels,
                                           uint32_t num_channels,
                                           uint32_t num_samples,
                                           std::vector<SeizureEvent>& events) {
    detector_.reset();
    
    // Process all samples
    for (uint32_t sample_idx = 0; sample_idx < num_samples; ++sample_idx) {
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            uint32_t data_idx = ch * num_samples + sample_idx;
            if (data_idx < data_channels.size()) {
                uint16_t adc_code = data_channels[data_idx];
                detector_.processSample(adc_code, ch, sample_idx);
            }
        }
    }
    
    events = detector_.getEvents();
    return true;
}

bool SeizureProcessor::processFile(const std::string& rhd_filepath) {
    last_error_.clear();
    
    // Read RHD file
    RhdReader::RhdData rhd_data;
    if (!RhdReader::readFile(rhd_filepath, rhd_data)) {
        last_error_ = "Failed to read RHD file: " + rhd_filepath;
        return false;
    }
    
    // Extract data channels (skip 0-1, use 2-31)
    std::vector<uint16_t> data_channels = RhdReader::extractDataChannels(rhd_data);
    if (data_channels.empty()) {
        last_error_ = "No data channels extracted from RHD file";
        return false;
    }
    
    uint32_t num_data_channels = 30; // Channels 2-31
    uint32_t num_samples = rhd_data.num_samples;
    
    // Try FPGA first, fall back to software
    std::vector<SeizureEvent> events;
    bool success = false;
    
    if (fpga_initialized_ || initializeFpga()) {
        success = processWithFpga(data_channels, num_data_channels, num_samples, events);
        if (!success) {
            std::cerr << "[FPGA] FPGA processing failed, falling back to software" << std::endl;
            last_error_ = "FPGA processing failed: " + (fpga_processor_ ? fpga_processor_->getLastError() : "Not initialized");
        }
    }
    
    if (!success) {
        // Fall back to software processing
        success = processWithSoftware(data_channels, num_data_channels, num_samples, events);
        if (!success) {
            last_error_ = "Software processing failed";
            return false;
        }
    }
    
    // Write results
    if (!writeResults(rhd_filepath, events, num_data_channels)) {
        last_error_ = "Failed to write results file";
        return false;
    }
    
    return true;
}

bool SeizureProcessor::writeResults(const std::string& rhd_filepath,
                                    const std::vector<SeizureEvent>& events,
                                    uint32_t num_channels) {
    std::filesystem::path rhd_path(rhd_filepath);
    std::string stem = rhd_path.stem().string();
    
    std::filesystem::path result_dir = rhd_path.parent_path() / stem;
    std::filesystem::create_directories(result_dir);
    
    // Group events by channel
    std::map<uint32_t, std::vector<SeizureEvent>> events_by_channel;
    for (const auto& event : events) {
        events_by_channel[event.channel].push_back(event);
    }
    
    // Write one file per channel that has detections
    for (const auto& [channel, ch_events] : events_by_channel) {
        std::string result_filename = "ch" + std::to_string(channel) + "_" + stem + ".txt";
        std::filesystem::path result_path = result_dir / result_filename;
        
        // Write clean format file: start, end (one pair per line)
        // If no END event, write: start,
        std::ofstream out(result_path);
        if (!out.is_open()) {
            continue; // Skip this channel if we can't write
        }
        
        // Iterate through events one by one, pairing START with next END
        uint32_t pending_start = 0;
        bool has_pending_start = false;
        
        for (const auto& event : ch_events) {
            if (event.type == SeizureEvent::Type::START) {
                // If there's already a pending START without END, write it as incomplete
                if (has_pending_start) {
                    out << pending_start << ",\n";
                }
                pending_start = event.timestamp;
                has_pending_start = true;
            } else if (event.type == SeizureEvent::Type::END && has_pending_start) {
                // Pair this END with the pending START
                out << pending_start << ", " << event.timestamp << "\n";
                has_pending_start = false;
            }
        }
        
        // Write any remaining pending START without END
        if (has_pending_start) {
            out << pending_start << ",\n";
        }
        
        out.close();
    }
    
    return true;
}
