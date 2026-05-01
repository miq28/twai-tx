#include "can_bus.h"
#include "config.h"
#include <Arduino.h>
#include "led_activity.h"

namespace CANDriver
{
    struct CANStatus
    {
        twai_state_t state;
        uint32_t tec;
        uint32_t rec;
    };

    static uint32_t currentBaud = 500000;
    static bool currentListenOnly = false;
    static bool driverRunning = false;

    // ===== auto-recovery =====
    static bool recoveryActive = false;
    static uint32_t recoveryStartTime = 0;
    static uint32_t degradedSince = 0;
    constexpr uint32_t RECOVERY_TIMEOUT_MS = 1000;
    constexpr uint32_t DEGRADED_TIMEOUT_MS = 2000;

    bool getTiming(uint32_t baud, twai_timing_config_t &timing)
    {
        switch (baud)
        {
        case 1000000:
            timing = TWAI_TIMING_CONFIG_1MBITS();
            return true;
        case 800000:
            timing = TWAI_TIMING_CONFIG_800KBITS();
            return true;
#if defined(WEACT_STUDIO_CAN485_V1)
        case 500000:
            // timing.brp = 16;
            // timing.tseg_1 = 7;
            // timing.tseg_2 = 2;
            // timing.sjw = 1;
            // timing.triple_sampling = false;
            // return true;
            timing = TWAI_TIMING_CONFIG_500KBITS();
            return true;
#else
        case 500000:
            timing = TWAI_TIMING_CONFIG_500KBITS();
            return true;
#endif
        case 250000:
            timing = TWAI_TIMING_CONFIG_250KBITS();
            return true;
        case 125000:
            timing = TWAI_TIMING_CONFIG_125KBITS();
            return true;
        case 100000:
            timing = TWAI_TIMING_CONFIG_100KBITS();
            return true;
        case 50000:
            timing = TWAI_TIMING_CONFIG_50KBITS();
            return true;
        case 25000:
            timing = TWAI_TIMING_CONFIG_25KBITS();
            return true;
        case 83333:
            timing.brp = 48;
            timing.tseg_1 = 15;
            timing.tseg_2 = 4;
            timing.sjw = 3;
            timing.triple_sampling = false;
            return true;
        case 33333:
            timing.brp = 120;
            timing.tseg_1 = 15;
            timing.tseg_2 = 4;
            timing.sjw = 3;
            timing.triple_sampling = false;
            return true;
        default:
            return false;
        }
    }

    static bool startDriver(uint32_t baud, bool listenOnly)
    {
        twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
            CAN_TX, CAN_RX,
            listenOnly ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);

        general.tx_queue_len = 32;
        general.rx_queue_len = 256;

        general.alerts_enabled = TWAI_ALERT_ALL;

        twai_timing_config_t timing;
        if (!getTiming(baud, timing))
        {
            CAN_LOG("[CAN] Unsupported baud %lu -> fallback 500k\n", baud);
            timing = TWAI_TIMING_CONFIG_500KBITS();
            baud = 500000;
        }
        else
        {
            CAN_LOG("[CAN] Init OK -> baud: %lu\n", baud);
        }

        twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        if (twai_driver_install(&general, &timing, &filter) != ESP_OK)
        {
            CAN_LOG("[CAN] Install failed\n");
            driverRunning = false;
            return false;
        }

        if (twai_start() != ESP_OK)
        {
            CAN_LOG("[CAN] Start failed\n");
            driverRunning = false;
            return false;
        }

        currentBaud = baud;
        currentListenOnly = listenOnly;
        driverRunning = true;
        CAN_LOG("[CAN] Started (%s)\n", listenOnly ? "LISTEN ONLY" : "NORMAL");
        return true;
    }

    void init(uint32_t baud, bool listenOnly)
    {
        startDriver(baud, listenOnly);
        CANEvents::startTask();
        CANTxBuffer::startTask();
    }

    bool reinit(uint32_t baud, bool listenOnly)
    {
        CAN_LOG("[CAN] Reinit requested -> baud:%lu listen:%d\n", baud, listenOnly);

        // 1. STOP TASKS FIRST (critical)
        CANEvents::stopTask(); // <-- new
        CANRxBuffer::stopTask();
        CANTxBuffer::stopTask();

        // 2. STOP DRIVER
        twai_stop();
        driverRunning = false;

        // 3. UNINSTALL DRIVER
        twai_driver_uninstall();

        // 4. START DRIVER (fresh)
        bool ok = startDriver(baud, listenOnly);

        // 5. CLEAR BUFFERS
        CANRxBuffer::clear();

        // 6. RESTART TASKS
        CANEvents::startTask(); // <-- ADD THIS
        CANRxBuffer::startTask();
        CANTxBuffer::startTask();

        return ok;
    }

    bool send(const twai_message_t &msg)
    {
        return twai_transmit((twai_message_t *)&msg, 0) == ESP_OK;
    }

    bool isRunning()
    {
        return driverRunning;
    }

    uint32_t getCurrentBaud()
    {
        return currentBaud;
    }

    bool isListenOnly()
    {
        return currentListenOnly;
    }

    CANHealthState currentHealth = CAN_HEALTH_OK;

    CANHealthState getCANHealth()
    {
        twai_status_info_t s;

        if (twai_get_status_info(&s) != ESP_OK)
            return CAN_HEALTH_ERROR;

        return getHealthFromState(
            s.state,
            s.tx_error_counter,
            s.rx_error_counter);
    }

    twai_state_t getStateRaw()
    {
        twai_status_info_t s;
        if (twai_get_status_info(&s) == ESP_OK)
            return s.state;

        return TWAI_STATE_STOPPED; // safe fallback
    }

    const char *getStateStr(twai_state_t s)
    {
        switch (s)
        {
        case TWAI_STATE_STOPPED:
            return "STOPPED";
        case TWAI_STATE_RUNNING:
            return "RUNNING";
        case TWAI_STATE_BUS_OFF:
            return "BUS_OFF";
        case TWAI_STATE_RECOVERING:
            return "RECOVERING";
        default:
            return "UNKNOWN";
        }
    }

    const char *getStateStr()
    {
        return getStateStr(getStateRaw());
    }

    CANHealthState getHealthFromState(twai_state_t s,
                                      uint32_t tec,
                                      uint32_t rec)
    {
        if (s == TWAI_STATE_BUS_OFF)
            return CAN_HEALTH_BUS_OFF;

        if (tec > 127 || rec > 127)
            return CAN_HEALTH_ERROR;

        if (tec > 96 || rec > 96)
            return CAN_HEALTH_DEGRADED;

        return CAN_HEALTH_OK;
    }

    bool getStatus(CANStatus &out)
    {
        twai_status_info_t s;
        if (twai_get_status_info(&s) != ESP_OK)
            return false;

        out.state = s.state;
        out.tec = s.tx_error_counter;
        out.rec = s.rx_error_counter;
        return true;
    }

    CANHealthState getHealth(const CANStatus &s)
    {
        return getHealthFromState(s.state, s.tec, s.rec);
    }

    void logStatus(const CANStatus &s)
    {
        const char *state_str = getStateStr(s.state);

        CAN_LOG("[CAN] %s TEC=%d REC=%d\n",
                state_str,
                s.tec,
                s.rec);
    }
}

namespace CANRxBuffer
{
    // ===== CONTROL =====
    static TaskHandle_t rxTaskHandle = nullptr;
    static volatile bool rxRunning = false;

    static StaticEventGroup_t rxEventGroupBuf;
    static EventGroupHandle_t rxEventGroup = nullptr;

#define RX_TASK_STOPPED_BIT BIT0

    // ===== BUFFER =====
    constexpr uint16_t RX_BUF_SIZE = 1024;

    CANRxItem rxBuffer[RX_BUF_SIZE];
    volatile uint16_t rxHead = 0;
    volatile uint16_t rxTail = 0;

    volatile uint32_t dropCount = 0;
    volatile uint32_t totalFrames = 0;
    volatile uint16_t maxUsage = 0;

    // FPS
    static uint32_t lastTotalFrames = 0;
    static uint32_t lastFpsTime = 0;
    static uint32_t rxFps = 0;

    // ===== RATE TRACKING =====
    static uint32_t lastRateTime = 0;

    static uint32_t last_total = 0;
    static uint32_t last_drop = 0;

    static uint32_t rate_rx = 0;
    static uint32_t rate_drop = 0;

    // ===== PUSH =====
    bool push(const twai_message_t &msg, uint32_t ts)
    {
        uint16_t next = (rxHead + 1) % RX_BUF_SIZE;

        uint16_t used = (rxHead - rxTail + RX_BUF_SIZE) % RX_BUF_SIZE;
        if (used > maxUsage)
            maxUsage = used;

        if (next == rxTail)
        {
            rxTail = (rxTail + 1) % RX_BUF_SIZE;
            dropCount = dropCount + 1;
        }

        rxBuffer[rxHead].msg = msg;
        rxBuffer[rxHead].timestamp = ts;
        rxHead = next;

        totalFrames = totalFrames + 1;
        return true;
    }

    // ===== TASK =====
    void task(void *)
    {
        twai_message_t msg;

        rxRunning = true;

        while (rxRunning)
        {
            updateRates();

            // ---- RECEIVE ONLY (critical path) ----
            // Drain ALL available frames (non-blocking)
            int burst = 0;
            bool received = false;

            while (twai_receive(&msg, 0) == ESP_OK)
            {
                received = true;

                (void)push(msg, micros());
                ledRxEvent();

                if (++burst >= 32)
                {
                    burst = 0;
                    taskYIELD();
                }
            }

            if (!received)
            {
                vTaskDelay(1); // sleep when no traffic
            }
        }

        // =========================================================
        // CLEAN SHUTDOWN
        // =========================================================
        xEventGroupSetBits(rxEventGroup, RX_TASK_STOPPED_BIT);

        rxTaskHandle = nullptr;
        vTaskDelete(NULL);
    }

    // ===== POP =====
    bool pop(CANRxItem &out)
    {
        if (rxTail == rxHead)
            return false;

        out = rxBuffer[rxTail];
        rxTail = (rxTail + 1) % RX_BUF_SIZE;
        return true;
    }

    int count()
    {
        return (rxHead - rxTail + RX_BUF_SIZE) % RX_BUF_SIZE;
    }

    // ===== START =====
    void startTask()
    {
        if (!rxEventGroup)
        {
            rxEventGroup = xEventGroupCreateStatic(&rxEventGroupBuf);
        }

        if (rxTaskHandle)
        {
            stopTask();
        }

        xEventGroupClearBits(rxEventGroup, RX_TASK_STOPPED_BIT);
        rxRunning = true;

        xTaskCreatePinnedToCore(
            task,
            "can_rx",
            4096,
            NULL,
            16,
            &rxTaskHandle,
            0);
    }

    // ===== STOP (SAFE) =====
    void stopTask()
    {
        if (rxTaskHandle)
        {
            rxRunning = false;

            // wait until task fully exits
            xEventGroupWaitBits(
                rxEventGroup,
                RX_TASK_STOPPED_BIT,
                pdTRUE,
                pdTRUE,
                pdMS_TO_TICKS(200));
        }
    }

    // ===== UTIL =====
    void clear()
    {
        rxTail = rxHead;
    }

    uint32_t getDropCount() { return dropCount; }
    uint32_t getTotalFrames() { return totalFrames; }
    uint16_t getMaxUsage() { return maxUsage; }

    uint32_t getRxFps()
    {
        uint32_t now = millis();

        if (now - lastFpsTime >= 200)
        {
            uint32_t current = totalFrames;
            rxFps = current - lastTotalFrames;

            lastTotalFrames = current;
            lastFpsTime = now;
        }

        return rxFps;
    }

    void resetStats()
    {
        dropCount = 0;
        totalFrames = 0;
        maxUsage = 0;
    }

    void updateRates()
    {
        uint32_t now = millis();

        if (now - lastRateTime >= 1000)
        {
            rate_rx = totalFrames - last_total;
            rate_drop = dropCount - last_drop;

            last_total = totalFrames;
            last_drop = dropCount;

            lastRateTime = now;
        }
    }

    uint32_t getRateRx() { return rate_rx; }
    uint32_t getRateDrop() { return rate_drop; }

    uint16_t getUsage()
    {
        return (rxHead - rxTail + RX_BUF_SIZE) % RX_BUF_SIZE;
    }

    uint16_t getCapacity()
    {
        return RX_BUF_SIZE;
    }
}

// Helper: check if a category is enabled
static inline bool catEnabled(uint32_t cat)
{
    return (settings.canLogMask & cat) != 0;
}

// Rate limiter
static inline bool logEvery(uint32_t &last, uint32_t interval_ms)
{
    uint32_t now = millis();
    if (now - last >= interval_ms)
    {
        last = now;
        return true;
    }
    return false;
}

void setCanLogPreset(const char *name)
{
    uint32_t mask = settings.canLogMask;

    if (!strcmp(name, "prod"))
        mask = CAN_ALERT_CRITICAL | CAN_ALERT_OPERATIONAL;

    else if (!strcmp(name, "debug"))
        mask = CAN_ALERT_CRITICAL | CAN_ALERT_OPERATIONAL | CAN_ALERT_INFO;

    else if (!strcmp(name, "verbose"))
        mask = 0xFFFFFFFF;

    else if (!strcmp(name, "silent"))
        mask = 0;

    setCANLogMask(mask); // ← IMPORTANT (persist + apply)
}

static const struct
{
    const char *name;
    uint32_t bit;
} kCatMap[] = {
    {"critical", CAN_ALERT_CRITICAL},
    {"operational", CAN_ALERT_OPERATIONAL},
    {"info", CAN_ALERT_INFO},
    {"rare", CAN_ALERT_RARE},
};

uint32_t parseCategories(const String &csv)
{
    uint32_t mask = 0;
    int start = 0;
    while (start >= 0)
    {
        int comma = csv.indexOf(',', start);
        String tok = (comma < 0) ? csv.substring(start) : csv.substring(start, comma);
        tok.trim();
        tok.toLowerCase();

        for (auto &e : kCatMap)
        {
            if (tok == e.name)
            {
                mask |= e.bit;
                break;
            }
        }

        if (comma < 0)
            break;
        start = comma + 1;
    }
    return mask;
}

String categoriesToString(uint32_t mask)
{
    String out;
    for (auto &e : kCatMap)
    {
        if (mask & e.bit)
        {
            if (out.length())
                out += ",";
            out += e.name;
        }
    }
    if (!out.length())
        out = "none";
    return out;
}

// ACTION layer (no logging here)
static void handleAlertAction(uint32_t bit)
{
    switch (bit)
    {
    case TWAI_ALERT_BUS_OFF:
        if (twai_initiate_recovery() == ESP_OK)
        {
            CANDriver::recoveryActive = true;
        }
        break;

    case TWAI_ALERT_BUS_RECOVERED:
        twai_start(); // required for legacy driver
        CANDriver::recoveryActive = false;
        break;

    // health-relevant (no immediate action, but you may hook counters here)
    case TWAI_ALERT_ERR_PASS:
    case TWAI_ALERT_ABOVE_ERR_WARN:
    case TWAI_ALERT_BELOW_ERR_WARN:
    case TWAI_ALERT_ERR_ACTIVE:
    case TWAI_ALERT_RECOVERY_IN_PROGRESS:
        break;

    // operational signals (optional hooks)
    case TWAI_ALERT_TX_FAILED:
    case TWAI_ALERT_RX_QUEUE_FULL:
    case TWAI_ALERT_RX_FIFO_OVERRUN:
        break;

    // informational / rare → no action
    case TWAI_ALERT_TX_IDLE:
    case TWAI_ALERT_TX_SUCCESS:
    case TWAI_ALERT_RX_DATA:
    case TWAI_ALERT_ARB_LOST:
    case TWAI_ALERT_BUS_ERROR:
    case TWAI_ALERT_TX_RETRIED:
    case TWAI_ALERT_PERIPH_RESET:
    default:
        break;
    }
}

// LOG layer (category-filtered + throttled)
static void handleAlertLog(uint32_t bit)
{
    // throttles for noisy alerts
    static uint32_t t_bus_err = 0;
    static uint32_t t_arb = 0;
    static uint32_t t_tx_succ = 0;

    switch (bit)
    {
    // =========================
    // 🔴 CRITICAL
    // =========================
    case TWAI_ALERT_BUS_OFF:
        if (catEnabled(CAN_ALERT_CRITICAL))
            CAN_LOG("[CAN EVT] BUS OFF → start recovery\n");
        break;

    case TWAI_ALERT_BUS_RECOVERED:
        if (catEnabled(CAN_ALERT_CRITICAL))
            CAN_LOG("[CAN EVT] BUS RECOVERED\n");
        break;

    case TWAI_ALERT_ERR_PASS:
        if (catEnabled(CAN_ALERT_CRITICAL))
            CAN_LOG("[CAN EVT] ERROR PASSIVE\n");
        break;

    case TWAI_ALERT_ABOVE_ERR_WARN:
        if (catEnabled(CAN_ALERT_CRITICAL))
            CAN_LOG("[CAN EVT] ABOVE ERROR WARNING\n");
        break;

    // =========================
    // 🟠 OPERATIONAL
    // =========================
    case TWAI_ALERT_TX_FAILED:
        if (catEnabled(CAN_ALERT_OPERATIONAL))
            CAN_LOG("[CAN EVT] TX FAILED\n");
        break;

    case TWAI_ALERT_RX_QUEUE_FULL:
        if (catEnabled(CAN_ALERT_OPERATIONAL))
            CAN_LOG("[CAN EVT] RX QUEUE FULL\n");
        break;

    case TWAI_ALERT_RX_FIFO_OVERRUN:
        if (catEnabled(CAN_ALERT_OPERATIONAL))
            CAN_LOG("[CAN EVT] RX FIFO OVERRUN\n");
        break;

    case TWAI_ALERT_BELOW_ERR_WARN:
        if (catEnabled(CAN_ALERT_OPERATIONAL))
            CAN_LOG("[CAN EVT] BELOW ERROR WARNING\n");
        break;

    case TWAI_ALERT_ERR_ACTIVE:
        if (catEnabled(CAN_ALERT_OPERATIONAL))
            CAN_LOG("[CAN EVT] ERROR ACTIVE\n");
        break;

    case TWAI_ALERT_RECOVERY_IN_PROGRESS:
        if (catEnabled(CAN_ALERT_OPERATIONAL))
            CAN_LOG("[CAN EVT] RECOVERY IN PROGRESS\n");
        break;

    // =========================
    // 🔵 INFO (noisy)
    // =========================
    case TWAI_ALERT_TX_IDLE:
        if (catEnabled(CAN_ALERT_INFO))
            CAN_LOG("[CAN EVT] TX IDLE\n");
        break;

    case TWAI_ALERT_TX_SUCCESS:
        if (catEnabled(CAN_ALERT_INFO) && logEvery(t_tx_succ, 200))
            CAN_LOG("[CAN EVT] TX SUCCESS\n");
        break;

    case TWAI_ALERT_ARB_LOST:
        if (catEnabled(CAN_ALERT_INFO) && logEvery(t_arb, 200))
            CAN_LOG("[CAN EVT] ARBITRATION LOST\n");
        break;

    case TWAI_ALERT_BUS_ERROR:
        if (catEnabled(CAN_ALERT_INFO) && logEvery(t_bus_err, 200))
            CAN_LOG("[CAN EVT] BUS ERROR\n");
        break;

    case TWAI_ALERT_RX_DATA:
        // intentionally ignored (RX handled elsewhere)
        break;

    // =========================
    // 🟣 RARE
    // =========================
    case TWAI_ALERT_TX_RETRIED:
        if (catEnabled(CAN_ALERT_RARE))
            CAN_LOG("[CAN EVT] TX RETRIED (ERRATA)\n");
        break;

    case TWAI_ALERT_PERIPH_RESET:
        if (catEnabled(CAN_ALERT_RARE))
            CAN_LOG("[CAN EVT] PERIPHERAL RESET\n");
        break;

    default:
        if (catEnabled(CAN_ALERT_RARE))
            CAN_LOG("[CAN EVT] UNKNOWN: 0x%08lX\n", bit);
        break;
    }
}

namespace CANEvents
{
    static twai_state_t last_state = TWAI_STATE_STOPPED;

    static TaskHandle_t evtTaskHandle = nullptr;
    static volatile bool evtRunning = false;

    void process()
    {
        uint32_t now = millis();

        // -------------------------
        // ALERT EVENTS (non-blocking)
        // -------------------------
        uint32_t alerts;
        if (twai_read_alerts(&alerts, 0) == ESP_OK && alerts)
        {
            uint32_t pending = alerts;

            while (pending)
            {
                uint32_t bit = pending & -pending;
                pending &= ~bit;

                // 1) ALWAYS execute behavior
                handleAlertAction(bit);

                // 2) OPTIONAL logging
                handleAlertLog(bit);
            }
        }

        // -------------------------
        // SINGLE STATUS FETCH
        // -------------------------
        CANDriver::CANStatus st;
        if (!CANDriver::getStatus(st))
            return;

        // -------------------------
        // LED (health)
        // -------------------------
        CANHealthState h = CANDriver::getHealth(st);
        ledSetCANHealth(h);

        // -------------------------
        // LOG (throttled)
        // -------------------------
        static uint32_t lastPrint = 0;
        static uint32_t lastTec = 0;
        static uint32_t lastRec = 0;

        if (now - lastPrint > 200)
        {
            lastPrint = now;

            // log only when error appears OR changes
            if ((st.tec != lastTec || st.rec != lastRec))
            {
                if (st.tec > 0 || st.rec > 0)
                {
                    CANDriver::logStatus(st);
                }
                else if (lastTec > 0 || lastRec > 0)
                {
                    CAN_LOG("[CAN] RECOVERED (TEC=0 REC=0)\n");
                }

                lastTec = st.tec;
                lastRec = st.rec;
            }
        }

        // -------------------------
        // SYSTEM RATE (1s)
        // -------------------------
        static uint32_t lastRatePrint = 0;

        if (now - lastRatePrint > 1000)
        {
            lastRatePrint = now;

            uint16_t used = CANRxBuffer::getUsage();
            uint16_t cap = CANRxBuffer::getCapacity();

            uint32_t usage_pct = (used * 100UL) / cap;
            uint32_t max_pct = (CANRxBuffer::getMaxUsage() * 100UL) / cap;

            DEBUG("[CAN] RX:%lu/s drop:%lu/s buf:%u%% max:%u%% | TX a:%lu ok:%lu f:%lu d:%lu b:%lu\n",
                  CANRxBuffer::getRateRx(),
                  CANRxBuffer::getRateDrop(),
                  usage_pct,
                  max_pct,
                  CANTxBuffer::getRateAttempt(),
                  CANTxBuffer::getRateOk(),
                  CANTxBuffer::getRateFail(),
                  CANTxBuffer::getRateDrop(),
                  CANTxBuffer::getRateBlock());
        }
    }

    void canEventTask(void *)
    {
        evtRunning = true;

        while (evtRunning)
        {
            CANEvents::process();
            vTaskDelay(1);
        }

        evtTaskHandle = nullptr;
        vTaskDelete(NULL);
    }

    void startTask()
    {
        if (evtTaskHandle)
            return;

        xTaskCreatePinnedToCore(
            canEventTask,
            "can_evt",
            3072,
            NULL,
            10,
            &evtTaskHandle, // <-- important
            0);
    }

    void stopTask()
    {
        if (evtTaskHandle)
        {
            evtRunning = false;
            vTaskDelay(pdMS_TO_TICKS(10)); // allow exit
        }
    }
}

namespace CANTxBuffer
{
    // ===== CONTROL =====
    static TaskHandle_t txTaskHandle = nullptr;
    static volatile bool txRunning = false;

    // ===== BUFFER =====
    constexpr uint16_t TX_BUF_SIZE = 1024;

    struct TXItem
    {
        twai_message_t msg;
    };

    static TXItem txBuffer[TX_BUF_SIZE];
    static volatile uint16_t txHead = 0;
    static volatile uint16_t txTail = 0;

    static volatile uint32_t tx_attempt = 0;
    static volatile uint32_t tx_ok = 0;
    static volatile uint32_t tx_fail = 0;
    static volatile uint32_t tx_drop = 0;
    static volatile uint32_t tx_block = 0;

    // rate state
    static uint32_t lastRateTime = 0;

    static uint32_t last_attempt = 0;
    static uint32_t last_ok = 0;
    static uint32_t last_fail = 0;
    static uint32_t last_drop = 0;
    static uint32_t last_block = 0;

    static uint32_t rate_attempt = 0;
    static uint32_t rate_ok = 0;
    static uint32_t rate_fail = 0;
    static uint32_t rate_drop = 0;
    static uint32_t rate_block = 0;

    // ===== PUSH =====
    bool push(const twai_message_t &msg)
    {
        // count attempt at API level
        tx_attempt += 1;

        if (CANDriver::isListenOnly())
        {
            tx_block += 1;
            return false;
        }

        uint16_t next = (txHead + 1) % TX_BUF_SIZE;

        if (next == txTail)
        {
            tx_drop++;
            return false;
        }

        txBuffer[txHead].msg = msg;
        txHead = next;
        return true;
    }

    // ===== POP =====
    bool pop(twai_message_t &msg)
    {
        if (txTail == txHead)
            return false;

        msg = txBuffer[txTail].msg;
        txTail = (txTail + 1) % TX_BUF_SIZE;
        return true;
    }

    // ===== Rate Update =====
    void updateRates()
    {
        uint32_t now = millis();

        if (now - lastRateTime >= 1000) // 1 second window
        {
            rate_attempt = tx_attempt - last_attempt;
            rate_ok = tx_ok - last_ok;
            rate_fail = tx_fail - last_fail;
            rate_drop = tx_drop - last_drop;
            rate_block = tx_block - last_block;

            last_attempt = tx_attempt;
            last_ok = tx_ok;
            last_fail = tx_fail;
            last_drop = tx_drop;
            last_block = tx_block;

            lastRateTime = now;
        }
    }

    // ===== TASK =====
    void task(void *)
    {
        twai_message_t msg;
        txRunning = true;

        while (txRunning)
        {
            if (!pop(msg))
            {
                vTaskDelay(1);
                continue;
            }

            // safety guard (should rarely trigger if push() is correct)
            if (CANDriver::isListenOnly())
            {
                tx_block += 1;
                continue;
            }

            // yield occasionally to avoid starving RX
            taskYIELD();

            if (twai_transmit(&msg, pdMS_TO_TICKS(2)) == ESP_OK)
            {
                tx_ok += 1;
                ledTxEvent();
            }
            else
            {
                tx_fail += 1;
            }

            updateRates();
        }

        txTaskHandle = nullptr;
        vTaskDelete(NULL);
    }

    // ===== START =====
    void startTask()
    {
        if (txTaskHandle)
            return;

        txRunning = true;

        xTaskCreatePinnedToCore(
            task,
            "can_tx",
            4096,
            NULL,
            15,
            &txTaskHandle,
            0); // same core as RX is fine
    }

    // ===== STOP =====
    void stopTask()
    {
        if (txTaskHandle)
        {
            txRunning = false;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // ===== STATS =====
    uint32_t getTxAttempt() { return tx_attempt; }
    uint32_t getTxOk() { return tx_ok; }
    uint32_t getTxFail() { return tx_fail; }
    uint32_t getTxDrop() { return tx_drop; }
    uint32_t getTxBlock() { return tx_block; }

    uint32_t getRateAttempt() { return rate_attempt; }
    uint32_t getRateOk() { return rate_ok; }
    uint32_t getRateFail() { return rate_fail; }
    uint32_t getRateDrop() { return rate_drop; }
    uint32_t getRateBlock() { return rate_block; }

    void resetStats()
    {
        tx_attempt = 0;
        tx_ok = 0;
        tx_fail = 0;
        tx_drop = 0;
        tx_block = 0;
    }
}