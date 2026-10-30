#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUDDY_HOST_ID_LEN          6
#define BUDDY_TOOL_NAME_MAX        32
#define BUDDY_WORKSPACE_MAX        48
#define BUDDY_MODEL_NAME_MAX       48
#define BUDDY_MSG_PREVIEW_MAX      16
#define BUDDY_TOOL_HIST_NAME_MAX   11
#define BUDDY_BRIGHTNESS_MIN       10
#define BUDDY_BRIGHTNESS_MAX       100
#define BUDDY_MASCOT_COUNT         24

#define BUDDY_PAIR_REQ             0xE0
#define BUDDY_UNPAIR               0xE1
#define BUDDY_PAIR_ACCEPTED        0xE0
#define BUDDY_PAIR_REJECTED        0xE1
#define BUDDY_PAIR_PENDING         0xE2
#define BUDDY_BRIGHTNESS           0xFE
#define BUDDY_ORIENTATION          0xFD
#define BUDDY_WORKSPACE            0xFC
#define BUDDY_MSG_PREVIEW          0xFB
#define BUDDY_STATS                0xFA
#define BUDDY_MODEL                0xF9
#define BUDDY_SUBAGENT             0xF8
#define BUDDY_EVENT                0xF7
#define BUDDY_TIME_HINT            0xF6
#define BUDDY_TOOL_HIST            0xF5
#define BUDDY_APPROVE              0xF0
#define BUDDY_DENY                 0xF1
#define BUDDY_SKIP                 0xF2

typedef enum {
    BUDDY_KIND_NONE = 0,
    BUDDY_KIND_PAIR_REQ,
    BUDDY_KIND_UNPAIR,
    BUDDY_KIND_BRIGHTNESS,
    BUDDY_KIND_ORIENTATION,
    BUDDY_KIND_WORKSPACE,
    BUDDY_KIND_MODEL,
    BUDDY_KIND_STATS,
    BUDDY_KIND_SUBAGENT,
    BUDDY_KIND_EVENT,
    BUDDY_KIND_TIME_HINT,
    BUDDY_KIND_MSG_PREVIEW,
    BUDDY_KIND_TOOL_HIST,
    BUDDY_KIND_TOOL_HIST_CLEAR,
    BUDDY_KIND_AGENT,
} buddy_kind_t;

typedef struct {
    buddy_kind_t kind;
    uint8_t host_id[BUDDY_HOST_ID_LEN];
    uint8_t brightness;
    uint8_t orientation;
    uint8_t source_id;
    uint8_t status_id;
    uint8_t tool_len;
    char tool[BUDDY_TOOL_NAME_MAX + 1];
    uint8_t workspace_len;
    char workspace[BUDDY_WORKSPACE_MAX + 1];
    uint8_t model_len;
    char model[BUDDY_MODEL_NAME_MAX + 1];
    uint8_t active_sessions;
    uint8_t total_sessions;
    uint16_t tool_calls;
    uint8_t duration_min;
    uint8_t subagent_count;
    uint8_t event_id;
    uint8_t hour;
    uint8_t msg_index;
    uint8_t msg_total;
    uint8_t hist_index;
    uint8_t hist_name_len;
    char hist_name[BUDDY_TOOL_HIST_NAME_MAX + 1];
    bool hist_success;
} buddy_frame_t;

typedef enum {
    BUDDY_SCENE_SLEEP = 0,
    BUDDY_SCENE_WORK,
    BUDDY_SCENE_ALERT,
    BUDDY_SCENE_QUESTION,
    BUDDY_SCENE_COUNT,
} buddy_scene_t;

bool buddy_parse_frame(const uint8_t *data, size_t len, buddy_frame_t *out);
buddy_scene_t buddy_status_to_scene(uint8_t status);
uint8_t buddy_clamp_brightness(uint8_t percent);
uint8_t buddy_clamp_orientation(uint8_t orientation);
bool buddy_host_id_eq(const uint8_t *a, const uint8_t *b);

#ifdef __cplusplus
}
#endif
