#include <linux/of.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include "linux/opp.h"

struct msm_opp_tracker_entry {
	struct device *dev;
	struct msm_opp_resources *res;
	struct list_head node;
};

static LIST_HEAD(msm_opp_tracker_list);
static DEFINE_MUTEX(msm_opp_tracker_lock);

/* Function helpers */

static struct msm_opp_resources *msm_opp_find_resources(struct device *dev)
{
	struct msm_opp_tracker_entry *entry;

	mutex_lock(&msm_opp_tracker_lock);

	list_for_each_entry(entry, &msm_opp_tracker_list, node) {
		if (entry->dev == dev) {
			mutex_unlock(&msm_opp_tracker_lock);
			return entry->res;
		}
	}

	mutex_unlock(&msm_opp_tracker_lock);
	return NULL;
}

static void devm_opp_tracker_list_purge(void *data)
{
	struct msm_opp_tracker_entry *entry = data;

	mutex_lock(&msm_opp_tracker_lock);
	list_del(&entry->node);
	mutex_unlock(&msm_opp_tracker_lock);

	kfree(entry);
}

static u32 of_get_icc_path_count(struct device_node *np)
{
	const __be32 *data;
	int len;

	data = of_get_property(np, "interconnects", &len);
	if (!data || len <= 0)
		return 0;

	return len / (sizeof(u32) * 2);
}

static inline void
dev_pm_opp_get_bw_limits(struct dev_pm_opp *opp, u32 *avg, u32 *peak)
{
	struct device_node *np;

	*avg = 0;
	*peak = 0;

	np = dev_pm_opp_get_of_node(opp);
	if (!np)
		return;

	of_property_read_u32(np, "opp-peak-kBps", peak);
	of_property_read_u32(np, "opp-avg-kBps", avg);

	of_node_put(np);
}

static void devm_pm_opp_table_remove_action(void *data)
{
	dev_pm_opp_of_remove_table(data);
}

static void dev_pm_opp_of_put_supported_hw(void *data)
{
	dev_pm_opp_put_supported_hw(data);
}

static void dev_pm_opp_of_put_clkname(void *data)
{
	dev_pm_opp_put_clkname(data);
}

/* Functions */

int devm_pm_opp_of_add_table(struct device *dev)
{
	struct msm_opp_resources *res;
	struct msm_opp_tracker_entry *entry;
	u32 path_count;
	size_t alloc_size;
	bool has_clock;
	int ret;

	ret = dev_pm_opp_of_add_table(dev);
	if (ret)
		return ret;

	path_count = of_get_icc_path_count(dev->of_node);
	has_clock = of_property_read_bool(dev->of_node, "clocks");

	if (!path_count && !has_clock)
		goto register_remove_action;

	alloc_size = sizeof(*res) +
		     path_count * sizeof(struct icc_path *);

	res = devm_kzalloc(dev, alloc_size, GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	res->num_paths = path_count;

	if (path_count) {
		ret = devm_of_icc_get(dev,
				      res->paths,
				      &res->num_paths);
		if (ret) {
			dev_err(dev,
				"Failed to acquire ICC paths\n");
			return ret;
		}
	}

	if (has_clock) {
		res->clk = devm_clk_get(dev, NULL);

		if (IS_ERR(res->clk)) {
			dev_warn(dev,
				 "Clock property present but lookup failed: %ld\n",
				 PTR_ERR(res->clk));
			res->clk = NULL;
		}
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->dev = dev;
	entry->res = res;

	mutex_lock(&msm_opp_tracker_lock);
	list_add(&entry->node, &msm_opp_tracker_list);
	mutex_unlock(&msm_opp_tracker_lock);

	ret = devm_add_action_or_reset(dev,
				       devm_opp_tracker_list_purge,
				       entry);
	if (ret)
		return ret;

register_remove_action:
	return devm_add_action_or_reset(dev,
					devm_pm_opp_table_remove_action,
					dev);
}

int dev_pm_opp_set_opp(struct device *dev,
		       struct dev_pm_opp *opp)
{
	struct msm_opp_resources *res;
	unsigned long freq;
	u32 peak_kbps = 0;
	u32 avg_kbps = 0;
	u32 i;
	int ret;

	res = msm_opp_find_resources(dev);

	if (!opp) {
		if (!res)
			return 0;

		for (i = 0; i < res->num_paths; i++)
			icc_set_bw(res->paths[i], 0, 0);

		if (res->clk && res->clk_enabled) {
			clk_disable_unprepare(res->clk);
			res->clk_enabled = false;
		}

		return 0;
	}

	freq = dev_pm_opp_get_freq(opp);

	dev_pm_opp_get_bw_limits(opp,
				 &avg_kbps,
				 &peak_kbps);

	if (res && res->clk) {
		if (!res->clk_enabled) {
			ret = clk_prepare_enable(res->clk);
			if (ret)
				return ret;

			res->clk_enabled = true;
		}

		ret = clk_set_rate(res->clk, freq);
		if (ret)
			goto rollback_clk;
	}

	if (res && res->num_paths) {
		u64 peak_bw;
		u64 avg_bw;

		if (!peak_kbps)
			return 0;

		peak_bw = KBps_to_icc(peak_kbps);
		avg_bw = KBps_to_icc(avg_kbps ?
				     avg_kbps :
				     peak_kbps);

		for (i = 0; i < res->num_paths; i++) {
			ret = icc_set_bw(res->paths[i],
					 avg_bw,
					 peak_bw);
			if (ret)
				goto rollback_icc;
		}
	}

	return 0;

rollback_icc:
	while (i--)
		icc_set_bw(res->paths[i], 0, 0);

rollback_clk:
	if (res->clk && res->clk_enabled) {
		clk_disable_unprepare(res->clk);
		res->clk_enabled = false;
	}

	return ret;
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
		dev_pm_opp_of_put_supported_hw,
		opp_table);
}

int devm_pm_opp_set_clkname(struct device *dev,
			    const char *name)
{
	struct opp_table *opp_table;

	opp_table = dev_pm_opp_set_clkname(dev,
					   name);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	return devm_add_action_or_reset(
		dev,
		dev_pm_opp_of_put_clkname,
		opp_table);
}
