#include "can/canutil.h"
#include "canutil_pic32.h"
#include "signals.h"
#include "util/log.h"
#include "gpio.h"

#if defined(CROSSCHASM_C5)
    #define CAN1_TRANSCEIVER_SWITCHED
    #define CAN1_TRANSCEIVER_ENABLE_POLARITY    0
    #define CAN1_TRANSCEIVER_ENABLE_PIN            38 // PORTD BIT10 (RD10)
#endif

#define CAN_RX_CHANNEL 1
#define BUS_MEMORY_BUFFER_SIZE 2 * 8 * 16

namespace gpio = openxc::gpio;

using openxc::gpio::GpioValue;
using openxc::util::log::debug;
using openxc::gpio::GPIO_VALUE_LOW;
using openxc::gpio::GPIO_VALUE_HIGH;
using openxc::gpio::GPIO_DIRECTION_OUTPUT;

CAN can1Actual(CAN::CAN1);
CAN can2Actual(CAN::CAN2);
CAN* can1 = &can1Actual;
CAN* can2 = &can2Actual;

/* Private:  A message area for each bus, for 2 channels to store 8 16 byte
 * messages - required by the PIC32 CAN library. We could add this to the CanBus
 * struct, but the PIC32 has way more memory than some of our other supported
 * platforms so I don't want to burden them unnecessarily.
 */
uint8_t CAN_CONTROLLER_BUFFERS[2][BUS_MEMORY_BUFFER_SIZE];

static CAN::OP_MODE switchControllerMode(CanBus* bus, CAN::OP_MODE mode) {
    CAN::OP_MODE previousMode = CAN_CONTROLLER(bus)->getOperatingMode();
    if(previousMode != mode) {
        CAN_CONTROLLER(bus)->setOperatingMode(mode);
        while(CAN_CONTROLLER(bus)->getOperatingMode() != mode);
    }
    return previousMode;
}

static bool setAcceptanceFilterStatus(CanBus* bus, bool enabled) {
    CAN::OP_MODE previousMode = switchControllerMode(bus, CAN::CONFIGURATION);
    if(enabled) {
        debug("Enabling primary AF filter mask for bus %d", bus->address);
        CAN_CONTROLLER(bus)->configureFilterMask(CAN::FILTER_MASK0, 0xFFF,
                CAN::EID, CAN::FILTER_MASK_ANY_TYPE);
    } else {
        debug("Disabling primary AF filter mask to allow all messages through");
        CAN_CONTROLLER(bus)->configureFilterMask(CAN::FILTER_MASK0, 0, CAN::EID,
            CAN::FILTER_MASK_ANY_TYPE);
        CAN_CONTROLLER(bus)->configureFilter(CAN::FILTER0, 0, CAN::EID);
        CAN_CONTROLLER(bus)->linkFilterToChannel(
                CAN::FILTER0, CAN::FILTER_MASK0, CAN::CHANNEL1);
        CAN_CONTROLLER(bus)->enableFilter(CAN::FILTER0,
                true);
    }
    switchControllerMode(bus, previousMode);
    return true;
}

bool openxc::can::updateAcceptanceFilterTable(CanBus* buses, const int busCount) {
    // For the PIC32 we *could* only change the filters for one bus, but to
    // simplify things we'll reset everything like we have to with the LPC1768
    for(int i = 0; i < busCount; i++) {
        CanBus* bus = &buses[i];
        uint16_t filterCount = 0;
        CAN::OP_MODE previousMode = switchControllerMode(bus, CAN::CONFIGURATION);

        bool afFilterStatusSet = false;
        AcceptanceFilterListEntry* entry;
        LIST_FOREACH(entry, &bus->acceptanceFilters, entries) {
            if(++filterCount > MAX_ACCEPTANCE_FILTERS) {
                break;
            }

            if(!afFilterStatusSet) {
                // Must set the master AF filter status first and only once,
                // because it wipes anything you've configured when you set it.
                setAcceptanceFilterStatus(bus, true);
                afFilterStatusSet = true;
            }

            // Must disable before changing or else the filters do not work!
            CAN_CONTROLLER(bus)->enableFilter(CAN::FILTER(filterCount), false);
            CAN_CONTROLLER(bus)->configureFilter(
                    CAN::FILTER(filterCount), entry->filter,
                    entry->format == CanMessageFormat::STANDARD ?
                        CAN::SID : CAN::EID);
            CAN_CONTROLLER(bus)->linkFilterToChannel(CAN::FILTER(filterCount),
                    CAN::FILTER_MASK0, CAN::CHANNEL(CAN_RX_CHANNEL));
            CAN_CONTROLLER(bus)->enableFilter(CAN::FILTER(filterCount), true);
        }

        if(filterCount == 0) {
            debug("No filters configured, turning off acceptance filter");
            setAcceptanceFilterStatus(bus, false);
        } else {
            // Disable the remaining unused filters. When AF is "off" we are
            // actually using filter 0, so we don't want to disable that.
            for(int disabledFilters = filterCount;
                    disabledFilters < MAX_ACCEPTANCE_FILTERS; ++disabledFilters) {
                CAN_CONTROLLER(bus)->enableFilter(CAN::FILTER(disabledFilters), false);
            }
        }

        switchControllerMode(bus, previousMode);
    }

    return true;
}

/* Public: Change the operational mode of the specified CAN module to
 * CAN_DISABLE. Also set state of any off-chip CAN line driver as needed for
 * platform.
 *
 * CAN module will still be capable of wake from sleep.
 * The OP_MODE of the CAN module itself is actually irrelevant
 * when going to sleep. The main reason for this is to provide a generic
 * function call to disable the off-chip transceiver(s), which saves power,
 * without disabling the CAN module itself.
 */
void openxc::can::deinitialize(CanBus* bus) {
    switchControllerMode(bus, CAN::DISABLE);

    // disable off-chip line driver
    #if defined(CAN1_TRANSCEIVER_SWITCHED)
    GpioValue value = CAN1_TRANSCEIVER_ENABLE_POLARITY ? GPIO_VALUE_LOW : GPIO_VALUE_HIGH;
    gpio::setDirection(0, CAN1_TRANSCEIVER_ENABLE_PIN, GPIO_DIRECTION_OUTPUT);
    gpio::setValue(0, CAN1_TRANSCEIVER_ENABLE_PIN, value);
    #endif
}

void openxc::can::initialize(CanBus* bus, bool writable, CanBus* buses,
        const int busCount) {
    can::initializeCommon(bus);
    // Switch the CAN module ON and switch it to Configuration mode. Wait till
    // the switch is complete
    CAN_CONTROLLER(bus)->enableModule(true);
    switchControllerMode(bus, CAN::CONFIGURATION);

    // Configure the CAN Module Clock. The CAN::BIT_CONFIG data structure is
    // used for this purpose. The propagation, phase segment 1 and phase segment
    // 2 are configured to have 3TQ. The CANSetSpeed() function sets the baud.
    CAN::BIT_CONFIG canBitConfig;
    canBitConfig.phaseSeg2Tq            = CAN::BIT_3TQ;
    canBitConfig.phaseSeg1Tq            = CAN::BIT_3TQ;
    canBitConfig.propagationSegTq       = CAN::BIT_3TQ;
    canBitConfig.phaseSeg2TimeSelect    = CAN::TRUE;
    canBitConfig.sample3Time            = CAN::TRUE;
    canBitConfig.syncJumpWidth          = CAN::BIT_2TQ;
    CAN_CONTROLLER(bus)->setSpeed(&canBitConfig, SYS_FREQ, bus->speed);

    // Assign the buffer area to the CAN module. Note the size of each Channel
    // area. It is 2 (Channels) * 8 (Messages Buffers) 16 (bytes/per message
    // buffer) bytes. Each CAN module should have its own message area.
    CAN_CONTROLLER(bus)->assignMemoryBuffer(
            CAN_CONTROLLER_BUFFERS[bus->address - 1],
            BUS_MEMORY_BUFFER_SIZE);

    // Configure channel 0 for TX with 8 byte buffers and with "Remote Transmit
    // Request" disabled, meaning that other nodes can't request for us to
    // transmit data.
    CAN_CONTROLLER(bus)->configureChannelForTx(CAN::CHANNEL0, 8,
            CAN::TX_RTR_DISABLED, CAN::LOW_MEDIUM_PRIORITY);

    // Configure channel 1 for RX with 8 byte buffers - remember this is channel
    // 1 on the given bus, it doesn't mean CAN1 or CAN2 on the chipKIT board.
    CAN_CONTROLLER(bus)->configureChannelForRx(CAN::CHANNEL1, 8,
            CAN::RX_FULL_RECEIVE);

    if(!configureDefaultFilters(bus, openxc::signals::getMessages(),
                openxc::signals::getMessageCount(), buses, busCount)) {
        debug("Unable to initialize CAN acceptance filters");
    }

    // Enable interrupt and events. Enable the receive channel not empty event
    // (channel event) and the receive channel event (module event). The
    // interrrupt peripheral library is used to enable the CAN interrupt to the
    // CPU.
    CAN_CONTROLLER(bus)->enableChannelEvent(CAN::CHANNEL1,
            CAN::RX_CHANNEL_NOT_EMPTY, true);
    CAN_CONTROLLER(bus)->enableModuleEvent(CAN::RX_EVENT, true);

    // enable the bus activity wake-up event (to enable wake from sleep)
    CAN_CONTROLLER(bus)->enableModuleEvent(
            CAN::BUS_ACTIVITY_WAKEUP_EVENT, true);
    CAN_CONTROLLER(bus)->enableFeature(CAN::WAKEUP_BUS_FILTER, true);

    // switch ON off-chip CAN line drivers (if necessary)
    #if defined(CAN1_TRANSCEIVER_SWITCHED)
    GpioValue value = CAN1_TRANSCEIVER_ENABLE_POLARITY ? GPIO_VALUE_HIGH : GPIO_VALUE_LOW;
    gpio::setDirection(0, CAN1_TRANSCEIVER_ENABLE_PIN, GPIO_DIRECTION_OUTPUT);
    gpio::setValue(0, CAN1_TRANSCEIVER_ENABLE_PIN, value);
    #endif

    // move CAN module to OPERATIONAL state (go on bus)
    CAN::OP_MODE mode;
    if(writable) {
        debug("Initializing bus %d in writable mode", bus->address);
        mode = CAN::NORMAL_OPERATION;
    } else {
        debug("Initializing bus %d in listen only mode", bus->address);
        mode = CAN::LISTEN_ONLY;
    }
    switchControllerMode(bus, mode);

    CAN_CONTROLLER(bus)->attachInterrupt(bus->interruptHandler);
    debug("Done.");
}
