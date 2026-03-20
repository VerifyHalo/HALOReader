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
    : external_fpga_(nullptr)
    , fpga_initialized_(false)
    , threshold_(threshold)
    , window_timeout_(window_timeout)
    , transition_count_(transition_count)
{
    // Try to initialize FPGA (non-blocking, will try again on first use)
    initializeFpga();
}

SeizureProcessor::SeizureProcessor(FpgaProcessor* externalFpga,
                                   uint32_t threshold,
                                   uint32_t window_timeout,
                                   uint32_t transition_count)
    : external_fpga_(externalFpga)
    , fpga_initialized_(true)  // device is already open and programmed
    , threshold_(threshold)
    , window_timeout_(window_timeout)
    , transition_count_(transition_count)
{
    // Apply initial parameters to the external device
    if (external_fpga_) {
        external_fpga_->setThreshold(threshold_);
        external_fpga_->setWindowTimeout(window_timeout_);
        external_fpga_->setTransitionCount(transition_count_);
    }
}

void SeizureProcessor::setThreshold(uint32_t threshold) {
    threshold_ = threshold;
    if (external_fpga_) {
        external_fpga_->setThreshold(threshold);
    } else if (fpga_processor_) {
        fpga_processor_->setThreshold(threshold);
    }
}

void SeizureProcessor::setWindowTimeout(uint32_t timeout) {
    window_timeout_ = timeout;
    if (external_fpga_) {
        external_fpga_->setWindowTimeout(timeout);
    } else if (fpga_processor_) {
        fpga_processor_->setWindowTimeout(timeout);
    }
}

void SeizureProcessor::setTransitionCount(uint32_t count) {
    transition_count_ = count;
    if (external_fpga_) {
        external_fpga_->setTransitionCount(count);
    } else if (fpga_processor_) {
        fpga_processor_->setTransitionCount(count);
    }
}

bool SeizureProcessor::initializeFpga() {
    if (fpga_initialized_) {
        return true;  // covers both external_fpga_ path and already-initialized owned path
    }
    
    if (!std::filesystem::exists(BITFILE_PATH)) {
        std::cerr << "[FPGA] Bitfile not found at: " << BITFILE_PATH << std::endl;
        return false;
    }
    
    try {
        fpga_processor_ = std::make_unique<FpgaProcessor>(
            threshold_,
            window_timeout_,
            transition_count_
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
    FpgaProcessor* active = external_fpga_ ? external_fpga_ : fpga_processor_.get();
    if (!active || !fpga_initialized_) {
        return false;
    }
    return active->processData(data_channels, num_channels, num_samples, events);
}


bool SeizureProcessor::processFile(const std::string& rhd_filepath) {
    last_error_.clear();

    // Read RHD file
    RhdReader::RhdData rhd_data;
    if (!RhdReader::readFile(rhd_filepath, rhd_data)) {
        last_error_ = "Failed to read RHD file: " + rhd_filepath;
        return false;
    }

    // Extract data channels
    std::vector<uint16_t> data_channels = RhdReader::extractDataChannels(rhd_data);
    if (data_channels.empty()) {
        last_error_ = "No data channels extracted from RHD file";
        return false;
    }

    uint32_t num_data_channels = rhd_data.num_channels;
    uint32_t num_samples = rhd_data.num_samples;

    // Always create the result directory so the folder appears even if FPGA is unavailable
    {
        std::filesystem::path rhd_path(rhd_filepath);
        std::filesystem::path result_dir = rhd_path.parent_path() / rhd_path.stem();
        std::filesystem::create_directories(result_dir);
    }

    // FPGA-only processing
    std::vector<SeizureEvent> events;

    std::cerr << "[Processor] external_fpga_=" << (void*)external_fpga_
              << " fpga_processor_=" << (void*)fpga_processor_.get()
              << " fpga_initialized_=" << fpga_initialized_ << std::endl;

    if (fpga_initialized_ || initializeFpga()) {
        if (!processWithFpga(data_channels, num_data_channels, num_samples, events)) {
            last_error_ = "FPGA processing failed: " +
                (external_fpga_ ? external_fpga_->getLastError() :
                 fpga_processor_ ? fpga_processor_->getLastError() : "Not initialized");
            std::cerr << "[Processor] " << last_error_ << std::endl;
            return false;
        }
    } else {
        last_error_ = "FPGA not available - check device connection and bitfile";
        std::cerr << "[Processor] " << last_error_ << std::endl;
        return false;
    }

    // Write detection results
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

    // Write a file for every channel (empty if no detections)
    for (uint32_t channel = 0; channel < num_channels; ++channel) {
        std::string result_filename = "ch" + std::to_string(channel) + "_" + stem + ".txt";
        std::filesystem::path result_path = result_dir / result_filename;

        std::ofstream out(result_path);
        if (!out.is_open()) {
            continue;
        }

        auto it = events_by_channel.find(channel);
        if (it != events_by_channel.end()) {
            const auto& ch_events = it->second;

            uint32_t pending_start = 0;
            bool has_pending_start = false;

            for (const auto& event : ch_events) {
                if (event.type == SeizureEvent::Type::START) {
                    if (has_pending_start) {
                        out << pending_start << ",\n";
                    }
                    pending_start = event.timestamp;
                    has_pending_start = true;
                } else if (event.type == SeizureEvent::Type::END && has_pending_start) {
                    out << pending_start << ", " << event.timestamp << "\n";
                    has_pending_start = false;
                }
            }

            // Pending START with no END — detection ran to end of recording
            if (has_pending_start) {
                out << pending_start << ",\n";
            }
        }
        // else: empty file — channel was processed but had no detections

        out.close();
    }
    
    return true;
}
