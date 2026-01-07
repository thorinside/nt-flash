/*
 * NT Flash Tool - Unified firmware flasher for disting NT
 *
 * Copyright (c) 2024
 *
 * This tool wraps the NXP BLFWK library to provide a simple command-line
 * interface for flashing disting NT firmware.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

// BLFWK includes
#include "blfwk/Logging.h"
#include "blfwk/host_types.h"
#include "blfwk/utils.h"
#include "blfwk/SDPCommand.h"
#include "blfwk/Command.h"
#include "blfwk/UsbHidPeripheral.h"
#include "blfwk/SDPUsbHidPacketizer.h"
#include "blfwk/UsbHidPacketizer.h"
#include "blfwk/Bootloader.h"
#include "blfwk/Peripheral.h"
#include "hidapi.h"

// Embedded libraries
#include "miniz.h"
#include "miniz_zip.h"

extern "C" {
#include "cJSON.h"
}

#if defined(WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <signal.h>
#endif

using namespace blfwk;

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

const char* VERSION = "0.1.0";
const char* TOOL_NAME = "nt-flash";

// USB IDs for disting NT
const uint16_t SDP_VID = 0x1FC9;    // NXP ROM bootloader
const uint16_t SDP_PID = 0x0135;    // i.MX RT in SDP mode
const uint16_t BL_VID = 0x15A2;     // NXP flashloader
const uint16_t BL_PID = 0x0073;     // Flashloader running

// Memory addresses for i.MX RT1060
const uint32_t FLASHLOADER_ADDR = 0x20001C00;  // RAM address for flashloader
const uint32_t FLASH_BASE = 0x60000000;        // External flash base
const uint32_t FIRMWARE_ADDR = 0x60001000;     // Firmware write address
const uint32_t CONFIG_ADDR = 0x2000;           // Configuration memory

// FlexSPI configuration values
const uint32_t FLEXSPI_NOR_CONFIG = 0xC0000008;
const uint32_t FCB_CONFIG = 0xF000000F;
const uint32_t MEMORY_ID_FLEXSPI_NOR = 9;

// Timeouts
const uint32_t SDP_TIMEOUT_MS = 5000;
const uint32_t BL_TIMEOUT_MS = 60000;  // Long timeout for flash operations

// Expert Sleepers firmware URLs
const char* FIRMWARE_BASE_URL = "https://www.expert-sleepers.co.uk/downloads/firmware/";

//------------------------------------------------------------------------------
// Globals
//------------------------------------------------------------------------------

static bool g_verbose = false;
static bool g_dryRun = false;
static bool g_machineOutput = false;

//------------------------------------------------------------------------------
// Logging
//------------------------------------------------------------------------------

void logInfo(const char* fmt, ...) {
    if (g_machineOutput) return;  // Suppress in machine mode
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

void logVerbose(const char* fmt, ...) {
    if (!g_verbose || g_machineOutput) return;  // Suppress in machine mode
    va_list args;
    va_start(args, fmt);
    printf("  ");
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

void logError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (g_machineOutput) {
        printf("ERROR:");
        vprintf(fmt, args);
        printf("\n");
        fflush(stdout);
    } else {
        fprintf(stderr, "ERROR: ");
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        fflush(stderr);
    }
    va_end(args);
}

//------------------------------------------------------------------------------
// Machine-readable output (for --machine flag)
// Format: TYPE:STAGE:PERCENT:MESSAGE
//------------------------------------------------------------------------------

void machineStatus(const char* stage, int percent, const char* message) {
    if (!g_machineOutput) return;
    printf("STATUS:%s:%d:%s\n", stage, percent, message);
    fflush(stdout);
}

void machineProgress(const char* stage, int percent, const char* message) {
    if (!g_machineOutput) return;
    printf("PROGRESS:%s:%d:%s\n", stage, percent, message);
    fflush(stdout);
}

//------------------------------------------------------------------------------
// File Utilities
//------------------------------------------------------------------------------

// Load a local file into memory
bool loadFile(const char* path, std::vector<uint8_t>& data) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        logError("Cannot open file: %s", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    data.resize(size);
    size_t bytesRead = fread(data.data(), 1, size, f);
    fclose(f);

    if (bytesRead != (size_t)size) {
        logError("Failed to read file: %s", path);
        return false;
    }

    logVerbose("Loaded %s (%zu bytes)", path, data.size());
    return true;
}

// Get the system temp directory (cross-platform)
std::string getTempDir() {
#ifdef WIN32
    char tempPath[MAX_PATH];
    DWORD len = GetTempPathA(MAX_PATH, tempPath);
    if (len > 0 && len < MAX_PATH) {
        return std::string(tempPath);
    }
    const char* userProfile = getenv("USERPROFILE");
    if (userProfile) {
        return std::string(userProfile) + "\\Downloads\\";
    }
    return ".\\";
#else
    const char* tmpdir = getenv("TMPDIR");
    if (tmpdir && tmpdir[0]) {
        std::string dir(tmpdir);
        if (dir.back() != '/') dir += '/';
        return dir;
    }
    return "/tmp/";
#endif
}

// Save data to a temporary file
std::string saveToTempFile(const std::vector<uint8_t>& data, const char* suffix) {
#ifdef WIN32
    char tempPath[MAX_PATH];
    char tempFile[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    GetTempFileNameA(tempPath, "ntf", 0, tempFile);
    std::string path = std::string(tempFile) + suffix;
#else
    std::string path = getTempDir() + "nt_flash_XXXXXX" + suffix;
    // Create unique file
    char* pathBuf = strdup(path.c_str());
    int fd = mkstemps(pathBuf, strlen(suffix));
    if (fd < 0) {
        free(pathBuf);
        return "";
    }
    close(fd);
    path = pathBuf;
    free(pathBuf);
#endif

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        return "";
    }
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);

    return path;
}

//------------------------------------------------------------------------------
// Firmware Package Handling
//------------------------------------------------------------------------------

struct FirmwarePackage {
    std::vector<uint8_t> flashloader;
    std::vector<uint8_t> firmware;
    std::string flashloaderPath;  // Temp file path for BLFWK
    std::string firmwarePath;     // Temp file path for BLFWK
    std::string version;
    bool valid;

    FirmwarePackage() : valid(false) {}

    ~FirmwarePackage() {
        // Clean up temp files
        if (!flashloaderPath.empty()) {
            remove(flashloaderPath.c_str());
        }
        if (!firmwarePath.empty()) {
            remove(firmwarePath.c_str());
        }
    }
};

// Extract a file from a ZIP archive in memory
bool extractFileFromZip(const std::vector<uint8_t>& zipData,
                        const char* filename,
                        std::vector<uint8_t>& outData) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_mem(&zip, zipData.data(), zipData.size(), 0)) {
        logError("Failed to open ZIP archive");
        return false;
    }

    int fileIndex = mz_zip_reader_locate_file(&zip, filename, NULL, 0);
    if (fileIndex < 0) {
        mz_zip_reader_end(&zip);
        logError("File not found in ZIP: %s", filename);
        return false;
    }

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&zip, fileIndex, &stat)) {
        mz_zip_reader_end(&zip);
        logError("Failed to get file info: %s", filename);
        return false;
    }

    outData.resize((size_t)stat.m_uncomp_size);
    if (!mz_zip_reader_extract_to_mem(&zip, fileIndex, outData.data(), outData.size(), 0)) {
        mz_zip_reader_end(&zip);
        logError("Failed to extract file: %s", filename);
        return false;
    }

    mz_zip_reader_end(&zip);
    logVerbose("Extracted %s (%zu bytes)", filename, outData.size());
    return true;
}

// Parse MANIFEST.json from firmware package
bool parseManifest(const std::vector<uint8_t>& jsonData, std::string& firmwarePath) {
    std::string jsonStr(jsonData.begin(), jsonData.end());
    cJSON* root = cJSON_Parse(jsonStr.c_str());
    if (!root) {
        logError("Failed to parse MANIFEST.json");
        return false;
    }

    cJSON* processor = cJSON_GetObjectItem(root, "processor");
    if (processor && processor->valuestring && strcmp(processor->valuestring, "MIMXRT1060") != 0) {
        logError("Unsupported processor: %s (expected MIMXRT1060)", processor->valuestring);
        cJSON_Delete(root);
        return false;
    }

    cJSON* appFirmware = cJSON_GetObjectItem(root, "app_firmware");
    if (appFirmware && appFirmware->valuestring) {
        firmwarePath = appFirmware->valuestring;
    } else {
        firmwarePath = "bootable_images/disting_NT.bin";
    }

    cJSON_Delete(root);
    return true;
}

// Load firmware package from ZIP file
FirmwarePackage* loadFirmwarePackage(const char* zipPath) {
    FirmwarePackage* pkg = new FirmwarePackage();

    logInfo("Loading firmware package: %s", zipPath);
    machineStatus("LOAD", 0, "Loading firmware package");

    std::vector<uint8_t> zipData;
    if (!loadFile(zipPath, zipData)) {
        delete pkg;
        return nullptr;
    }

    // Parse manifest
    std::vector<uint8_t> manifestData;
    if (!extractFileFromZip(zipData, "MANIFEST.json", manifestData)) {
        delete pkg;
        return nullptr;
    }

    std::string firmwareBinPath;
    if (!parseManifest(manifestData, firmwareBinPath)) {
        delete pkg;
        return nullptr;
    }

    // Extract flashloader
    if (!extractFileFromZip(zipData, "bootable_images/unsigned_MIMXRT1060_flashloader.bin",
                            pkg->flashloader)) {
        delete pkg;
        return nullptr;
    }

    // Extract firmware
    if (!extractFileFromZip(zipData, firmwareBinPath.c_str(), pkg->firmware)) {
        delete pkg;
        return nullptr;
    }

    // Save to temp files (BLFWK needs file paths for write-file command)
    pkg->flashloaderPath = saveToTempFile(pkg->flashloader, ".bin");
    pkg->firmwarePath = saveToTempFile(pkg->firmware, ".bin");

    if (pkg->flashloaderPath.empty() || pkg->firmwarePath.empty()) {
        logError("Failed to create temporary files");
        delete pkg;
        return nullptr;
    }

    pkg->valid = true;
    logInfo("Package loaded: flashloader=%zu bytes, firmware=%zu bytes",
            pkg->flashloader.size(), pkg->firmware.size());

    return pkg;
}

//------------------------------------------------------------------------------
// Download Functions
//------------------------------------------------------------------------------

// Download a file using system curl
bool downloadFile(const char* url, const char* destPath) {
    logInfo("Downloading: %s", url);
    machineStatus("DOWNLOAD", 0, "Downloading firmware");

    char cmd[2048];
#ifdef WIN32
    snprintf(cmd, sizeof(cmd), "curl.exe -L -s -o \"%s\" \"%s\"", destPath, url);
#else
    snprintf(cmd, sizeof(cmd), "curl -L -s -o \"%s\" \"%s\"", destPath, url);
#endif

    int ret = system(cmd);
    if (ret != 0) {
        logError("Download failed (curl exit code: %d)", ret);
        return false;
    }

    logVerbose("Downloaded to: %s", destPath);
    return true;
}

//------------------------------------------------------------------------------
// Progress Display
//------------------------------------------------------------------------------

static const char* g_currentStage = "WRITE";

static void displayProgress(int percentage, int segmentIndex, int segmentCount) {
    if (g_machineOutput) {
        char message[128];
        snprintf(message, sizeof(message), "Segment %d/%d", segmentIndex, segmentCount);
        machineProgress(g_currentStage, percentage, message);
    } else {
        printf("\r  Progress: (%d/%d) %d%%", segmentIndex, segmentCount, percentage);
        fflush(stdout);
        if (percentage >= 100) {
            printf(" Done!\n");
        }
    }
}

//------------------------------------------------------------------------------
// SDP Operations (ROM Bootloader)
//------------------------------------------------------------------------------

class SDPOperations {
public:
    SDPOperations() : m_peripheral(nullptr), m_packetizer(nullptr) {}

    ~SDPOperations() {
        close();
    }

    bool connect() {
        if (g_dryRun) {
            logVerbose("[DRY RUN] Would connect to SDP device %04X:%04X", SDP_VID, SDP_PID);
            return true;
        }

        try {
            m_peripheral = new UsbHidPeripheral(SDP_VID, SDP_PID, "", "");
            m_packetizer = new SDPUsbHidPacketizer(m_peripheral, SDP_TIMEOUT_MS);

            // Test with error-status command
            string_vector_t cmdArgs;
            cmdArgs.push_back("error-status");

            SDPCommand* cmd = SDPCommand::create(&cmdArgs);
            if (!cmd) {
                throw std::runtime_error("Failed to create error-status command");
            }

            cmd->sendTo(*m_packetizer);

            const uint32_vector_t* response = cmd->getResponseValues();
            if (response->size() == 0 || response->at(0) == SDPCommand::kStatus_NoResponse) {
                delete cmd;
                throw std::runtime_error("No response from device");
            }

            logVerbose("SDP connected (status: 0x%08X)", response->at(0));
            delete cmd;
            return true;
        }
        catch (const std::exception& e) {
            logVerbose("SDP connection failed: %s", e.what());
            close();
            return false;
        }
    }

    bool writeFile(uint32_t address, const std::string& filePath) {
        if (g_dryRun) {
            logVerbose("[DRY RUN] Would write file to 0x%08X", address);
            return true;
        }

        try {
            char addrStr[32];
            snprintf(addrStr, sizeof(addrStr), "0x%X", address);

            string_vector_t cmdArgs;
            cmdArgs.push_back("write-file");
            cmdArgs.push_back(addrStr);
            cmdArgs.push_back(filePath);

            SDPCommand* cmd = SDPCommand::create(&cmdArgs);
            if (!cmd) {
                logError("Failed to create write-file command");
                return false;
            }

            Progress progress(displayProgress, nullptr);
            cmd->registerProgress(&progress);

            cmd->sendTo(*m_packetizer);

            const uint32_vector_t* response = cmd->getResponseValues();
            bool success = (response->size() > 0 && response->at(0) != SDPCommand::kStatus_NoResponse);

            delete cmd;

            if (!success) {
                logError("write-file command failed");
                return false;
            }

            logVerbose("File written to 0x%08X", address);
            return true;
        }
        catch (const std::exception& e) {
            logError("write-file failed: %s", e.what());
            return false;
        }
    }

    bool jumpAddress(uint32_t address) {
        if (g_dryRun) {
            logVerbose("[DRY RUN] Would jump to 0x%08X", address);
            return true;
        }

        try {
            char addrStr[32];
            snprintf(addrStr, sizeof(addrStr), "0x%X", address);

            string_vector_t cmdArgs;
            cmdArgs.push_back("jump-address");
            cmdArgs.push_back(addrStr);

            SDPCommand* cmd = SDPCommand::create(&cmdArgs);
            if (!cmd) {
                logError("Failed to create jump-address command");
                return false;
            }

            cmd->sendTo(*m_packetizer);
            delete cmd;

            logVerbose("Jump command sent to 0x%08X", address);
            return true;
        }
        catch (const std::exception& e) {
            // Expected - device disconnects
            logVerbose("Jump command completed (device disconnected)");
            return true;
        }
    }

    void close() {
        if (m_packetizer) {
            delete m_packetizer;
            m_packetizer = nullptr;
        }
        // Peripheral is owned by packetizer, don't double-delete
        m_peripheral = nullptr;
    }

private:
    UsbHidPeripheral* m_peripheral;
    SDPUsbHidPacketizer* m_packetizer;
};

//------------------------------------------------------------------------------
// Bootloader Operations (Flashloader)
//------------------------------------------------------------------------------

class BootloaderOperations {
public:
    BootloaderOperations() : m_bootloader(nullptr) {}

    ~BootloaderOperations() {
        close();
    }

    bool connect() {
        if (g_dryRun) {
            logVerbose("[DRY RUN] Would connect to bootloader %04X:%04X", BL_VID, BL_PID);
            return true;
        }

        // Try multiple times as device may take time to enumerate
        for (int attempt = 0; attempt < 5; attempt++) {
            try {
                Peripheral::PeripheralConfigData config;
                config.peripheralType = Peripheral::kHostPeripheralType_USB_HID;
                config.usbHidVid = BL_VID;
                config.usbHidPid = BL_PID;
                config.packetTimeoutMs = BL_TIMEOUT_MS;
                config.ping = false;

                m_bootloader = new Bootloader(config);

                // Test with get-property command
                string_vector_t cmdArgs;
                cmdArgs.push_back("get-property");
                cmdArgs.push_back("1");  // Current version

                Command* cmd = Command::create(&cmdArgs);
                if (!cmd) {
                    throw std::runtime_error("Failed to create get-property command");
                }

                m_bootloader->inject(*cmd);
                m_bootloader->flush();

                const uint32_vector_t* response = cmd->getResponseValues();
                if (response->size() > 0 && response->at(0) != kStatus_NoResponse) {
                    logVerbose("Bootloader connected");
                    delete cmd;
                    return true;
                }

                delete cmd;
                close();
            }
            catch (...) {
                close();
            }

            logVerbose("Bootloader not ready, retrying... (%d/5)", attempt + 1);
#ifdef WIN32
            Sleep(1000);
#else
            sleep(1);
#endif
        }

        logError("Failed to connect to bootloader");
        return false;
    }

    bool runCommand(const string_vector_t& args) {
        if (g_dryRun) {
            std::string cmdStr;
            for (size_t i = 0; i < args.size(); i++) {
                if (i > 0) cmdStr += " ";
                cmdStr += args[i];
            }
            logVerbose("[DRY RUN] Would run: %s", cmdStr.c_str());
            return true;
        }

        try {
            Command* cmd = Command::create(&args);
            if (!cmd) {
                logError("Failed to create command: %s", args[0].c_str());
                return false;
            }

            // Register progress for write-memory
            Progress progress(displayProgress, nullptr);
            cmd->registerProgress(&progress);

            m_bootloader->inject(*cmd);
            m_bootloader->flush();

            const uint32_vector_t* response = cmd->getResponseValues();
            bool success = true;

            if (response->size() > 0) {
                uint32_t status = response->at(0);
                if (status == kStatus_NoResponse) {
                    logError("No response for command: %s", args[0].c_str());
                    success = false;
                } else if (status != kStatus_Success && status != kStatus_NoResponseExpected) {
                    logError("Command %s failed with status: 0x%X", args[0].c_str(), status);
                    success = false;
                }
            }

            delete cmd;
            return success;
        }
        catch (const std::exception& e) {
            logError("Command failed: %s", e.what());
            return false;
        }
    }

    bool fillMemory(uint32_t address, uint32_t size, uint32_t pattern) {
        char addrStr[32], sizeStr[32], patternStr[32];
        snprintf(addrStr, sizeof(addrStr), "0x%X", address);
        snprintf(sizeStr, sizeof(sizeStr), "%u", size);
        snprintf(patternStr, sizeof(patternStr), "0x%X", pattern);

        string_vector_t args;
        args.push_back("fill-memory");
        args.push_back(addrStr);
        args.push_back(sizeStr);
        args.push_back(patternStr);
        args.push_back("word");

        return runCommand(args);
    }

    bool configureMemory(uint32_t memoryId, uint32_t configAddr) {
        char memIdStr[32], addrStr[32];
        snprintf(memIdStr, sizeof(memIdStr), "%u", memoryId);
        snprintf(addrStr, sizeof(addrStr), "0x%X", configAddr);

        string_vector_t args;
        args.push_back("configure-memory");
        args.push_back(memIdStr);
        args.push_back(addrStr);

        return runCommand(args);
    }

    bool flashEraseRegion(uint32_t address, uint32_t size, uint32_t memoryId = 0) {
        char addrStr[32], sizeStr[32], memIdStr[32];
        snprintf(addrStr, sizeof(addrStr), "0x%X", address);
        snprintf(sizeStr, sizeof(sizeStr), "%u", size);
        snprintf(memIdStr, sizeof(memIdStr), "%u", memoryId);

        string_vector_t args;
        args.push_back("flash-erase-region");
        args.push_back(addrStr);
        args.push_back(sizeStr);
        args.push_back(memIdStr);  // Explicit memory ID (0 = internal/memory-mapped)

        return runCommand(args);
    }

    bool writeMemory(uint32_t address, const std::string& filePath, uint32_t memoryId = 0) {
        char addrStr[32], memIdStr[32];
        snprintf(addrStr, sizeof(addrStr), "0x%X", address);
        snprintf(memIdStr, sizeof(memIdStr), "%u", memoryId);

        string_vector_t args;
        args.push_back("write-memory");
        args.push_back(addrStr);
        args.push_back(filePath);
        args.push_back(memIdStr);  // Explicit memory ID (0 = internal/memory-mapped)

        return runCommand(args);
    }

    bool reset() {
        string_vector_t args;
        args.push_back("reset");

        try {
            runCommand(args);
        }
        catch (...) {
            // Expected - device disconnects
        }
        return true;
    }

    void close() {
        if (m_bootloader) {
            delete m_bootloader;
            m_bootloader = nullptr;
        }
    }

private:
    Bootloader* m_bootloader;
};

//------------------------------------------------------------------------------
// Flash Orchestration
//------------------------------------------------------------------------------

bool flashFirmware(FirmwarePackage* pkg, bool skipSdp = false) {
    if (!pkg || !pkg->valid) {
        logError("Invalid firmware package");
        return false;
    }

    logInfo("=== Starting disting NT flash ===");
    machineStatus("START", 0, "Starting disting NT flash");

    // Phase 1: SDP - Load flashloader (skip if already in flashloader mode)
    if (!skipSdp) {
        SDPOperations sdp;

        logInfo("[1/7] Connecting to SDP bootloader...");
        machineStatus("SDP_CONNECT", 5, "Connecting to SDP bootloader");
        if (!sdp.connect()) {
            // Check if device is already in flashloader mode
            BootloaderOperations blCheck;
            machineStatus("BL_CHECK", 10, "Checking for flashloader mode");
            if (blCheck.connect()) {
                logInfo("Device already in flashloader mode, skipping SDP phase...");
                machineStatus("BL_FOUND", 15, "Device already in flashloader mode");
                blCheck.close();
                skipSdp = true;
            } else {
                logError("Device not found in SDP mode or flashloader mode");
                logInfo("Make sure disting NT is in bootloader mode:");
                logInfo("  Menu > Misc > Enter bootloader mode...");
                return false;
            }
        }

        if (!skipSdp) {
            logInfo("[2/7] Uploading flashloader to RAM...");
            machineStatus("SDP_UPLOAD", 15, "Uploading flashloader to RAM");
            g_currentStage = "SDP_UPLOAD";
            if (!sdp.writeFile(FLASHLOADER_ADDR, pkg->flashloaderPath)) {
                return false;
            }

            logInfo("[3/7] Starting flashloader...");
            machineStatus("SDP_JUMP", 25, "Starting flashloader");
            if (!sdp.jumpAddress(FLASHLOADER_ADDR)) {
                return false;
            }

            sdp.close();

            // Wait for device to re-enumerate (give it extra time on macOS)
            logInfo("[4/7] Waiting for flashloader to start...");
            machineStatus("WAIT_ENUM", 30, "Waiting for flashloader to start");
#ifdef WIN32
            Sleep(3000);
#else
            sleep(5);  // Increased from 3 to 5 seconds
#endif
        }
    }

    // Reset HID subsystem to get fresh device list after re-enumeration
    // This is critical on macOS where the IOHIDManager caches devices
    hid_exit();

    // Phase 2: Bootloader - Flash firmware
    BootloaderOperations bl;

    logInfo("[5/7] Connecting to flashloader...");
    machineStatus("BL_CONNECT", 40, "Connecting to flashloader");
    if (!bl.connect()) {
        return false;
    }

    logInfo("[6/7] Configuring flash and erasing...");
    machineStatus("CONFIGURE", 50, "Configuring flash memory");

    // Configure FlexSPI NOR
    logVerbose("Configuring FlexSPI NOR...");
    if (!bl.fillMemory(CONFIG_ADDR, 4, FLEXSPI_NOR_CONFIG)) {
        return false;
    }
    if (!bl.configureMemory(MEMORY_ID_FLEXSPI_NOR, CONFIG_ADDR)) {
        return false;
    }

    // Erase flash region (FCB area + firmware size, matching official script exactly)
    // The FCB is at 0x60000000, firmware starts at 0x60001000 (0x1000 offset)
    uint32_t eraseSize = pkg->firmware.size() + 0x1000;  // Matches official: firmware + FCB area
    logVerbose("Erasing flash region 0x%08X, size %u bytes...", FLASH_BASE, eraseSize);
    machineStatus("ERASE", 55, "Erasing flash region");
    if (!bl.flashEraseRegion(FLASH_BASE, eraseSize, 0)) {
        return false;
    }

    // Create FCB
    logVerbose("Creating Flash Configuration Block...");
    machineStatus("FCB", 60, "Creating Flash Configuration Block");
    if (!bl.fillMemory(CONFIG_ADDR, 4, FCB_CONFIG)) {
        return false;
    }
    if (!bl.configureMemory(MEMORY_ID_FLEXSPI_NOR, CONFIG_ADDR)) {
        return false;
    }

    logInfo("[7/7] Writing firmware (%zu bytes)...", pkg->firmware.size());
    machineStatus("WRITE", 65, "Writing firmware");
    g_currentStage = "WRITE";
    if (!bl.writeMemory(FIRMWARE_ADDR, pkg->firmwarePath, 0)) {
        return false;
    }

    logInfo("Resetting device...");
    machineStatus("RESET", 95, "Resetting device");
    bl.reset();
    bl.close();

    logInfo("=== Flash complete! ===");
    machineStatus("COMPLETE", 100, "Flash complete");
    return true;
}

//------------------------------------------------------------------------------
// CLI Interface
//------------------------------------------------------------------------------

void printUsage() {
    printf("NT Flash Tool v%s - Disting NT Firmware Flasher\n\n", VERSION);
    printf("Usage:\n");
    printf("  %s <firmware.zip>              Flash from local ZIP file\n", TOOL_NAME);
    printf("  %s --version <X.Y.Z>           Download and flash specific version\n", TOOL_NAME);
    printf("  %s --latest                    Download and flash latest version\n", TOOL_NAME);
    printf("  %s --url <url>                 Download and flash from URL\n", TOOL_NAME);
    printf("  %s --list                      List available firmware versions\n", TOOL_NAME);
    printf("\n");
    printf("Options:\n");
    printf("  -v, --verbose                  Show detailed output\n");
    printf("  -n, --dry-run                  Validate without flashing\n");
    printf("  -m, --machine                  Machine-readable output for tool integration\n");
    printf("  -h, --help                     Show this help\n");
    printf("\n");
    printf("Before flashing, put disting NT in bootloader mode:\n");
    printf("  Menu > Misc > Enter bootloader mode...\n");
}

void printVersionInfo() {
    printf("NT Flash Tool v%s\n", VERSION);
}

int main(int argc, char* argv[]) {
    // Initialize BLFWK logger (suppress unless verbose)
    StdoutLogger* logger = new StdoutLogger();
    logger->setFilterLevel(Logger::kWarning);
    Log::setLogger(logger);

    if (argc < 2) {
        printUsage();
        return 1;
    }

    // Parse arguments
    std::string zipPath;
    std::string version;
    std::string url;
    bool listVersions = false;
    bool useLatest = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        }
        else if (arg == "-V" || arg == "--version-info") {
            printVersionInfo();
            return 0;
        }
        else if (arg == "-v" || arg == "--verbose") {
            g_verbose = true;
            logger->setFilterLevel(Logger::kDebug);
        }
        else if (arg == "-n" || arg == "--dry-run") {
            g_dryRun = true;
        }
        else if (arg == "-m" || arg == "--machine") {
            g_machineOutput = true;
        }
        else if (arg == "--list") {
            listVersions = true;
        }
        else if (arg == "--latest") {
            useLatest = true;
        }
        else if (arg == "--version" && i + 1 < argc) {
            version = argv[++i];
        }
        else if (arg == "--url" && i + 1 < argc) {
            url = argv[++i];
        }
        else if (arg[0] != '-') {
            zipPath = arg;
        }
        else {
            logError("Unknown option: %s", arg.c_str());
            return 1;
        }
    }

    // Handle --list
    if (listVersions) {
        logInfo("Available firmware versions from Expert Sleepers:");
        logInfo("  https://www.expert-sleepers.co.uk/distingNTfirmwareupdates.html");
        logInfo("\nKnown versions: 1.12.0, 1.11.0, 1.10.0, 1.9.0, 1.8.0, 1.7.1, 1.7.0, 1.6.1, 1.6.0");
        return 0;
    }

    if (useLatest) {
        logInfo("Downloading latest firmware (1.12.0)...");
        version = "1.12.0";
    }

    // Determine source
    std::string tempZipPath;

    if (!version.empty()) {
        // Download specific version
        tempZipPath = getTempDir() + "distingNT_" + version + ".zip";
        char downloadUrl[512];
        snprintf(downloadUrl, sizeof(downloadUrl), "%sdistingNT_%s.zip",
                 FIRMWARE_BASE_URL, version.c_str());
        if (!downloadFile(downloadUrl, tempZipPath.c_str())) {
            return 1;
        }
        zipPath = tempZipPath;
    }
    else if (!url.empty()) {
        // Download from URL
        tempZipPath = getTempDir() + "distingNT_download.zip";
        if (!downloadFile(url.c_str(), tempZipPath.c_str())) {
            return 1;
        }
        zipPath = tempZipPath;
    }

    if (zipPath.empty()) {
        logError("No firmware source specified");
        printUsage();
        return 1;
    }

    // Load and flash
    FirmwarePackage* pkg = loadFirmwarePackage(zipPath.c_str());
    if (!pkg) {
        return 1;
    }

    if (g_dryRun) {
        logInfo("[DRY RUN MODE - No actual flashing will occur]");
    }

    bool success = flashFirmware(pkg);

    delete pkg;

    // Clean up downloaded zip
    if (!tempZipPath.empty()) {
        remove(tempZipPath.c_str());
    }

    return success ? 0 : 1;
}
