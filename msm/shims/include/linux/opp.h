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
 * We can take this even further by using the devm_icc_of_get function in combination with
 * our new this key realization to make sure no memory gets leaked whenever uninit/clean-up
 * is called on the dev.
 *
 * IMPLEMENTATION:
 * By making a tracker system:
 * - We can track each and every dev inside a global list.
 * - Add the required level of abstraction for drivers without needing new fields, making
 *   that much more scalable it.
 *
 * NOTE: This implementation can be changed later, right now it works really well.
 * devres_set_drvdata isn't used here as it has a tendency to collide or get overwritten
 * when multiple devs try to register under the same platform context.
 *
 * devm_pm_opp_of_add_table():
 * Parse the DT's of the dev and check if interconnects and clocks are present right after
 * dev_pm_opp_of_add_table() creates the opp-table.
 * - If both exist, store them both.
 * - If one or the other is missing, store whatever is present.
 * - If neither are present create the opp-table regardless and just return.
 *
 * dev_pm_opp_set_opp():
 * - Look up the dev in our global tracker list.
 * - Extract the properties ("opp-hz", "opp-peak-kBps") for the requested OPP target node.
 *   (standard OPP operations)
 * - Scale all the clocks.
 * - Scale all the interconnects(msm-bus API handles the opp-level implicitly for us)
 */

#include <linux/pm_opp.h>
#include "interconnector.h"

/**
 * struct msm_opp_resources - Tracks dynamically allocated hardware handles for OPP tracking
 * @clk: Captured clock handle if present in device tree
 * @clk_enabled: Runtime state variable tracking clock status
 * @num_paths: Number of interconnect lanes extracted from DT
 * @paths: Flexible array mapping modern OPP votes to underlying interconnect nodes
 */

struct msm_opp_resources {
	struct clk *clk;
	bool clk_enabled;

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
