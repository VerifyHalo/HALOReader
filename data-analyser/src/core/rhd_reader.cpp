#include "rhd_reader.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <filesystem>

namespace {
    // RHD file format constants
    const uint32_t RHD_MAGIC_NUMBER = 0xc6912702;
    const int SAMPLES_PER_BLOCK_V1 = 60;
    const int SAMPLES_PER_BLOCK_V2 = 128;
    
    // Helper function to read little-endian values
    template<typename T>
    T read_le(std::ifstream& file) {
        T value;
        file.read(reinterpret_cast<char*>(&value), sizeof(T));
        // Convert from little-endian if needed (most systems are little-endian)
        return value;
    }
    
    // Read QString (Qt string format)
    std::string read_qstring(std::ifstream& file) {
        uint32_t length;
        file.read(reinterpret_cast<char*>(&length), 4);
        
        if (length == 0xFFFFFFFF) {
            return "";
        }
        
        if (length == 0) {
            return "";
        }
        
        // Convert from bytes to 16-bit Unicode words
        uint32_t num_chars = length / 2;
        std::string result;
        result.reserve(num_chars);
        
        for (uint32_t i = 0; i < num_chars; ++i) {
            uint16_t c;
            file.read(reinterpret_cast<char*>(&c), 2);
            if (c < 128) { // Simple ASCII conversion
                result += static_cast<char>(c);
            } else {
                result += '?'; // Placeholder for non-ASCII
            }
        }
        
        return result;
    }
    
    struct RhdHeader {
        uint16_t version_major;
        uint16_t version_minor;
        float sample_rate;
        uint16_t num_samples_per_block;
        uint32_t num_amplifier_channels;
        uint32_t num_aux_input_channels;
        uint32_t num_supply_voltage_channels;
        uint32_t num_board_adc_channels;
        uint32_t num_board_dig_in_channels;
        uint32_t num_board_dig_out_channels;
        uint32_t num_temp_sensor_channels;
        std::vector<uint32_t> amplifier_channel_indices; // Native order for each channel
    };
    
    bool read_header(std::ifstream& file, RhdHeader& header) {
        // Check magic number
        uint32_t magic = read_le<uint32_t>(file);
        if (magic != RHD_MAGIC_NUMBER) {
            std::cerr << "Invalid RHD file: wrong magic number" << std::endl;
            return false;
        }
        
        // Read version
        header.version_major = read_le<uint16_t>(file);
        header.version_minor = read_le<uint16_t>(file);
        
        // Determine samples per block
        if (header.version_major > 1) {
            header.num_samples_per_block = SAMPLES_PER_BLOCK_V2;
        } else {
            header.num_samples_per_block = SAMPLES_PER_BLOCK_V1;
        }
        
        // Read sample rate
        header.sample_rate = read_le<float>(file);
        
        // Skip frequency settings (26 bytes)
        file.seekg(26, std::ios::cur);
        
        // Skip notch filter frequency (2 bytes)
        file.seekg(2, std::ios::cur);
        
        // Skip impedance test frequencies (8 bytes)
        file.seekg(8, std::ios::cur);
        
        // Skip notes (3 QStrings)
        for (int i = 0; i < 3; ++i) {
            read_qstring(file);
        }
        
        // Read num temp sensor channels
        uint16_t num_temp_sensors = 0;
        if ((header.version_major == 1 && header.version_minor >= 1) || header.version_major > 1) {
            num_temp_sensors = read_le<uint16_t>(file);
        }
        
        // Skip eval board mode (2 bytes)
        if ((header.version_major == 1 && header.version_minor >= 3) || header.version_major > 1) {
            file.seekg(2, std::ios::cur);
        }
        
        // Skip reference channel (QString for v2.0+)
        if (header.version_major > 1) {
            read_qstring(file);
        }
        
        // Read signal summary
        uint16_t num_signal_groups = read_le<uint16_t>(file);
        
        header.num_amplifier_channels = 0;
        header.num_aux_input_channels = 0;
        header.num_supply_voltage_channels = 0;
        header.num_board_adc_channels = 0;
        header.num_board_dig_in_channels = 0;
        header.num_board_dig_out_channels = 0;
        header.amplifier_channel_indices.clear();
        
        // Read each signal group
        for (uint16_t group = 0; group < num_signal_groups; ++group) {
            std::string signal_group_name = read_qstring(file);
            std::string signal_group_prefix = read_qstring(file);
            
            uint16_t signal_group_enabled, signal_group_num_channels, dummy;
            file.read(reinterpret_cast<char*>(&signal_group_enabled), 2);
            file.read(reinterpret_cast<char*>(&signal_group_num_channels), 2);
            file.read(reinterpret_cast<char*>(&dummy), 2);
            
            if (signal_group_num_channels > 0 && signal_group_enabled > 0) {
                for (uint16_t ch = 0; ch < signal_group_num_channels; ++ch) {
                    // Read channel information
                    std::string native_channel_name = read_qstring(file);
                    std::string custom_channel_name = read_qstring(file);
                    
                    int16_t native_order, custom_order, signal_type, channel_enabled, chip_channel, board_stream;
                    file.read(reinterpret_cast<char*>(&native_order), 2);
                    file.read(reinterpret_cast<char*>(&custom_order), 2);
                    file.read(reinterpret_cast<char*>(&signal_type), 2);
                    file.read(reinterpret_cast<char*>(&channel_enabled), 2);
                    file.read(reinterpret_cast<char*>(&chip_channel), 2);
                    file.read(reinterpret_cast<char*>(&board_stream), 2);
                    
                    // Skip trigger channel info (8 bytes)
                    file.seekg(8, std::ios::cur);
                    
                    // Skip impedance (8 bytes)
                    file.seekg(8, std::ios::cur);
                    
                    // Count channels by type
                    if (channel_enabled) {
                        if (signal_type == 0) { // Amplifier
                            header.amplifier_channel_indices.push_back(header.num_amplifier_channels);
                            header.num_amplifier_channels++;
                        } else if (signal_type == 1) { // Aux input
                            header.num_aux_input_channels++;
                        } else if (signal_type == 2) { // Supply voltage
                            header.num_supply_voltage_channels++;
                        } else if (signal_type == 3) { // Board ADC
                            header.num_board_adc_channels++;
                        } else if (signal_type == 4) { // Digital input
                            header.num_board_dig_in_channels++;
                        } else if (signal_type == 5) { // Digital output
                            header.num_board_dig_out_channels++;
                        }
                    }
                }
            }
        }
        
        header.num_temp_sensor_channels = num_temp_sensors;
        
        return true;
    }
    
    // Calculate bytes per data block
    uint32_t calculate_bytes_per_block(const RhdHeader& header) {
        uint32_t bytes = 0;
        
        // Timestamps: 4 bytes per sample
        bytes += header.num_samples_per_block * 4;
        
        // Amplifier data: 2 bytes per sample per channel
        bytes += header.num_samples_per_block * header.num_amplifier_channels * 2;
        
        // Auxiliary data: 2 bytes per (sample/4) per channel
        bytes += (header.num_samples_per_block / 4) * header.num_aux_input_channels * 2;
        
        // Supply voltage: 2 bytes per block per channel
        bytes += 1 * header.num_supply_voltage_channels * 2;
        
        // Board ADC: 2 bytes per sample per channel
        bytes += header.num_samples_per_block * header.num_board_adc_channels * 2;
        
        // Digital inputs: 2 bytes per sample (if any channels enabled)
        if (header.num_board_dig_in_channels > 0) {
            bytes += header.num_samples_per_block * 1 * 2;
        }
        
        // Digital outputs: 2 bytes per sample (if any channels enabled)
        if (header.num_board_dig_out_channels > 0) {
            bytes += header.num_samples_per_block * 1 * 2;
        }
        
        // Temp sensor: 2 bytes per block per channel
        bytes += 1 * header.num_temp_sensor_channels * 2;
        
        return bytes;
    }
    
    bool read_data_blocks(std::ifstream& file, const RhdHeader& header, 
                         std::vector<std::vector<uint16_t>>& amplifier_data) {
        // Get current position
        size_t header_end = file.tellg();
        
        // Get file size
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        
        // Seek back to after header
        file.seekg(header_end, std::ios::beg);
        
        // Calculate bytes per block
        uint32_t bytes_per_block = calculate_bytes_per_block(header);
        
        // Calculate number of blocks
        size_t data_bytes = file_size - header_end;
        if (data_bytes == 0) {
            return false; // No data
        }
        
        uint32_t num_blocks = static_cast<uint32_t>(data_bytes / bytes_per_block);
        
        if (data_bytes % bytes_per_block != 0) {
            std::cerr << "Warning: File size doesn't match expected block size. "
                      << "Expected multiple of " << bytes_per_block 
                      << " bytes, got " << data_bytes << " bytes. "
                      << "Calculated " << num_blocks << " blocks, "
                      << (data_bytes % bytes_per_block) << " bytes remaining." << std::endl;
            // Continue anyway - might be a rounding issue or extra padding
        }
        uint32_t num_samples = num_blocks * header.num_samples_per_block;
        
        // Initialize amplifier data arrays
        amplifier_data.resize(header.num_amplifier_channels);
        for (uint32_t ch = 0; ch < header.num_amplifier_channels; ++ch) {
            amplifier_data[ch].resize(num_samples);
        }
        
        // Read each data block
        for (uint32_t block = 0; block < num_blocks; ++block) {
            // Read timestamps (skip them)
            file.seekg(header.num_samples_per_block * 4, std::ios::cur);
            
            // Read amplifier data for this block
            for (uint32_t sample = 0; sample < header.num_samples_per_block; ++sample) {
                for (uint32_t ch = 0; ch < header.num_amplifier_channels; ++ch) {
                    uint16_t value = read_le<uint16_t>(file);
                    uint32_t sample_idx = block * header.num_samples_per_block + sample;
                    amplifier_data[ch][sample_idx] = value;
                }
            }
            
            // Skip aux input data
            file.seekg((header.num_samples_per_block / 4) * header.num_aux_input_channels * 2, std::ios::cur);
            
            // Skip supply voltage data
            file.seekg(1 * header.num_supply_voltage_channels * 2, std::ios::cur);
            
            // Skip temp sensor data
            file.seekg(1 * header.num_temp_sensor_channels * 2, std::ios::cur);
            
            // Skip board ADC data
            file.seekg(header.num_samples_per_block * header.num_board_adc_channels * 2, std::ios::cur);
            
            // Skip digital input data
            if (header.num_board_dig_in_channels > 0) {
                file.seekg(header.num_samples_per_block * 2, std::ios::cur);
            }
            
            // Skip digital output data
            if (header.num_board_dig_out_channels > 0) {
                file.seekg(header.num_samples_per_block * 2, std::ios::cur);
            }
        }
        
        return true;
    }
}

bool RhdReader::readFile(const std::string& filepath, RhdData& data) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open RHD file: " << filepath << std::endl;
        return false;
    }
    
    // Read header
    RhdHeader header;
    if (!read_header(file, header)) {
        std::cerr << "Failed to read RHD header" << std::endl;
        return false;
    }
    
    if (header.num_amplifier_channels == 0) {
        std::cerr << "No amplifier channels found in RHD file" << std::endl;
        return false;
    }
    
    // Read data blocks
    std::vector<std::vector<uint16_t>> amplifier_data;
    if (!read_data_blocks(file, header, amplifier_data)) {
        std::cerr << "Failed to read RHD data blocks" << std::endl;
        return false;
    }
    
    // Copy to output structure
    data.amplifier_data = std::move(amplifier_data);
    data.sample_rate = header.sample_rate;
    data.num_channels = header.num_amplifier_channels;
    data.num_samples = data.amplifier_data.empty() ? 0 : data.amplifier_data[0].size();
    
    return true;
}

std::vector<uint16_t> RhdReader::extractDataChannels(const RhdData& rhd_data) {
    std::vector<uint16_t> result;
    
    // Skip channels 0-1 (reference/ground), use channels 2-31
    uint32_t num_data_channels = std::min(static_cast<uint32_t>(30), 
                                         rhd_data.num_channels > 2 ? rhd_data.num_channels - 2 : 0);
    if (num_data_channels == 0 || rhd_data.num_samples == 0) {
        return result;
    }
    
    result.reserve(num_data_channels * rhd_data.num_samples);
    
    // Extract channels 2-31 (map to FPGA channels 0-29)
    for (uint32_t fpga_ch = 0; fpga_ch < num_data_channels; ++fpga_ch) {
        uint32_t rhd_ch = fpga_ch + 2;  // FPGA channel 0 = RHD channel 2
        if (rhd_ch < rhd_data.amplifier_data.size()) {
            const auto& channel_data = rhd_data.amplifier_data[rhd_ch];
            result.insert(result.end(), channel_data.begin(), channel_data.end());
        }
    }
    
    return result;
}
