#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "can/canutil.h"
#include "interface/uart.h"
#include "interface/usb.h"
#include "interface/network.h"
#include "diagnostics.h"
#include "pipeline.h"
#include <payload/payload.h>

// TODO find a good home for this
#define MAX_OUTGOING_PAYLOAD_SIZE 256
#define UART_BAUD_RATE 230400

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

namespace openxc {
namespace config {

typedef enum {
    ALWAYS_ON,
    SILENT_CAN,
    OBD2_IGNITION_CHECK,
} PowerManagement;

typedef enum {
    NOT_RUNNING,
    CAN_ONLY,
    ALL_IO,
} RunLevel;

typedef struct {
    int messageSetIndex;
    const char* version;
    openxc::payload::PayloadFormat payloadFormat;
    bool initialized;
    bool recurringObd2Requests;
    openxc::interface::uart::UartDevice uart;
    openxc::interface::network::NetworkDevice network;
    openxc::interface::usb::UsbDevice usb;
    openxc::diagnostics::DiagnosticsManager diagnosticsManager;
    openxc::pipeline::Pipeline pipeline;
    uint8_t obd2BusAddress;
    PowerManagement powerManagement;
    bool sendCanAcks;
    bool emulatedData;
    bool uartLogging;
    bool calculateMetrics;
    RunLevel runLevel;
    RunLevel desiredRunLevel;
} Configuration;

Configuration* getConfiguration();

void getFirmwareDescriptor(char* buffer, size_t length);

} // namespace config
} // namespace openxc

#endif // _CONFIG_H_
