QT += core widgets

CONFIG += c++17

# Application icon
RESOURCES += resources.qrc
win32 {
    # Use ICO if available, otherwise PNG
    exists(app_icon.ico) {
        RC_ICONS = app_icon.ico
    } else {
        RC_ICONS = app_icon.png
    }
}

# Use Qt's MinGW runtime to avoid ABI mismatches
win32 {
    QMAKE_LFLAGS += -static-libgcc -static-libstdc++
}

TARGET = HALOReader
TEMPLATE = app

SOURCES += \
    ../main.cpp \
    seizure_analyzer.cpp \
    fpga_device_dialog.cpp \
    file_processor_worker.cpp \
    ../core/hdf5_reader.cpp \
    ../core/fpga_logger.cpp \
    ../core/halo_response_decoder.cpp \
    ../core/hdf5_writer.cpp \
    ../core/seizure_detector.cpp \
    ../core/seizure_processor.cpp \
    ../core/rhd_reader.cpp \
    ../core/ok_frontpanel.cpp \
    ../core/fpga_processor.cpp

HEADERS += \
    seizure_analyzer.h \
    fpga_device_dialog.h \
    file_processor_worker.h \
    ../core/hdf5_reader.h \
    ../core/fpga_logger.h \
    ../core/halo_response_decoder.h \
    ../core/hdf5_writer.h \
    ../core/seizure_detector.h \
    ../core/seizure_processor.h \
    ../core/rhd_reader.h \
    ../core/ok_frontpanel.h \
    ../core/fpga_processor.h

# FORMS += \
#     seizure_analyzer.ui

# HDF5 support
macx {
    INCLUDEPATH += /opt/homebrew/include
    LIBS += -L/opt/homebrew/lib -lhdf5 -ldl
}

win32 {
    # Windows HDF5 paths - adjust these to match your HDF5 installation
    # Common locations:
    # - C:\Program Files\HDF_Group\HDF5\1.14.x
    # - C:\hdf5
    # - vcpkg: C:\vcpkg\installed\x64-windows
    HDF5_DIR = $$(HDF5_DIR)
    isEmpty(HDF5_DIR) {
        HDF5_DIR = C:/hdf5
    }
    
    INCLUDEPATH += $$HDF5_DIR/include
    LIBS += -L$$HDF5_DIR/lib
    
    # HDF5 library names on Windows
    CONFIG(debug, debug|release) {
        LIBS += -lhdf5_D -lhdf5_cpp_D
    } else {
        LIBS += -lhdf5 -lhdf5_cpp
    }
    
    # Windows-specific libraries
    LIBS += -lws2_32 -lwsock32
}

unix:!macx {
    LIBS += -lhdf5
}
