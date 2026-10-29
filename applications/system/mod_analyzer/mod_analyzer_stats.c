#include "mod_analyzer_stats.h"

#include <string.h>

static int8_t mod_stats_percentile(const uint16_t* hist, uint32_t total, uint32_t permille) {
    const uint32_t want = (total * permille) / 1000;
    uint32_t seen = 0;
    for(int32_t v = -128; v <= 127; v++) {
        seen += hist[(uint8_t)(int8_t)v];
        if(seen > want) return (int8_t)v;
    }
    return 127;
}

static bool mod_stats_level(const uint8_t* s, size_t count, size_t i, int8_t mid) {
    const bool cur = (int8_t)s[i] > mid;
    if(i == 0 || i + 1 >= count) return cur;
    const bool prev = (int8_t)s[i - 1] > mid;
    const bool next = (int8_t)s[i + 1] > mid;
    return (prev && cur) || (cur && next) || (prev && next);
}

void mod_stats_window(
    const uint8_t* samples,
    size_t count,
    uint32_t period_us,
    float floor_dbm,
    uint16_t* hist,
    ModWindowStats* out) {
    memset(out, 0, sizeof(*out));
    if(count == 0) return;

    /* --- Levels ---------------------------------------------------------------- */
    memset(hist, 0, 256 * sizeof(uint16_t));
    for(size_t i = 0; i < count; i++)
        hist[samples[i]]++;

    out->p10 = mod_stats_percentile(hist, count, 100);
    out->p50 = mod_stats_percentile(hist, count, 500);
    out->p90 = mod_stats_percentile(hist, count, 900);
    out->p90_dbm = mod_stats_raw_to_dbm((uint8_t)out->p90);
    out->mid = (int8_t)(((int32_t)out->p10 + (int32_t)out->p90) / 2);

    uint32_t n_on = 0, n_off = 0;
    memset(hist, 0, 256 * sizeof(uint16_t));
    for(size_t i = 0; i < count; i++) {
        if((int8_t)samples[i] > out->mid) {
            hist[samples[i]]++;
            n_on++;
        }
    }
    if(n_on) {
        const int8_t c10 = mod_stats_percentile(hist, n_on, 100);
        const int8_t c90 = mod_stats_percentile(hist, n_on, 900);
        out->carrier_db = ((float)c90 - (float)c10) / 2.0f;
    }

    memset(hist, 0, 256 * sizeof(uint16_t));
    for(size_t i = 0; i < count; i++) {
        if((int8_t)samples[i] <= out->mid) {
            hist[samples[i]]++;
            n_off++;
        }
    }
    if(n_off) {
        const int8_t f10 = mod_stats_percentile(hist, n_off, 100);
        const int8_t f90 = mod_stats_percentile(hist, n_off, 900);
        out->floor_db = ((float)f90 - (float)f10) / 2.0f;
    }

    out->n_on = (uint16_t)n_on;
    out->duty_pct = (uint8_t)((n_on * 100) / count);
    out->depth_db = ((float)out->p90 - (float)out->p10) / 2.0f;

    const int32_t floor_raw = (int32_t)((floor_dbm + 74.0f) * 2.0f);
    const int32_t floor_near = floor_raw + (int32_t)(MOD_STATS_FLOOR_NEAR_DB * 2.0f);

    bool prev = mod_stats_level(samples, count, 0, out->mid);
    size_t run_start = 0;
    uint32_t on_run_max = 0;
    uint8_t interior_off = 0;
    bool off_reaches_floor = false;
    int8_t run_min = (int8_t)samples[0];

    for(size_t i = 1; i <= count; i++) {
        const bool cur = (i < count) ? mod_stats_level(samples, count, i, out->mid) : !prev;
        if(i < count && (int8_t)samples[i] < run_min) run_min = (int8_t)samples[i];

        if(cur == prev && i < count) continue;

        const size_t run_len = i - run_start;
        const bool touches_edge = (run_start == 0) || (i == count);

        if(prev) {
            const uint32_t us = (uint32_t)run_len * period_us;
            if(us > on_run_max) on_run_max = us;
        } else if(!touches_edge) {
            interior_off++;
            if((int32_t)run_min <= floor_near) off_reaches_floor = true;
        }

        if(i < count) out->n_transitions++;
        prev = cur;
        run_start = i;
        run_min = (i < count) ? (int8_t)samples[i] : 0;
    }
    out->on_run_max_us = on_run_max;
    out->n_interior_off_runs = interior_off;
    out->off_runs_reach_floor = off_reaches_floor;

    if(out->carrier_db >= MOD_STATS_AM_CARRIER_DB) {
        out->vote_spread = ModVoteAm;
    } else if(out->carrier_db <= MOD_STATS_FM_CARRIER_DB) {
        out->vote_spread = ModVoteFm;
    }

    if(out->n_interior_off_runs >= 2 && out->off_runs_reach_floor) {
        out->vote_structure = ModVoteAm;
    } else if(out->depth_db < MOD_STATS_FLAT_DB && out->p90_dbm >= floor_dbm + MOD_STATS_SNR_DB) {
        out->vote_structure = ModVoteFm;
    }

    if(out->vote_spread != ModVoteAbstain && out->vote_spread == out->vote_structure) {
        out->vote = out->vote_spread;
        out->weight = 2;
    } else if(out->vote_spread != ModVoteAbstain && out->vote_structure == ModVoteAbstain) {
        out->vote = out->vote_spread;
        out->weight = 1;
    } else if(out->vote_structure != ModVoteAbstain && out->vote_spread == ModVoteAbstain) {
        out->vote = out->vote_structure;
        out->weight = 1;
    } else {
        out->vote = ModVoteAbstain;
        out->weight = 0;
    }
}

bool mod_stats_window_is_valid(const ModWindowStats* w, float floor_dbm) {
    return w->p90_dbm >= floor_dbm + MOD_STATS_SNR_DB;
}

bool mod_stats_suspect_saturation(const ModWindowStats* w) {
    return w->carrier_db < MOD_STATS_FM_CARRIER_DB && w->duty_pct > 80 && w->p90_dbm > -30.0f;
}

void mod_stats_session_reset(ModSession* s) {
    memset(s, 0, sizeof(*s));
    s->best_p90_dbm = -200.0f;
    s->best_fm_carrier_db = 1000.0f;
}

void mod_stats_session_add(ModSession* s, const ModWindowStats* w, float floor_dbm) {
    s->windows_total++;

    if(!mod_stats_window_is_valid(w, floor_dbm)) {
        if(w->p90_dbm >= floor_dbm + MOD_STATS_SNR_DB - 2.0f) s->saw_weak_energy = true;
        return;
    }
    s->windows_valid++;

    if(mod_stats_suspect_saturation(w)) s->saw_saturation = true;

    if(w->weight == 0) {
        if(w->vote_spread != ModVoteAbstain && w->vote_structure != ModVoteAbstain) {
            s->disagreements++;
        }
        return;
    }
    s->windows_decisive++;
    if(w->vote == ModVoteAm) {
        s->weight_am += w->weight;
        if(w->carrier_db > s->best_carrier_db) s->best_carrier_db = w->carrier_db;
    } else {
        s->weight_fm += w->weight;
        if(w->carrier_db < s->best_fm_carrier_db) s->best_fm_carrier_db = w->carrier_db;
    }
    if(w->p90_dbm > s->best_p90_dbm) s->best_p90_dbm = w->p90_dbm;
}

ModResult mod_stats_session_verdict(const ModSession* s, uint8_t* confidence_pct) {
    if(confidence_pct) *confidence_pct = 0;

    if(s->windows_valid == 0) {
        if(s->windows_total == 0) return ModResultListening;
        return s->saw_weak_energy ? ModResultTooWeak : ModResultNoSignal;
    }

    if(s->windows_decisive < MOD_STATS_MIN_DECISIVE) {
        if(s->disagreements >= MOD_STATS_MIN_DECISIVE) {
            return s->saw_saturation ? ModResultTooStrong : ModResultUnstable;
        }
        return ModResultListening;
    }

    const uint32_t total = s->weight_am + s->weight_fm;
    if(total == 0) return ModResultInconclusive;

    const uint32_t winner = (s->weight_am > s->weight_fm) ? s->weight_am : s->weight_fm;
    const uint32_t agree_pct = (winner * 100) / total;
    if(agree_pct < MOD_STATS_MIN_AGREE_PCT) {
        return s->saw_saturation ? ModResultTooStrong : ModResultUnstable;
    }

    float conf = (float)agree_pct / 100.0f;

    const float count_factor =
        (s->windows_decisive >= MOD_STATS_MIN_DECISIVE * 2) ?
            1.0f :
            (float)s->windows_decisive / (float)(MOD_STATS_MIN_DECISIVE * 2);
    conf *= 0.6f + 0.4f * count_factor;

    const float margin = (s->weight_am > s->weight_fm) ?
                             (s->best_carrier_db - MOD_STATS_AM_CARRIER_DB) :
                             (MOD_STATS_FM_CARRIER_DB - s->best_fm_carrier_db);
    const float margin_factor = (margin >= 4.0f) ? 1.0f : (0.7f + 0.3f * (margin / 4.0f));
    conf *= (margin_factor > 0.7f) ? margin_factor : 0.7f;

    if(s->best_p90_dbm < MOD_STATS_BAND_LOW_DBM || s->best_p90_dbm > MOD_STATS_BAND_HIGH_DBM) {
        conf *= 0.75f;
    }
    if(s->saw_saturation) conf *= 0.6f;

    if(confidence_pct) {
        int32_t pct = (int32_t)(conf * 100.0f + 0.5f);
        if(pct < 0) pct = 0;
        if(pct > 100) pct = 100;
        *confidence_pct = (uint8_t)pct;
    }

    return (s->weight_am > s->weight_fm) ? ModResultAm : ModResultFm;
}

const char* mod_stats_result_text(ModResult r) {
    switch(r) {
    case ModResultAm:
        return "AM";
    case ModResultFm:
        return "FM";
    case ModResultNoSignal:
        return "No signal";
    case ModResultTooWeak:
        return "Too weak";
    case ModResultTooStrong:
        return "Too strong";
    case ModResultUnstable:
        return "Unstable";
    case ModResultInconclusive:
        return "Unclear";
    case ModResultListening:
    default:
        return "Listening";
    }
}
