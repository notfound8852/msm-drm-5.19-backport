#include <linux/device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/pm_domain.h>

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

	kfree(res->cache);
	kfree(res->clk_name);
}

static struct _opp_resources *_find_opp_res(struct device *dev)
{
	return devres_find(dev, _opp_res_release, NULL, NULL);
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

	path_count = of_icc_get_count(dev);

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
 * Flatten @opp into what we can actually commit. Mainline reads opp->rate,
 * opp->level and opp->bandwidth[] straight out of the OPP; whatever this base
 * kernel's core does not carry comes back off opp->np instead. See the shrink
 * list in opp.h.
 *
 * @freq is passed in because every caller already has it.
 */
static void _parse_opp_target(struct _opp_resources *res,
			      struct dev_pm_opp *opp,
			      unsigned long freq,
			      struct _opp_target *target)
{
	struct device_node *np;
	u32 peak_kbps = 0;
	u32 avg_kbps = 0;

	memset(target, 0, sizeof(*target));

	target->freq = freq;
	target->level = dev_pm_opp_get_level(opp);

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
	target->avg_bw = KBps_to_icc(avg_kbps);
}

/*
 * Flatten the whole table once, at probe.
 *
 * dev_pm_opp_find_freq_ceil() walks available OPPs in ascending order, which is
 * the only way to enumerate them from outside the core, and conveniently leaves
 * the cache sorted for lookup. OPPs filtered out by "opp-supported-hw" are
 * skipped, which is what we want - they can never be applied.
 *
 * Failing to build the cache is not fatal. A miss falls back to reading the
 * node, so the shim still works, just as it did before the cache existed.
 */
static void _build_opp_cache(struct device *dev, struct _opp_resources *res)
{
	struct dev_pm_opp *opp;
	unsigned long freq = 0;
	int count;
	u32 i = 0;

	count = dev_pm_opp_get_opp_count(dev);
	if (count <= 0)
		return;

	res->cache = kmalloc_array(count, sizeof(*res->cache), GFP_KERNEL);
	if (!res->cache)
		return;

	while (i < (u32)count) {
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp))
			break;

		_parse_opp_target(res, opp, freq, &res->cache[i++]);

		dev_pm_opp_put(opp);

		freq++;
	}

	res->cache_count = i;
}

/* Ascending by freq, and frequencies are unique in a table, so bisect. */
static const struct _opp_target *_cached_target(struct _opp_resources *res,
						unsigned long freq)
{
	u32 lo = 0;
	u32 hi = res->cache_count;

	while (lo < hi) {
		u32 mid = lo + (hi - lo) / 2;

		if (res->cache[mid].freq == freq)
			return &res->cache[mid];

		if (res->cache[mid].freq < freq)
			lo = mid + 1;
		else
			hi = mid;
	}

	return NULL;
}

static void _read_opp_target(struct _opp_resources *res,
			     struct dev_pm_opp *opp,
			     struct _opp_target *target)
{
	unsigned long freq = dev_pm_opp_get_freq(opp);
	const struct _opp_target *hit = _cached_target(res, freq);

	if (hit) {
		*target = *hit;
		return;
	}

	/*
	 * Not in the table we walked at probe - no cache, or an OPP that has
	 * been enabled since. Read it live.
	 */
	_parse_opp_target(res, opp, freq, target);
}

/*
 * Raw vote. A path that rejects it leaves the ones before it already updated,
 * so walk back and restore them to the last vote that was known good rather
 * than dropping them to zero.
 */
static int _vote_bw(struct _opp_resources *res, u64 avg_bw, u64 peak_bw)
{
	u32 i;
	int ret;

	for (i = 0; i < res->num_paths; i++) {
		ret = icc_set_bw(res->paths[i], avg_bw, peak_bw);
		if (ret) {
			while (i--)
				icc_set_bw(res->paths[i],
					   res->cur.avg_bw,
					   res->cur.peak_bw);
			return ret;
		}
	}

	res->cur.avg_bw = avg_bw;
	res->cur.peak_bw = peak_bw;

	return 0;
}

/*
 * Transition vote. An OPP that declares no bandwidth leaves whatever vote is
 * standing alone - these tables mix rate-only and rate-plus-bandwidth nodes,
 * and collapsing to zero on the former starves the bus mid-stream. The disable
 * path wants zero to mean zero, so it goes through _vote_bw() instead.
 */
static int _set_opp_bw(struct _opp_resources *res,
		       const struct _opp_target *target)
{
	if (!target->peak_bw)
		return 0;

	return _vote_bw(res, target->avg_bw, target->peak_bw);
}

/*
 * The corner vote.
 *
 * Mainline routes "opp-level" to a power domain: dev_pm_opp_set_opp() hands the
 * level to genpd, which asks rpmhpd for the corner. None of that exists here -
 * rpmhpd and the required-opps plumbing both land in 5.10.
 */
static int _set_opp_voltage(struct device *dev,
			    const struct _opp_target *target)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 1, 0)
	/*
	 * HACK: We can fully skip voltage on downstream versions that use the
	 * msm-bus API. Whenever icc_set_bw is invoked, the underlying msm-bus
	 * driver implicitly scales to the required voltage/corner vote on the
	 * hardware rails. This allows us to bypass upstream genpd framework.
	 */

	return 0;
#else
	if (target->level)
		dev_warn_once(dev,
			      "opp-level %u ignored: no genpd corner voting on this kernel\n",
			      target->level);

	return 0;
#endif
}

/*
 * The rate goes through the core rather than a bare clk_set_rate().
 * dev_pm_opp_set_rate() exists on every base kernel this shim targets and does
 * a good deal more: it scales any "opp-microvolt" supplies and any genpd
 * performance state the OPP declares, rounds the target with clk_round_rate(),
 * skips the write entirely when the rate is already what was asked for, and
 * resolves the clock through opp_table->clk - the same one
 * devm_pm_opp_set_clkname() named. None of that is worth reimplementing.
 *
 * The prepare/enable is ours. The OPP core has never done it on any version, and
 * it has to happen first or the rate write lands on a gated clock.
 *
 * No unwinding on failure, matching mainline _set_opp(): every failure there is
 * dev_err() then return. A rate that would not take leaves the clock running at
 * the rate it already had, which is a state the hardware was happy with a
 * moment ago. Killing the clock outright - which this used to do - turns a
 * failed transition into a dead device.
 */
static int _set_opp_clk(struct device *dev,
			struct _opp_resources *res,
			unsigned long freq)
{
	int ret;

	if (!res->clk)
		return 0;

	if (!res->clk_enabled) {
		ret = clk_prepare_enable(res->clk);
		if (ret)
			return ret;

		res->clk_enabled = true;
	}

	return dev_pm_opp_set_rate(dev, freq);
}

static void _disable_opp_clk(struct device *dev, struct _opp_resources *res)
{
	if (!res->clk || !res->clk_enabled)
		return;

	/*
	 * Straight to the clock on the way down: dev_pm_opp_set_rate() rejects a
	 * target of 0 outright ("Invalid target frequency") before it looks at
	 * anything else, so it cannot wind a clock down. Best effort - a clock
	 * that will not take 0 still gets disabled below.
	 */
	clk_set_rate(res->clk, 0);

	clk_disable_unprepare(res->clk);
	res->clk_enabled = false;

	dev_pm_genpd_set_performance_state(dev, 0);
}

/* Mainline's _set_opp(dev, opp_table, NULL, ...) tail: drop every vote. */
static int _disable_opp(struct device *dev, struct _opp_resources *res)
{
	const struct _opp_target off = { };

	_vote_bw(res, 0, 0);
	_disable_opp_clk(dev, res);

	res->cur = off;

	return 0;
}

/*
 * The transition proper. @res is what mainline passes an opp_table for - the
 * caller has already resolved it, so this only has to apply.
 *
 * Ordering mirrors mainline _set_opp() exactly. Everything the part depends on
 * is raised before it speeds up and lowered only after it has slowed down, so
 * it is never fast on a rail or a bus that has not been told about it:
 *
 *	scaling up:		voltage -> bandwidth -> clock
 *	scaling down:	clock	-> bandwidth -> voltage
 *
 * Mainline gets that shape from _set_opp_level() and _set_required_opps()
 * bracketing _set_opp_bw() on both sides of the clock; ours is the same
 * sequence with the two helpers we have. An unknown starting point counts as
 * scaling up.
 */
static int _set_opp(struct device *dev,
		    struct _opp_resources *res,
		    struct dev_pm_opp *opp)
{
	struct _opp_target target;
	struct _opp_target old;
	bool scaling_down;
	int ret;

	if (!opp)
		return _disable_opp(dev, res);

	_read_opp_target(res, opp, &target);

	old = res->cur;
	scaling_down = old.freq && target.freq < old.freq;

	if (!scaling_down) {
		ret = _set_opp_voltage(dev, &target);
		if (ret)
			goto err;

		ret = _set_opp_bw(res, &target);
		if (ret)
			goto err;
	}

	ret = _set_opp_clk(dev, res, target.freq);
	if (ret)
		goto rollback_bw;

	if (scaling_down) {
		ret = _set_opp_bw(res, &target);
		if (ret)
			goto err;

		ret = _set_opp_voltage(dev, &target);
		if (ret)
			goto err;
	}

	res->cur = target;

	return 0;

	/*
	 * The bandwidth vote is the only thing put back, and only when it was
	 * raised ahead of a clock that then refused to follow - leaving the bus
	 * wide open for a rate the part never reached. Nothing else unwinds:
	 * mainline unwinds nothing at all, and a half-applied rate or corner is
	 * still a rate or corner the hardware is running at.
	 */
rollback_bw:
	if (!scaling_down)
		_set_opp_bw(res, &old);

err:
	dev_err(dev, "Failed to apply OPP %lu Hz: %d\n", target.freq, ret);

	return ret;
}

/* Functions */
#if !OPP_CORE_HAS_DEVM
int devm_pm_opp_of_add_table(struct device *dev)
{
	struct _opp_resources *res;
	u32 path_count;
	bool has_clock;
	u32 i;
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

	path_count = of_icc_get_count(dev);
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
	 * rather than letting the loop below run off the end of the array.
	 */
	if (WARN_ON(path_count > res->max_paths))
		return -EINVAL;

	/*
	 * By index, not by name: this wants every path the node declares, and
	 * "interconnect-names" is optional in DT. Paths taken before a failure
	 * are already counted in num_paths, so _opp_res_release() frees them.
	 */
	for (i = 0; i < path_count; i++) {
		struct icc_path *path = of_icc_get_by_index(dev, i);

		if (IS_ERR_OR_NULL(path)) {
			ret = path ? PTR_ERR(path) : -ENODEV;
			dev_err(dev,
				"Failed to acquire ICC path %u: %d\n",
				i, ret);
			return ret;
		}

		res->paths[res->num_paths++] = path;
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

	/* Last: _parse_opp_target() reads num_paths to decide on bandwidth. */
	_build_opp_cache(dev, res);

	res->populated = true;

	return 0;
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
#endif

#if !OPP_CORE_HAS_SET_OPP
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
#endif

#if !OPP_CORE_HAS_LEVEL
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
#endif
