/*
 * SPDX-License-Identifier: MIT
 *
 * P3 Zigbee Window Covering prototype — sleepy End Device.
 * UART motor TX and ZCL interpretation of DP3/DP13 are compile-time
 * DISABLED by default. Bootloader/SoftDevice map not yet verified.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/settings/settings.h>
#include <string.h>

#include "tuya_uart.h"

/* Select HA Window Covering device definition before ZBOSS header import. */
#define ZB_HA_DEFINE_DEVICE_WINDOW_COVERING
#include <zboss_api.h>
#include <zboss_api_zcl.h>
#include <zboss_api_addons.h>
#include <zb_nrf_platform.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>

#define BLIND_ENDPOINT 10
#define BLIND_REPORT_RECORDS 8

/* LocalDIY read-only Basic-cluster diagnostics. Standard lift/battery
 * reporting remains disabled until the physical motor semantics are proven.
 * These private attribute IDs are accessed without a manufacturer header.
 */
#define P3_DIAG_RX_BYTES 0xF100U
#define P3_DIAG_VALID_FRAMES 0xF101U
#define P3_DIAG_REJECTED 0xF102U
#define P3_DIAG_DROPPED 0xF103U
#define P3_DIAG_REPORTS 0xF104U
#define P3_DIAG_LAST_COMMAND 0xF105U
#define P3_DIAG_LAST_FORMAT 0xF106U
#define P3_DIAG_DP1 0xF107U
#define P3_DIAG_DP3 0xF108U
#define P3_DIAG_DP13 0xF109U
#define P3_DIAG_RX_READY 0xF10AU
#define P3_DIAG_STARTUP_SENT 0xF10BU
#define P3_DIAG_STARTUP_DONE 0xF10CU
#define P3_CAL_COMMAND 0xF10DU
#define P3_CAL_BOTTOM 0xF10EU
#define P3_CAL_TOP 0xF10FU
#define P3_CAL_VALID 0xF110U
#define P3_HW_CAL_COMMAND 0xF111U /* 1 reset, 2 start, 3 finish */
#define P3_HW_CAL_PHASE 0xF112U /* 0 idle, 1 reset, 2 training */
#define P3_DIAG_TX_FRAMES 0xF113U
#define P3_DIAG_QUEUE_TO_TX_MS 0xF114U
#define P3_DIAG_UART_TX_MS 0xF115U
#define P3_DIAG_RX_AFTER_TX_MS 0xF116U
#define P3_ZB_COMMANDS 0xF117U
#define P3_ZB_CALLBACK_TO_TX_MS 0xF118U

LOG_MODULE_REGISTER(zigbee_blind, LOG_LEVEL_INF);

struct blind_device_context {
    zb_zcl_basic_attrs_t basic_attr;
    zb_zcl_identify_attrs_t identify_attr;
    zb_zcl_groups_attrs_t groups_attr;
    zb_zcl_scenes_attrs_t scenes_attr;

    zb_uint8_t covering_type;
    zb_uint8_t covering_config_status;
    zb_uint8_t position_lift_pct;
    zb_uint8_t position_tilt_pct;
    zb_uint16_t open_limit_lift;
    zb_uint16_t closed_limit_lift;
    zb_uint16_t open_limit_tilt;
    zb_uint16_t closed_limit_tilt;
    zb_uint8_t covering_mode;

    zb_uint8_t battery_voltage;       /* 0xFF: unknown, no ADC measurement */
    zb_uint8_t battery_remaining_half;/* 0xFF: unknown, ZCL unit 0.5% */

    /* Raw, volatile UART receiver diagnostics (Basic 0xF100..0xF10A). */
    zb_uint32_t diag_rx_bytes;
    zb_uint32_t diag_tx_frames;
    zb_uint32_t diag_zigbee_commands;
    zb_uint32_t diag_zigbee_to_tx_ms;
    zb_uint32_t diag_queue_to_tx_ms;
    zb_uint32_t diag_uart_tx_ms;
    zb_uint32_t diag_rx_after_tx_ms;
    zb_uint32_t diag_valid_frames;
    zb_uint32_t diag_rejected_frames;
    zb_uint32_t diag_dropped_bytes;
    zb_uint32_t diag_accepted_reports;
    zb_uint8_t diag_last_command;
    zb_uint8_t diag_last_format;
    zb_uint8_t diag_dp1_raw;
    zb_uint8_t diag_dp3_raw;
    zb_uint8_t diag_dp13_raw;
    zb_uint8_t diag_rx_ready;
    zb_uint8_t diag_startup_requests_sent;
    zb_uint8_t diag_startup_finished;
    zb_uint8_t cal_command;
    zb_uint8_t cal_bottom;
    zb_uint8_t cal_top;
    zb_uint8_t cal_valid;
    zb_uint8_t hw_cal_command;
    zb_uint8_t hw_cal_phase;
};

static struct blind_device_context blind_ctx;
/* Zigbee strings use a one-byte LENGTH followed by UTF-8 ASCII bytes. */
static zb_uint8_t blind_manufacturer[] = {8, 'L','o','c','a','l','D','I','Y'};
static zb_uint8_t blind_model[] = {11, 'P','3','-','N','R','F','5','2','8','4','0'};
static atomic_t zigbee_started;
static atomic_t p3_update_pending;

/* Software travel endpoints in raw DP3 units, NOT hardware limit programming.
 * mask bit 0 = bottom, bit 1 = top. Minimum span 5%.
 * Without both valid endpoints, fall back to proven build 35 mapping.
 */
struct p3_calibration { zb_uint8_t bottom, top, mask, magic; };
static struct p3_calibration p3_cal;
#define P3_CAL_MAGIC 0x36U
void p3_zcl_schedule_sync(void);
static zb_uint8_t p3_hw_cal_phase;
static atomic_t p3_cal_dirty;
static void p3_cal_save_work_fn(struct k_work *work);
K_WORK_DEFINE(p3_cal_save_work, p3_cal_save_work_fn);

static bool p3_cal_complete(void)
{
    return (p3_cal.mask & 3U) == 3U &&
        ((int)p3_cal.bottom >= (int)p3_cal.top + 5 ||
         (int)p3_cal.top >= (int)p3_cal.bottom + 5);
}

static zb_uint8_t p3_raw_to_zcl(zb_uint8_t raw)
{
    int out;
    if (!p3_cal_complete()) {
        return IS_ENABLED(CONFIG_P3_ZCL_POSITION_INVERT) ? 100U - raw : raw;
    }
    out = ((int)raw - (int)p3_cal.top) * 100 /
        ((int)p3_cal.bottom - (int)p3_cal.top);
    return (zb_uint8_t)CLAMP(out, 0, 100);
}

static zb_uint8_t p3_zcl_to_raw(zb_uint8_t target)
{
    int raw;
    if (!p3_cal_complete()) {
        return IS_ENABLED(CONFIG_P3_ZCL_POSITION_INVERT) ? 100U - target : target;
    }
    raw = (int)p3_cal.top +
        ((int)p3_cal.bottom - (int)p3_cal.top) * (int)target / 100;
    return (zb_uint8_t)CLAMP(raw, 0, 100);
}

/* Do not block the Zigbee callback on flash I/O. */
static void p3_cal_save_work_fn(struct k_work *work)
{
    struct p3_calibration copy;
    int err;
    ARG_UNUSED(work);
    if (!atomic_set(&p3_cal_dirty, 0)) return;
    copy = p3_cal;
    copy.magic = P3_CAL_MAGIC;
    err = settings_save_one("p3/calibration", &copy, sizeof(copy));
    if (err != 0) LOG_ERR("P3 calibration save error %d", err);
}

static void p3_cal_load(void)
{
    struct p3_calibration saved = {0};
    ssize_t n = settings_load_one("p3/calibration", &saved, sizeof(saved));
    if (n == sizeof(saved) && saved.magic == P3_CAL_MAGIC &&
        saved.bottom <= 100U && saved.top <= 100U &&
        !(saved.mask & ~3U)) {
        p3_cal = saved;
    }
}

static bool p3_cal_apply_command(zb_uint8_t command)
{
    struct tuya_motor_state state;
    struct p3_calibration proposed = p3_cal;
    if (command == 3U) {
        memset(&proposed, 0, sizeof(proposed));
    } else if (command == 1U || command == 2U) {
        tuya_uart_get_state(&state);
        if (!state.position_valid || state.position > 100U) return false;
        if (command == 1U) {
            proposed.bottom = state.position;
            proposed.mask |= 1U;
        } else {
            proposed.top = state.position;
            proposed.mask |= 2U;
        }
        if ((proposed.mask & 3U) == 3U &&
            ((int)proposed.bottom < (int)proposed.top + 5 &&
             (int)proposed.top < (int)proposed.bottom + 5)) return false;
    } else return false;
    p3_cal = proposed;
    atomic_set(&p3_cal_dirty, 1);
    k_work_submit(&p3_cal_save_work);
    p3_zcl_schedule_sync();
    return true;
}


/* The programmable BLUE status LED is active-low at nRF52840 P0.15
 * on nice!nano-compatible boards. Do not touch charger LED hardware.
 * Zigbee joined/rejoined => LED off, unjoined => LED on.
 */
static const struct gpio_dt_spec p3_status_led =
    GPIO_DT_SPEC_GET(DT_ALIAS(status_led), gpios);
static bool p3_status_led_ready;

static void set_join_status_led(bool joined)
{
    if (p3_status_led_ready) {
        /* GPIO logical 0 means physically HIGH => LED OFF. */
        int err = gpio_pin_set_dt(&p3_status_led, joined ? 0 : 1);
        if (err != 0) {
            LOG_WRN("P3 status LED write failed: %d", err);
        }
    }
}

ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(basic_attr_list, ZB_ZCL_BASIC)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, &blind_ctx.basic_attr.zcl_version)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, &blind_ctx.basic_attr.power_source)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, blind_manufacturer)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, blind_model)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_RX_BYTES, &blind_ctx.diag_rx_bytes,
    ZB_ZCL_ATTR_TYPE_U32, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_VALID_FRAMES, &blind_ctx.diag_valid_frames,
    ZB_ZCL_ATTR_TYPE_U32, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_REJECTED, &blind_ctx.diag_rejected_frames,
    ZB_ZCL_ATTR_TYPE_U32, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_DROPPED, &blind_ctx.diag_dropped_bytes,
    ZB_ZCL_ATTR_TYPE_U32, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_REPORTS, &blind_ctx.diag_accepted_reports,
    ZB_ZCL_ATTR_TYPE_U32, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_LAST_COMMAND, &blind_ctx.diag_last_command,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_LAST_FORMAT, &blind_ctx.diag_last_format,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_DP1, &blind_ctx.diag_dp1_raw,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_DP3, &blind_ctx.diag_dp3_raw,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_DP13, &blind_ctx.diag_dp13_raw,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_RX_READY, &blind_ctx.diag_rx_ready,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_STARTUP_SENT, &blind_ctx.diag_startup_requests_sent,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_STARTUP_DONE, &blind_ctx.diag_startup_finished,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_CAL_COMMAND, &blind_ctx.cal_command,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_WRITE_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_CAL_BOTTOM, &blind_ctx.cal_bottom,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_CAL_TOP, &blind_ctx.cal_top,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_CAL_VALID, &blind_ctx.cal_valid,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_HW_CAL_COMMAND, &blind_ctx.hw_cal_command,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_WRITE_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_HW_CAL_PHASE, &blind_ctx.hw_cal_phase,
    ZB_ZCL_ATTR_TYPE_U8, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_TX_FRAMES, &blind_ctx.diag_tx_frames,
    ZB_ZCL_ATTR_TYPE_U32, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_QUEUE_TO_TX_MS, &blind_ctx.diag_queue_to_tx_ms,
    ZB_ZCL_ATTR_TYPE_U32, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_UART_TX_MS, &blind_ctx.diag_uart_tx_ms,
    ZB_ZCL_ATTR_TYPE_U32, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_DIAG_RX_AFTER_TX_MS, &blind_ctx.diag_rx_after_tx_ms,
    ZB_ZCL_ATTR_TYPE_U32, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_ZB_COMMANDS, &blind_ctx.diag_zigbee_commands,
    ZB_ZCL_ATTR_TYPE_U32, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(P3_ZB_CALLBACK_TO_TX_MS, &blind_ctx.diag_zigbee_to_tx_ms,
    ZB_ZCL_ATTR_TYPE_U32, ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;

ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(
    identify_attr_list,
    &blind_ctx.identify_attr.identify_time);

ZB_ZCL_DECLARE_GROUPS_ATTRIB_LIST(
    groups_attr_list,
    &blind_ctx.groups_attr.name_support);

ZB_ZCL_DECLARE_SCENES_ATTRIB_LIST(
    scenes_attr_list,
    &blind_ctx.scenes_attr.scene_count,
    &blind_ctx.scenes_attr.current_scene,
    &blind_ctx.scenes_attr.current_group,
    &blind_ctx.scenes_attr.scene_valid,
    &blind_ctx.scenes_attr.name_support);

/* Build 36: advertise CURRENT_POSITION_LIFT_PERCENTAGE as reportable.
 * The SDK's generic ZB_ZCL_SET_ATTR_DESC hides the reporting-access flag;
 * explicit access enables coordinator ConfigureReporting -> change reports.
 */
ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(covering_attr_list, ZB_ZCL_WINDOW_COVERING)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_WINDOW_COVERING_WINDOW_COVERING_TYPE_ID,
    &blind_ctx.covering_type)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_WINDOW_COVERING_CONFIG_STATUS_ID,
    &blind_ctx.covering_config_status)
ZB_ZCL_SET_ATTR_DESC_M(
    ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
    &blind_ctx.position_lift_pct, ZB_ZCL_ATTR_TYPE_U8,
    ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_TILT_PERCENTAGE_ID,
    &blind_ctx.position_tilt_pct)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_WINDOW_COVERING_INSTALLED_OPEN_LIMIT_LIFT_ID,
    &blind_ctx.open_limit_lift)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_WINDOW_COVERING_INSTALLED_CLOSED_LIMIT_LIFT_ID,
    &blind_ctx.closed_limit_lift)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_WINDOW_COVERING_INSTALLED_OPEN_LIMIT_TILT_ID,
    &blind_ctx.open_limit_tilt)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_WINDOW_COVERING_INSTALLED_CLOSED_LIMIT_TILT_ID,
    &blind_ctx.closed_limit_tilt)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_WINDOW_COVERING_MODE_ID,
    &blind_ctx.covering_mode)
ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;

/* Read-only BatteryVoltage is deliberately unavailable (0xff). A voltage
 * was measured with a multimeter once, but that is not an ADC sensor.
 * BatteryRemainingPercentage stays unknown until separately enabled.
 */
ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(power_config_attr_list,
                                                    ZB_ZCL_POWER_CONFIG)
ZB_ZCL_SET_ATTR_DESC_M(ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
                       &blind_ctx.battery_voltage, ZB_ZCL_ATTR_TYPE_U8,
                       ZB_ZCL_ATTR_ACCESS_READ_ONLY)
ZB_ZCL_SET_ATTR_DESC_M(ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
                       &blind_ctx.battery_remaining_half, ZB_ZCL_ATTR_TYPE_U8,
                       ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING)
ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;

/* Custom six-server-cluster Window Covering endpoint: the SDK's predefined
 * HA helper has only FIVE clusters and would omit Power Configuration.
 */
zb_zcl_cluster_desc_t blind_cluster_list[] = {
    ZB_ZCL_CLUSTER_DESC(ZB_ZCL_CLUSTER_ID_IDENTIFY,
                        ZB_ZCL_ARRAY_SIZE(identify_attr_list, zb_zcl_attr_t),
                        identify_attr_list, ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ZB_ZCL_MANUF_CODE_INVALID),
    ZB_ZCL_CLUSTER_DESC(ZB_ZCL_CLUSTER_ID_BASIC,
                        ZB_ZCL_ARRAY_SIZE(basic_attr_list, zb_zcl_attr_t),
                        basic_attr_list, ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ZB_ZCL_MANUF_CODE_INVALID),
    ZB_ZCL_CLUSTER_DESC(ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
                        ZB_ZCL_ARRAY_SIZE(covering_attr_list, zb_zcl_attr_t),
                        covering_attr_list, ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ZB_ZCL_MANUF_CODE_INVALID),
    ZB_ZCL_CLUSTER_DESC(ZB_ZCL_CLUSTER_ID_SCENES,
                        ZB_ZCL_ARRAY_SIZE(scenes_attr_list, zb_zcl_attr_t),
                        scenes_attr_list, ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ZB_ZCL_MANUF_CODE_INVALID),
    ZB_ZCL_CLUSTER_DESC(ZB_ZCL_CLUSTER_ID_GROUPS,
                        ZB_ZCL_ARRAY_SIZE(groups_attr_list, zb_zcl_attr_t),
                        groups_attr_list, ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ZB_ZCL_MANUF_CODE_INVALID),
    ZB_ZCL_CLUSTER_DESC(ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                        ZB_ZCL_ARRAY_SIZE(power_config_attr_list, zb_zcl_attr_t),
                        power_config_attr_list, ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ZB_ZCL_MANUF_CODE_INVALID)
};

ZB_DECLARE_SIMPLE_DESC(6, 0);
ZB_AF_SIMPLE_DESC_TYPE(6, 0) simple_desc_blind_endpoint = {
    BLIND_ENDPOINT,
    ZB_AF_HA_PROFILE_ID,
    ZB_HA_WINDOW_COVERING_DEVICE_ID,
    ZB_HA_DEVICE_VER_WINDOW_COVERING,
    0,
    6,
    0,
    {
        ZB_ZCL_CLUSTER_ID_BASIC,
        ZB_ZCL_CLUSTER_ID_IDENTIFY,
        ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
        ZB_ZCL_CLUSTER_ID_SCENES,
        ZB_ZCL_CLUSTER_ID_GROUPS,
        ZB_ZCL_CLUSTER_ID_POWER_CONFIG
    }
};

ZBOSS_DEVICE_DECLARE_REPORTING_CTX(blind_reporting, BLIND_REPORT_RECORDS);
ZB_AF_DECLARE_ENDPOINT_DESC(
    blind_endpoint, BLIND_ENDPOINT, ZB_AF_HA_PROFILE_ID,
    0, NULL,
    ZB_ZCL_ARRAY_SIZE(blind_cluster_list, zb_zcl_cluster_desc_t),
    blind_cluster_list,
    (zb_af_simple_desc_1_1_t *)&simple_desc_blind_endpoint,
    BLIND_REPORT_RECORDS, blind_reporting,
    0, NULL);

ZBOSS_DECLARE_DEVICE_CTX_1_EP(blind_device_ctx, blind_endpoint);

static void initialize_attributes(void)
{
    blind_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
    blind_ctx.basic_attr.power_source = ZB_ZCL_BASIC_POWER_SOURCE_BATTERY;
    blind_ctx.identify_attr.identify_time =
        ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;

    blind_ctx.covering_type = ZB_ZCL_ATTR_WINDOW_COVERING_TYPE_ROLLERSHADE;
    blind_ctx.covering_config_status = 0; /* Disabled until report and TX qualified */
    blind_ctx.position_lift_pct =
        ZB_ZCL_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_DEFAULT_VALUE;
    blind_ctx.position_tilt_pct = 0xffU; /* No tilt motor */
    blind_ctx.open_limit_lift =
        ZB_ZCL_WINDOW_COVERING_INSTALLED_OPEN_LIMIT_LIFT_DEFAULT_VALUE;
    blind_ctx.closed_limit_lift =
        ZB_ZCL_WINDOW_COVERING_INSTALLED_CLOSED_LIMIT_LIFT_DEFAULT_VALUE;
    blind_ctx.open_limit_tilt =
        ZB_ZCL_WINDOW_COVERING_INSTALLED_OPEN_LIMIT_TILT_DEFAULT_VALUE;
    blind_ctx.closed_limit_tilt =
        ZB_ZCL_WINDOW_COVERING_INSTALLED_CLOSED_LIMIT_TILT_DEFAULT_VALUE;
    blind_ctx.covering_mode = 0;

    blind_ctx.battery_voltage = 0xffU;
    blind_ctx.battery_remaining_half = 0xffU;
    blind_ctx.diag_last_command = 0xffU;
    blind_ctx.diag_last_format = 0xffU;
    blind_ctx.diag_dp1_raw = 0xffU;
    blind_ctx.diag_dp3_raw = 0xffU;
    blind_ctx.diag_dp13_raw = 0xffU;
    blind_ctx.groups_attr.name_support = 0;
    blind_ctx.scenes_attr.scene_count = 0;
    blind_ctx.scenes_attr.current_scene = 0;
    blind_ctx.scenes_attr.current_group = 0;
    blind_ctx.scenes_attr.scene_valid = ZB_FALSE;
    blind_ctx.scenes_attr.name_support = 0;
}

static void apply_p3_zcl_state(zb_uint8_t dummy)
{
    struct tuya_motor_state state;
    struct tuya_uart_diagnostics diag;
    zb_uint8_t value;

    ARG_UNUSED(dummy);
    atomic_set(&p3_update_pending, 0);
    tuya_uart_get_state(&state);
    tuya_uart_get_diagnostics(&diag);

    /* Attribute storage belongs to ZBOSS; update it only in its context.
     * Every value is read-only and obtainable with standard ZCL Read.
     */
    blind_ctx.diag_rx_bytes = diag.rx_bytes;
    blind_ctx.diag_tx_frames = diag.tx_frames;
    blind_ctx.diag_zigbee_commands = diag.zigbee_commands;
    blind_ctx.diag_zigbee_to_tx_ms = diag.last_zigbee_to_tx_ms;
    blind_ctx.diag_queue_to_tx_ms = diag.last_queue_to_tx_ms;
    blind_ctx.diag_uart_tx_ms = diag.last_uart_tx_ms;
    blind_ctx.diag_rx_after_tx_ms = diag.last_rx_after_tx_ms;
    blind_ctx.diag_valid_frames = diag.valid_frames;
    blind_ctx.diag_rejected_frames = diag.rejected_frames;
    blind_ctx.diag_dropped_bytes = diag.dropped_bytes;
    blind_ctx.diag_accepted_reports = state.report_count;
    blind_ctx.diag_last_command = diag.last_command;
    blind_ctx.diag_last_format = diag.last_format;
    blind_ctx.diag_dp1_raw = state.motion_valid ? state.motion : 0xffU;
    blind_ctx.diag_dp3_raw = state.position_valid ? state.position : 0xffU;
    blind_ctx.diag_dp13_raw = state.battery_valid ? state.battery : 0xffU;
    blind_ctx.diag_rx_ready = diag.rx_ready;
    blind_ctx.diag_startup_requests_sent = diag.diag_startup_requests_sent;
    blind_ctx.diag_startup_finished = diag.diag_startup_finished;
    blind_ctx.cal_bottom = (p3_cal.mask & 1U) ? p3_cal.bottom : 0xffU;
    blind_ctx.cal_top = (p3_cal.mask & 2U) ? p3_cal.top : 0xffU;
    blind_ctx.cal_valid = p3_cal_complete() ? 1U : 0U;
    blind_ctx.hw_cal_phase = p3_hw_cal_phase;

    if (IS_ENABLED(CONFIG_P3_ZCL_POSITION_ENABLE) && state.position_valid) {
        value = p3_raw_to_zcl(state.position);
        ZB_ZCL_SET_ATTRIBUTE(BLIND_ENDPOINT, ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
            ZB_ZCL_CLUSTER_SERVER_ROLE,
            ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
            &value, ZB_FALSE);
    }

    if (IS_ENABLED(CONFIG_P3_ZCL_BATTERY_ENABLE) && state.battery_valid) {
        value = (zb_uint8_t)(state.battery * 2U);
        ZB_ZCL_SET_ATTRIBUTE(BLIND_ENDPOINT, ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
            ZB_ZCL_CLUSTER_SERVER_ROLE,
            ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
            &value, ZB_FALSE);
    }

    if (IS_ENABLED(CONFIG_P3_UART_TX_ENABLE) &&
        IS_ENABLED(CONFIG_P3_ZCL_POSITION_ENABLE) && state.position_valid) {
        value = ZB_ZCL_ATTR_WINDOW_COVERING_CONFIG_OPERATIONAL |
                ZB_ZCL_ATTR_WINDOW_COVERING_CONFIG_ONLINE;
    } else {
        value = 0;
    }
    ZB_ZCL_SET_ATTRIBUTE(BLIND_ENDPOINT, ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ZCL_ATTR_WINDOW_COVERING_CONFIG_STATUS_ID,
        &value, ZB_FALSE);
}

/* From UART RX Zephyr thread: schedule work on ZBOSS context, never call
 * zb_zcl_set_attr_val() directly from the UART thread. Coalesce updates.
 */
void p3_zcl_schedule_sync(void)
{
    if (!atomic_get(&zigbee_started)) {
        return;
    }
    if (atomic_cas(&p3_update_pending, 0, 1)) {
        if (ZB_SCHEDULE_APP_CALLBACK(apply_p3_zcl_state, 0) != RET_OK) {
            atomic_set(&p3_update_pending, 0);
            LOG_WRN("ZBOSS scheduler full; P3 report not submitted");
        }
    }
}

static bool p3_hw_cal_execute(zb_uint8_t command)
{
    if (!IS_ENABLED(CONFIG_P3_HW_CALIBRATION_ENABLE)) return false;
    if (command == 1U) {
        if (!tuya_uart_submit_hw_calibration(command)) return false;
        p3_hw_cal_phase = 1U;
    } else if (command == 2U) {
        if (p3_hw_cal_phase != 1U ||
            !tuya_uart_submit_hw_calibration(command)) return false;
        p3_hw_cal_phase = 2U;
    } else if (command == 3U) {
        if (p3_hw_cal_phase != 2U ||
            !tuya_uart_submit_hw_calibration(command)) return false;
        p3_hw_cal_phase = 0U;
    } else return false;
    p3_zcl_schedule_sync();
    return true;
}

static void covering_device_callback(zb_bufid_t bufid)
{
    zb_zcl_device_callback_param_t *param =
        ZB_BUF_GET_PARAM(bufid, zb_zcl_device_callback_param_t);
    enum tuya_motor_action action;
    bool queued = false;

    if (param->endpoint != BLIND_ENDPOINT) {
        return;
    }
    switch (param->device_cb_id) {
    case ZB_ZCL_SET_ATTR_VALUE_CB_ID:
        if (param->cb_param.set_attr_value_param.cluster_id == ZB_ZCL_CLUSTER_ID_BASIC &&
            param->cb_param.set_attr_value_param.attr_id == P3_HW_CAL_COMMAND) {
            param->status = p3_hw_cal_execute(
                param->cb_param.set_attr_value_param.values.data8) ? RET_OK : RET_ERROR;
            return;
        }
        if (param->cb_param.set_attr_value_param.cluster_id == ZB_ZCL_CLUSTER_ID_BASIC &&
            param->cb_param.set_attr_value_param.attr_id == P3_CAL_COMMAND) {
            param->status = p3_cal_apply_command(
                param->cb_param.set_attr_value_param.values.data8) ? RET_OK : RET_ERROR;
        }
        return;
    case ZB_ZCL_WINDOW_COVERING_UP_OPEN_CB_ID:
        action = TUYA_MOTOR_OPEN;
        tuya_uart_mark_zigbee_command();
        queued = tuya_uart_submit_action(action);
        break;
    case ZB_ZCL_WINDOW_COVERING_DOWN_CLOSE_CB_ID:
        action = TUYA_MOTOR_CLOSE;
        tuya_uart_mark_zigbee_command();
        queued = tuya_uart_submit_action(action);
        break;
    case ZB_ZCL_WINDOW_COVERING_STOP_CB_ID:
        action = TUYA_MOTOR_STOP;
        tuya_uart_mark_zigbee_command();
        queued = tuya_uart_submit_action(action);
        break;
    case ZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE_CB_ID: {
        /* Build 35: guarded DP2 position test on the experimental P3 only.
         * ZCL position is 0=open, 100=closed. Reverse only when configured.
         * Never set the reported position optimistically: wait for DP3 RX.
         */
        const zb_zcl_go_to_lift_percentage_req_t *req =
            ZB_ZCL_DEVICE_CMD_PARAM_IN_GET(
                bufid, zb_zcl_go_to_lift_percentage_req_t);
        zb_uint8_t percent;

        if (req == NULL || req->percentage_lift_value > 100U ||
            !IS_ENABLED(CONFIG_P3_POSITION_TARGET_ENABLE)) {
            param->status = RET_ERROR;
            return;
        }
        percent = p3_zcl_to_raw(req->percentage_lift_value);
        tuya_uart_mark_zigbee_command();
        queued = tuya_uart_submit_position(percent);
        LOG_INF("P3 position target ZCL=%u DP2=%u queued=%u",
                req->percentage_lift_value, percent, queued);
        break;
    }
    case ZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE_CB_ID:
        param->status = RET_ERROR; /* P3 is a lift-only blind */
        return;
    default:
        return;
    }

    /* Success means queued into UART, NOT motor movement completed.
     * If disabled, not initialized, or full queue: explicit error.
     */
    param->status = queued ? RET_OK : RET_ERROR;
    if (!queued) {
        LOG_WRN("Window Covering action rejected (motor TX disabled/not ready)");
    }
}

void zboss_signal_handler(zb_bufid_t bufid)
{
    zb_zdo_app_signal_hdr_t *signal_hdr = NULL;
    zb_zdo_app_signal_type_t signal = zb_get_app_signal(bufid, &signal_hdr);
    zb_ret_t signal_status = ZB_GET_APP_SIGNAL_STATUS(bufid);

    /* Keep Nordic's commissioning/rejoin handler intact. */
    ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));

    if (signal == ZB_BDB_SIGNAL_DEVICE_REBOOT ||
        signal == ZB_BDB_SIGNAL_STEERING) {
        if (signal_status == RET_OK) {
            set_join_status_led(true);  /* Joined/rejoined: blue LED OFF */
        } else {
            set_join_status_led(false); /* Not joined: show status */
        }
    } else if (signal == ZB_ZDO_SIGNAL_LEAVE &&
               signal_status == RET_OK) {
        set_join_status_led(false);
    }

    if (bufid) {
        zb_buf_free(bufid);
    }
}

int main(void)
{
    LOG_INF("Starting P3 Zigbee Window Covering sleepy End Device");

    /* Do not interfere with USB UF2 bootloader behavior; this runs only
     * after the application starts. If the clone has no compatible LED
     * on P0.15, initialization failure leaves Zigbee operation intact.
     */
    if (gpio_is_ready_dt(&p3_status_led)) {
        int err = gpio_pin_configure_dt(&p3_status_led, GPIO_OUTPUT_ACTIVE);
        if (err == 0) {
            p3_status_led_ready = true;
            set_join_status_led(false);
        } else {
            LOG_WRN("P3 status LED setup failed: %d", err);
        }
    } else {
        LOG_WRN("P3 status LED GPIO unavailable");
    }

    {
        int settings_err = settings_subsys_init();
        if (settings_err == 0) {
            p3_cal_load();
        } else {
            LOG_ERR("P3 calibration settings unavailable: %d", settings_err);
        }
    }
    ZB_AF_REGISTER_DEVICE_CTX(&blind_device_ctx);
    initialize_attributes();
    ZB_ZCL_REGISTER_DEVICE_CB(covering_device_callback);

    zb_set_ed_timeout(ED_AGING_TIMEOUT_64MIN);
    zb_set_keepalive_timeout(ZB_MILLISECONDS_TO_BEACON_INTERVAL(30000));
    zigbee_configure_sleepy_behavior(true);

    zigbee_enable();
    atomic_set(&zigbee_started, 1);
    p3_zcl_schedule_sync();

    LOG_INF("Endpoint %d live; P3 TX=%u, position=%u, battery=%u",
            BLIND_ENDPOINT, IS_ENABLED(CONFIG_P3_UART_TX_ENABLE),
            IS_ENABLED(CONFIG_P3_ZCL_POSITION_ENABLE),
            IS_ENABLED(CONFIG_P3_ZCL_BATTERY_ENABLE));
    return 0;
}
