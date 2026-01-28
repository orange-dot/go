/**
 * @file ekk_gateway.c
 * @brief EK-KOR v2 - Gateway Event Store Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Gateway event store core implementation.
 */

#include "ekk/ekk_gateway.h"
#include "ekk/ekk_hal.h"
#include <string.h>

/* ============================================================================
 * CRC32 TABLE (IEEE 802.3 polynomial)
 * ============================================================================ */

static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

/**
 * @brief Compute CRC32
 */
static uint32_t crc32_compute(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/* ============================================================================
 * FLASH ENTRY CRC
 * ============================================================================ */

uint32_t ekk_flash_entry_crc(const ekk_flash_entry_t *entry) {
    if (!entry) return 0;

    /* CRC everything except the CRC field itself */
    uint32_t crc = 0xFFFFFFFF;

    /* CRC the global_seq (4 bytes) */
    const uint8_t *p = (const uint8_t *)&entry->global_seq;
    for (int i = 0; i < 4; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }

    /* Skip CRC field, continue from length */
    p = (const uint8_t *)&entry->length;
    for (uint32_t i = 0; i < sizeof(ekk_flash_entry_t) - 8; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

bool ekk_flash_entry_verify(const ekk_flash_entry_t *entry) {
    if (!entry) return false;
    return entry->crc32 == ekk_flash_entry_crc(entry);
}

/* ============================================================================
 * PROJECTION HELPERS
 * ============================================================================ */

/**
 * @brief Find projection by module ID
 */
static ekk_module_projection_t *find_projection(ekk_gateway_t *gw,
                                                  ekk_module_id_t module_id) {
    for (uint8_t i = 0; i < gw->projection_count; i++) {
        if (gw->projections[i].module_id == module_id) {
            return &gw->projections[i];
        }
    }
    return NULL;
}

/**
 * @brief Create new projection
 */
static ekk_module_projection_t *create_projection(ekk_gateway_t *gw,
                                                    ekk_module_id_t module_id) {
    if (gw->projection_count >= EKK_GATEWAY_MAX_MODULES) {
        return NULL;
    }

    ekk_module_projection_t *proj = &gw->projections[gw->projection_count];
    memset(proj, 0, sizeof(*proj));
    proj->module_id = module_id;
    proj->state = EKK_MODULE_INIT;
    gw->projection_count++;

    return proj;
}

/**
 * @brief Update projection from event
 */
static void update_projection(ekk_gateway_t *gw, const ekk_event_v2_t *event) {
    ekk_module_projection_t *proj = find_projection(gw, event->origin_id);
    if (!proj) {
        proj = create_projection(gw, event->origin_id);
        if (!proj) return;
    }

    proj->last_seen = event->timestamp_us;
    proj->event_count++;

    /* Update based on event type */
    switch (event->event_type) {
        case EKK_EVENT_STATE_TRANSITION:
            if (event->payload[0] < EKK_MODULE_SHUTDOWN) {
                proj->state = (ekk_module_state_t)event->payload[0];
            }
            break;

        case EKK_EVENT_FIELD_PUBLISHED: {
            /* Payload format: [field_id, value_hi, value_lo, ...] */
            uint8_t field_id = event->payload[0];
            ekk_fixed_t value = (ekk_fixed_t)(
                (event->payload[1] << 24) |
                (event->payload[2] << 16) |
                (event->payload[3] << 8) |
                event->payload[4]
            );

            switch (field_id) {
                case EKK_FIELD_LOAD:    proj->load = value; break;
                case EKK_FIELD_THERMAL: proj->thermal = value; break;
                case EKK_FIELD_POWER:   proj->power = value; break;
                default: break;
            }
            break;
        }

        case EKK_EVENT_NEIGHBOR_JOINED:
            if (proj->neighbor_count < 255) {
                proj->neighbor_count++;
            }
            break;

        case EKK_EVENT_NEIGHBOR_LEFT:
            if (proj->neighbor_count > 0) {
                proj->neighbor_count--;
            }
            break;

        default:
            break;
    }
}

/* ============================================================================
 * GATEWAY IMPLEMENTATION
 * ============================================================================ */

ekk_error_t ekk_gateway_init(ekk_gateway_t *gw, ekk_module_id_t gateway_id,
                              uint32_t flash_base, ekk_index_entry_t *index_buffer,
                              uint32_t index_capacity) {
    if (!gw || !index_buffer || index_capacity == 0) {
        return EKK_ERR_INVALID_ARG;
    }

    memset(gw, 0, sizeof(*gw));
    gw->gateway_id = gateway_id;

    /* Initialize flash log */
    ekk_error_t err = ekk_flash_log_init(&gw->flash_log, flash_base,
                                          EKK_FLASH_LOG_CAPACITY);
    if (err != EKK_OK) {
        return err;
    }

    /* Initialize index */
    err = ekk_stream_index_init(&gw->index, index_buffer, index_capacity);
    if (err != EKK_OK) {
        return err;
    }

    /* Initialize CAN interfaces */
    for (int i = 0; i < EKK_CAN_INTERFACES; i++) {
        gw->can[i].index = (uint8_t)i;
        gw->can[i].enabled = false;
    }

    gw->initialized = true;
    return EKK_OK;
}

ekk_error_t ekk_gateway_process_can(ekk_gateway_t *gw, uint8_t can_idx,
                                     const uint8_t *data, uint32_t len) {
    if (!gw || !data || can_idx >= EKK_CAN_INTERFACES) {
        return EKK_ERR_INVALID_ARG;
    }

    gw->can[can_idx].rx_count++;
    gw->can[can_idx].last_activity = ekk_hal_time_us();
    gw->events_received++;

    /* Check for gossip message */
    if (len >= 1 && data[0] == EKK_MSG_EVENT_GOSSIP) {
        if (len >= sizeof(ekk_gossip_msg_t)) {
            const ekk_gossip_msg_t *msg = (const ekk_gossip_msg_t *)data;

            /* Process each event */
            for (uint8_t i = 0; i < msg->event_count && i < EKK_GOSSIP_BATCH_SIZE; i++) {
                ekk_gateway_append(gw, msg->source_module, &msg->events[i]);
            }
        }
    }

    return EKK_OK;
}

uint32_t ekk_gateway_append(ekk_gateway_t *gw, ekk_module_id_t module_id,
                             const ekk_event_v2_t *event) {
    if (!gw || !event) {
        return 0;
    }

    /* Create flash entry */
    ekk_flash_entry_t entry;
    memset(&entry, 0, sizeof(entry));

    entry.global_seq = gw->flash_log.global_sequence++;
    entry.stream_id = module_id;
    entry.event_type = event->event_type;
    entry.timestamp_us = event->timestamp_us;
    entry.origin_seq = event->origin_seq;
    entry.length = sizeof(event->payload);

    memcpy(entry.data, event->payload, sizeof(event->payload));

    /* Compute CRC */
    entry.crc32 = ekk_flash_entry_crc(&entry);

    /* Write to flash */
    uint32_t offset = ekk_flash_log_write(&gw->flash_log, &entry);
    if (offset == 0xFFFFFFFF) {
        return 0;
    }

    /* Add to index */
    ekk_index_entry_t idx_entry;
    idx_entry.global_seq = entry.global_seq;
    idx_entry.flash_offset = offset;
    idx_entry.timestamp_us = entry.timestamp_us;
    idx_entry.stream_id = module_id;
    idx_entry.event_type = event->event_type;
    idx_entry.stream_seq = (uint16_t)(event->origin_seq & 0xFFFF);

    ekk_stream_index_add(&gw->index, &idx_entry);

    /* Update projection */
    update_projection(gw, event);

    gw->events_stored++;

    return entry.global_seq;
}

uint32_t ekk_gateway_read_stream(ekk_gateway_t *gw, ekk_module_id_t stream_id,
                                  uint32_t from_seq, ekk_event_v2_t *buffer,
                                  uint32_t max_count) {
    if (!gw || !buffer || max_count == 0) {
        return 0;
    }

    uint32_t first_idx, count;
    ekk_error_t err = ekk_stream_index_find_stream(&gw->index, stream_id,
                                                    from_seq, &first_idx, &count);
    if (err != EKK_OK) {
        return 0;
    }

    if (count > max_count) {
        count = max_count;
    }

    /* Read entries from flash */
    uint32_t read = 0;
    for (uint32_t i = 0; i < count; i++) {
        const ekk_index_entry_t *idx = &gw->index.entries[first_idx + i];

        ekk_flash_entry_t entry;
        if (ekk_flash_log_read(&gw->flash_log, idx->flash_offset, &entry) == EKK_OK) {
            /* Convert to event_v2 */
            buffer[read].sequence = entry.global_seq;
            buffer[read].timestamp_us = entry.timestamp_us;
            buffer[read].event_type = entry.event_type;
            buffer[read].origin_id = entry.stream_id;
            buffer[read].origin_seq = entry.origin_seq;
            buffer[read].hop_count = 0;
            buffer[read].flags = 0;
            memcpy(buffer[read].payload, entry.data, 20);

            read++;
        }
    }

    gw->queries_served++;
    return read;
}

ekk_error_t ekk_gateway_module_state(ekk_gateway_t *gw, ekk_module_id_t module_id,
                                      ekk_module_projection_t *projection) {
    if (!gw || !projection) {
        return EKK_ERR_INVALID_ARG;
    }

    ekk_module_projection_t *proj = find_projection(gw, module_id);
    if (!proj) {
        return EKK_ERR_NOT_FOUND;
    }

    *projection = *proj;
    return EKK_OK;
}

ekk_error_t ekk_gateway_state_at(ekk_gateway_t *gw, ekk_module_id_t module_id,
                                  uint32_t timestamp_us,
                                  ekk_module_projection_t *projection) {
    if (!gw || !projection) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Initialize empty projection */
    memset(projection, 0, sizeof(*projection));
    projection->module_id = module_id;
    projection->state = EKK_MODULE_INIT;

    /* Find events up to timestamp */
    uint32_t first_idx, count;
    ekk_error_t err = ekk_stream_index_find_time(&gw->index, 0, timestamp_us,
                                                  &first_idx, &count);
    if (err != EKK_OK) {
        return EKK_OK;  /* No events, return initial state */
    }

    /* Replay events to build projection */
    for (uint32_t i = 0; i < count; i++) {
        const ekk_index_entry_t *idx = &gw->index.entries[first_idx + i];

        if (idx->stream_id != module_id) {
            continue;
        }

        ekk_flash_entry_t entry;
        if (ekk_flash_log_read(&gw->flash_log, idx->flash_offset, &entry) == EKK_OK) {
            /* Build event_v2 and update projection */
            ekk_event_v2_t event;
            event.timestamp_us = entry.timestamp_us;
            event.event_type = entry.event_type;
            event.origin_id = entry.stream_id;
            memcpy(event.payload, entry.data, 20);

            /* Apply to projection */
            projection->last_seen = event.timestamp_us;
            projection->event_count++;

            switch (event.event_type) {
                case EKK_EVENT_STATE_TRANSITION:
                    if (event.payload[0] < EKK_MODULE_SHUTDOWN) {
                        projection->state = (ekk_module_state_t)event.payload[0];
                    }
                    break;
                default:
                    break;
            }
        }
    }

    return EKK_OK;
}

ekk_error_t ekk_gateway_tick(ekk_gateway_t *gw, ekk_time_us_t now) {
    if (!gw) {
        return EKK_ERR_INVALID_ARG;
    }

    EKK_UNUSED(now);

    /* Check for upstream sync */
    if (gw->upstream.connected && gw->upstream.pending_count > 0) {
        ekk_upstream_sync(gw);
    }

    return EKK_OK;
}

/* ============================================================================
 * UPSTREAM SYNC
 * ============================================================================ */

ekk_error_t ekk_upstream_init(ekk_upstream_t *upstream) {
    if (!upstream) {
        return EKK_ERR_INVALID_ARG;
    }

    memset(upstream, 0, sizeof(*upstream));
    return EKK_OK;
}

ekk_error_t ekk_upstream_sync(ekk_gateway_t *gw) {
    if (!gw) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Upstream sync would use Ethernet HAL to push events */
    /* Implementation depends on cloud protocol (gRPC, etc.) */

    return EKK_OK;
}

ekk_error_t ekk_upstream_pull(ekk_gateway_t *gw, uint32_t from_seq) {
    if (!gw) {
        return EKK_ERR_INVALID_ARG;
    }

    EKK_UNUSED(from_seq);

    /* Pull events from upstream for recovery */

    return EKK_OK;
}
