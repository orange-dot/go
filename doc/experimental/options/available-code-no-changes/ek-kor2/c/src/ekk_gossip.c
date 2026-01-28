/**
 * @file ekk_gossip.c
 * @brief EK-KOR v2 - Event Gossip Protocol Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implementation of epidemic gossip protocol for event synchronization.
 */

#include "ekk/ekk_gossip.h"
#include "ekk/ekk_hal.h"
#include <string.h>

/* ============================================================================
 * VERSION VECTOR IMPLEMENTATION
 * ============================================================================ */

void ekk_vv_init(ekk_version_vector_t *vv) {
    if (!vv) return;
    memset(vv, 0, sizeof(*vv));
}

uint32_t ekk_vv_get(const ekk_version_vector_t *vv, ekk_module_id_t module_id) {
    if (!vv) return 0;

    for (uint8_t i = 0; i < vv->count; i++) {
        if (vv->entries[i].module_id == module_id) {
            return vv->entries[i].sequence;
        }
    }
    return 0;
}

ekk_error_t ekk_vv_set(ekk_version_vector_t *vv, ekk_module_id_t module_id, uint32_t seq) {
    if (!vv) return EKK_ERR_INVALID_ARG;

    /* Find existing entry */
    for (uint8_t i = 0; i < vv->count; i++) {
        if (vv->entries[i].module_id == module_id) {
            vv->entries[i].sequence = seq;
            return EKK_OK;
        }
    }

    /* Add new entry */
    if (vv->count >= EKK_K_NEIGHBORS + 1) {
        return EKK_ERR_NO_MEMORY;
    }

    vv->entries[vv->count].module_id = module_id;
    vv->entries[vv->count].sequence = seq;
    vv->count++;

    return EKK_OK;
}

uint32_t ekk_vv_increment(ekk_version_vector_t *vv, ekk_module_id_t module_id) {
    if (!vv) return 0;

    uint32_t current = ekk_vv_get(vv, module_id);
    uint32_t next = current + 1;
    ekk_vv_set(vv, module_id, next);
    return next;
}

ekk_vv_order_t ekk_vv_compare(const ekk_version_vector_t *a, const ekk_version_vector_t *b) {
    if (!a || !b) return EKK_VV_CONCURRENT;

    bool a_dominates = true;   /* a >= b for all entries */
    bool b_dominates = true;   /* b >= a for all entries */

    /* Check all entries from a */
    for (uint8_t i = 0; i < a->count; i++) {
        uint32_t a_seq = a->entries[i].sequence;
        uint32_t b_seq = ekk_vv_get(b, a->entries[i].module_id);

        if (a_seq < b_seq) a_dominates = false;
        if (a_seq > b_seq) b_dominates = false;
    }

    /* Check entries in b not in a */
    for (uint8_t i = 0; i < b->count; i++) {
        uint32_t b_seq = b->entries[i].sequence;
        uint32_t a_seq = ekk_vv_get(a, b->entries[i].module_id);

        if (a_seq < b_seq) a_dominates = false;
        if (a_seq > b_seq) b_dominates = false;
    }

    if (a_dominates && b_dominates) return EKK_VV_EQUAL;
    if (a_dominates) return EKK_VV_AFTER;   /* a happened after b */
    if (b_dominates) return EKK_VV_BEFORE;  /* a happened before b */
    return EKK_VV_CONCURRENT;
}

void ekk_vv_merge(ekk_version_vector_t *local, const ekk_version_vector_t *remote) {
    if (!local || !remote) return;

    for (uint8_t i = 0; i < remote->count; i++) {
        ekk_module_id_t mod = remote->entries[i].module_id;
        uint32_t remote_seq = remote->entries[i].sequence;
        uint32_t local_seq = ekk_vv_get(local, mod);

        if (remote_seq > local_seq) {
            ekk_vv_set(local, mod, remote_seq);
        }
    }
}

void ekk_vv_to_summary(const ekk_version_vector_t *vv, ekk_vv_summary_t *summary,
                       const ekk_module_id_t *neighbor_ids, uint8_t count) {
    if (!vv || !summary || !neighbor_ids) return;

    memset(summary, 0, sizeof(*summary));

    for (uint8_t i = 0; i < count && i < EKK_K_NEIGHBORS; i++) {
        uint32_t seq = ekk_vv_get(vv, neighbor_ids[i]);
        summary->seqs[i] = (uint8_t)(seq & 0xFF);  /* Truncate to 8 bits */
    }
}

void ekk_vv_from_summary(ekk_version_vector_t *vv, const ekk_vv_summary_t *summary,
                         const ekk_module_id_t *neighbor_ids, uint8_t count) {
    if (!vv || !summary || !neighbor_ids) return;

    for (uint8_t i = 0; i < count && i < EKK_K_NEIGHBORS; i++) {
        if (summary->seqs[i] > 0) {
            ekk_vv_set(vv, neighbor_ids[i], summary->seqs[i]);
        }
    }
}

/* ============================================================================
 * LWW IMPLEMENTATION
 * ============================================================================ */

bool ekk_lww_is_newer(ekk_lww_timestamp_t a, ekk_lww_timestamp_t b) {
    if (a.timestamp_us != b.timestamp_us) {
        return a.timestamp_us > b.timestamp_us;
    }
    /* Tiebreaker: higher origin_id wins */
    return a.origin_id > b.origin_id;
}

ekk_lww_timestamp_t ekk_lww_from_event(const ekk_event_v2_t *event) {
    ekk_lww_timestamp_t ts = {0};
    if (event) {
        ts.timestamp_us = event->timestamp_us;
        ts.origin_id = event->origin_id;
    }
    return ts;
}

/* ============================================================================
 * GOSSIP CONTEXT IMPLEMENTATION
 * ============================================================================ */

ekk_error_t ekk_gossip_init(ekk_gossip_ctx_t *ctx, ekk_module_id_t my_id) {
    if (!ctx || my_id == EKK_INVALID_MODULE_ID) {
        return EKK_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->my_id = my_id;
    ctx->local_sequence = 1;

    ekk_vv_init(&ctx->vv);
    ekk_vv_set(&ctx->vv, my_id, 0);

    return EKK_OK;
}

ekk_error_t ekk_gossip_add_neighbor(ekk_gossip_ctx_t *ctx, ekk_module_id_t neighbor_id) {
    if (!ctx || neighbor_id == EKK_INVALID_MODULE_ID) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Check if already exists */
    for (uint8_t i = 0; i < ctx->neighbor_count; i++) {
        if (ctx->neighbors[i].id == neighbor_id) {
            return EKK_ERR_ALREADY_EXISTS;
        }
    }

    /* Check capacity */
    if (ctx->neighbor_count >= EKK_K_NEIGHBORS) {
        return EKK_ERR_NO_MEMORY;
    }

    /* Add neighbor */
    ekk_neighbor_gossip_t *n = &ctx->neighbors[ctx->neighbor_count];
    n->id = neighbor_id;
    n->last_seen_seq = 0;
    n->cursor = 0;
    n->last_sync = 0;
    ctx->neighbor_count++;

    return EKK_OK;
}

ekk_error_t ekk_gossip_remove_neighbor(ekk_gossip_ctx_t *ctx, ekk_module_id_t neighbor_id) {
    if (!ctx) return EKK_ERR_INVALID_ARG;

    for (uint8_t i = 0; i < ctx->neighbor_count; i++) {
        if (ctx->neighbors[i].id == neighbor_id) {
            /* Shift remaining neighbors */
            for (uint8_t j = i; j < ctx->neighbor_count - 1; j++) {
                ctx->neighbors[j] = ctx->neighbors[j + 1];
            }
            ctx->neighbor_count--;
            return EKK_OK;
        }
    }

    return EKK_ERR_NOT_FOUND;
}

ekk_error_t ekk_gossip_emit(ekk_gossip_ctx_t *ctx, uint8_t event_type,
                            const uint8_t *payload, uint8_t payload_len) {
    if (!ctx) return EKK_ERR_INVALID_ARG;
    if (payload_len > 20) return EKK_ERR_INVALID_ARG;

    /* Check buffer space */
    if (ctx->pending_count >= 8) {
        return EKK_ERR_NO_MEMORY;
    }

    /* Create event */
    ekk_event_v2_t *event = &ctx->pending_events[ctx->pending_count];
    memset(event, 0, sizeof(*event));

    event->sequence = ctx->local_sequence;
    event->timestamp_us = (uint32_t)ekk_hal_time_us();
    event->event_type = event_type;
    event->flags = 0;
    event->origin_id = ctx->my_id;
    event->hop_count = 0;
    event->origin_seq = ctx->local_sequence;

    if (payload && payload_len > 0) {
        memcpy(event->payload, payload, payload_len);
    }

    /* Update local state */
    ctx->local_sequence++;
    ctx->pending_count++;
    ekk_vv_increment(&ctx->vv, ctx->my_id);

    /* Notify application */
    ekk_gossip_on_event(event);

    /* Store locally */
    ekk_gossip_store_event(event);

    return EKK_OK;
}

/**
 * @brief Send gossip message to a neighbor
 */
static ekk_error_t send_gossip_to_neighbor(ekk_gossip_ctx_t *ctx,
                                            ekk_module_id_t neighbor_id,
                                            ekk_event_v2_t *events,
                                            uint8_t event_count) {
    if (event_count == 0 || event_count > EKK_GOSSIP_BATCH_SIZE) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Build message */
    ekk_gossip_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    msg.msg_type = EKK_MSG_EVENT_GOSSIP;
    msg.source_module = ctx->my_id;
    msg.event_count = event_count;

    /* Build neighbor ID array for VV summary */
    ekk_module_id_t neighbor_ids[EKK_K_NEIGHBORS];
    for (uint8_t i = 0; i < ctx->neighbor_count; i++) {
        neighbor_ids[i] = ctx->neighbors[i].id;
    }
    ekk_vv_to_summary(&ctx->vv, &msg.vv_summary, neighbor_ids, ctx->neighbor_count);

    /* Copy events */
    for (uint8_t i = 0; i < event_count; i++) {
        msg.events[i] = events[i];
        /* Increment hop count for forwarded events */
        if (msg.events[i].origin_id != ctx->my_id) {
            msg.events[i].hop_count++;
        }
    }

    /* Send via HAL callback */
    ctx->stats.events_sent += event_count;
    return ekk_gossip_send(neighbor_id, (const uint8_t *)&msg, sizeof(msg));
}

ekk_error_t ekk_gossip_tick(ekk_gossip_ctx_t *ctx, ekk_time_us_t now) {
    if (!ctx) return EKK_ERR_INVALID_ARG;

    /* Check tick interval */
    if (now - ctx->last_gossip_tick < EKK_GOSSIP_TICK_US) {
        return EKK_OK;
    }
    ctx->last_gossip_tick = now;

    /* Send pending events to all neighbors */
    if (ctx->pending_count > 0) {
        for (uint8_t n = 0; n < ctx->neighbor_count; n++) {
            ekk_module_id_t neighbor = ctx->neighbors[n].id;

            /* Send in batches of 2 */
            for (uint8_t i = 0; i < ctx->pending_count; i += EKK_GOSSIP_BATCH_SIZE) {
                uint8_t batch_size = ctx->pending_count - i;
                if (batch_size > EKK_GOSSIP_BATCH_SIZE) {
                    batch_size = EKK_GOSSIP_BATCH_SIZE;
                }

                send_gossip_to_neighbor(ctx, neighbor, &ctx->pending_events[i], batch_size);
            }
        }

        /* Clear pending after sending */
        ctx->pending_count = 0;
    }

    /* Check for anti-entropy sync */
    if (now - ctx->last_sync > EKK_GOSSIP_SYNC_INTERVAL_US) {
        ctx->last_sync = now;

        /* Sync with each neighbor that hasn't been synced recently */
        for (uint8_t n = 0; n < ctx->neighbor_count; n++) {
            if (now - ctx->neighbors[n].last_sync > EKK_GOSSIP_SYNC_INTERVAL_US) {
                ekk_gossip_sync(ctx, ctx->neighbors[n].id);
                ctx->neighbors[n].last_sync = now;
            }
        }
    }

    return EKK_OK;
}

ekk_error_t ekk_gossip_handle_msg(ekk_gossip_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    if (!ctx || !data || len < 1) {
        return EKK_ERR_INVALID_ARG;
    }

    uint8_t msg_type = data[0];

    switch (msg_type) {
        case EKK_MSG_EVENT_GOSSIP: {
            if (len < sizeof(ekk_gossip_msg_t)) {
                return EKK_ERR_INVALID_ARG;
            }

            const ekk_gossip_msg_t *msg = (const ekk_gossip_msg_t *)data;

            /* Process each event */
            for (uint8_t i = 0; i < msg->event_count && i < EKK_GOSSIP_BATCH_SIZE; i++) {
                ekk_gossip_handle_event(ctx, &msg->events[i], msg->source_module);
            }

            /* Merge VV summary */
            ekk_module_id_t neighbor_ids[EKK_K_NEIGHBORS];
            for (uint8_t i = 0; i < ctx->neighbor_count; i++) {
                neighbor_ids[i] = ctx->neighbors[i].id;
            }
            ekk_version_vector_t remote_vv;
            ekk_vv_init(&remote_vv);
            ekk_vv_from_summary(&remote_vv, &msg->vv_summary, neighbor_ids, ctx->neighbor_count);
            ekk_vv_merge(&ctx->vv, &remote_vv);

            break;
        }

        case EKK_MSG_EVENT_ACK: {
            if (len < sizeof(ekk_gossip_ack_t)) {
                return EKK_ERR_INVALID_ARG;
            }
            /* ACKs are informational - no action needed with at-least-once delivery */
            break;
        }

        case EKK_MSG_EVENT_REQUEST: {
            if (len < sizeof(ekk_gossip_request_t)) {
                return EKK_ERR_INVALID_ARG;
            }

            const ekk_gossip_request_t *req = (const ekk_gossip_request_t *)data;

            /* Only respond if we're the target origin */
            if (req->target_origin == ctx->my_id) {
                /* Send requested events */
                for (uint32_t seq = req->from_seq; seq <= req->to_seq; seq++) {
                    ekk_event_v2_t event;
                    if (ekk_gossip_load_event(ctx->my_id, seq, &event) == EKK_OK) {
                        send_gossip_to_neighbor(ctx, req->requester, &event, 1);
                        ctx->stats.gaps_filled++;
                    }
                }
            }
            break;
        }

        default:
            return EKK_ERR_INVALID_ARG;
    }

    return EKK_OK;
}

ekk_error_t ekk_gossip_handle_event(ekk_gossip_ctx_t *ctx, const ekk_event_v2_t *event,
                                     ekk_module_id_t sender) {
    if (!ctx || !event) {
        return EKK_ERR_INVALID_ARG;
    }

    ctx->stats.events_received++;

    /* Check for duplicate via version vector */
    uint32_t known_seq = ekk_vv_get(&ctx->vv, event->origin_id);
    if (event->origin_seq <= known_seq) {
        ctx->stats.duplicates++;
        return EKK_ERR_ALREADY_EXISTS;
    }

    /* Check for gap */
    if (event->origin_seq > known_seq + 1) {
        ctx->stats.gaps_detected++;

        /* Buffer the event */
        if (ctx->buffered_count < 4) {
            ctx->buffered_events[ctx->buffered_count++] = *event;
        }

        /* Send gap fill request */
        ekk_gossip_request_t req;
        req.msg_type = EKK_MSG_EVENT_REQUEST;
        req.requester = ctx->my_id;
        req.target_origin = event->origin_id;
        req._reserved = 0;
        req.from_seq = known_seq + 1;
        req.to_seq = event->origin_seq - 1;

        ekk_gossip_send(sender, (const uint8_t *)&req, sizeof(req));

        return EKK_ERR_NOT_FOUND;  /* Gap detected */
    }

    /* Check hop limit */
    if (event->hop_count >= EKK_GOSSIP_MAX_HOPS) {
        ctx->stats.ttl_expired++;
        /* Still store locally, just don't forward */
    }

    /* Update version vector */
    ekk_vv_set(&ctx->vv, event->origin_id, event->origin_seq);

    /* Store event */
    ekk_gossip_store_event(event);

    /* Notify application */
    ekk_gossip_on_event(event);

    /* Forward to neighbors (if not at hop limit) */
    if (event->hop_count < EKK_GOSSIP_MAX_HOPS) {
        ekk_event_v2_t forward_event = *event;
        forward_event.hop_count++;

        for (uint8_t n = 0; n < ctx->neighbor_count; n++) {
            /* Don't send back to sender */
            if (ctx->neighbors[n].id != sender) {
                send_gossip_to_neighbor(ctx, ctx->neighbors[n].id, &forward_event, 1);
            }
        }
    }

    /* Check if any buffered events can now be processed */
    for (uint8_t i = 0; i < ctx->buffered_count; ) {
        ekk_event_v2_t *buffered = &ctx->buffered_events[i];
        uint32_t expected = ekk_vv_get(&ctx->vv, buffered->origin_id) + 1;

        if (buffered->origin_seq == expected) {
            /* Can now process this buffered event */
            ekk_vv_set(&ctx->vv, buffered->origin_id, buffered->origin_seq);
            ekk_gossip_store_event(buffered);
            ekk_gossip_on_event(buffered);

            /* Remove from buffer */
            for (uint8_t j = i; j < ctx->buffered_count - 1; j++) {
                ctx->buffered_events[j] = ctx->buffered_events[j + 1];
            }
            ctx->buffered_count--;
            /* Don't increment i - check same position again */
        } else {
            i++;
        }
    }

    return EKK_OK;
}

ekk_error_t ekk_gossip_sync(ekk_gossip_ctx_t *ctx, ekk_module_id_t neighbor_id) {
    if (!ctx) return EKK_ERR_INVALID_ARG;

    /* Send our full version vector for anti-entropy sync */
    /* In a full implementation, this would trigger bidirectional sync */

    EKK_UNUSED(neighbor_id);

    return EKK_OK;
}

void ekk_gossip_get_stats(const ekk_gossip_ctx_t *ctx, ekk_gossip_stats_t *stats) {
    if (!ctx || !stats) return;
    *stats = ctx->stats;
}

void ekk_gossip_reset_stats(ekk_gossip_ctx_t *ctx) {
    if (!ctx) return;
    memset(&ctx->stats, 0, sizeof(ctx->stats));
}

/* ============================================================================
 * WEAK CALLBACKS (override in application)
 * ============================================================================ */

EKK_WEAK ekk_error_t ekk_gossip_send(ekk_module_id_t dest, const uint8_t *data, uint32_t len) {
    EKK_UNUSED(dest);
    EKK_UNUSED(data);
    EKK_UNUSED(len);
    return EKK_OK;
}

EKK_WEAK void ekk_gossip_on_event(const ekk_event_v2_t *event) {
    EKK_UNUSED(event);
}

EKK_WEAK ekk_error_t ekk_gossip_store_event(const ekk_event_v2_t *event) {
    EKK_UNUSED(event);
    return EKK_OK;
}

EKK_WEAK ekk_error_t ekk_gossip_load_event(ekk_module_id_t origin, uint32_t seq,
                                            ekk_event_v2_t *event) {
    EKK_UNUSED(origin);
    EKK_UNUSED(seq);
    EKK_UNUSED(event);
    return EKK_ERR_NOT_FOUND;
}
