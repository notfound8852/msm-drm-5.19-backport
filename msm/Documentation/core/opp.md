# OPP shim

`shims/core/opp.c`, `shims/include/linux/opp.h`

Provides the mainline OPP entry points the 5.19 MSM driver calls, on top of the
OPP core 4.19 actually ships.

| Shimmed | Backed by |
| --- | --- |
| `devm_pm_opp_of_add_table()` | `dev_pm_opp_of_add_table()` + `devm_add_action_or_reset()` |
| `devm_pm_opp_set_supported_hw()` | `dev_pm_opp_set_supported_hw()` + same |
| `devm_pm_opp_set_clkname()` | `dev_pm_opp_set_clkname()` + same |
| `dev_pm_opp_get_level()` | re-parsed from `opp->np` |
| `dev_pm_opp_set_opp()` | reimplemented — see below |

## Why

Mainline turned OPP from a frequency table into a single place that applies a
whole operating point. A driver calls `dev_pm_opp_set_opp()` and the core scales
the clock, votes the performance level at genpd, and votes peak/average
bandwidth through the interconnect framework:

- `opp-hz` → clock rate
- `opp-level` → performance level / voltage corner
- `opp-peak-kBps`, `opp-avg-kBps` → interconnect bandwidth

That consolidation arrived in pieces — bandwidth in 5.5, `dev_pm_opp_set_opp()`
itself in 5.18 — and 4.19 predates all of it.

## What 4.19 actually gives us

`struct dev_pm_opp` in 4.19 is:

```c
bool available, dynamic, turbo, suspend;
unsigned int pstate;
unsigned long rate;
struct dev_pm_opp_supply *supplies;
unsigned long clock_latency_ns;
struct opp_table *opp_table;
struct device_node *np;
```

No `level`. No `bandwidth[]`. Grepping `drivers/opp/` for `opp-level`,
`opp-peak-kBps`, `opp-avg-kBps`, `bandwidth` or `icc_path` returns nothing — the
core parses `opp-hz` and the supply properties and ignores the rest.

The one thing that saves this is `np`: the core keeps the OPP's device tree node
alive, so anything it declined to parse can be read back out with
`dev_pm_opp_get_of_node()`. That is how both `dev_pm_opp_get_level()` and the
bandwidth read work.

`struct opp_table` is not an option for storage. It is defined in
`drivers/opp/opp.h`, which is not under `include/`, and `_find_opp_table()` is
not exported — a module can hold an opaque pointer to one and nothing else.

## Device tree

**Mainline OPP tables drop in unchanged.** Every property the shim cares about
keeps its mainline name and meaning, so an `operating-points-v2` node can be
lifted straight out of a mainline DTS:

| Property | Read by | Notes |
| --- | --- | --- |
| `opp-hz` | 4.19 OPP core | |
| `opp-microvolt`, `opp-microamp` | 4.19 OPP core | |
| `opp-supported-hw` | 4.19 OPP core | pairs with `devm_pm_opp_set_supported_hw()` |
| `opp-level` | this shim, off `opp->np` | |
| `opp-peak-kBps`, `opp-avg-kBps` | this shim, off `opp->np` | |
| `required-opps` | **nobody** | silently ignored; no genpd corner voting here |

```dts
/* TODO: drop in the real table from the device DTS */
gpu_opp_table: opp-table {
	compatible = "operating-points-v2";

	opp-710000000 {
		opp-hz = /bits/ 64 <710000000>;
		opp-level = <RPMH_REGULATOR_LEVEL_TURBO_L1>;
		opp-peak-kBps = <7216000>;
	};
	/* ... */
};
```

The one thing you cannot copy from mainline is the **`interconnects` property on
the consuming device node**. Mainline writes `<&provider MASTER &provider SLAVE>`;
there is no interconnect provider on 4.19, so the shim expects two bare cells
holding the msm-bus `src` and `dst` instead. That property is what sizes the
path array this shim allocates, and it is the only DT change the port requires.
See [`interconnector.md`](interconnector.md) for the form and an example.

## Design: one devres node per device

Everything the shim needs per device lives in `struct _opp_resources`, the
payload of a devres node. It is the local stand-in for `opp_table`.

Lookup is `devres_find(dev, _opp_res_release, NULL, NULL)` — keyed on the
release function's address. That key is what makes the slot safe:

- devres lists are per-`struct device`, so the GPU, the GMU, the DPU and the DSI
  host each get an independent node
- `_opp_res_release` is `static` in `opp.c`, so no code outside this file can
  name it, and therefore cannot find, replace or free the node

This is the property `dev_set_drvdata()`/`devres_set_drvdata()` lacks: drvdata is
a single shared slot per device that the driver core and the driver both write,
which is exactly how the earlier version of this shim lost state when a second
device registered under the same platform context.

The node **owns** the clock and the interconnect paths — plain `clk_get()` and
`of_icc_get()`, not the managed variants — so `_opp_res_release()` is the single
teardown point and gets the internal order right: drop a still-held enable,
then put the clock, then put the paths. Without that, unbinding with an OPP
still applied would put an enabled clock.

The node is created lazily by whichever of `devm_pm_opp_set_clkname()` or
`devm_pm_opp_of_add_table()` runs first, and is sized from the DT `interconnects`
count, which is readable at either entry point.

## The cache, and the shrink list

Anything the core does not track has to come off `opp->np`, and doing that on
every transition means re-walking the same device tree properties forever. So
the whole table is flattened once at probe: `_build_opp_cache()` enumerates it
with `dev_pm_opp_find_freq_ceil()` — the only way to iterate OPPs from outside
the core — and stores one `_opp_target` per OPP, ascending by frequency.
Lookup is a bisect on frequency, which is unique within a table.

A miss falls back to reading the node live, so a failed allocation or an OPP
enabled after probe still works; the cache is an optimisation, never a
requirement. OPPs filtered out by `opp-supported-hw` are skipped, which is
correct — they can never be applied.

The point of isolating it this way is the *shrink list* in `opp.h`:

```c
#define OPP_CORE_HAS_LEVEL	(LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0))
```

The OPP subsystem absorbed these properties one at a time on the way to 5.11.
Every entry on that list is something the shim tracks *only* because the base
kernel's core does not yet. Raise the floor past one and the core starts
carrying it, the corresponding read drops out of `_parse_opp_target()`, and the
cache shrinks by a field. Supporting a range of base kernels is therefore a
matter of tracking **less**, not of tracking differently — which is why the
versions live in one block rather than scattered across the use sites.

Bandwidth is deliberately not on the list. It is read off `opp->np`, and `np` is
kept by every version from 4.19 up; the core gained `opp->bandwidth[]` in 5.5
but no plain getter to switch to, so the DT read stays correct either way.

Treat the versions in that block as this shim's record, not as verified fact —
only the bottom of the range has ever been exercised. Check the target tree
before raising the floor past an entry.

## Applying a transition

`_set_opp(dev, res, opp)` takes the resources the same way mainline's `_set_opp()`
takes an `opp_table`, so the public function stays a lookup and a delegate.

`_read_opp_target()` flattens the OPP into `struct _opp_target` — rate, level,
avg/peak bandwidth in bytes/sec. Mainline reads those off the OPP; here the
bandwidth and level come back off `opp->np` on every transition.

Order follows mainline `_set_opp()`:

| | scaling up | scaling down |
| --- | --- | --- |
| 1 | bandwidth | clock |
| 2 | voltage | bandwidth |
| 3 | clock | voltage |

Widen the bus before the part gets faster; narrow it only once the part is
already slower. An unknown starting point (`cur.freq == 0`) counts as scaling
up. `res->cur` holds the last committed target and stands in for mainline's
`opp_table->current_opp`.

## Voltage

`_set_opp_voltage()` is where the corner vote would go. On Downstream kernels
there is nothing to do, and that is not an omission.

`msm_bus_scale_update_bw()` does not just program the bus. It resolves the
requested traffic level into the corner the rails need in order to sustain it,
and votes for that too, on every call. So the bandwidth vote `_set_opp_bw()`
just placed has already carried the voltage — which is why this shim can skip
genpd entirely, on a kernel where `rpmhpd` and `required-opps` do not exist
(both land in 5.10).

While software architectures differ completely between Downstream and Mainline, the physical hardware behavior on the SoC power rails remains identical:

| Layer | Mainline (>= 5.10) via `genpd` | Downstream via `msm-bus` |
| :--- | :--- | :--- |
| **Software Abstraction** | Explicit, decoupled kernel state tracking | Single monolithic driver IPC wrapper |
| **Voltage Authority** | **Linux Kernel Core** via `genpd` & `rpmhpd` | **Qualcomm RPMh Firmware** (AOP/ARC) |
| **Kernel API** | `dev_pm_opp_set_opp()` -> `dev_pm_genpd_set_performance_state()` | `dev_pm_opp_set_opp()` -> `icc_set_bw()` -> `msm_bus_scale_update_bw()` |
| **IPC Payload** | Separate RPMh command sets for Bus and Power Domains | Unified bandwidth vector payload sent over SMD/RPMh IPC |
| **Hardware Target** | PMIC corner rails ($VDD\_CX$, $VDD\_MX$) | PMIC corner rails ($VDD\_CX$, $VDD\_MX$) |

Qualcomm dropped msm-bus when the generic interconnect framework landed in 5.1,
and generic ICC providers carry no voltage vote. So the helper is gated: below
5.1 it is a documented no-op, at 5.1 and above it warns once and returns, because
doing it properly there means `required-opps` parsing against a working `rpmhpd`.
Nothing in-tree needs that yet.

## Deliberate divergences from mainline

- **No opp_table.** Cannot be borrowed or extended from a module; see above.
- **An OPP with no `opp-peak-kBps` leaves the standing bandwidth vote alone.**
  Mainline would vote whatever the OPP declares, i.e. zero. These tables mix
  rate-only and rate-plus-bandwidth nodes, and dropping to zero on the former
  starves the bus mid-stream. The a6xx GPU tables all carry `opp-peak-kBps`, so
  this only matters if a rate-only node is ever added.
- **`dev_pm_opp_set_opp()` returns 0 when the device has no node.** Mainline
  errors out. A device with neither clocks nor interconnects legitimately never
  gets a node, and for it a transition really is a no-op.
- **Failed transitions unwind to the previous vote.** Mainline returns the error
  with the bandwidth vote left applied. On this SoC a stranded high vote after a
  failed transition is worse than the extra code.
- **The shim prepares and enables the clock.** The OPP core only ever calls
  `clk_set_rate()`. Tracked in `clk_enabled`, dropped on
  `dev_pm_opp_set_opp(dev, NULL)` and again at release.

## Things to note:

- `devm_pm_opp_set_clkname()` must be called **before**
  `devm_pm_opp_of_add_table()` for the named clock to be the one that gets
  scaled. Called the other way round, the node is already populated and the
  clock has been resolved to index 0. This matches the ordering every caller
  already uses (`dsi_host.c`, `dp_ctrl.c`, `dpu_kms.c`).
- `dev_pm_opp_get_level()` returns 0 both for "level is 0" and "no `opp-level`
  property".
- The OPP table itself is not devres-backed. `devm_pm_opp_of_add_table()`
  registers its removal action immediately after the table is created,
  before anything else can fail, precisely so no error path can strand it.
