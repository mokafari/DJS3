/**
 * @file ableton_link.c
 * @brief Ableton Link protocol implementation for ESP32
 * 
 * Implements Link protocol for tempo/beat/phase synchronization:
 * - UDP multicast discovery on 224.76.78.75:20808
 * - Peer session management with TTL-based expiration
 * - Timeline synchronization (tempo, beat origin, time origin)
 * - Phase alignment based on configurable quantum
 * - Optional start/stop state synchronization
 * 
 * Protocol reference: https://github.com/Ableton/link
 */

#include "ableton_link.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/igmp.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

static const char *TAG = "ableton_link";

// Link protocol constants
#define LINK_MULTICAST_ADDR     "224.76.78.75"
#define LINK_MULTICAST_PORT     20808
#define LINK_PROTOCOL_VERSION   1
#define LINK_TTL_SECONDS        8
#define LINK_BROADCAST_INTERVAL_MS  1000
#define LINK_MAX_PEERS          8
#define LINK_MESSAGE_MAX_SIZE   512

// Message types (protocol v1)
#define MSG_TYPE_ALIVE          0x616C6976  // 'aliv'
#define MSG_TYPE_BYEBYE         0x62796562  // 'byeb'
#define MSG_TYPE_TIMELINE       0x746D6C6E  // 'tmln'
#define MSG_TYPE_START_STOP     0x73737473  // 'ssts'

// Node identifier (unique per Link instance)
typedef struct {
    uint8_t data[8];
} node_id_t;

// Timeline structure (matches Link protocol)
typedef struct {
    double tempo_bpm;          // Tempo in BPM
    int64_t beat_origin;       // Beat origin (in micro-beats)
    int64_t time_origin;       // Time origin (microseconds)
} timeline_t;

// Start/stop state
typedef struct {
    bool is_playing;
    int64_t time_us;
} start_stop_state_t;

// Peer information
typedef struct {
    node_id_t id;
    timeline_t timeline;
    start_stop_state_t start_stop;
    int64_t last_seen_us;
    bool active;
} peer_t;

// Internal session state
typedef struct {
    timeline_t timeline;
    start_stop_state_t start_stop;
    bool has_peers;
    size_t num_peers;
} internal_state_t;

// Main Link structure
struct ableton_link_s {
    // Configuration
    double quantum;
    bool start_stop_sync_enabled;
    
    // Identity
    node_id_t node_id;
    
    // Session state (protected by mutex)
    SemaphoreHandle_t state_mutex;
    internal_state_t state;
    
    // Audio thread state (lock-free)
    internal_state_t audio_state;
    volatile bool audio_state_pending;
    
    // Peer management
    peer_t peers[LINK_MAX_PEERS];
    size_t peer_count;
    
    // Network
    int socket_fd;
    struct sockaddr_in multicast_addr;
    bool enabled;
    
    // Tasks
    TaskHandle_t rx_task;
    TaskHandle_t tx_task;
    bool running;
    
    // Callbacks
    link_peer_callback_t peer_callback;
    void *peer_callback_data;
    link_tempo_callback_t tempo_callback;
    void *tempo_callback_data;
    link_start_stop_callback_t start_stop_callback;
    void *start_stop_callback_data;
};

// ============================================================================
// Utility functions
// ============================================================================

static void generate_node_id(node_id_t *id) {
    // Generate random node ID using ESP32 random number generator
    for (int i = 0; i < 8; i++) {
        id->data[i] = (uint8_t)(esp_random() & 0xFF);
    }
}

static bool node_id_equals(const node_id_t *a, const node_id_t *b) {
    return memcmp(a->data, b->data, sizeof(a->data)) == 0;
}

static int64_t get_link_time_us(void) {
    return (int64_t)esp_timer_get_time();
}

// Micro-beats conversion (Link uses micro-beats internally: 1 beat = 1000000 micro-beats)
#define MICRO_BEATS_PER_BEAT 1000000LL

static int64_t beats_to_micro(double beats) {
    return (int64_t)(beats * MICRO_BEATS_PER_BEAT);
}

static double micro_to_beats(int64_t micro) {
    return (double)micro / MICRO_BEATS_PER_BEAT;
}

// ============================================================================
// Timeline calculations
// ============================================================================

static double timeline_beat_at_time(const timeline_t *tl, int64_t time_us) {
    if (tl->tempo_bpm <= 0) return 0.0;
    
    // microseconds per beat = 60,000,000 / bpm
    double us_per_beat = 60000000.0 / tl->tempo_bpm;
    double delta_beats = (double)(time_us - tl->time_origin) / us_per_beat;
    return micro_to_beats(tl->beat_origin) + delta_beats;
}

static int64_t timeline_time_at_beat(const timeline_t *tl, double beat) {
    if (tl->tempo_bpm <= 0) return tl->time_origin;
    
    double us_per_beat = 60000000.0 / tl->tempo_bpm;
    double delta_beats = beat - micro_to_beats(tl->beat_origin);
    return tl->time_origin + (int64_t)(delta_beats * us_per_beat);
}

static double phase_at_time(const timeline_t *tl, int64_t time_us, double quantum) {
    if (quantum <= 0) return 0.0;
    
    double beat = timeline_beat_at_time(tl, time_us);
    double phase = fmod(beat, quantum);
    if (phase < 0) phase += quantum;
    return phase;
}

static void timeline_set_tempo(timeline_t *tl, double bpm, int64_t at_time) {
    // Preserve beat position when changing tempo
    double current_beat = timeline_beat_at_time(tl, at_time);
    tl->tempo_bpm = bpm;
    tl->beat_origin = beats_to_micro(current_beat);
    tl->time_origin = at_time;
}

// ============================================================================
// Protocol serialization
// ============================================================================

// Write 32-bit big-endian
static void write_u32_be(uint8_t *buf, uint32_t val) {
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

// Read 32-bit big-endian
static uint32_t read_u32_be(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           (uint32_t)buf[3];
}

// Write 64-bit big-endian
static void write_i64_be(uint8_t *buf, int64_t val) {
    buf[0] = (val >> 56) & 0xFF;
    buf[1] = (val >> 48) & 0xFF;
    buf[2] = (val >> 40) & 0xFF;
    buf[3] = (val >> 32) & 0xFF;
    buf[4] = (val >> 24) & 0xFF;
    buf[5] = (val >> 16) & 0xFF;
    buf[6] = (val >> 8) & 0xFF;
    buf[7] = val & 0xFF;
}

// Read 64-bit big-endian
static int64_t read_i64_be(const uint8_t *buf) {
    return ((int64_t)buf[0] << 56) |
           ((int64_t)buf[1] << 48) |
           ((int64_t)buf[2] << 40) |
           ((int64_t)buf[3] << 32) |
           ((int64_t)buf[4] << 24) |
           ((int64_t)buf[5] << 16) |
           ((int64_t)buf[6] << 8) |
           (int64_t)buf[7];
}

// Write double as 64-bit IEEE 754
static void write_double_be(uint8_t *buf, double val) {
    union { double d; int64_t i; } u;
    u.d = val;
    write_i64_be(buf, u.i);
}

// Read double from 64-bit IEEE 754
static double read_double_be(const uint8_t *buf) {
    union { double d; int64_t i; } u;
    u.i = read_i64_be(buf);
    return u.d;
}

// Build state message
static int build_state_message(ableton_link_t *link, uint8_t *buf, int max_len) {
    if (max_len < 100) return 0;
    
    int offset = 0;
    
    // Header: protocol version (1 byte), message type
    buf[offset++] = LINK_PROTOCOL_VERSION;
    
    // TTL (1 byte)
    buf[offset++] = LINK_TTL_SECONDS;
    
    // Node ID (8 bytes)
    memcpy(&buf[offset], link->node_id.data, 8);
    offset += 8;
    
    // Message type: alive
    write_u32_be(&buf[offset], MSG_TYPE_ALIVE);
    offset += 4;
    
    // Timeline payload
    write_u32_be(&buf[offset], MSG_TYPE_TIMELINE);
    offset += 4;
    
    // Timeline size (24 bytes: tempo + beat_origin + time_origin)
    write_u32_be(&buf[offset], 24);
    offset += 4;
    
    // Tempo (double, 8 bytes)
    write_double_be(&buf[offset], link->state.timeline.tempo_bpm);
    offset += 8;
    
    // Beat origin (int64, 8 bytes)
    write_i64_be(&buf[offset], link->state.timeline.beat_origin);
    offset += 8;
    
    // Time origin (int64, 8 bytes)
    write_i64_be(&buf[offset], link->state.timeline.time_origin);
    offset += 8;
    
    // Start/stop state (if enabled)
    if (link->start_stop_sync_enabled) {
        write_u32_be(&buf[offset], MSG_TYPE_START_STOP);
        offset += 4;
        
        // Start/stop size (9 bytes: playing + time)
        write_u32_be(&buf[offset], 9);
        offset += 4;
        
        buf[offset++] = link->state.start_stop.is_playing ? 1 : 0;
        write_i64_be(&buf[offset], link->state.start_stop.time_us);
        offset += 8;
    }
    
    return offset;
}

// Build bye-bye message
static int build_byebye_message(ableton_link_t *link, uint8_t *buf, int max_len) {
    if (max_len < 14) return 0;
    
    int offset = 0;
    
    buf[offset++] = LINK_PROTOCOL_VERSION;
    buf[offset++] = 0;  // TTL 0 for bye-bye
    
    memcpy(&buf[offset], link->node_id.data, 8);
    offset += 8;
    
    write_u32_be(&buf[offset], MSG_TYPE_BYEBYE);
    offset += 4;
    
    return offset;
}

// Parse incoming message
static void parse_message(ableton_link_t *link, const uint8_t *buf, int len) {
    if (len < 14) return;
    
    int offset = 0;
    
    uint8_t version = buf[offset++];
    if (version != LINK_PROTOCOL_VERSION) {
        ESP_LOGD(TAG, "Unknown protocol version: %d", version);
        return;
    }
    
    uint8_t ttl = buf[offset++];
    
    node_id_t peer_id;
    memcpy(peer_id.data, &buf[offset], 8);
    offset += 8;
    
    // Ignore our own messages
    if (node_id_equals(&peer_id, &link->node_id)) {
        return;
    }
    
    uint32_t msg_type = read_u32_be(&buf[offset]);
    offset += 4;
    
    if (msg_type == MSG_TYPE_BYEBYE) {
        // Remove peer
        for (int i = 0; i < LINK_MAX_PEERS; i++) {
            if (link->peers[i].active && node_id_equals(&link->peers[i].id, &peer_id)) {
                link->peers[i].active = false;
                link->peer_count--;
                ESP_LOGI(TAG, "Peer left, %zu remaining", link->peer_count);
                
                if (link->peer_callback) {
                    link->peer_callback(link->peer_callback_data, link->peer_count);
                }
                break;
            }
        }
        return;
    }
    
    if (msg_type != MSG_TYPE_ALIVE) {
        return;
    }
    
    // Find or create peer entry
    peer_t *peer = NULL;
    int free_slot = -1;
    
    for (int i = 0; i < LINK_MAX_PEERS; i++) {
        if (link->peers[i].active && node_id_equals(&link->peers[i].id, &peer_id)) {
            peer = &link->peers[i];
            break;
        }
        if (!link->peers[i].active && free_slot < 0) {
            free_slot = i;
        }
    }
    
    bool new_peer = false;
    if (!peer && free_slot >= 0) {
        peer = &link->peers[free_slot];
        peer->id = peer_id;
        peer->active = true;
        link->peer_count++;
        new_peer = true;
        ESP_LOGI(TAG, "New peer discovered, %zu total", link->peer_count);
    }
    
    if (!peer) {
        ESP_LOGW(TAG, "Max peers reached, ignoring");
        return;
    }
    
    peer->last_seen_us = get_link_time_us();
    
    // Parse payload entries
    while (offset + 8 <= len) {
        uint32_t entry_type = read_u32_be(&buf[offset]);
        offset += 4;
        
        uint32_t entry_size = read_u32_be(&buf[offset]);
        offset += 4;
        
        if (offset + (int)entry_size > len) break;
        
        if (entry_type == MSG_TYPE_TIMELINE && entry_size >= 24) {
            peer->timeline.tempo_bpm = read_double_be(&buf[offset]);
            peer->timeline.beat_origin = read_i64_be(&buf[offset + 8]);
            peer->timeline.time_origin = read_i64_be(&buf[offset + 16]);
            
            // Adopt peer's timeline if it's newer or we're new
            if (xSemaphoreTake(link->state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                double old_tempo = link->state.timeline.tempo_bpm;
                
                // Simple strategy: adopt tempo from any peer (last writer wins)
                // More sophisticated: compare timestamps, use weighted average
                link->state.timeline = peer->timeline;
                link->state.has_peers = true;
                link->state.num_peers = link->peer_count;
                
                if (fabs(old_tempo - peer->timeline.tempo_bpm) > 0.01) {
                    ESP_LOGI(TAG, "Tempo changed to %.2f BPM", peer->timeline.tempo_bpm);
                    if (link->tempo_callback) {
                        link->tempo_callback(link->tempo_callback_data, peer->timeline.tempo_bpm);
                    }
                }
                
                xSemaphoreGive(link->state_mutex);
            }
        }
        else if (entry_type == MSG_TYPE_START_STOP && entry_size >= 9) {
            peer->start_stop.is_playing = buf[offset] != 0;
            peer->start_stop.time_us = read_i64_be(&buf[offset + 1]);
            
            if (link->start_stop_sync_enabled) {
                if (xSemaphoreTake(link->state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    bool old_playing = link->state.start_stop.is_playing;
                    
                    // Adopt start/stop state if newer
                    if (peer->start_stop.time_us > link->state.start_stop.time_us) {
                        link->state.start_stop = peer->start_stop;
                        
                        if (old_playing != peer->start_stop.is_playing && link->start_stop_callback) {
                            link->start_stop_callback(link->start_stop_callback_data, 
                                                      peer->start_stop.is_playing);
                        }
                    }
                    
                    xSemaphoreGive(link->state_mutex);
                }
            }
        }
        
        offset += entry_size;
    }
    
    if (new_peer && link->peer_callback) {
        link->peer_callback(link->peer_callback_data, link->peer_count);
    }
}

// ============================================================================
// Network tasks
// ============================================================================

static void rx_task(void *arg) {
    ableton_link_t *link = (ableton_link_t *)arg;
    uint8_t buf[LINK_MESSAGE_MAX_SIZE];
    
    ESP_LOGI(TAG, "RX task started");
    
    while (link->running) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        int len = recvfrom(link->socket_fd, buf, sizeof(buf), 0,
                          (struct sockaddr *)&from_addr, &from_len);
        
        if (len > 0) {
            parse_message(link, buf, len);
        }
        else if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGE(TAG, "recvfrom error: %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    ESP_LOGI(TAG, "RX task stopped");
    vTaskDelete(NULL);
}

static void tx_task(void *arg) {
    ableton_link_t *link = (ableton_link_t *)arg;
    uint8_t buf[LINK_MESSAGE_MAX_SIZE];
    int64_t last_prune_us = 0;
    
    ESP_LOGI(TAG, "TX task started");
    
    while (link->running) {
        // Broadcast state
        if (xSemaphoreTake(link->state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            int len = build_state_message(link, buf, sizeof(buf));
            xSemaphoreGive(link->state_mutex);
            
            if (len > 0) {
                int sent = sendto(link->socket_fd, buf, len, 0,
                                 (struct sockaddr *)&link->multicast_addr,
                                 sizeof(link->multicast_addr));
                if (sent < 0) {
                    ESP_LOGW(TAG, "sendto error: %d", errno);
                }
            }
        }
        
        // Prune expired peers (every 2 seconds)
        int64_t now_us = get_link_time_us();
        if (now_us - last_prune_us > 2000000) {
            last_prune_us = now_us;
            int64_t timeout_us = LINK_TTL_SECONDS * 1000000LL + 1000000LL;
            
            for (int i = 0; i < LINK_MAX_PEERS; i++) {
                if (link->peers[i].active && 
                    (now_us - link->peers[i].last_seen_us) > timeout_us) {
                    link->peers[i].active = false;
                    link->peer_count--;
                    ESP_LOGI(TAG, "Peer timed out, %zu remaining", link->peer_count);
                    
                    if (link->peer_callback) {
                        link->peer_callback(link->peer_callback_data, link->peer_count);
                    }
                }
            }
            
            // Update state
            if (xSemaphoreTake(link->state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                link->state.has_peers = (link->peer_count > 0);
                link->state.num_peers = link->peer_count;
                xSemaphoreGive(link->state_mutex);
            }
        }
        
        // Update audio state (lock-free update)
        if (xSemaphoreTake(link->state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            link->audio_state = link->state;
            link->audio_state_pending = false;
            xSemaphoreGive(link->state_mutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(LINK_BROADCAST_INTERVAL_MS));
    }
    
    // Send bye-bye
    int len = build_byebye_message(link, buf, sizeof(buf));
    if (len > 0) {
        sendto(link->socket_fd, buf, len, 0,
               (struct sockaddr *)&link->multicast_addr,
               sizeof(link->multicast_addr));
    }
    
    ESP_LOGI(TAG, "TX task stopped");
    vTaskDelete(NULL);
}

// ============================================================================
// Network setup
// ============================================================================

static int setup_multicast_socket(ableton_link_t *link) {
    // Create UDP socket
    link->socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (link->socket_fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", errno);
        return -1;
    }
    
    // Allow address reuse
    int reuse = 1;
    setsockopt(link->socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    // Bind to port
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(LINK_MULTICAST_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    
    if (bind(link->socket_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: %d", errno);
        close(link->socket_fd);
        return -1;
    }
    
    // Join multicast group
    struct ip_mreq mreq = {
        .imr_multiaddr.s_addr = inet_addr(LINK_MULTICAST_ADDR),
        .imr_interface.s_addr = htonl(INADDR_ANY)
    };
    
    if (setsockopt(link->socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
                   &mreq, sizeof(mreq)) < 0) {
        ESP_LOGE(TAG, "Failed to join multicast group: %d", errno);
        close(link->socket_fd);
        return -1;
    }
    
    // Set multicast TTL
    uint8_t ttl = 1;  // Local network only
    setsockopt(link->socket_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    
    // Enable loopback (useful for testing on same device)
    uint8_t loop = 1;
    setsockopt(link->socket_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    
    // Set receive timeout
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(link->socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // Setup multicast destination address
    link->multicast_addr.sin_family = AF_INET;
    link->multicast_addr.sin_port = htons(LINK_MULTICAST_PORT);
    link->multicast_addr.sin_addr.s_addr = inet_addr(LINK_MULTICAST_ADDR);
    
    ESP_LOGI(TAG, "Multicast socket ready on %s:%d", 
             LINK_MULTICAST_ADDR, LINK_MULTICAST_PORT);
    
    return 0;
}

static void cleanup_socket(ableton_link_t *link) {
    if (link->socket_fd >= 0) {
        // Leave multicast group
        struct ip_mreq mreq = {
            .imr_multiaddr.s_addr = inet_addr(LINK_MULTICAST_ADDR),
            .imr_interface.s_addr = htonl(INADDR_ANY)
        };
        setsockopt(link->socket_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, 
                   &mreq, sizeof(mreq));
        
        close(link->socket_fd);
        link->socket_fd = -1;
    }
}

// ============================================================================
// Public API
// ============================================================================

ableton_link_t *ableton_link_create(const link_config_t *config) {
    ableton_link_t *link = calloc(1, sizeof(ableton_link_t));
    if (!link) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return NULL;
    }
    
    // Initialize
    link->socket_fd = -1;
    link->quantum = config ? config->quantum : 4.0;
    link->start_stop_sync_enabled = config ? config->start_stop_sync : false;
    
    // Generate unique node ID
    generate_node_id(&link->node_id);
    
    // Create mutex
    link->state_mutex = xSemaphoreCreateMutex();
    if (!link->state_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(link);
        return NULL;
    }
    
    // Initialize timeline
    double initial_tempo = config ? config->initial_tempo : 120.0;
    int64_t now = get_link_time_us();
    
    link->state.timeline.tempo_bpm = initial_tempo;
    link->state.timeline.beat_origin = 0;
    link->state.timeline.time_origin = now;
    link->state.start_stop.is_playing = false;
    link->state.start_stop.time_us = now;
    link->state.has_peers = false;
    link->state.num_peers = 0;
    
    link->audio_state = link->state;
    
    ESP_LOGI(TAG, "Link created with tempo %.2f BPM, quantum %.1f", 
             initial_tempo, link->quantum);
    
    return link;
}

void ableton_link_destroy(ableton_link_t *link) {
    if (!link) return;
    
    ableton_link_enable(link, false);
    
    if (link->state_mutex) {
        vSemaphoreDelete(link->state_mutex);
    }
    
    free(link);
    ESP_LOGI(TAG, "Link destroyed");
}

int ableton_link_enable(ableton_link_t *link, bool enable) {
    if (!link) return -1;
    
    if (enable == link->enabled) {
        return 0;
    }
    
    if (enable) {
        // Setup network
        if (setup_multicast_socket(link) < 0) {
            return -1;
        }
        
        link->running = true;
        
        // Start tasks
        BaseType_t ret;
        ret = xTaskCreate(rx_task, "link_rx", 4096, link, 5, &link->rx_task);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create RX task");
            cleanup_socket(link);
            return -1;
        }
        
        ret = xTaskCreate(tx_task, "link_tx", 4096, link, 4, &link->tx_task);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create TX task");
            link->running = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            cleanup_socket(link);
            return -1;
        }
        
        link->enabled = true;
        ESP_LOGI(TAG, "Link enabled");
    }
    else {
        // Stop tasks
        link->running = false;
        vTaskDelay(pdMS_TO_TICKS(LINK_BROADCAST_INTERVAL_MS * 2));
        
        // Cleanup
        cleanup_socket(link);
        
        // Clear peers
        memset(link->peers, 0, sizeof(link->peers));
        link->peer_count = 0;
        
        if (xSemaphoreTake(link->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            link->state.has_peers = false;
            link->state.num_peers = 0;
            xSemaphoreGive(link->state_mutex);
        }
        
        link->enabled = false;
        ESP_LOGI(TAG, "Link disabled");
    }
    
    return 0;
}

bool ableton_link_is_enabled(const ableton_link_t *link) {
    return link ? link->enabled : false;
}

size_t ableton_link_num_peers(const ableton_link_t *link) {
    return link ? link->peer_count : 0;
}

void ableton_link_enable_start_stop_sync(ableton_link_t *link, bool enable) {
    if (link) {
        link->start_stop_sync_enabled = enable;
    }
}

bool ableton_link_is_start_stop_sync_enabled(const ableton_link_t *link) {
    return link ? link->start_stop_sync_enabled : false;
}

void ableton_link_set_peer_callback(ableton_link_t *link, 
                                    link_peer_callback_t callback,
                                    void *user_data) {
    if (link) {
        link->peer_callback = callback;
        link->peer_callback_data = user_data;
    }
}

void ableton_link_set_tempo_callback(ableton_link_t *link,
                                     link_tempo_callback_t callback,
                                     void *user_data) {
    if (link) {
        link->tempo_callback = callback;
        link->tempo_callback_data = user_data;
    }
}

void ableton_link_set_start_stop_callback(ableton_link_t *link,
                                          link_start_stop_callback_t callback,
                                          void *user_data) {
    if (link) {
        link->start_stop_callback = callback;
        link->start_stop_callback_data = user_data;
    }
}

void ableton_link_capture_audio_state(const ableton_link_t *link, 
                                      link_session_state_t *state) {
    if (!link || !state) return;
    
    // Lock-free read for audio thread
    const internal_state_t *s = &link->audio_state;
    int64_t now = get_link_time_us();
    
    state->tempo = s->timeline.tempo_bpm;
    state->beat = timeline_beat_at_time(&s->timeline, now);
    state->phase = phase_at_time(&s->timeline, now, link->quantum);
    state->time_us = now;
    state->is_playing = s->start_stop.is_playing;
    state->start_stop_time = s->start_stop.time_us;
}

void ableton_link_capture_app_state(const ableton_link_t *link,
                                    link_session_state_t *state) {
    if (!link || !state) return;
    
    ableton_link_t *mutable_link = (ableton_link_t *)link;
    
    if (xSemaphoreTake(mutable_link->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int64_t now = get_link_time_us();
        
        state->tempo = link->state.timeline.tempo_bpm;
        state->beat = timeline_beat_at_time(&link->state.timeline, now);
        state->phase = phase_at_time(&link->state.timeline, now, link->quantum);
        state->time_us = now;
        state->is_playing = link->state.start_stop.is_playing;
        state->start_stop_time = link->state.start_stop.time_us;
        
        xSemaphoreGive(mutable_link->state_mutex);
    }
}

double ableton_link_state_tempo(const link_session_state_t *state) {
    return state ? state->tempo : 120.0;
}

void ableton_link_state_set_tempo(link_session_state_t *state, 
                                  double bpm, 
                                  int64_t at_time) {
    if (state) {
        state->tempo = bpm;
        state->time_us = at_time;
    }
}

double ableton_link_state_beat_at_time(const link_session_state_t *state,
                                       int64_t time_us,
                                       double quantum) {
    if (!state || state->tempo <= 0) return 0.0;
    
    double us_per_beat = 60000000.0 / state->tempo;
    double delta_beats = (double)(time_us - state->time_us) / us_per_beat;
    return state->beat + delta_beats;
}

double ableton_link_state_phase_at_time(const link_session_state_t *state,
                                        int64_t time_us,
                                        double quantum) {
    if (!state || quantum <= 0) return 0.0;
    
    double beat = ableton_link_state_beat_at_time(state, time_us, quantum);
    double phase = fmod(beat, quantum);
    if (phase < 0) phase += quantum;
    return phase;
}

int64_t ableton_link_state_time_at_beat(const link_session_state_t *state,
                                        double beat,
                                        double quantum) {
    if (!state || state->tempo <= 0) return 0;
    
    double us_per_beat = 60000000.0 / state->tempo;
    double delta_beats = beat - state->beat;
    return state->time_us + (int64_t)(delta_beats * us_per_beat);
}

void ableton_link_state_request_beat_at_time(link_session_state_t *state,
                                             double beat,
                                             int64_t time_us,
                                             double quantum) {
    if (!state) return;
    
    // Calculate current phase and target phase
    double current_phase = ableton_link_state_phase_at_time(state, time_us, quantum);
    double target_phase = fmod(beat, quantum);
    if (target_phase < 0) target_phase += quantum;
    
    // Calculate phase difference
    double phase_diff = target_phase - current_phase;
    if (phase_diff < 0) phase_diff += quantum;
    
    // Update beat to achieve target phase
    double current_beat = ableton_link_state_beat_at_time(state, time_us, quantum);
    state->beat = current_beat + phase_diff;
    state->time_us = time_us;
}

void ableton_link_state_force_beat_at_time(link_session_state_t *state,
                                           double beat,
                                           int64_t time_us,
                                           double quantum) {
    if (!state) return;
    
    state->beat = beat;
    state->time_us = time_us;
}

bool ableton_link_state_is_playing(const link_session_state_t *state) {
    return state ? state->is_playing : false;
}

void ableton_link_state_set_is_playing(link_session_state_t *state,
                                       bool is_playing,
                                       int64_t time_us) {
    if (state) {
        state->is_playing = is_playing;
        state->start_stop_time = time_us;
    }
}

void ableton_link_commit_audio_state(ableton_link_t *link,
                                     const link_session_state_t *state) {
    if (!link || !state) return;
    
    // Mark pending update for TX task to pick up
    link->audio_state_pending = true;
    
    // Update internal state (will be synced by TX task)
    // This is a simplified approach - real implementation would use
    // lock-free queue for audio thread communication
    if (xSemaphoreTake(link->state_mutex, 0) == pdTRUE) {
        link->state.timeline.tempo_bpm = state->tempo;
        link->state.timeline.beat_origin = beats_to_micro(state->beat);
        link->state.timeline.time_origin = state->time_us;
        link->state.start_stop.is_playing = state->is_playing;
        link->state.start_stop.time_us = state->start_stop_time;
        
        xSemaphoreGive(link->state_mutex);
    }
}

void ableton_link_commit_app_state(ableton_link_t *link,
                                   const link_session_state_t *state) {
    if (!link || !state) return;
    
    if (xSemaphoreTake(link->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        double old_tempo = link->state.timeline.tempo_bpm;
        bool old_playing = link->state.start_stop.is_playing;
        
        link->state.timeline.tempo_bpm = state->tempo;
        link->state.timeline.beat_origin = beats_to_micro(state->beat);
        link->state.timeline.time_origin = state->time_us;
        link->state.start_stop.is_playing = state->is_playing;
        link->state.start_stop.time_us = state->start_stop_time;
        
        // Trigger callbacks if changed
        if (fabs(old_tempo - state->tempo) > 0.01 && link->tempo_callback) {
            link->tempo_callback(link->tempo_callback_data, state->tempo);
        }
        
        if (old_playing != state->is_playing && link->start_stop_callback) {
            link->start_stop_callback(link->start_stop_callback_data, state->is_playing);
        }
        
        xSemaphoreGive(link->state_mutex);
    }
}

int64_t ableton_link_clock_micros(const ableton_link_t *link) {
    (void)link;
    return get_link_time_us();
}

void ableton_link_set_quantum(ableton_link_t *link, double quantum) {
    if (link && quantum > 0) {
        link->quantum = quantum;
    }
}

double ableton_link_get_quantum(const ableton_link_t *link) {
    return link ? link->quantum : 4.0;
}
