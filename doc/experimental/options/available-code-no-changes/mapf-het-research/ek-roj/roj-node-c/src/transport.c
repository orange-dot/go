/*
 * ROJ Transport - UDP messaging implementation
 *
 * SPDX-License-Identifier: AGPL-3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transport.h"
#include "cJSON.h"

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
static SOCKET g_socket = INVALID_SOCKET;
#define SOCKET_TYPE SOCKET
#define SOCKET_INVALID INVALID_SOCKET
#define CLOSE_SOCKET closesocket
#else
#include <unistd.h>
static int g_socket = -1;
#define SOCKET_TYPE int
#define SOCKET_INVALID -1
#define CLOSE_SOCKET close
#endif

static char g_recv_buf[ROJ_MSG_MAX_SIZE];

int transport_init(int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[ERROR] WSAStartup failed\n");
        return -1;
    }
#endif

    g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_socket == SOCKET_INVALID) {
        fprintf(stderr, "[ERROR] Failed to create socket\n");
        return -1;
    }

    /* Enable broadcast */
    int broadcast = 1;
    setsockopt(g_socket, SOL_SOCKET, SO_BROADCAST,
               (const char*)&broadcast, sizeof(broadcast));

    /* Enable address reuse */
    int reuse = 1;
    setsockopt(g_socket, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(g_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[ERROR] Failed to bind to port %d\n", port);
        CLOSE_SOCKET(g_socket);
        return -1;
    }

    printf("[INFO] Listening on port %d\n", port);
    return 0;
}

void transport_shutdown(void) {
    if (g_socket != SOCKET_INVALID) {
        CLOSE_SOCKET(g_socket);
        g_socket = SOCKET_INVALID;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

int transport_get_socket(void) {
    return (int)g_socket;
}

int transport_recv(roj_message_t* msg, struct sockaddr_in* from) {
    socklen_t from_len = sizeof(*from);

    int n = recvfrom(g_socket, g_recv_buf, sizeof(g_recv_buf) - 1, 0,
                     (struct sockaddr*)from, &from_len);
    if (n <= 0) {
        return -1;
    }

    g_recv_buf[n] = '\0';
    return message_from_json(g_recv_buf, msg);
}

int transport_send(const roj_message_t* msg, const struct sockaddr_in* to) {
    char buf[ROJ_MSG_MAX_SIZE];
    int len = message_to_json(msg, buf, sizeof(buf));
    if (len < 0) {
        return -1;
    }

    int sent = sendto(g_socket, buf, len, 0,
                      (const struct sockaddr*)to, sizeof(*to));
    return sent > 0 ? 0 : -1;
}

int transport_broadcast(const roj_message_t* msg,
                       const struct sockaddr_in* addrs, int addr_count) {
    char buf[ROJ_MSG_MAX_SIZE];
    int len = message_to_json(msg, buf, sizeof(buf));
    if (len < 0) {
        return -1;
    }

    int success = 0;
    for (int i = 0; i < addr_count; i++) {
        if (sendto(g_socket, buf, len, 0,
                   (const struct sockaddr*)&addrs[i], sizeof(addrs[i])) > 0) {
            success++;
        }
    }
    return success;
}

int message_to_json(const roj_message_t* msg, char* buf, size_t buf_size) {
    cJSON* root = cJSON_CreateObject();
    if (!root) return -1;

    switch (msg->type) {
        case MSG_ANNOUNCE:
            cJSON_AddStringToObject(root, "type", "ANNOUNCE");
            cJSON_AddStringToObject(root, "node_id", msg->data.announce.node_id);
            cJSON_AddStringToObject(root, "lang", lang_to_str(msg->data.announce.lang));
            cJSON_AddItemToObject(root, "capabilities",
                                  cJSON_CreateStringArray((const char*[]){"consensus"}, 1));
            cJSON_AddStringToObject(root, "version", msg->data.announce.version);
            break;

        case MSG_PROPOSE:
            cJSON_AddStringToObject(root, "type", "PROPOSE");
            cJSON_AddStringToObject(root, "proposal_id", msg->data.propose.proposal_id);
            cJSON_AddStringToObject(root, "from", msg->data.propose.from);
            cJSON_AddStringToObject(root, "key", msg->data.propose.key);
            cJSON_AddNumberToObject(root, "value", (double)msg->data.propose.value);
            cJSON_AddNumberToObject(root, "timestamp", (double)msg->data.propose.timestamp);
            break;

        case MSG_VOTE:
            cJSON_AddStringToObject(root, "type", "VOTE");
            cJSON_AddStringToObject(root, "proposal_id", msg->data.vote.proposal_id);
            cJSON_AddStringToObject(root, "from", msg->data.vote.from);
            cJSON_AddStringToObject(root, "vote", vote_to_str(msg->data.vote.vote));
            break;

        case MSG_COMMIT:
            cJSON_AddStringToObject(root, "type", "COMMIT");
            cJSON_AddStringToObject(root, "proposal_id", msg->data.commit.proposal_id);
            cJSON_AddStringToObject(root, "key", msg->data.commit.key);
            cJSON_AddNumberToObject(root, "value", (double)msg->data.commit.value);
            {
                cJSON* voters = cJSON_CreateArray();
                for (int i = 0; i < msg->data.commit.voter_count; i++) {
                    cJSON_AddItemToArray(voters,
                        cJSON_CreateString(msg->data.commit.voters[i]));
                }
                cJSON_AddItemToObject(root, "voters", voters);
            }
            break;

        default:
            cJSON_Delete(root);
            return -1;
    }

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json) return -1;

    int len = (int)strlen(json);
    if ((size_t)len >= buf_size) {
        free(json);
        return -1;
    }

    strcpy(buf, json);
    free(json);
    return len;
}

int message_from_json(const char* json, roj_message_t* msg) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return -1;

    memset(msg, 0, sizeof(*msg));

    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!type || !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return -1;
    }

    const char* type_str = type->valuestring;

    if (strcmp(type_str, "ANNOUNCE") == 0) {
        msg->type = MSG_ANNOUNCE;

        cJSON* node_id = cJSON_GetObjectItem(root, "node_id");
        cJSON* lang = cJSON_GetObjectItem(root, "lang");
        cJSON* version = cJSON_GetObjectItem(root, "version");

        if (node_id && cJSON_IsString(node_id)) {
            strncpy(msg->data.announce.node_id, node_id->valuestring,
                    ROJ_NODE_ID_MAX - 1);
        }
        if (lang && cJSON_IsString(lang)) {
            msg->data.announce.lang = str_to_lang(lang->valuestring);
        }
        if (version && cJSON_IsString(version)) {
            strncpy(msg->data.announce.version, version->valuestring, 15);
        }
    }
    else if (strcmp(type_str, "PROPOSE") == 0) {
        msg->type = MSG_PROPOSE;

        cJSON* proposal_id = cJSON_GetObjectItem(root, "proposal_id");
        cJSON* from = cJSON_GetObjectItem(root, "from");
        cJSON* key = cJSON_GetObjectItem(root, "key");
        cJSON* value = cJSON_GetObjectItem(root, "value");
        cJSON* timestamp = cJSON_GetObjectItem(root, "timestamp");

        if (proposal_id && cJSON_IsString(proposal_id)) {
            strncpy(msg->data.propose.proposal_id, proposal_id->valuestring,
                    ROJ_PROPOSAL_ID_LEN - 1);
        }
        if (from && cJSON_IsString(from)) {
            strncpy(msg->data.propose.from, from->valuestring, ROJ_NODE_ID_MAX - 1);
        }
        if (key && cJSON_IsString(key)) {
            strncpy(msg->data.propose.key, key->valuestring, ROJ_KEY_MAX - 1);
        }
        if (value && cJSON_IsNumber(value)) {
            msg->data.propose.value = (int64_t)value->valuedouble;
        }
        if (timestamp && cJSON_IsNumber(timestamp)) {
            msg->data.propose.timestamp = (int64_t)timestamp->valuedouble;
        }
    }
    else if (strcmp(type_str, "VOTE") == 0) {
        msg->type = MSG_VOTE;

        cJSON* proposal_id = cJSON_GetObjectItem(root, "proposal_id");
        cJSON* from = cJSON_GetObjectItem(root, "from");
        cJSON* vote = cJSON_GetObjectItem(root, "vote");

        if (proposal_id && cJSON_IsString(proposal_id)) {
            strncpy(msg->data.vote.proposal_id, proposal_id->valuestring,
                    ROJ_PROPOSAL_ID_LEN - 1);
        }
        if (from && cJSON_IsString(from)) {
            strncpy(msg->data.vote.from, from->valuestring, ROJ_NODE_ID_MAX - 1);
        }
        if (vote && cJSON_IsString(vote)) {
            msg->data.vote.vote = str_to_vote(vote->valuestring);
        }
    }
    else if (strcmp(type_str, "COMMIT") == 0) {
        msg->type = MSG_COMMIT;

        cJSON* proposal_id = cJSON_GetObjectItem(root, "proposal_id");
        cJSON* key = cJSON_GetObjectItem(root, "key");
        cJSON* value = cJSON_GetObjectItem(root, "value");
        cJSON* voters = cJSON_GetObjectItem(root, "voters");

        if (proposal_id && cJSON_IsString(proposal_id)) {
            strncpy(msg->data.commit.proposal_id, proposal_id->valuestring,
                    ROJ_PROPOSAL_ID_LEN - 1);
        }
        if (key && cJSON_IsString(key)) {
            strncpy(msg->data.commit.key, key->valuestring, ROJ_KEY_MAX - 1);
        }
        if (value && cJSON_IsNumber(value)) {
            msg->data.commit.value = (int64_t)value->valuedouble;
        }
        if (voters && cJSON_IsArray(voters)) {
            int count = cJSON_GetArraySize(voters);
            if (count > ROJ_MAX_VOTERS) count = ROJ_MAX_VOTERS;
            msg->data.commit.voter_count = count;
            for (int i = 0; i < count; i++) {
                cJSON* voter = cJSON_GetArrayItem(voters, i);
                if (voter && cJSON_IsString(voter)) {
                    strncpy(msg->data.commit.voters[i], voter->valuestring,
                            ROJ_NODE_ID_MAX - 1);
                }
            }
        }
    }
    else {
        msg->type = MSG_UNKNOWN;
    }

    cJSON_Delete(root);
    return 0;
}
