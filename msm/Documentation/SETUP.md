# Quick how-to

**Note:** Please make sure to check everything before applying any of these.

## For power domains:

If `drivers/clk/qcom/gpucc-sdm845.c` doesn't have CX and GX. This will be for `KVER < 5.3` (I believe.)

```bash
git apply msm-drm-5.19-backport/patches/clk-qcom-sdm845-add-gdsc-power-domains.patch
```

---

## Pixel blending

**NOTE:** This is for 4.19 only as it doesn't have `pixel_blend_mode` and `blend_mode_property`

```bash
git apply msm-drm-5.19-backport/patches/drm-atomic-add-pixel-blend-mode-property.patch
```
