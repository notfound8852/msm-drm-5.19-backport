# Interconnect shim

`shims/core/interconnector.c`, `shims/include/linux/interconnector.h`

Implements enough of the mainline interconnect API — `of_icc_get()`,
`icc_set_bw()`, `icc_put()`, `devm_of_icc_get()` — for the MSM driver to vote
bandwidth, backed by downstream msm-bus.

## Why

**Mainline never used msm-bus.**

msm-bus (`include/linux/msm-bus.h`, `drivers/soc/qcom/msm_bus/`) is a
Qualcomm downstream/CAF API. It was never merged upstream. Mainline had no
generic bandwidth abstraction at all until the interconnect framework landed in
5.1, and the mainline MSM display and GPU drivers have talked `of_icc_get()` /
`icc_set_bw()` ever since. There was no transition from one to the other in
mainline — there was nothing, and then there was ICC.

So the two sides of the port never overlapped:

- the 5.19 driver calls an API that does not exist on 4.19
- the 4.19 CAF platform offers an API the driver has never heard of

That gap is why the 4.19 MSM driver does no bandwidth voting worth the name, and
it is the specific reason panels underflow when the mainline driver is dropped
onto this base without a shim.

## Why not use msm-bus directly

The usual downstream pattern is `msm_bus_scale_register_client()` +
`msm_bus_scale_client_update_request()`: the DT carries verbose multi-vector
arrays of pre-defined bandwidth "cases", and the driver picks a case by index.
That cannot work here. `dpu_core_perf.c` computes bandwidth at runtime from the
active pipe configuration — there is no way to enumerate the cases in advance.

`msm_bus_scale_register()` / `msm_bus_scale_update_bw()` is the other downstream
entry point, present since msm-bus was introduced (somewhere around Linux 3.10.x).
It takes raw bytes/sec per path, synchronously or asynchronously, which is exactly
the shape ICC consumers want. Its one demand is that the caller supply the `src`
and `dst` endpoint IDs.

## The API

Mainline's, exactly — signatures and return contract:

```c
struct icc_path *of_icc_get(struct device *dev, const char *name);
struct icc_path *of_icc_get_by_index(struct device *dev, int idx);
struct icc_path *devm_of_icc_get(struct device *dev, const char *name);
int icc_set_bw(struct icc_path *path, u64 ab, u64 ib);
void icc_put(struct icc_path *path);
```

That is deliberate and it is the point: **a consumer can be lifted out of a
mainline tree unchanged.** No `#if LINUX_VERSION_CODE` around call sites, no
second implementation to keep in sync — `msm_mdss.c`, `dpu_kms.c`, `mdp5_kms.c`
and the a3xx/a4xx OCMEM paths all read exactly as they do upstream.

The return contract carries the part consumers actually depend on: a device with
no `interconnects` gets **NULL, not an error**, which is what makes
`IS_ERR_OR_NULL(path0)` / `PTR_ERR_OR_ZERO(path0)` behave — bandwidth voting
stays optional instead of failing probe.

One addition that is not mainline: `of_icc_get_count()`. Mainline consumers know
their paths by name and leave enumeration to the provider; there is no provider
here, so a consumer that wants *every* path — the OPP shim — has to count the
cells itself.

## The DT binding

This is the only place the two diverge. The shim parses endpoint IDs out of an
`interconnects` property that looks like the mainline binding but **is not** —
mainline's form is `<&provider MASTER_ID &provider SLAVE_ID>` with phandles to
an interconnect provider node, and there is no provider on 4.19. The shim's form
is two bare cells per path, the msm-bus `src` and `dst` directly:

```dts
#include <dt-bindings/msm/msm-bus-ids.h>

mdss: mdss@ae00000 {
	compatible = "qcom,sdm845-mdss";
	reg = <0x0ae00000 0x1000>;

	interconnects = <MSM_BUS_MASTER_MDP_PORT0 MSM_BUS_SLAVE_EBI_CH0>,
			<MSM_BUS_MASTER_MDP_PORT1 MSM_BUS_SLAVE_EBI_CH0>;
	/* Alternatively, raw numbers. (Not recommended) */
	interconnects = <22 512>, <23 512>;

	interconnect-names = "mdp0-mem", "mdp1-mem";

	status = "okay";
};
```

`interconnect-names` keeps its mainline meaning and **is required** for
name-based lookup — `of_icc_get(dev, "mdp0-mem")` resolves the name through it
and nothing else. DT written against the older index-only form of this shim has
`interconnects` but no names, and every named lookup against it fails with the
error `of_property_match_string()` returned. The shim logs the missing name when
that happens, because the failure is otherwise silent and looks like a driver
bug rather than a DT one.

### The IDs are the same across versions

`include/dt-bindings/msm/msm-bus-ids.h` is **one flat global namespace shared by
every SoC downstream ever shipped**, not a per-SoC numbering. Masters count up
from 1, slaves from 512, fabrics sit on 1024-step boundaries, and the
`*_DISPLAY` mirrors live at 20000+. New SoCs append new IDs; existing ones are
never renumbered, because shipped DTs already reference them.

So the values hold across both SoCs and kernel versions — 
* `MSM_BUS_MASTER_MDP_PORT0` is 22 on a 3.10 MSM8994 (Sony Xperia Z5) and 22 on a 
4.19 sdm845,
* `MSM_BUS_SLAVE_EBI_CH0` is 512 on both. Treat it as the downstream ABI it is.

Two things that genuinely do vary:

- **Whether an endpoint exists on this SoC.** The ID is stable; the bus topology
  is not. msm-bus only knows the endpoints its fabric devices register, so a
  path between two IDs this SoC does not wire up fails at
  `msm_bus_scale_register()`, not at parse time.
- **Anything to do with mainline.** Mainline's
  `dt-bindings/interconnect/qcom,sdm845.h` is small per-provider indices, wholly
  unrelated to these numbers, and needs a phandle. Nothing crosses that line.

Also worth knowing:

- A property whose length is not a whole number of `<src dst>` pairs is rejected
  outright rather than silently truncated.
- The properties belong on the **MDSS** node, not the DPU child — that is where
  the mainline binding puts them, and the consumers reach for `dev->parent`
  accordingly.

### Recommendation

Include `<dt-bindings/msm/msm-bus-ids.h>` and write the macros, never the raw
numbers. They are stable and they are self-documenting; `<22 512>` is neither to
anyone reading it later. Pair every `interconnects` with an `interconnect-names`
using mainline's names (`mdp0-mem`, `mdp1-mem`, `gfx-mem`), and put both on the
node the mainline binding puts them on.

## active_only

Paths are registered with `active_only = true`. This is not a tuning choice —
dual-context registration crashes the SoC.

Mainline ICC consumers apply a vote while the device is powered and drop it on
suspend; they never expect a persistent sleep-set vote. Registering with
`active_only = false` makes every `msm_bus_scale_update_bw()` — including the
`(0, 0)` removal on runtime suspend — commit the sleep and wake sets via
`rpmh_write_batch()`. On this SoC that batch commit times out during GPU
collapse and `rpmh_rsc_debug()` calls `BUG()`. Active-only keeps updates on the
active TCS via `rpmh_write()` and stays off that path.

## Bandwidth units

`icc_set_bw()` takes bytes/sec, matching mainline, and passes straight through
to `msm_bus_scale_update_bw()`. The `KBps_to_icc()` / `MBps_to_icc()` /
`Bps_to_icc()` macros are the mainline ones.

## Managed vs unmanaged

`devm_of_icc_get()` wraps one path in a devres node, exactly as mainline does.
It is the right thing to reach for from a probe path with nowhere else to put
the handle.

The OPP shim does not use it. It wants every path the node declares, so it pairs
`of_icc_get_count()` with a loop over `of_icc_get_by_index()` and stores the
handles in `_opp_resources`, which releases them from its own devres node. One
owner rather than two records of the same handles. See [`opp.md`](opp.md).
