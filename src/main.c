/*
 * SPDX-License-Identifier: MIT
 *
 * P3 Zigbee Window Covering prototype — sleepy End Device.
 * UART motor TX and ZCL interpretation of DP3/DP13 are compile-time
 * DISABLED by default. Bootloader/SoftDevice map not yet verified.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

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
};

static struct blind_device_context blind_ctx;
/* Zigbee strings use a one-byte LENGTH followed by UTF-8 ASCII bytes. */
static zb_uint8_t blind_manufacturer[] = {8, 'L','o','c','a','l','D','I','Y'};
static zb_uint8_t blind_model[] = {11, 'P','3','-','N','R','F','5','2','8','4','0'};
static atomic_t zigbee_started;
static atomic_t p3_update_pending;

ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(basic_attr_list, ZB_ZCL_BASIC)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, &blind_ctx.basic_attr.zcl_version)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, &blind_ctx.basic_attr.power_source)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, blind_manufacturer)
ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, blind_model)
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

ZB_ZCL_DECLARE_WINDOW_COVERING_CLUSTER_ATTRIB_LIST(
    covering_attr_list,
    &blind_ctx.covering_type,
    &blind_ctx.covering_config_status,
    &blind_ctx.position_lift_pct,
    &blind_ctx.position_tilt_pct,
    &blind_ctx.open_limit_lift,
    &blind_ctx.closed_limit_lift,
    &blind_ctx.open_limit_tilt,
    &blind_ctx.closed_limit_tilt,
    &blind_ctx.covering_mode);

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
    zb_uint8_t value;

    ARG_UNUSED(dummy);
    atomic_set(&p3_update_pending, 0);
    tuya_uart_get_state(&state);

    if (IS_ENABLED(CONFIG_P3_ZCL_POSITION_ENABLE) && state.position_valid) {
        value = IS_ENABLED(CONFIG_P3_ZCL_POSITION_INVERT) ?
                (zb_uint8_t)(100U - state.position) : state.position;
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
    case ZB_ZCL_WINDOW_COVERING_UP_OPEN_CB_ID:
        action = TUYA_MOTOR_OPEN;
        queued = tuya_uart_submit_action(action);
        break;
    case ZB_ZCL_WINDOW_COVERING_DOWN_CLOSE_CB_ID:
        action = TUYA_MOTOR_CLOSE;
        queued = tuya_uart_submit_action(action);
        break;
    case ZB_ZCL_WINDOW_COVERING_STOP_CB_ID:
        action = TUYA_MOTOR_STOP;
        queued = tuya_uart_submit_action(action);
        break;
    case ZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE_CB_ID:
    {
        const zb_zcl_go_to_lift_percentage_req_t *req =
            ZB_ZCL_DEVICE_CMD_PARAM_IN_GET(bufid,
                                           zb_zcl_go_to_lift_percentage_req_t);
        if (req != NULL && req->percentage_lift_value <= 100U) {
            uint8_t target = req->percentage_lift_value;
            if (IS_ENABLED(CONFIG_P3_ZCL_POSITION_INVERT)) {
                target = (uint8_t)(100U - target);
            }
            queued = tuya_uart_submit_position(target);
        }
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
    ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
    if (bufid) {
        zb_buf_free(bufid);
    }
}

int main(void)
{
    LOG_INF("Starting P3 Zigbee Window Covering sleepy End Device");
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
