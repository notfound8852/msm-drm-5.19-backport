#ifndef OPP_HANDLE_H
#define OPP_HANDLE_H

/*
 * OPP shim - the mainline dev_pm_opp_* entry points the MSM driver calls, on top
 * of the OPP core downstream ships.
 *
 * Downstream parses "opp-hz" and the supply properties and nothing else: struct
 * dev_pm_opp has no level field and no bandwidth array. What the core declined
 * to parse is recovered from opp->np, and the clock and interconnect handles
 * live in a private devres node per device.
 *
 * See Documentation/core/opp.md for the rationale, the DT properties involved,
 * and where behaviour deliberately differs from mainline.
 */

#include <linux/pm_opp.h>
#include <linux/version.h>
#include "interconnector.h"

/*
 * The shrink list.
 *
 * Each entry is something this shim tracks only because the OPP core of the
 * base kernel does not. The OPP subsystem picked these up one at a time on the
 * way to 5.11, so as the floor rises the core starts carrying them itself, the
 * matching read drops out, and the probe-time cache shrinks with it. Support
 * for a range of base kernels is a matter of tracking less, not of tracking
 * differently.
 *
 * This block is the only place the versions are written down - adjust here, not
 * at the use sites. They are this shim's record and have only ever been
 * exercised at the bottom of the range; verify against the target tree before
 * raising the floor past one.
 *
 * Not listed, because it never needs a gate: the bandwidth properties are read
 * off opp->np, and opp->np is kept by every version from 4.19 up. The core
 * gained opp->bandwidth[] in 5.5 but no plain getter to switch to, so the DT
 * read stays correct either way.
 */
#define OPP_CORE_HAS_LEVEL	(LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0))

/**
 * struct _opp_target - One OPP flattened into the values we actually commit
 * @freq: Rate from "opp-hz", in Hz
 * @level: Performance level from "opp-level", 0 if undeclared
 * @avg_bw: Average bandwidth for every path, in bytes/sec, 0 if undeclared
 * @peak_bw: Peak bandwidth for every path, in bytes/sec, 0 if undeclared
 *
 * A zero @peak_bw means the OPP declared no bandwidth, which leaves any standing
 * vote alone rather than dropping it to zero.
 */
struct _opp_target {
	unsigned long freq;
	u32 level;
	u64 avg_bw;
	u64 peak_bw;
};

/**
 * struct _opp_resources - Hardware handles owned by one device's OPP shim
 * @clk: Clock to scale, NULL if the device has none
 * @clk_name: con_id recorded by devm_pm_opp_set_clkname(), NULL for index 0
 * @clk_enabled: Whether the shim currently holds an enable on @clk
 * @populated: Set once devm_pm_opp_of_add_table() has claimed the handles
 * @cur: Last target committed by dev_pm_opp_set_opp(), all zero if none
 * @cache: Every OPP in the table, flattened at probe, ascending by @freq
 * @cache_count: Entries in @cache
 * @max_paths: Interconnect paths this node was sized for, from DT
 * @num_paths: Interconnect paths actually acquired, <= @max_paths
 * @paths: Flexible array of acquired interconnect paths
 *
 * Local stand-in for mainline's struct opp_table, which cannot be borrowed from
 * a module. Payload of a devres node whose release function doubles as its
 * lookup key; the node owns @clk and @paths outright, so its release is the
 * single teardown point for both.
 */

struct _opp_resources {
	struct clk *clk;
	const char *clk_name;
	bool clk_enabled;
	bool populated;

	struct _opp_target cur;

	struct _opp_target *cache;
	u32 cache_count;

	u32 max_paths;
	u32 num_paths;
	struct icc_path *paths[];
};

/**
 * devm_pm_opp_of_add_table() - Add the device's OPP table and claim its handles
 * @dev: Device to add the table for
 *
 * As mainline. Additionally resolves the clock and the interconnect paths the
 * table's OPPs refer to; all of it is released at unbind.
 *
 * Call after devm_pm_opp_set_clkname() if the clock to scale is not index 0.
 *
 * Return: 0 on success, negative errno otherwise.
 */
int devm_pm_opp_of_add_table(struct device *dev);

/**
 * dev_pm_opp_set_opp() - Apply an operating point
 * @dev: Device to apply it to
 * @opp: OPP to apply, or NULL to drop every vote
 *
 * Scales bandwidth, voltage and clock in mainline's order: bandwidth first when
 * scaling up, last when scaling down. A failed transition is unwound to the
 * previous vote.
 *
 * Return: 0 on success - including when @dev has no handles to scale - and a
 * negative errno if the transition failed and was unwound.
 */
int dev_pm_opp_set_opp(struct device *dev, struct dev_pm_opp *opp);

#if !OPP_CORE_HAS_LEVEL
/**
 * dev_pm_opp_get_level() - Read an OPP's "opp-level"
 * @opp: OPP to read
 *
 * Return: The level, or 0 if the OPP does not declare one.
 */
unsigned int dev_pm_opp_get_level(struct dev_pm_opp *opp);
#endif

/**
 * devm_pm_opp_set_supported_hw() - Managed dev_pm_opp_set_supported_hw()
 * @dev: Device to set the supported hardware versions for
 * @versions: Array of hierarchy version values
 * @count: Number of entries in @versions
 *
 * Return: 0 on success, negative errno otherwise.
 */
int devm_pm_opp_set_supported_hw(struct device *dev,
				 const u32 *versions,
				 unsigned int count);

/**
 * devm_pm_opp_set_clkname() - Managed dev_pm_opp_set_clkname()
 * @dev: Device to set the clock name for
 * @name: con_id of the clock, NULL for index 0
 *
 * Also records @name so a subsequent devm_pm_opp_of_add_table() scales that
 * clock. Has no effect on which clock is scaled if called after it.
 *
 * Return: 0 on success, negative errno otherwise.
 */
int devm_pm_opp_set_clkname(struct device *dev,
			    const char *name);

#endif
