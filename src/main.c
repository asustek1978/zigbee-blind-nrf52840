/*
 * SPDX-License-Identifier: MIT
 *
 * Zigbee Window Covering End Device skeleton for nice!nano nRF52840.
 * IMPORTANT: P3 motor UART is not yet implemented.
 * The Window Covering cluster reports an unknown position and rejects motion
 * commands; no motion is executed or falsely acknowledged.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zboss_api.h>
#include <zboss_api_zcl.h>
#include <zboss_api_addons.h>
#include <zb_nrf_platform.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>

#define BLIND_ENDPOINT 10

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
};

static struct blind_device_context blind_ctx;

ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST(
    basic_attr_list,
    &blind_ctx.basic_attr.zcl_version,
    &blind_ctx.basic_attr.power_source);

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

ZB_HA_DECLARE_WINDOW_COVERING_CLUSTER_LIST(
    blind_cluster_list,
    basic_attr_list,
    identify_attr_list,
    groups_attr_list,
    scenes_attr_list,
    covering_attr_list);

ZB_HA_DECLARE_WINDOW_COVERING_EP(
    blind_endpoint,
    BLIND_ENDPOINT,
    blind_cluster_list);

ZBOSS_DECLARE_DEVICE_CTX_1_EP(blind_device_ctx, blind_endpoint);

static void initialize_attributes(void)
{
    blind_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
    blind_ctx.basic_attr.power_source = ZB_ZCL_BASIC_POWER_SOURCE_BATTERY;
    blind_ctx.identify_attr.identify_time =
        ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;

    /* P3 motor reports have not yet been integrated. */
    blind_ctx.covering_type = ZB_ZCL_ATTR_WINDOW_COVERING_TYPE_ROLLERSHADE;
    blind_ctx.covering_config_status = 0; /* Not operational until UART works */
    blind_ctx.position_lift_pct =
        ZB_ZCL_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_DEFAULT_VALUE;
    blind_ctx.position_tilt_pct =
        ZB_ZCL_WINDOW_COVERING_CURRENT_POSITION_TILT_PERCENTAGE_DEFAULT_VALUE;
    blind_ctx.open_limit_lift =
        ZB_ZCL_WINDOW_COVERING_INSTALLED_OPEN_LIMIT_LIFT_DEFAULT_VALUE;
    blind_ctx.closed_limit_lift =
        ZB_ZCL_WINDOW_COVERING_INSTALLED_CLOSED_LIMIT_LIFT_DEFAULT_VALUE;
    blind_ctx.open_limit_tilt =
        ZB_ZCL_WINDOW_COVERING_INSTALLED_OPEN_LIMIT_TILT_DEFAULT_VALUE;
    blind_ctx.closed_limit_tilt =
        ZB_ZCL_WINDOW_COVERING_INSTALLED_CLOSED_LIMIT_TILT_DEFAULT_VALUE;
    blind_ctx.covering_mode = 0; /* No unverified calibration state */

    /* No scenes, no grouping by default. */
    blind_ctx.groups_attr.name_support = 0;
    blind_ctx.scenes_attr.scene_count = 0;
    blind_ctx.scenes_attr.current_scene = 0;
    blind_ctx.scenes_attr.current_group = 0;
    blind_ctx.scenes_attr.scene_valid = ZB_FALSE;
    blind_ctx.scenes_attr.name_support = 0;
}

/* Never acknowledge motion as executed before the P3 bridge is functional. */
static void covering_device_callback(zb_bufid_t bufid)
{
    zb_zcl_device_callback_param_t *param =
        ZB_BUF_GET_PARAM(bufid, zb_zcl_device_callback_param_t);

    if (param->endpoint != BLIND_ENDPOINT) {
        return;
    }

    switch (param->device_cb_id) {
    case ZB_ZCL_WINDOW_COVERING_UP_OPEN_CB_ID:
    case ZB_ZCL_WINDOW_COVERING_DOWN_CLOSE_CB_ID:
    case ZB_ZCL_WINDOW_COVERING_STOP_CB_ID:
    case ZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE_CB_ID:
    case ZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE_CB_ID:
        /* No motor control until Tuya P3 UART support is implemented. */
        param->status = RET_ERROR;
        LOG_WRN("Window Covering command rejected: motor UART is not implemented");
        break;

    default:
        break;
    }
}

void zboss_signal_handler(zb_bufid_t bufid)
{
    /* The Nordic helper manages commissioning and rejoins. */
    ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));

    if (bufid) {
        zb_buf_free(bufid);
    }
}

int main(void)
{
    LOG_INF("Starting Zigbee window covering End Device");

    ZB_AF_REGISTER_DEVICE_CTX(&blind_device_ctx);
    initialize_attributes();

    ZB_ZCL_REGISTER_DEVICE_CB(covering_device_callback);

    /* Sleepy End Device, not a router. Retain pairing state across reboots. */
    zb_set_ed_timeout(ED_AGING_TIMEOUT_64MIN);
    zb_set_keepalive_timeout(ZB_MILLISECONDS_TO_BEACON_INTERVAL(30000));
    zigbee_configure_sleepy_behavior(true);

    zigbee_enable();

    LOG_INF("Window Covering endpoint %d registered (motor UART pending)",
            BLIND_ENDPOINT);

    return 0;
}
