#include "get_vin_command.h"

#include "config.h"
#include "diagnostics.h"
#include "interface/usb.h"
#include "util/log.h"
#include "config.h"
#include "pb_decode.h"
#include <payload/payload.h>
#include "signals.h"
#include <can/canutil.h>
#include <bitfield/bitfield.h>
#include <limits.h>

using openxc::util::log::debug;
using openxc::config::getConfiguration;
using openxc::payload::PayloadFormat;
using openxc::signals::getCanBuses;
using openxc::signals::getCanBusCount;
using openxc::signals::getSignals;
using openxc::signals::getSignalCount;
using openxc::signals::getCommands;
using openxc::signals::getCommandCount;
using openxc::can::lookupBus;
using openxc::can::lookupSignal;

namespace can = openxc::can;
namespace payload = openxc::payload;
namespace config = openxc::config;
namespace diagnostics = openxc::diagnostics;
namespace usb = openxc::interface::usb;
namespace uart = openxc::interface::uart;
namespace pipeline = openxc::pipeline;

bool openxc::commands::handleGetVinCommand() {
    char* vin;
    bool status = false;

    if (openxc::diagnostics::haveVINfromCan()) {
    	status = true;
    	vin = (char *)openxc::diagnostics::getVIN();
    } else {
        status = true;
        vin = strdup(config::getConfiguration()->dummyVin);
    }
    sendCommandResponse(openxc_ControlCommand_Type_GET_VIN, status, vin, strlen(vin));

    return status;
}