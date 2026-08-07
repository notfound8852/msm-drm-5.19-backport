# Shim documentation

One `.md` per shim that needs more explanation than a header comment can carry,
laid out to mirror the source tree:

```
shims/core/opp.c            ->  Documentation/core/opp.md
shims/core/interconnector.c ->  Documentation/core/interconnector.md
```

## What goes where

| Where | What belongs there |
| --- | --- |
| `shims/NOTE.md` | Tree layout, the per-version MSM driver comparisons, roadmap |
| `Documentation/<dir>/<shim>.md` | Why a shim exists, what the base kernel is missing, design, DT bindings, deliberate divergences from mainline |
| Header comment | One paragraph: what the shim provides, and a pointer to its doc |
| Kerneldoc on the declaration | What a caller has to know to call it correctly |
| Comment in the `.c` | Why *this* line is the way it is — hardware quirks, ordering constraints, bug workarounds |

The split that matters: a header tells you how to *use* the thing, the doc tells
you why it *exists*. Anything a caller does not need in order to call correctly
belongs in here, not in the header.

## Written so far

- [`core/opp.md`](core/opp.md) — mainline `dev_pm_opp_*` helpers on 4.19's OPP core
- [`core/interconnector.md`](core/interconnector.md) — the ICC API surface on top of msm-bus

Undocumented so far: `core/drm_shim.c`, `core/drm_missing_func.c`,
`compat/devm_compat.c`, `compat/dma-fence_missing_func.c`. The `backports/`
files are verbatim upstream and are documented upstream; they need nothing here
beyond a note of which version they were taken from.

## Driver related 

* MSM driver [FIXES](FIXES.md)
* Genpd for `<5.4` and pixel blending for `4.19` [SETUP](SETUP.md)

