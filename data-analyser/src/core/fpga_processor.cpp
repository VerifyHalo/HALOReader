#include "fpga_processor.h"
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
{
}

FpgaProcessor::~FpgaProcessor() {
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

    // Compute per-channel mean for DC removal.
    // The FPGA NEO = x[n]^2 - x[n-1]*x[n+1] operates on raw ADC codes, so a large DC
    // offset (≈32768) generates cross-terms proportional to DC*amplitude that dwarf the
    // pure oscillatory energy. Subtracting the mean and re-centering at 32768 makes NEO
    // reflect oscillatory energy only, giving meaningful threshold comparisons.
    std::vector<double> ch_mean(num_channels, 32768.0);
    for (uint32_t ch = 0; ch < num_channels; ++ch) {
        double sum = 0.0;
        uint32_t count = 0;
        for (uint32_t s = 0; s < num_samples; ++s) {
            uint32_t idx = ch * num_samples + s;
            if (idx < data_channels.size()) {
                sum += data_channels[idx];
                ++count;
            }
        }
        if (count > 0) ch_mean[ch] = sum / count;
    }

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
                    // DC-remove: subtract channel mean, re-center at 32768
                    int32_t centered = static_cast<int32_t>(data_channels[data_idx])
                                       - static_cast<int32_t>(ch_mean[ch]) + 32768;
                    centered = std::max(0, std::min(65535, centered));
                    code16 = static_cast<uint16_t>(centered);
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

    // Minimum detection duration: discard START/END pairs shorter than this many samples.
    // At 1 kHz, 100 samples = 100 ms.  Shorter events are almost certainly noise bursts
    // or FPGA FIFO overflow artefacts.
    static constexpr uint32_t MIN_DURATION_SAMPLES = 100;

    // Collect raw events first, then filter.
    struct RawEvent { uint8_t channel; uint8_t code; uint32_t ts; };
    std::vector<RawEvent> raw;

    // Parse 32-bit words: [31:30]=event_code, [29:25]=channel_id, [24:0]=timestamp
    const uint32_t warmup_samples = WARMUP_CHUNKS * SAMPLES_PER_CHUNK;
    for (size_t i = 0; i + 4 <= output_data.size(); i += 4) {
        uint32_t word = static_cast<uint32_t>(output_data[i]) |
                       (static_cast<uint32_t>(output_data[i+1]) << 8) |
                       (static_cast<uint32_t>(output_data[i+2]) << 16) |
                       (static_cast<uint32_t>(output_data[i+3]) << 24);

        uint8_t event_code = (word >> 30) & 0x3;
        uint8_t channel_id = (word >> 25) & 0x1F;
        uint32_t ts        = word & 0x01FFFFFF;

        if (channel_id >= num_channels) continue;
        if (event_code != 0x1 && event_code != 0x2) continue;

        // Discard warmup-period events and re-base to real-data timeline
        if (ts < warmup_samples) continue;
        ts -= warmup_samples;

        raw.push_back({channel_id, event_code, ts});
    }

    // Per-channel pairing: emit only START/END pairs with duration >= MIN_DURATION_SAMPLES.
    // Unpaired STARTs (no matching END) are passed through so the GUI can show
    // "detection ran to end of file" — but only if the recording had real activity.
    std::map<uint8_t, uint32_t> pending_start;   // channel → START timestamp
    std::map<uint8_t, bool>     has_pending;

    for (const auto& e : raw) {
        if (e.code == 0x1) { // START
            has_pending[e.channel] = true;
            pending_start[e.channel] = e.ts;
        } else if (e.code == 0x2 && has_pending[e.channel]) { // END
            uint32_t start_ts = pending_start[e.channel];
            uint32_t duration = (e.ts >= start_ts) ? (e.ts - start_ts) : 0;
            if (duration >= MIN_DURATION_SAMPLES) {
                std::cerr << "[FPGA] START ch=" << (int)e.channel
                          << " ts=" << start_ts << "ms  dur=" << duration << "ms" << std::endl;
                SeizureEvent s;
                s.type = SeizureEvent::Type::START;
                s.channel = e.channel; s.timestamp = start_ts; s.sample_index = start_ts;
                events.push_back(s);

                SeizureEvent end;
                end.type = SeizureEvent::Type::END;
                end.channel = e.channel; end.timestamp = e.ts; end.sample_index = e.ts;
                events.push_back(end);
            } else {
                std::cerr << "[FPGA] SKIP  ch=" << (int)e.channel
                          << " ts=" << start_ts << " dur=" << duration << "ms (<min)" << std::endl;
            }
            has_pending[e.channel] = false;
        }
    }

    // Flush any pending STARTs (detection ran to end of recording)
    for (auto& [ch, pending] : has_pending) {
        if (pending) {
            uint32_t start_ts = pending_start[ch];
            std::cerr << "[FPGA] START ch=" << (int)ch
                      << " ts=" << start_ts << "ms  (no END — ran to EOF)" << std::endl;
            SeizureEvent s;
            s.type = SeizureEvent::Type::START;
            s.channel = ch; s.timestamp = start_ts; s.sample_index = start_ts;
            events.push_back(s);
        }
    }
}

std::vector<uint8_t> FpgaProcessor::drainPipeOut() {
    std::vector<uint8_t> result;
    size_t total_nonzero = 0;
    for (int attempt = 0; attempt < 20; ++attempt) {
        auto buf = device_->readFromPipeOut(PIPE_OUT_ADDR, EVENT_BUFFER_SIZE);
        if (buf.empty()) break;           // read error
        bool has_events = false;
        for (size_t i = 0; i + 3 < buf.size(); i += 4) {
            uint32_t w = buf[i] | (buf[i+1]<<8) | (buf[i+2]<<16) | (buf[i+3]<<24);
            if (w) { has_events = true; ++total_nonzero; }
        }
        if (!has_events) break;           // FIFO empty
        result.insert(result.end(), buf.begin(), buf.end());
        if (total_nonzero >= MAX_EVENTS) {
            std::cerr << "[FPGA] drainPipeOut: hit MAX_EVENTS cap (" << MAX_EVENTS
                      << ") — FPGA FIFO is overflowing (signal too energetic or threshold too low)" << std::endl;
            break;
        }
    }
    return result;
}

bool FpgaProcessor::processData(const std::vector<uint16_t>& data_channels,
                               uint32_t num_channels,
                               uint32_t num_samples,
                               std::vector<SeizureEvent>& events) {
    if (!initialized_ || !device_) {
        last_error_ = "FPGA not initialized";
        return false;
    }

    // Limit to the first MAX_ACTIVE_CHANNELS channels.
    // Each word carries its own channel_id, so the FPGA handles fewer channels
    // without any framing issues — it simply never receives data for ch >= limit.
    if (num_channels > MAX_ACTIVE_CHANNELS) {
        std::cerr << "[FPGA] Capping channels " << num_channels
                  << " → " << MAX_ACTIVE_CHANNELS << std::endl;
        num_channels = MAX_ACTIVE_CHANNELS;
    }

    // Step 1: Set parameters before touching reset so FPGA latches known-good values
    std::random_device rd;
    std::mt19937 gen(rd());
    uint64_t ts = gen();
    device_->setWireInValue(WIREIN_THRESHOLD, threshold_ & 0xFFFFFFFF, 0xFFFFFFFF);
    device_->setWireInValue(WIREIN_WINDOW_TIMEOUT, window_timeout_ & 0xFFFFFFFF, 0xFFFFFFFF);
    device_->setWireInValue(WIREIN_TRANSITION_COUNT, transition_count_ & 0xFFFFFFFF, 0xFFFFFFFF);
    device_->setWireInValue(WIREIN_TS_LO, ts & 0xFFFFFFFF, 0xFFFFFFFF);
    device_->setWireInValue(WIREIN_TS_HI, (ts >> 32) & 0xFFFFFFFF, 0xFFFFFFFF);
    device_->updateWireIns();  // commit params before asserting reset

    // Step 2: Assert reset
    device_->setWireInValue(WIREIN_CTRL, 0x80000000, 0xFFFFFFFF);
    device_->updateWireIns();

    // Step 3: Release reset
    device_->setWireInValue(WIREIN_CTRL, 0x00000000, 0xFFFFFFFF);
    device_->updateWireIns();

    // Step 4: Wait for FPGA internal state machine and PipeIn FIFO to become ready
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Step 5: Drain any stale events that survived the reset in the USB host-side buffer.
    // A soft reset clears FPGA logic state but does NOT flush the OpalKelly PipeOut
    // USB endpoint buffer on the host. Events left over from the previous file would
    // otherwise be parsed as belonging to the current file.
    {
        std::vector<uint8_t> stale = drainPipeOut();
        if (!stale.empty()) {
            std::cerr << "[FPGA] Pre-run drain: discarded " << stale.size()
                      << " stale bytes from FIFO" << std::endl;
        }
    }

    // Generate chunks
    std::vector<std::vector<uint8_t>> chunks = generateChunks(data_channels, num_channels, num_samples);

    std::cerr << "[FPGA] Sending " << WARMUP_CHUNKS << " warmup + " << chunks.size() << " chunks, "
              << num_channels << " ch x " << num_samples << " samples" << std::endl;

    // Send warmup chunks (ADC midpoint = 32768) to stabilize FPGA NEO registers
    for (uint32_t wc = 0; wc < WARMUP_CHUNKS; ++wc) {
        std::vector<uint8_t> warmup;
        warmup.reserve(num_channels * SAMPLES_PER_CHUNK * SAMPLE_SIZE_BYTES);
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            for (uint32_t s = 0; s < SAMPLES_PER_CHUNK; ++s) {
                uint32_t word32 = (ch << 16) | 32768u;
                warmup.push_back(word32 & 0xFF);
                warmup.push_back((word32 >> 8) & 0xFF);
                warmup.push_back((word32 >> 16) & 0xFF);
                warmup.push_back((word32 >> 24) & 0xFF);
            }
        }
        size_t rem = warmup.size() % 16;
        if (rem) warmup.resize(warmup.size() + (16 - rem), 0);
        int status = device_->writeToPipeIn(PIPE_IN_ADDR, warmup);
        if (status < 0) {
            last_error_ = "WriteToPipeIn failed warmup chunk " + std::to_string(wc)
                          + " (code=" + std::to_string(status) + ")";
            return false;
        }
    }

    // Send all chunks (no concurrent reading)
    for (size_t i = 0; i < chunks.size(); ++i) {
        int status = device_->writeToPipeIn(PIPE_IN_ADDR, chunks[i]);
        if (status < 0) {
            last_error_ = "WriteToPipeIn failed chunk " + std::to_string(i)
                          + " (code=" + std::to_string(status) + ")";
            return false;
        }
        if ((i + 1) % 50 == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Wait for FPGA to finish processing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Drain PipeOut — read until FIFO is empty (all-zero buffer)
    std::vector<uint8_t> output_data = drainPipeOut();
    
    // Log what came back
    size_t nonzero = 0;
    for (size_t i = 0; i + 3 < output_data.size(); i += 4) {
        uint32_t w = output_data[i] | (output_data[i+1]<<8) | (output_data[i+2]<<16) | (output_data[i+3]<<24);
        if (w != 0) nonzero++;
    }
    std::cerr << "[FPGA] PipeOut: " << output_data.size() << " bytes total, "
              << nonzero << " non-zero 32-bit words" << std::endl;
    if (nonzero > 0) {
        std::cerr << "[FPGA] First non-zero words: ";
        int shown = 0;
        for (size_t i = 0; i + 3 < output_data.size() && shown < 8; i += 4) {
            uint32_t w = output_data[i] | (output_data[i+1]<<8) | (output_data[i+2]<<16) | (output_data[i+3]<<24);
            if (w != 0) { std::cerr << "0x" << std::hex << w << std::dec << " "; shown++; }
        }
        std::cerr << std::endl;
    }

    // Parse events
    parseEvents(output_data, num_channels, events);
    std::cerr << "[FPGA] Parsed " << events.size() << " events" << std::endl;
    
    return true;
}
