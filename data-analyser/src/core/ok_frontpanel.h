#ifndef OK_FRONTPANEL_H
#define OK_FRONTPANEL_H

#include <cstdint>
#include <string>
#include <vector>

// C++ wrapper for Opal Kelly FrontPanel library
class OkFrontPanel {
public:
    // Error codes
    enum ErrorCode {
        NoError = 0,
        Failed = -1,
        Timeout = -2,
        DoneNotHigh = -3,
        TransferError = -4,
        CommunicationError = -5,
        InvalidBitstream = -6,
        FileError = -7,
        DeviceNotOpen = -8,
        InvalidEndpoint = -9,
        InvalidBlockSize = -10,
        I2CRestrictedAddress = -11,
        I2CBitError = -12,
        I2CNack = -13,
        I2CUnknownStatus = -14,
        UnsupportedFeature = -15,
        FIFOUnderflow = -16,
        FIFOOverflow = -17,
        DataAlignmentError = -18,
        InvalidResetProfile = -19,
        InvalidParameter = -20
    };
    
    OkFrontPanel();
    ~OkFrontPanel();
    
    // Device management
    int getDeviceCount();
    std::string getDeviceListSerial(int num);
    int openBySerial(const std::string& serial);
    bool isOpen();
    
    // FPGA configuration
    int configureFPGA(const std::string& bitfile);
    
    // WireIn/WireOut operations
    int setWireInValue(uint32_t ep, uint32_t val, uint32_t mask = 0xFFFFFFFF);
    int updateWireIns();
    int updateWireOuts();
    uint32_t getWireOutValue(uint32_t ep);
    
    // Pipe operations
    int writeToPipeIn(uint32_t ep, const std::vector<uint8_t>& data);
    std::vector<uint8_t> readFromPipeOut(uint32_t ep, size_t length);
    
    // Error handling
    int getLastError();
    static std::string getErrorString(int error_code);

private:
    void* handle_; // Pointer to C library handle
};

#endif // OK_FRONTPANEL_H
