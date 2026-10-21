/* SPDX-License-Identifier: MIT */
#include "tuya_motor_state.h"

#include <string.h>

struct decode_ctx {
    struct tuya_motor_state next;
    struct tuya_motor_changes changes;
};

static void accept_report_dp(const struct tuya_dp *dp, void *user)
{
    struct decode_ctx *ctx = (struct decode_ctx *)user;
    uint32_t value;

    if (!tuya_dp_as_uint32(dp, &value)) {
        return;
    }

    switch (dp->id) {
    case P3_DP_MOTION:
        if (dp->type != 0x04U || dp->length != 1U || value > 2U) {
            return;
        }
        ctx->changes.motion |= (!ctx->next.motion_valid || ctx->next.motion != value);
        ctx->next.motion = (uint8_t)value;
        ctx->next.motion_valid = true;
        break;
    case P3_DP_POSITION:
        if (dp->type != 0x02U || dp->length != 4U || value > 100U) {
            return;
        }
        ctx->changes.position |= (!ctx->next.position_valid || ctx->next.position != value);
        ctx->next.position = (uint8_t)value;
        ctx->next.position_valid = true;
        break;
    case P3_DP_BATTERY:
        if (dp->type != 0x02U || dp->length != 4U || value > 100U) {
            return;
        }
        ctx->changes.battery |= (!ctx->next.battery_valid || ctx->next.battery != value);
        ctx->next.battery = (uint8_t)value;
        ctx->next.battery_valid = true;
        break;
    default:
        /* Other motor DPs are deliberately not mapped to Zigbee. */
        break;
    }
}

bool tuya_motor_apply_report(const struct tuya_frame *frame,
                             struct tuya_motor_state *state,
                             struct tuya_motor_changes *changes)
{
    struct decode_ctx ctx;

    if (frame == NULL || state == NULL || changes == NULL ||
        frame->format != TUYA_WIRE_CLASSIC ||
        (frame->version != 0x00U && frame->version != 0x03U) ||
        frame->command != 0x07U || frame->length == 0U) {
        return false;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.next = *state;

    /* tuya_foreach_report_dp() validates ALL entries before callbacks. */
    if (!tuya_foreach_report_dp(frame, accept_report_dp, &ctx)) {
        return false;
    }

    ctx.next.report_count++;
    *state = ctx.next;
    *changes = ctx.changes;
    return true;
}
