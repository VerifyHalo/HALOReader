QT += core widgets

CONFIG += c++17

TARGET = seizure_analyzer
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
    LIBS += -lhdf5
}

unix:!macx {
    LIBS += -lhdf5
}
