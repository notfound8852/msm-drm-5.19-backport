#ifndef OPP_HANDLE_H
#define OPP_HANDLE_H

/**
 *
 * Upstream/mainline kernels completely overhauled the OPP subsystem around Linux 5.11+
 * Instead of the drivers handling clocks, power domains, and interconnects explicitly,
 * modern OPP binds them into a single, unified node in the DTB. So when a driver calls
 * dev_pm_opp_set_opp(), the core kernel atomically scales the clock speed, hooks into
 * the generic power domain (genpd) for voltage, and scales peak/avg bandwidth using the
 * modern Interconnect (ICC) subsystem(introduced in >= Linux 5.1).
 *
 * - opp-hz             -> refer to clock speeds.
 * - opp-level          -> refers to the voltage level.
 * - opp-peak-kBps      -> refers to the interconnect B/W.
 *
 * PROBLEM: We don't have any of this... The core helpers are "devm_pm_opp_of_add_table",
 * "dev_pm_opp_set_opp", "dev_pm_opp_get_level".
 *
 * NOTE: Fortunately, we can trace how the new new helpers work. dev_pm_opp_set_opp()
 * simply acts as a selector in the opp-table of a dev. It's responsible for voting for
 * the power, the clk rates, the B/W.
 *
 * FIX: Downstream(4.19) has dev_pm_opp_of_add_table in the case for
 * devm_pm_opp_of_add_table allowing us to combine this with devm_add_action_or_reset()
 * to create the self clean-up devm version. And luckily most of the base dev versions
 * exist allowing us to shim the rest as well. dev_pm_opp_get_level() can be handled by
 * manually parsing the dtb.
 *
 * CORE PROBLEM: dev_pm_opp_set_opp()
 *
 * HACK: There is a key feature about the downstream msm-bus API that's is almost given
 * no value and sometimes even viewed as a negative. Whenever it is invoked, it implicitly
 * drops the exact required voltage/corner vote onto the hardware power rails to support
 * that traffic level... Every. Single. Time. Letting us completely bypass the need to a
 * implement a complex GenPD voltage flipping sub-system.
 *
 * We can take this even further by parking every handle we grab in a devres node, so no
 * memory gets leaked whenever uninit/clean-up is called on the dev.
 *
 * IMPLEMENTATION:
 * By hanging a private devres node off each dev:
 * - We get per-dev storage without a global list, a lock, or new driver fields.
 * - Lifetime is the dev's lifetime, so teardown is automatic and correctly ordered.
 * - The node owns the clk and the icc paths outright, so there is exactly one place
 *   that releases them.
 *
 * NOTE: devres_set_drvdata isn't used here as it has a tendency to collide or get
 * overwritten when multiple devs try to register under the same platform context.
 * Instead the resources live in a devres node of our own, looked up with
 * devres_find() keyed on our private release function. That key is per-device AND
 * private to this shim, so the GPU, the GMU, the DPU and the DSI host each get an
 * independent slot that nothing outside opp.c can address, let alone overwrite.
 * The node is also what owns the clock and the interconnect paths, so unbind
 * releases them in the right order without a second bookkeeping structure.
 *
 * devm_pm_opp_of_add_table():
 * Parse the DT's of the dev and check if interconnects and clocks are present right after
 * dev_pm_opp_of_add_table() creates the opp-table.
 * - If both exist, store them both.
 * - If one or the other is missing, store whatever is present.
 * - If neither are present create the opp-table regardless and just return.
 *
 * dev_pm_opp_set_opp():
 * - Look up the dev's resource node.
 * - Extract the properties ("opp-hz", "opp-peak-kBps") for the requested OPP target node.
 *   (standard OPP operations)
 * - Scale all the clocks.
 * - Scale all the interconnects(msm-bus API handles the opp-level implicitly for us)
 * - Order the two the way mainline _set_opp() does: bandwidth first when scaling up,
 *   last when scaling down, so the part never runs fast on a bus that has not heard
 *   about it yet.
 */

#include <linux/pm_opp.h>
#include "interconnector.h"

/**
 * struct _opp_target - One OPP flattened into the values we actually commit
 * @freq: Rate from "opp-hz", in Hz
 * @avg_bw: Average bandwidth for every path, in bytes/sec, 0 if undeclared
 * @peak_bw: Peak bandwidth for every path, in bytes/sec, 0 if undeclared
 *
 * 4.19's struct dev_pm_opp carries the rate and nothing else we need - there is
 * no level field and no bandwidth array, both landed in 5.5 and later - so this
 * has to be re-read from opp->np on every transition. A copy kept in
 * _opp_resources::cur stands in for mainline's opp_table->current_opp.
 */
struct _opp_target {
	unsigned long freq;
	u64 avg_bw;
	u64 peak_bw;
};

/**
 * struct _opp_resources - Hardware handles owned by one device's OPP shim
 * @clk: Captured clock handle if present in device tree
 * @clk_name: con_id recorded by devm_pm_opp_set_clkname(), NULL for index 0
 * @clk_enabled: Runtime state variable tracking clock status
 * @populated: Set once devm_pm_opp_of_add_table() has claimed the handles
 * @cur: Last target committed by dev_pm_opp_set_opp(), all zero if none
 * @max_paths: Interconnect lanes this node was sized for, from DT
 * @num_paths: Interconnect lanes actually acquired, <= @max_paths
 * @paths: Flexible array mapping modern OPP votes to underlying interconnect nodes
 *
 * This stands in for mainline's struct opp_table, which we cannot borrow: it is
 * defined in drivers/opp/opp.h with no exported accessor, so it can be neither
 * held nor extended from out here. Same role though - it is what _set_opp()
 * takes so the public entry point stays a lookup and a delegate.
 *
 * It is the payload of a devres node whose release function doubles as its
 * lookup key; see the discussion above. The node owns @clk and @paths outright,
 * so its release is the single teardown point for both.
 *
 * NOTE: @cur is what lets us mirror mainline _set_opp() ordering (bandwidth
 * first when scaling up, last when scaling down) and roll a half-applied
 * transition back to the previous vote instead of to zero.
 */

struct _opp_resources {
	struct clk *clk;
	const char *clk_name;
	bool clk_enabled;
	bool populated;

	struct _opp_target cur;

	u32 max_paths;
	u32 num_paths;
	struct icc_path *paths[];
};

int devm_pm_opp_of_add_table(struct device *dev);

int dev_pm_opp_set_opp(struct device *dev, struct dev_pm_opp *opp);

unsigned int dev_pm_opp_get_level(struct dev_pm_opp *opp);

int devm_pm_opp_set_supported_hw(struct device *dev,
				 const u32 *versions,
				 unsigned int count);

int devm_pm_opp_set_clkname(struct device *dev,
			    const char *name);

#endif
