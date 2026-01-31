# Data Analyser Makefile

# Detect OS
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
else
    DETECTED_OS := $(shell uname -s)
endif

# Directories
SRC_DIR = src
GUI_DIR = $(SRC_DIR)/gui
BUILD_DIR = build

# Platform-specific targets
ifeq ($(DETECTED_OS),Windows)
    TARGET = $(BUILD_DIR)/HALOReader.exe
    MKDIR = if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
    COPY = copy
    RM = del /Q
    RMDIR = rmdir /S /Q
    SEP = \\
else
    TARGET = $(BUILD_DIR)/seizure_analyzer.app/Contents/MacOS/seizure_analyzer
    MKDIR = mkdir -p
    COPY = cp
    RM = rm -f
    RMDIR = rm -rf
    SEP = /
endif

# Default target
all: $(TARGET)

# Build using Qt's qmake system - ALWAYS CLEAN FIRST
ifeq ($(DETECTED_OS),Windows)
$(TARGET):
	cd $(GUI_DIR) && $(if $(wildcard Makefile),$(if $(wildcard C:/MinGW/bin/mingw32-make.exe),mingw32-make clean,$(if $(wildcard C:/Program Files/Microsoft Visual Studio/*/VC/Tools/MSVC/*/bin/Hostx64/x64/nmake.exe),nmake clean,make clean)),@echo No Makefile found) && qmake && $(if $(wildcard C:/MinGW/bin/mingw32-make.exe),mingw32-make,$(if $(wildcard C:/Program Files/Microsoft Visual Studio/*/VC/Tools/MSVC/*/bin/Hostx64/x64/nmake.exe),nmake,make))
	$(MKDIR) $(BUILD_DIR)
	$(if $(wildcard $(GUI_DIR)/HALOReader.exe),$(COPY) $(GUI_DIR)/HALOReader.exe $(TARGET),$(if $(wildcard $(GUI_DIR)/debug/HALOReader.exe),$(COPY) $(GUI_DIR)/debug/HALOReader.exe $(TARGET),$(if $(wildcard $(GUI_DIR)/release/HALOReader.exe),$(COPY) $(GUI_DIR)/release/HALOReader.exe $(TARGET),@echo Executable not found)))
else
$(TARGET):
	cd $(GUI_DIR) && make clean 2>/dev/null || true && qmake && make
	$(MKDIR) $(BUILD_DIR)/seizure_analyzer.app/Contents/MacOS
	$(COPY) $(GUI_DIR)/seizure_analyzer.app/Contents/MacOS/seizure_analyzer $(TARGET)
endif

# Clean build artifacts
clean:
ifeq ($(DETECTED_OS),Windows)
	cd $(GUI_DIR) && $(if $(wildcard Makefile),$(if $(wildcard C:/MinGW/bin/mingw32-make.exe),mingw32-make clean,$(if $(wildcard C:/Program Files/Microsoft Visual Studio/*/VC/Tools/MSVC/*/bin/Hostx64/x64/nmake.exe),nmake clean,make clean)),@echo No Makefile found)
	$(if $(wildcard $(BUILD_DIR)/*.o),$(RM) $(BUILD_DIR)/*.o)
	$(if $(wildcard $(BUILD_DIR)/HALOReader.exe),$(RM) $(BUILD_DIR)/HALOReader.exe)
	$(if $(wildcard $(BUILD_DIR)/seizure_analyzer.exe),$(RM) $(BUILD_DIR)/seizure_analyzer.exe)
else
	cd $(GUI_DIR) && make clean 2>/dev/null || true
	$(RMDIR) $(BUILD_DIR)/obj 2>/dev/null || true
	$(RMDIR) $(BUILD_DIR)/seizure_analyzer.app 2>/dev/null || true
	$(RM) $(BUILD_DIR)/*.o 2>/dev/null || true
endif

# Clean everything including logs
distclean: clean
ifeq ($(DETECTED_OS),Windows)
	$(if $(wildcard $(BUILD_DIR)),$(RMDIR) $(BUILD_DIR))
	$(if $(wildcard logs\*),$(RM) logs\*.* /S /Q)
else
	$(RMDIR) $(BUILD_DIR)
	$(RMDIR) logs/* 2>/dev/null || true
endif

# Install (copy executable to a convenient location)
install: $(TARGET)
ifeq ($(DETECTED_OS),Windows)
	@echo Executable is already in $(BUILD_DIR)
else
	$(COPY) $(TARGET) $(BUILD_DIR)/seizure_analyzer
	@echo "Executable installed to $(BUILD_DIR)/seizure_analyzer"
endif

# Run the application
run: $(TARGET)
ifeq ($(DETECTED_OS),Windows)
	$(TARGET)
else
	$(TARGET)
endif

# Debug build
debug:
ifeq ($(DETECTED_OS),Windows)
	cd $(GUI_DIR) && qmake CONFIG+=debug && $(if $(wildcard C:/MinGW/bin/mingw32-make.exe),mingw32-make,$(if $(wildcard C:/Program Files/Microsoft Visual Studio/*/VC/Tools/MSVC/*/bin/Hostx64/x64/nmake.exe),nmake,make))
	$(MKDIR) $(BUILD_DIR)
	$(if $(wildcard $(GUI_DIR)/debug/seizure_analyzer.exe),$(COPY) $(GUI_DIR)/debug/seizure_analyzer.exe $(TARGET),$(if $(wildcard $(GUI_DIR)/seizure_analyzer.exe),$(COPY) $(GUI_DIR)/seizure_analyzer.exe $(TARGET),@echo Executable not found)))
else
	cd $(GUI_DIR) && qmake CONFIG+=debug && make
	$(MKDIR) $(BUILD_DIR)/seizure_analyzer.app/Contents/MacOS
	$(COPY) $(GUI_DIR)/seizure_analyzer.app/Contents/MacOS/seizure_analyzer $(TARGET)
endif

# Help
help:
	@echo "Available targets:"
	@echo "  all      - Build the seizure analyzer application (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  distclean- Remove all build files and logs"
	@echo "  install  - Install executable to build directory"
	@echo "  run      - Build and run the application"
	@echo "  debug    - Build with debug symbols"
	@echo "  help     - Show this help message"

.PHONY: all clean distclean install run debug help