#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H
#include <linux/regulator/consumer.h>

/*
 * This WAS a testing file. It's quite useless and will be deleted as it is literally never used.
 * If you were looking for OPP-tables check ./pm_opp.h and ./builds/pm_opp.c
*/
// Update: This file is fully obsolete, now.
static inline int mdss_gdsc_enable(void){
    int ret = 0;
    struct regulator *gdsc;

    gdsc = regulator_get(NULL, "mdss_core_gdsc");

    if (IS_ERR(gdsc)) {
        pr_err("Failed to find GDSC regulator\n");
        return PTR_ERR(gdsc);
    }

    // Enable the power domain
    ret = regulator_enable(gdsc);
    if (ret < 0) {
        pr_err("Failed to enable GDSC\n");
    }
    return ret;
}

static inline int mdss_gdsc_disable(void){
    int ret = 0;
    struct regulator *gdsc;

    gdsc = regulator_get(NULL, "mdss_core_gdsc");

    if (IS_ERR(gdsc)) {
        pr_err("Failed to find GDSC regulator\n");
        return PTR_ERR(gdsc);
    }

    // Disable the power domain
    ret = regulator_disable(gdsc);
    if (ret < 0) {
        pr_err("Failed to enable GDSC\n");
    }
    return ret;
}
#endif
