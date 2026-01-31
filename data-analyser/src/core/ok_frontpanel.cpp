#include "ok_frontpanel.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Forward declarations for C library functions
typedef void* (*okFrontPanel_Construct_t)();
typedef void (*okFrontPanel_Destruct_t)(void*);
typedef int (*okFrontPanel_GetDeviceCount_t)(void*);
typedef void (*okFrontPanel_GetDeviceListSerial_t)(void*, int, char*, int);
typedef int (*okFrontPanel_OpenBySerial_t)(void*, const char*);
typedef int (*okFrontPanel_ConfigureFPGA_t)(void*, const char*);
typedef int (*okFrontPanel_SetWireInValue_t)(void*, int, unsigned long, unsigned long);
typedef int (*okFrontPanel_UpdateWireIns_t)(void*);
typedef int (*okFrontPanel_UpdateWireOuts_t)(void*);
typedef unsigned long (*okFrontPanel_GetWireOutValue_t)(void*, int);
typedef long (*okFrontPanel_WriteToPipeIn_t)(void*, int, long, unsigned char*);
typedef long (*okFrontPanel_ReadFromPipeOut_t)(void*, int, long, unsigned char*);

#ifdef _WIN32
static HMODULE g_lib_handle = nullptr;
#else
static void* g_lib_handle = nullptr;
#endif
static okFrontPanel_Construct_t g_Construct = nullptr;
static okFrontPanel_Destruct_t g_Destruct = nullptr;
static okFrontPanel_GetDeviceCount_t g_GetDeviceCount = nullptr;
static okFrontPanel_GetDeviceListSerial_t g_GetDeviceListSerial = nullptr;
static okFrontPanel_OpenBySerial_t g_OpenBySerial = nullptr;
static okFrontPanel_ConfigureFPGA_t g_ConfigureFPGA = nullptr;
static okFrontPanel_SetWireInValue_t g_SetWireInValue = nullptr;
static okFrontPanel_UpdateWireIns_t g_UpdateWireIns = nullptr;
static okFrontPanel_UpdateWireOuts_t g_UpdateWireOuts = nullptr;
static okFrontPanel_GetWireOutValue_t g_GetWireOutValue = nullptr;
static okFrontPanel_WriteToPipeIn_t g_WriteToPipeIn = nullptr;
static okFrontPanel_ReadFromPipeOut_t g_ReadFromPipeOut = nullptr;

static bool loadLibrary() {
    if (g_lib_handle) return true;
    
#ifdef _WIN32
    // Windows DLL paths - check multiple locations
    const char* lib_paths[] = {
        "lib\\okFrontPanel.dll",
        "lib/okFrontPanel.dll",
        "..\\lib\\okFrontPanel.dll",
        "../lib/okFrontPanel.dll",
        "..\\..\\lib\\okFrontPanel.dll",
        "../../lib/okFrontPanel.dll",
        "build\\okFrontPanel.dll",
        "build/okFrontPanel.dll",
        "okFrontPanel.dll"
    };
    
    for (const char* path : lib_paths) {
        g_lib_handle = LoadLibraryA(path);
        if (g_lib_handle) {
            std::cerr << "[OK] Loaded library from: " << path << std::endl;
            break;
        }
    }
    
    if (!g_lib_handle) {
        DWORD error = GetLastError();
        std::cerr << "[OK] Failed to load okFrontPanel.dll (Error: " << error << ")" << std::endl;
        return false;
    }
    
    // Load function pointers
    g_Construct = (okFrontPanel_Construct_t)GetProcAddress(g_lib_handle, "okFrontPanel_Construct");
    g_Destruct = (okFrontPanel_Destruct_t)GetProcAddress(g_lib_handle, "okFrontPanel_Destruct");
    g_GetDeviceCount = (okFrontPanel_GetDeviceCount_t)GetProcAddress(g_lib_handle, "okFrontPanel_GetDeviceCount");
    g_GetDeviceListSerial = (okFrontPanel_GetDeviceListSerial_t)GetProcAddress(g_lib_handle, "okFrontPanel_GetDeviceListSerial");
    g_OpenBySerial = (okFrontPanel_OpenBySerial_t)GetProcAddress(g_lib_handle, "okFrontPanel_OpenBySerial");
    g_ConfigureFPGA = (okFrontPanel_ConfigureFPGA_t)GetProcAddress(g_lib_handle, "okFrontPanel_ConfigureFPGA");
    g_SetWireInValue = (okFrontPanel_SetWireInValue_t)GetProcAddress(g_lib_handle, "okFrontPanel_SetWireInValue");
    g_UpdateWireIns = (okFrontPanel_UpdateWireIns_t)GetProcAddress(g_lib_handle, "okFrontPanel_UpdateWireIns");
    g_UpdateWireOuts = (okFrontPanel_UpdateWireOuts_t)GetProcAddress(g_lib_handle, "okFrontPanel_UpdateWireOuts");
    g_GetWireOutValue = (okFrontPanel_GetWireOutValue_t)GetProcAddress(g_lib_handle, "okFrontPanel_GetWireOutValue");
    g_WriteToPipeIn = (okFrontPanel_WriteToPipeIn_t)GetProcAddress(g_lib_handle, "okFrontPanel_WriteToPipeIn");
    g_ReadFromPipeOut = (okFrontPanel_ReadFromPipeOut_t)GetProcAddress(g_lib_handle, "okFrontPanel_ReadFromPipeOut");
    
    if (!g_Construct || !g_Destruct || !g_GetDeviceCount || !g_GetDeviceListSerial ||
        !g_OpenBySerial || !g_ConfigureFPGA || !g_SetWireInValue || !g_UpdateWireIns ||
        !g_UpdateWireOuts || !g_GetWireOutValue || !g_WriteToPipeIn || !g_ReadFromPipeOut) {
        std::cerr << "[OK] Failed to load required functions from library" << std::endl;
        FreeLibrary(g_lib_handle);
        g_lib_handle = nullptr;
        return false;
    }
#else
    // Unix/macOS library paths
    const char* lib_paths[] = {
        "lib/libokFrontPanel.dylib",
        "../lib/libokFrontPanel.dylib",
        "../../lib/libokFrontPanel.dylib",
        "/Users/antonmelnychuk/workspace/pipeline/data-analyser/lib/libokFrontPanel.dylib"
    };
    
    for (const char* path : lib_paths) {
        g_lib_handle = dlopen(path, RTLD_LAZY);
        if (g_lib_handle) {
            std::cerr << "[OK] Loaded library from: " << path << std::endl;
            break;
        }
    }
    
    if (!g_lib_handle) {
        std::cerr << "[OK] Failed to load libokFrontPanel.dylib: " << dlerror() << std::endl;
        return false;
    }
    
    // Load function pointers
    g_Construct = (okFrontPanel_Construct_t)dlsym(g_lib_handle, "okFrontPanel_Construct");
    g_Destruct = (okFrontPanel_Destruct_t)dlsym(g_lib_handle, "okFrontPanel_Destruct");
    g_GetDeviceCount = (okFrontPanel_GetDeviceCount_t)dlsym(g_lib_handle, "okFrontPanel_GetDeviceCount");
    g_GetDeviceListSerial = (okFrontPanel_GetDeviceListSerial_t)dlsym(g_lib_handle, "okFrontPanel_GetDeviceListSerial");
    g_OpenBySerial = (okFrontPanel_OpenBySerial_t)dlsym(g_lib_handle, "okFrontPanel_OpenBySerial");
    g_ConfigureFPGA = (okFrontPanel_ConfigureFPGA_t)dlsym(g_lib_handle, "okFrontPanel_ConfigureFPGA");
    g_SetWireInValue = (okFrontPanel_SetWireInValue_t)dlsym(g_lib_handle, "okFrontPanel_SetWireInValue");
    g_UpdateWireIns = (okFrontPanel_UpdateWireIns_t)dlsym(g_lib_handle, "okFrontPanel_UpdateWireIns");
    g_UpdateWireOuts = (okFrontPanel_UpdateWireOuts_t)dlsym(g_lib_handle, "okFrontPanel_UpdateWireOuts");
    g_GetWireOutValue = (okFrontPanel_GetWireOutValue_t)dlsym(g_lib_handle, "okFrontPanel_GetWireOutValue");
    g_WriteToPipeIn = (okFrontPanel_WriteToPipeIn_t)dlsym(g_lib_handle, "okFrontPanel_WriteToPipeIn");
    g_ReadFromPipeOut = (okFrontPanel_ReadFromPipeOut_t)dlsym(g_lib_handle, "okFrontPanel_ReadFromPipeOut");
    
    if (!g_Construct || !g_Destruct || !g_GetDeviceCount || !g_GetDeviceListSerial ||
        !g_OpenBySerial || !g_ConfigureFPGA || !g_SetWireInValue || !g_UpdateWireIns ||
        !g_UpdateWireOuts || !g_GetWireOutValue || !g_WriteToPipeIn || !g_ReadFromPipeOut) {
        std::cerr << "[OK] Failed to load required functions from library" << std::endl;
        dlclose(g_lib_handle);
        g_lib_handle = nullptr;
        return false;
    }
#endif
    
    return true;
}

OkFrontPanel::OkFrontPanel() : handle_(nullptr) {
    if (!loadLibrary()) {
        throw std::runtime_error("Failed to load Opal Kelly FrontPanel library");
    }
    handle_ = g_Construct();
    if (!handle_) {
        throw std::runtime_error("Failed to construct okCFrontPanel");
    }
}

OkFrontPanel::~OkFrontPanel() {
    if (handle_ && g_Destruct) {
        g_Destruct(handle_);
        handle_ = nullptr;
    }
    // Note: We don't unload the library here as it may be used by other instances
    // The library will be unloaded when the process exits
}

int OkFrontPanel::getDeviceCount() {
    if (!handle_ || !g_GetDeviceCount) return 0;
    return g_GetDeviceCount(handle_);
}

std::string OkFrontPanel::getDeviceListSerial(int num) {
    if (!handle_ || !g_GetDeviceListSerial) return "";
    char buf[12] = {0};
    g_GetDeviceListSerial(handle_, num, buf, 11);
    return std::string(buf);
}

int OkFrontPanel::openBySerial(const std::string& serial) {
    if (!handle_ || !g_OpenBySerial) return DeviceNotOpen;
    return g_OpenBySerial(handle_, serial.c_str());
}

bool OkFrontPanel::isOpen() {
    return handle_ != nullptr;
}

int OkFrontPanel::configureFPGA(const std::string& bitfile) {
    if (!handle_ || !g_ConfigureFPGA) return DeviceNotOpen;
    return g_ConfigureFPGA(handle_, bitfile.c_str());
}

int OkFrontPanel::setWireInValue(uint32_t ep, uint32_t val, uint32_t mask) {
    if (!handle_ || !g_SetWireInValue) return DeviceNotOpen;
    return g_SetWireInValue(handle_, ep, val & 0xFFFFFFFF, mask & 0xFFFFFFFF);
}

int OkFrontPanel::updateWireIns() {
    if (!handle_ || !g_UpdateWireIns) return DeviceNotOpen;
    return g_UpdateWireIns(handle_);
}

int OkFrontPanel::updateWireOuts() {
    if (!handle_ || !g_UpdateWireOuts) return DeviceNotOpen;
    return g_UpdateWireOuts(handle_);
}

uint32_t OkFrontPanel::getWireOutValue(uint32_t ep) {
    if (!handle_ || !g_GetWireOutValue) return 0;
    return static_cast<uint32_t>(g_GetWireOutValue(handle_, ep));
}

int OkFrontPanel::writeToPipeIn(uint32_t ep, const std::vector<uint8_t>& data) {
    if (!handle_ || !g_WriteToPipeIn) return DeviceNotOpen;
    if (data.empty()) return 0;
    
    // Ensure data is multiple of 16 bytes (required for PipeIn)
    std::vector<uint8_t> padded_data = data;
    size_t rem = padded_data.size() % 16;
    if (rem != 0) {
        padded_data.resize(padded_data.size() + (16 - rem), 0);
    }
    
    return static_cast<int>(g_WriteToPipeIn(handle_, ep, padded_data.size(), padded_data.data()));
}

std::vector<uint8_t> OkFrontPanel::readFromPipeOut(uint32_t ep, size_t length) {
    std::vector<uint8_t> result;
    if (!handle_ || !g_ReadFromPipeOut) return result;
    
    result.resize(length);
    long bytes_read = g_ReadFromPipeOut(handle_, ep, length, result.data());
    if (bytes_read < 0) {
        result.clear();
    } else {
        result.resize(bytes_read);
    }
    return result;
}

int OkFrontPanel::getLastError() {
    return NoError; // C API doesn't provide GetLastError
}

std::string OkFrontPanel::getErrorString(int error_code) {
    switch (error_code) {
        case NoError: return "No error";
        case Failed: return "Operation failed";
        case Timeout: return "Timeout";
        case DeviceNotOpen: return "Device not open";
        case InvalidEndpoint: return "Invalid endpoint";
        case CommunicationError: return "Communication error";
        default: return "Unknown error " + std::to_string(error_code);
    }
}
