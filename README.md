# HALOReader — HALO Seizure Detection Pipeline

**HALOReader** is the host-side C++/Qt application that drives the HALO Brain-Computer Interface seizure detection system. It reads neural recordings from Intan RHD files, streams them to an Opal Kelly XEM7310 FPGA running the HALO bitstream (`detection.bit`), and parses the seizure-start / seizure-end events that come back out.

This document walks through the full in → FPGA → out path with the relevant code at each step, then notes every overflow or correctness concern found in the implementation.

---

## System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        Host (macOS / C++)                        │
│                                                                  │
│  .rhd file ──► RhdReader ──► channels[2..31] (uint16_t)         │
│                                  │                               │
│                             generateChunks()                     │
│                                  │                               │
│              32-bit words: [21:16]=ch_id [15:0]=ADC              │
│                                  │                               │
│              WireIns: threshold, window_timeout, trans_count     │
│                                  │                               │
└──────────────────────────────────┼──────────────────────────────┘
                                   │ USB 3.0 (OpalKelly FrontPanel)
                   PipeIn 0x80 ────▼──── PipeOut 0xA0
┌─────────────────────────────────────────────────────────────────┐
│              Opal Kelly XEM7310 (Artix-7, 100.8 MHz)             │
│                                                                  │
│  fifoIn ──► [21:16] ch_id / [15:0] ADC ──► datapath.sv          │
│                                                ├─ Stage 0: history│
│                                                ├─ Stage 1: NEO   │
│                                                └─ Stage 2: FSM   │
│                                                     │            │
│  fifoOut ◄── [31:30] event_code [29:25] ch [24:0] ts             │
└─────────────────────────────────────────────────────────────────┘
                                   │
                    parseEvents() ─▼
                    SeizureEvent { START | END, channel, timestamp }
```

---

## 1. Data Input — Intan RHD Files

The Intan RHD2000 amplifier produces binary `.rhd` files. The reader (`rhd_reader.h`) extracts amplifier data as raw 16-bit ADC codes.

```cpp
// rhd_reader.h
struct RhdData {
    std::vector<std::vector<uint16_t>> amplifier_data;  // [channel][sample]
    double   sample_rate;   // typically 20 000 or 30 000 Hz
    uint32_t num_channels;
    uint32_t num_samples;
};
```

**Key constants:**
- ADC range: 0–65535, midpoint = **32768** (maps to 0 µV)
- Samples per data block: 60 (RHD v1) or **128** (RHD v2+)
- Channel layout: channels 0–1 are reference/ground; the pipeline uses **channels 2–31** (30 neural channels)

```cpp
// Skip reference channels — extract channels 2-31 in flat channel-major order
// output: [ch0_s0, ch0_s1, ..., ch0_sN, ch1_s0, ...]
static std::vector<uint16_t> extractDataChannels(const RhdData& rhd_data);
```

---

## 2. Raw Log Format (On-Disk Storage)

Before FPGA processing, every acquisition block is saved to `.log` files in a flat binary format (`raw_log_format.h`). The format is intentionally simple — no HDF5 dependency, memory-mappable.

```
FileHeader (32 bytes, little-endian):
  char[8]  magic            = "HALOLOG\0"
  uint16_t version          = 1
  uint16_t reserved         = 0
  uint32_t channel_count    = 32
  uint32_t samples_per_record = 128
  uint32_t sample_bits      = 16
  uint32_t timestamp_bits   = 32

Record (8720 bytes, repeated):
  uint64_t unix_time_ns          —  wall-clock capture time
  uint32_t sequence_index        —  monotonic record counter
  uint32_t payload_bytes         —  fixed: 8704 (512 + 8192)
  uint32_t timestamps[128]       —  512 bytes, one per sample
  uint16_t waveform[32 * 128]    —  8192 bytes, channel-major:
                                     waveform[channel * 128 + sample]
```

Total per record: 16 (record header) + 512 + 8192 = **8720 bytes**.

---

## 3. FPGA Setup — WireIns and Reset

`FpgaProcessor::processData()` configures the FPGA before streaming any data.

### 3.1 WireIn Endpoint Map

| Address | Signal | Default | Purpose |
|---------|--------|---------|---------|
| `0x00` | `ep00wire[31]` | — | Reset (pulse high then low) |
| `0x01` | `ts_in_lo_w` | random | Timestamp seed, lower 32 bits |
| `0x02` | `ts_in_hi_w` | random | Timestamp seed, upper 32 bits |
| `0x03` | `threshold_value_w` | **25000** | NEO threshold |
| `0x04` | `window_timeout_w` | **200** | Samples without detection before END event |
| `0x05` | `transition_count_w` | **30** | Consecutive detections to fire START event |

The FPGA latches these on every `okClk` rising edge, falling back to hard-coded defaults if the host sends zero:

```verilog
// First.sv
always @ (posedge okClk) begin
    reset_ro          <= ep00wire[31];
    threshold_value_ro  <= threshold_value_w != 32'd0 ? threshold_value_w : 32'd25000;
    window_timeout_ro   <= window_timeout_w  != 32'd0 ? window_timeout_w  : 32'd200;
    transition_count_ro <= transition_count_w != 32'd0 ? transition_count_w : 32'd30;
end
```

### 3.2 Reset Sequence

```cpp
// fpga_processor.cpp — processData()
device_->setWireInValue(WIREIN_CTRL, 0x80000000, 0xFFFFFFFF);  // assert reset
device_->updateWireIns();
device_->setWireInValue(WIREIN_CTRL, 0x00000000, 0xFFFFFFFF);  // release reset
device_->updateWireIns();
```

This pulses `ep00wire[31]` which drives `srst` on both internal FIFOs and `rst_n` on the datapath, clearing all per-channel state.

---

## 4. Input Packet — Host to FPGA (PipeIn 0x80)

Each 16-bit ADC sample is packed into a 32-bit word and sent through the bulk USB pipe:

```cpp
// fpga_processor.cpp — generateChunks()
uint32_t word32 = (ch << 16) | code16;
//  bits [21:16] = channel_id  (channels 0-29 map to RHD channels 2-31)
//  bits [15:0]  = ADC code    (0-65535, midpoint 32768)
```

Chunks are `SAMPLES_PER_CHUNK = 128` samples × `num_channels` × 4 bytes each, padded to a 16-byte boundary for USB 3.0:

```cpp
static constexpr uint32_t SAMPLES_PER_CHUNK  = 128;
static constexpr uint32_t SAMPLE_SIZE_BYTES  = 4;
static constexpr size_t   MAX_EVENTS         = 10000;
static constexpr size_t   EVENT_BUFFER_SIZE  = MAX_EVENTS * SAMPLE_SIZE_BYTES;  // 40 KB
```

The FPGA receives the words and unpacks them one cycle at a time:

```verilog
// First.sv — input handler
always @ (posedge okClk) begin
    ...
    if (!in_wait_data_ro) begin
        if (!fifoInEmpty_ro) begin
            fifoInRead_ri   <= 1'b1;
            in_wait_data_ro <= 1'b1;
        end
    end else begin
        dp_data_ro       <= fifoInDataOut_ro[15:0];   // ADC code
        dp_channel_id_ro <= fifoInDataOut_ro[21:16];  // channel_id
        dp_data_valid_ro <= 1'b1;
        in_wait_data_ro  <= 1'b0;
    end
end
```

One word per two clock cycles (read request + data valid).

---

## 5. FPGA Datapath — `datapath.sv`

The datapath is a 3-stage synchronous pipeline clocked at **100.8 MHz** (`okClk`). It processes one sample per two cycles and maintains independent state for each of the 32 channels.

```verilog
// datapath.sv — top-level ports
module datapath (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [31:0] threshold_value,
    input  wire [31:0] window_timeout,
    input  wire [31:0] transition_count,
    input  wire        data_valid,
    input  wire [15:0] data,          // 16-bit ADC code
    input  wire [5:0]  channel_id,    // 0-31

    output reg         output_valid,
    output reg [31:0]  output_timestamp,
    output reg         output_event,  // 1 = START, 0 = END
    output reg [5:0]   output_channel
);
```

### 5.1 Per-Channel State

Each of the 32 channels carries its own independent context:

```verilog
gating_state_t  channel_state              [0:31];  // NORMAL / SEIZURE
logic [15:0]    channel_continuous_counter [0:31];  // timeout counter
logic [7:0]     channel_detection_counter  [0:31];  // onset counter
logic [31:0]    channel_timestamp          [0:31];  // sample count
logic [1:0]     channel_history_count      [0:31];  // 0-3 warmup
logic [15:0]    channel_sample_history     [0:31][0:2];  // sliding NEO window
```

### 5.2 Stage 0 — Sample History Collection

A three-sample sliding window is maintained per channel. NEO computation cannot start until `history_count >= 3`.

```verilog
// Slide window: prev ← curr ← next ← new
channel_sample_history[channel_id][0] <= channel_sample_history[channel_id][1];
channel_sample_history[channel_id][1] <= channel_sample_history[channel_id][2];
channel_sample_history[channel_id][2] <= data;

// Register the multiply inputs for Stage 1
if (history_count >= 2'd3) begin
    sample_history[0] = channel_sample_history[channel_id][0];
    sample_history[1] = channel_sample_history[channel_id][1];
    sample_history[2] = channel_sample_history[channel_id][2];
    curr_sq_pipe       <= curr_sq;
    neigh_mul_pipe     <= neigh_mul;
    channel_id_sq_pipe <= channel_id;
    data_valid_sq_pipe <= 1'b1;
end
```

The multiplies (`curr_sq`, `neigh_mul`) are combinational and feed into registered pipeline outputs.

### 5.3 Stage 1 — NEO Computation

The **Nonlinear Energy Operator** computes instantaneous signal energy in a way that amplifies high-frequency bursts characteristic of seizure activity:

```
ψ[n] = x[n]² − x[n−1] × x[n+1]
```

The ADC midpoint (32768) is subtracted first to centre the signal at zero:

```verilog
// Combinational (outside always_ff)
assign x_prev_signed = $signed({1'b0, sample_history[0]}) - 17'd32768;
assign x_curr_signed = $signed({1'b0, sample_history[1]}) - 17'd32768;
assign x_next_signed = $signed({1'b0, sample_history[2]}) - 17'd32768;

assign curr_sq   = x_curr_signed * x_curr_signed;  // 17×17 → 34 bits signed
assign neigh_mul = x_prev_signed * x_next_signed;  // 17×17 → 34 bits signed
```

**Bit widths:** 16-bit ADC → 17-bit signed after centering (range ±32767) → 34-bit signed product (range ≈ ±2³⁰). No overflow is possible here with Intan data.

Inside the clocked pipeline, Stage 1 resolves the subtraction and absolute value:

```verilog
// Stage 1 (always_ff, fires when data_valid_sq_pipe == 1)
neo_val <= curr_sq_pipe - neigh_mul_pipe;
neo_abs <= neo_val[33] ? (~neo_val + 1'b1) : neo_val;  // two's-complement abs
detected <= (neo_abs > threshold_value);
```

> **Note:** Due to non-blocking assignment semantics in `always_ff`, `neo_abs` reads the _previous cycle's_ `neo_val`, and `detected` reads the _previous cycle's_ `neo_abs`. This introduces two extra pipeline register stages beyond the code comments suggest — the effective latency from sample arrival to `detected_pipe` being valid is **4 clock cycles**, not 2. This is correct behaviour, just undocumented.

### 5.4 Stage 2 — Gating State Machine

Each channel's FSM tracks whether ongoing NEO threshold crossings constitute a seizure onset or offset.

**NORMAL state** — accumulating detections:
```verilog
if (detected_pipe) begin
    detection_counter  = detection_counter + 8'd1;
    continuous_counter = 16'd0;
    if (detection_counter >= (transition_count - 1)) begin
        // → SEIZURE: fire START event
        channel_state[channel_id_pipe] <= STATE_SEIZURE;
        output_valid   <= 1'b1;
        output_event   <= 1'b1;   // START
        output_channel <= channel_id_pipe;
        output_timestamp <= channel_timestamp[channel_id_pipe];
    end
end else begin
    if (continuous_counter >= window_timeout) begin
        // Gap too long — reset onset counter
        channel_detection_counter[channel_id_pipe]  <= 8'd0;
        channel_continuous_counter[channel_id_pipe] <= 16'd0;
    end
end
```

**SEIZURE state** — waiting for activity to cease:
```verilog
if (detected_pipe) begin
    channel_seizure_right[channel_id_pipe]      <= channel_timestamp[channel_id_pipe];
    channel_continuous_counter[channel_id_pipe] <= 16'd0;
end else begin
    if (continuous_counter >= window_timeout) begin
        // → NORMAL: fire END event
        channel_state[channel_id_pipe] <= STATE_NORMAL;
        output_valid   <= 1'b1;
        output_event   <= 1'b0;   // END
        output_channel <= channel_id_pipe;
        output_timestamp <= channel_timestamp[channel_id_pipe];
    end
end
```

---

## 6. Output Packet — FPGA to Host (PipeOut 0xA0)

The top-level module (`First.sv`) watches for rising edges on `output_valid` and deduplicates events per channel before writing to the output FIFO:

```verilog
wire valid_rising    = dp_output_valid_w && !prev_output_valid_ro;
wire [1:0] event_code_current = dp_output_event_w ? 2'b01 : 2'b10;
wire event_different = (last_event_per_channel[current_channel] != event_code_current);
wire has_edge        = valid_rising && event_different;
```

Each 32-bit output word is encoded as:

```
[31:30]  event_code    — 2'b01 = START, 2'b10 = END, 2'b00 = idle
[29:25]  channel_id    — 5 bits (0-31)
[24:0]   timestamp     — lower 25 bits of the channel's sample counter
```

```verilog
assign fifoOutDataIn_w = {latched_event_code_ro,  // [31:30]
                          latched_channel_ro,      // [29:25]
                          latched_timestamp_ro};   // [24:0]
```

Words with `event_code == 2'b00` are idle fillers and are discarded by the host parser.

---

## 7. Host Event Parsing

The host reads from PipeOut 0xA0 continuously in a background thread while data is being sent:

```cpp
// fpga_processor.cpp — readWorker()
while (reading_) {
    std::vector<uint8_t> data = device_->readFromPipeOut(PIPE_OUT_ADDR, EVENT_BUFFER_SIZE);
    if (!data.empty())
        accumulated_data.insert(accumulated_data.end(), data.begin(), data.end());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
```

After all chunks are sent (plus a 500 ms drain wait), `parseEvents()` decodes the buffer:

```cpp
// fpga_processor.cpp — parseEvents()
uint8_t  event_code = (word >> 30) & 0x3;
uint8_t  channel_id = (word >> 25) & 0x1F;
uint32_t timestamp  = word & 0x01FFFFFF;  // 25-bit field

if (event_code == 0x1) {         // START
    event.type       = SeizureEvent::START;
    event.channel    = channel_id;
    event.timestamp  = timestamp;
    event.sample_index = timestamp;
} else if (event_code == 0x2) {  // END
    ...
}
```

The result is a flat `std::vector<SeizureEvent>` consumed by the Qt GUI for display and HDF5 logging.

---

## 8. Overflow & Correctness Analysis

A full review of the implementation found the following issues.

### 8.1 `detection_counter` is 8 bits, `transition_count` is 32 bits ⚠️

```verilog
logic [7:0] channel_detection_counter [0:31];  // max 255
...
if (detection_counter >= (transition_count - 1))  // transition_count is 32-bit
```

Verilog zero-extends the 8-bit counter to 32 bits for the comparison. If `transition_count > 256`, the counter saturates at 255 and the condition is never satisfied — **seizure onset never fires**. The default is 30, so normal use is safe, but setting `transition_count` above 255 silently disables detection.

**Fix:** Widen `channel_detection_counter` to 32 bits, matching `transition_count`.

### 8.2 `continuous_counter` is 16 bits, `window_timeout` is 32 bits ⚠️

```verilog
logic [15:0] channel_continuous_counter [0:31];  // max 65535
...
if (continuous_counter >= window_timeout)
```

Same pattern. If `window_timeout > 65535`, the timeout never fires — both onset reset (NORMAL state) and seizure end (SEIZURE state) are broken. The default is 200, so normal use is safe.

**Fix:** Widen `channel_continuous_counter` to 32 bits.

### 8.3 25-bit output timestamp wraps at ~335 ms @ 100 MHz ⚠️

```verilog
latched_timestamp_ro <= dp_output_timestamp_w[24:0];  // truncated to 25 bits
```

The per-channel counter is 32 bits internally but only 25 bits are exported. At 100 MHz, 2²⁵ cycles ≈ **335 ms**. In any recording longer than that, timestamps alias. The host parser stores them as-is without wraparound compensation:

```cpp
event.timestamp  = timestamp25;
event.sample_index = timestamp25;
```

**Impact:** For short recordings (< 335 ms of processed data) this is fine. For longer sessions, event timestamps are ambiguous without a higher-order counter in a future bitstream revision.

### 8.4 NEO 34-bit arithmetic is correct — no overflow ✅

Centring subtracts 32768 from a 16-bit unsigned value, yielding a 17-bit signed result in ±32767. Squaring or multiplying two such values produces at most `32767² ≈ 2³⁰`, well within the 34-bit signed product registers. `neo_val` is bounded to approximately [−2³⁰, +2³¹] — the −2³³ edge-case required to overflow `neo_abs` cannot be reached with Intan ADC data.

### 8.5 Event buffer is fixed at 40 KB with no overflow guard ⚠️

```cpp
static constexpr size_t MAX_EVENTS        = 10000;
static constexpr size_t EVENT_BUFFER_SIZE = MAX_EVENTS * SAMPLE_SIZE_BYTES;  // 40 KB
```

If the FPGA returns more than 10 000 events per batch, `parseEvents()` will silently process all of them (the buffer is only an allocation hint for `readFromPipeOut`, not a hard cap), but the `events` vector itself is unbounded. No overflow here in practice, but the constant is misleading.

### 8.6 Channel ID encoding is correct ✅

Software packs the channel as `ch << 16`, landing in bits `[20:16]` for channels 0–31. The FPGA reads `[21:16]`, which captures the same value with bit 21 always zero for valid channel numbers. No data is misrouted.

---

## 9. Build & Run

### Dependencies

- Qt 5.x (GUI + file watcher)
- HDF5 (data logging)
- Opal Kelly FrontPanel SDK (`libokFrontPanel.dylib`, headers)
- `detection.bit` — compiled FPGA bitstream (in `lib/`)

### Build

```bash
cd data-analyser/src/gui
qmake seizure_analyzer.pro
make -j$(nproc)
```

### Run

```bash
./seizure_analyzer.app/Contents/MacOS/seizure_analyzer
```

Connect the XEM7310 via USB 3.0 before launching. The device dialog will enumerate available Opal Kelly boards and load `detection.bit` automatically.

---

## Related Repositories

| Repo | Description |
|------|-------------|
| [FPGAPipeline](https://github.com/VerifyHalo/FPGAPipeline) | Vivado project — SystemVerilog source for `detection.bit` |
| [IntanDAQ](https://github.com/VerifyHalo/IntanDAQ) | Intan RHD2000 Rhythm DAQ firmware (XEM7310 retarget) |
| [VerifyHaloGUI](https://github.com/VerifyHalo/VerifyHaloGUI) | *(archived)* Earlier Qt frontend for HALO BCI verification |
