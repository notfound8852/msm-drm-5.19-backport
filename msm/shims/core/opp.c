#include <linux/device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include "linux/opp.h"


/* Devres cleanup wrappers */
static void devm_pm_opp_of_table_release(void *data) { dev_pm_opp_of_remove_table(data); }
static void devm_pm_opp_supported_hw_release(void *data) { dev_pm_opp_put_supported_hw(data); }
static void devm_pm_opp_clkname_release(void *data) { dev_pm_opp_put_clkname(data); }

/* Resource node */

/*
 * Release for the per-device resource node. Its address is also the key
 * devres_find() matches on, which is what makes the slot private to this shim:
 * no other subsystem can name this function, so nothing else can find, replace
 * or free the node the way a shared drvdata pointer invites.
 *
 * Everything the node owns is torn down here, in order, from one place.
 */
static void _opp_res_release(struct device *dev, void *data)
{
	struct _opp_resources *res = data;
	u32 i;

	for (i = 0; i < res->num_paths; i++)
		icc_put(res->paths[i]);

	if (res->clk) {
		/*
		 * A device can be unbound with an OPP still applied - nothing
		 * guarantees dev_pm_opp_set_opp(dev, NULL) ran first - so drop
		 * our enable before letting go of the reference.
		 */
		if (res->clk_enabled)
			clk_disable_unprepare(res->clk);

		clk_put(res->clk);
	}

	kfree(res->clk_name);
}

static struct _opp_resources *_find_opp_res(struct device *dev)
{
	return devres_find(dev, _opp_res_release, NULL, NULL);
}

static u32 _get_icc_path_count(struct device_node *np)
{
	const __be32 *data;
	int len;

	data = of_get_property(np, "interconnects", &len);
	if (!data || len <= 0)
		return 0;

	/* Each entry = <src dst>; a stray cell means the property is junk. */
	if (len % (sizeof(u32) * 2))
		return 0;

	return len / (sizeof(u32) * 2);
}

/*
 * Fetch the node for @dev, creating it on first use. devm_pm_opp_set_clkname()
 * and devm_pm_opp_of_add_table() both come through here so the two can be
 * called in either order; the path count comes from the device tree, which is
 * readable at either entry point, so the node is always sized correctly no
 * matter who gets here first.
 */
static struct _opp_resources *_get_opp_res(struct device *dev)
{
	struct _opp_resources *res;
	size_t size;
	u32 path_count;

	res = _find_opp_res(dev);
	if (res)
		return res;

	path_count = _get_icc_path_count(dev->of_node);

	size = sizeof(*res) +
	       path_count * sizeof(struct icc_path *);

	res = devres_alloc(_opp_res_release, size, GFP_KERNEL);
	if (!res)
		return ERR_PTR(-ENOMEM);

	res->max_paths = path_count;

	devres_add(dev, res);

	return res;
}

/* Function helpers */

/*
 * Flatten @opp into what we can actually commit. Mainline reads opp->rate and
 * opp->bandwidth[] straight out of the OPP; 4.19 only has the rate, so the
 * bandwidth comes back off the node every time.
 *
 * An OPP that declares no bandwidth resolves to a zero peak, which is the
 * signal to leave whatever vote is standing alone - these tables mix rate-only
 * and rate-plus-bandwidth nodes, and collapsing to zero on the former starves
 * the bus mid-stream.
 */
static void _read_opp_target(struct _opp_resources *res,
			     struct dev_pm_opp *opp,
			     struct _opp_target *target)
{
	struct device_node *np;
	u32 peak_kbps = 0;
	u32 avg_kbps = 0;

	memset(target, 0, sizeof(*target));

	target->freq = dev_pm_opp_get_freq(opp);

	if (!res->num_paths)
		return;

	np = dev_pm_opp_get_of_node(opp);
	if (!np)
		return;

	of_property_read_u32(np, "opp-peak-kBps", &peak_kbps);
	of_property_read_u32(np, "opp-avg-kBps", &avg_kbps);

	of_node_put(np);

	if (!peak_kbps)
		return;

	target->peak_bw = KBps_to_icc(peak_kbps);
	target->avg_bw = KBps_to_icc(avg_kbps ?
				     avg_kbps :
				     peak_kbps);
}

/*
 * Commit @target's bandwidth to every path. A path that rejects the vote leaves
 * the ones before it already updated, so walk back and restore them to the
 * last vote that was known good rather than dropping them to zero.
 */
static int _set_opp_bw(struct _opp_resources *res,
		       const struct _opp_target *target)
{
	u32 i;
	int ret;

	for (i = 0; i < res->num_paths; i++) {
		ret = icc_set_bw(res->paths[i],
				 target->avg_bw,
				 target->peak_bw);
		if (ret) {
			while (i--)
				icc_set_bw(res->paths[i],
					   res->cur.avg_bw,
					   res->cur.peak_bw);
			return ret;
		}
	}

	res->cur.avg_bw = target->avg_bw;
	res->cur.peak_bw = target->peak_bw;

	return 0;
}

/*
 * @enabled_here reports whether this call was the one that took the clock from
 * off to on, so the caller can unwind its own work without stealing an enable
 * that an earlier dev_pm_opp_set_opp() is still relying on.
 */
static int _set_opp_clk(struct _opp_resources *res,
			unsigned long freq,
			bool *enabled_here)
{
	int ret;

	if (!res->clk)
		return 0;

	if (!res->clk_enabled) {
		ret = clk_prepare_enable(res->clk);
		if (ret)
			return ret;

		res->clk_enabled = true;
		*enabled_here = true;
	}

	ret = clk_set_rate(res->clk, freq);
	if (ret && *enabled_here) {
		clk_disable_unprepare(res->clk);
		res->clk_enabled = false;
		*enabled_here = false;
	}

	return ret;
}

static void _disable_opp_clk(struct _opp_resources *res)
{
	if (res->clk && res->clk_enabled) {
		clk_disable_unprepare(res->clk);
		res->clk_enabled = false;
	}
}

/* Mainline's _set_opp(dev, opp_table, NULL, ...) tail: drop every vote. */
static int _disable_opp(struct _opp_resources *res)
{
	const struct _opp_target off = { };

	_set_opp_bw(res, &off);
	_disable_opp_clk(res);

	res->cur = off;

	return 0;
}

/*
 * The transition proper. @res is what mainline passes an opp_table for - the
 * caller has already resolved it, so this only has to apply.
 *
 * Ordering mirrors mainline _set_opp(): widen the bus before speeding the clock
 * up, narrow it only after slowing the clock down, so the hardware is never
 * fast on a bus that has not been told about it. An unknown starting point
 * counts as scaling up.
 */
static int _set_opp(struct device *dev,
		    struct _opp_resources *res,
		    struct dev_pm_opp *opp)
{
	struct _opp_target target;
	struct _opp_target old;
	bool enabled_here = false;
	bool scaling_down;
	int ret;

	if (!opp)
		return _disable_opp(res);

	_read_opp_target(res, opp, &target);

	old = res->cur;
	scaling_down = old.freq && target.freq < old.freq;

	if (target.peak_bw && !scaling_down) {
		ret = _set_opp_bw(res, &target);
		if (ret)
			goto err;
	}

	ret = _set_opp_clk(res, target.freq, &enabled_here);
	if (ret)
		goto rollback_bw;

	if (target.peak_bw && scaling_down) {
		ret = _set_opp_bw(res, &target);
		if (ret)
			goto rollback_clk;
	}

	res->cur = target;

	return 0;

rollback_clk:
	if (res->clk && old.freq)
		clk_set_rate(res->clk, old.freq);

	if (enabled_here) {
		clk_disable_unprepare(res->clk);
		res->clk_enabled = false;
	}

rollback_bw:
	if (target.peak_bw && !scaling_down)
		_set_opp_bw(res, &old);

err:
	dev_err(dev, "Failed to apply OPP %lu Hz: %d\n", target.freq, ret);

	return ret;
}

/* Functions */

int devm_pm_opp_of_add_table(struct device *dev)
{
	struct _opp_resources *res;
	u32 path_count;
	bool has_clock;
	int ret;

	ret = dev_pm_opp_of_add_table(dev);
	if (ret)
		return ret;

	/*
	 * Register the table teardown before anything else can fail. The OPP
	 * table is not devres-backed, so every error return from here on would
	 * otherwise strand it on the device.
	 */
	ret = devm_add_action_or_reset(dev,
				       devm_pm_opp_of_table_release,
				       dev);
	if (ret)
		return ret;

	path_count = _get_icc_path_count(dev->of_node);
	has_clock = of_property_read_bool(dev->of_node, "clocks");

	if (!path_count && !has_clock)
		return 0;

	res = _get_opp_res(dev);
	if (IS_ERR(res))
		return PTR_ERR(res);

	if (res->populated) {
		dev_warn(dev, "OPP resources already claimed for this device\n");
		return 0;
	}

	/*
	 * The node was sized from the same immutable DT property, so this can
	 * only trip if that assumption ever stops holding - catch it here
	 * rather than letting of_icc_get() run off the end of the array.
	 */
	if (WARN_ON(path_count > res->max_paths))
		return -EINVAL;

	if (path_count) {
		ret = of_icc_get(dev,
				 res->paths,
				 &res->num_paths);
		if (ret) {
			dev_err(dev,
				"Failed to acquire ICC paths\n");
			return ret;
		}
	}

	if (has_clock) {
		/*
		 * A NULL con_id resolves to clock index 0, matching what the
		 * OPP core picks by default; devm_pm_opp_set_clkname() having
		 * run first overrides it.
		 */
		res->clk = clk_get(dev, res->clk_name);

		if (IS_ERR(res->clk)) {
			dev_warn(dev,
				 "Clock property present but lookup failed: %ld\n",
				 PTR_ERR(res->clk));
			res->clk = NULL;
		}
	}

	res->populated = true;

	return 0;
}

int dev_pm_opp_set_opp(struct device *dev,
		       struct dev_pm_opp *opp)
{
	struct _opp_resources *res;

	/*
	 * Mainline errors out when the device has no opp_table. We do not: a
	 * device with neither clocks nor interconnects never gets a node, and
	 * for it a transition is legitimately a no-op.
	 */
	res = _find_opp_res(dev);
	if (!res)
		return 0;

	return _set_opp(dev, res, opp);
}

unsigned int dev_pm_opp_get_level(struct dev_pm_opp *opp)
{
	struct device_node *np;
	u32 level = 0;

	np = dev_pm_opp_get_of_node(opp);
	if (!np)
		return 0;

	of_property_read_u32(np,
			     "opp-level",
			     &level);

	of_node_put(np);

	return level;
}

int devm_pm_opp_set_supported_hw(struct device *dev,
				 const u32 *versions,
				 unsigned int count)
{
	struct opp_table *opp_table;

	opp_table = dev_pm_opp_set_supported_hw(dev,
						versions,
						count);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	return devm_add_action_or_reset(
		dev,
		devm_pm_opp_supported_hw_release,
		opp_table);
}

int devm_pm_opp_set_clkname(struct device *dev,
			    const char *name)
{
	struct _opp_resources *res;
	struct opp_table *opp_table;
	char *clk_name;
	int ret;

	/*
	 * Record the con_id so a later devm_pm_opp_of_add_table() scales the
	 * clock the caller actually named instead of falling back to index 0.
	 */
	res = _get_opp_res(dev);
	if (IS_ERR(res))
		return PTR_ERR(res);

	clk_name = kstrdup(name, GFP_KERNEL);
	if (name && !clk_name)
		return -ENOMEM;

	opp_table = dev_pm_opp_set_clkname(dev, name);
	if (IS_ERR(opp_table)) {
		kfree(clk_name);
		return PTR_ERR(opp_table);
	}

	ret = devm_add_action_or_reset(
		dev,
		devm_pm_opp_clkname_release,
		opp_table);
	if (ret) {
		kfree(clk_name);
		return ret;
	}

	kfree(res->clk_name);
	res->clk_name = clk_name;

	return 0;
}
