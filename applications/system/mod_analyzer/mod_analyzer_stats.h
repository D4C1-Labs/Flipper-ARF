#pragma once

#include <furi.h>

#define MOD_STATS_SAMPLES   128
#define MOD_STATS_PERIOD_US 100

#define MOD_STATS_AM_CARRIER_DB 8.0f

#define MOD_STATS_FM_CARRIER_DB 3.0f

#define MOD_STATS_SNR_DB 10.0f

#define MOD_STATS_FLOOR_NEAR_DB 6.0f

#define MOD_STATS_FLAT_DB 6.0f

#define MOD_STATS_MIN_DECISIVE 6

#define MOD_STATS_MIN_AGREE_PCT 80

#define MOD_STATS_BAND_LOW_DBM  (-74.0f)
#define MOD_STATS_BAND_HIGH_DBM (-36.0f)

typedef enum {
    ModVoteAbstain,
    ModVoteAm,
    ModVoteFm,
} ModVote;

typedef enum {
    ModResultListening,
    ModResultAm,
    ModResultFm,
    ModResultNoSignal,
    ModResultTooWeak,
    ModResultTooStrong,
    ModResultUnstable,
    ModResultInconclusive,
} ModResult;

typedef struct {
    int8_t p10;
    int8_t p50;
    int8_t p90;
    int8_t mid;
    float p90_dbm;

    float carrier_db;

    float floor_db;

    float depth_db;

    uint16_t n_on;
    uint8_t duty_pct;

    uint16_t n_transitions;
    uint32_t on_run_max_us;
    uint8_t n_interior_off_runs;
    bool off_runs_reach_floor;

    ModVote vote_spread;
    ModVote vote_structure;

    uint8_t weight;
    ModVote vote;
} ModWindowStats;

typedef struct {
    uint16_t windows_total;
    uint16_t windows_valid;
    uint16_t windows_decisive;
    uint16_t disagreements;
    uint32_t weight_am;
    uint32_t weight_fm;

    float best_carrier_db;
    float best_fm_carrier_db;
    float best_p90_dbm;
    bool saw_weak_energy;
    bool saw_saturation;
} ModSession;

/** Reduce one capture window to statistics and per-axis votes.
 *
 * @param samples    MOD_STATS_SAMPLES raw RSSI bytes
 * @param count      number of samples
 * @param period_us  the ACHIEVED sample period, not the requested one
 * @param floor_dbm  the learned quiet-channel level; off periods are judged against
 *                   this and not against anything derived from the window itself
 * @param hist       caller-owned 256-entry scratch histogram
 * @param out        result
 */
void mod_stats_window(
    const uint8_t* samples,
    size_t count,
    uint32_t period_us,
    float floor_dbm,
    uint16_t* hist,
    ModWindowStats* out);

bool mod_stats_window_is_valid(const ModWindowStats* w, float floor_dbm);

bool mod_stats_suspect_saturation(const ModWindowStats* w);

void mod_stats_session_reset(ModSession* s);
void mod_stats_session_add(ModSession* s, const ModWindowStats* w, float floor_dbm);

ModResult mod_stats_session_verdict(const ModSession* s, uint8_t* confidence_pct);

const char* mod_stats_result_text(ModResult r);

static inline float mod_stats_raw_to_dbm(uint8_t raw) {
    return ((float)(int8_t)raw) / 2.0f - 74.0f;
}
