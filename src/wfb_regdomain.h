#include <net/regulatory.h>
#include <linux/nl80211.h> 

static const struct ieee80211_regdomain wfb_regdom = {
    .alpha2 = "WB",
    .dfs_region = NL80211_DFS_FCC,
    .n_reg_rules = 4,
    .reg_rules = {
        /* Bande 2.4 GHz (Canaux 1 à 11) */
        {
            .freq_range = {
                .start_freq_khz = 2412 * 1000,
                .end_freq_khz = 2462 * 1000,
                .max_bandwidth_khz = 40 * 1000,
            },
            .power_rule = {
                .max_antenna_gain = 6,
                .max_eirp = 30,
            },
            .flags = 0,
        },
        /* Bande 5 GHz UNII-1 (Canaux 36 à 48) */
        {
            .freq_range = {
                .start_freq_khz = 5180 * 1000,
                .end_freq_khz = 5240 * 1000,
                .max_bandwidth_khz = 80 * 1000,
            },
            .power_rule = {
                .max_antenna_gain = 6,
                .max_eirp = 30,
            },
            .flags = 0,
        },
        /* Bande 5 GHz UNII-2A avec DFS obligatoire (Canaux 52 à 64) */
        {
            .freq_range = {
                .start_freq_khz = 5260 * 1000,
                .end_freq_khz = 5320 * 1000,
                .max_bandwidth_khz = 80 * 1000,
            },
            .power_rule = {
                .max_antenna_gain = 6,
                .max_eirp = 24,
            },
            .flags = NL80211_RRF_DFS,
        },
        /* Bande 5 GHz UNII-3 (Canaux 149 à 165) */
        {
            .freq_range = {
                .start_freq_khz = 5745 * 1000,
                .end_freq_khz = 5825 * 1000,
                .max_bandwidth_khz = 80 * 1000,
            },
            .power_rule = {
                .max_antenna_gain = 6,
                .max_eirp = 30,
            },
            .flags = 0,
        },
    },
};

