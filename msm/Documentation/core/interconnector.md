# Interconnect shim

`shims/core/interconnector.c`, `shims/include/linux/interconnector.h`

Implements enough of the mainline interconnect API — `of_icc_get()`,
`icc_set_bw()`, `icc_put()`, `devm_of_icc_get()` — for the MSM driver to vote
bandwidth, backed by downstream msm-bus.

## Why

The framing to get right, because it is the whole problem: **mainline never used
msm-bus.**

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
entry point, present since msm-bus was introduced. It takes raw bytes/sec per
path, synchronously or asynchronously, which is exactly the shape ICC consumers
want. Its one demand is that the caller supply the `src` and `dst` endpoint IDs.

## The DT binding

The shim parses those endpoint IDs out of an `interconnects` property. It looks
like the mainline binding but **is not** — mainline's form is
`<&provider MASTER_ID &provider SLAVE_ID>` with phandles to an interconnect
provider node, and there is no provider on 4.19. The shim's form is two bare
cells per path, the msm-bus `src` and `dst` directly:

```dts
dummy_device: device@10000000 {
	compatible = "vendor,dummy-device";
	reg = <0x10000000 0x1000>;

	interconnects = <22 512>, <23 512>;
	/* or, with the msm-bus headers included: */
	interconnects = <MSM_BUS_MASTER_MDP_PORT0 MSM_BUS_SLAVE_EBI_CH0>,
			<MSM_BUS_MASTER_MDP_PORT1 MSM_BUS_SLAVE_EBI_CH0>;

	status = "okay";
};
```

Consequences worth knowing:

- **`interconnect-names` is not supported.** The shim's `of_icc_get()` is
  index-based — it fills a caller-supplied array in DT order and returns a
  count. It does not take a name. Call sites that want the mainline
  name-based signature (`of_icc_get(dev, "mdp0-mem")`) are behind
  `LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)` and resolve to the real API.
- **The caller sizes the array.** `of_icc_get()` writes `count` entries without
  knowing the array's capacity. Derive the count from the same property first;
  `opp.c` does this and sanity-checks it with a `WARN_ON`.
- **The IDs are magic numbers and they are not portable.** msm-bus endpoint IDs
  are per-SoC and have changed across downstream kernel versions. Any DT written
  against this shim is pinned to one SoC on one base kernel.
- A property whose length is not a multiple of two cells is rejected outright
  rather than silently truncated.

## active_only

Paths are registered with `active_only = true`. This is not a tuning choice —
dual-context registration crashes this SoC.

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

`devm_of_icc_get()` releases the paths at unbind via a devres closure holding
its own copy of the handles. It is the right thing to reach for from a probe
path with nowhere else to put them.

The OPP shim does not use it — `_opp_resources` owns its paths and releases them
from its own devres node, so that there is exactly one owner rather than two
records of the same handles. See [`opp.md`](opp.md).
