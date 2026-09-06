# Performance validation

Measured on 2026-09-06 with an AMD Radeon RX 6600, Release build, 3840 × 2160 offscreen output.

## GPU results

| Scenario | Previous shader | Optimized shader | GPU time change |
| --- | ---: | ---: | ---: |
| Neutral effects | 0.4418 ms | 0.1949 ms | 55.9% less |
| CRT scanlines, phosphor, vignette and tint | 0.4560 ms | 0.3144 ms | 31.0% less |
| Many effects enabled, including bloom, grain and chromatic aberration | 0.6012 ms | 0.6305 ms | 4.9% more |

Earlier runs of the retained shader showed 55–56% less time for neutral effects, 27–31% less for CRT with tint, and 2–5% more for the heavy case. Conditional branches avoid expensive work when effects are disabled; their overhead can slightly increase cost when most effects are enabled. These results describe the tested GPU and effect combinations, not a guarantee for other hardware or settings.

The reference is the unmodified P31 pixel shader from commit `4cf1743`, preserved in `tests/reference/screenfx_pre_optimization.hlsl`. Both shaders are compiled with FXC `/O3` and run through the current renderer with the same synthetic gradient/checker texture. D3D11 timestamp queries measure batches of 48 draws after eight warmup draws. Each result is the median of five samples; execution order alternates within the five reference/optimized pairs. Disjoint clock measurements and timed-out queries fail explicitly.

These numbers measure the offscreen GPU render pass. They exclude desktop capture, window composition, presentation and application CPU work. They do not imply equivalent increases in monitor refresh rate or game FPS. The cache and redundant-clear improvements are shared by both benchmark paths, so their benefit is not included in the table's comparison. No FPS cap was added; the existing Uncapped/VSync presentation options remain.

## Resource reuse and validation

- The renderer retains up to four texture/view pairs, matching the bounded capture pool. It reuses shader resource views across texture rotations and releases the cache on resize or shutdown. COM references keep cached resource identities valid; capture-frame leases still control when a texture may be rewritten.
- The full-screen opaque triangle overwrites every output pixel, removing the need for a separate black clear.
- Automated verification compares 95 combinations against the previous shader, including neutral effects, bypass/blend boundaries, bloom threshold one, negative colors, animated grain and deterministic randomized settings. Both paths use the exact same constant buffer, including animation time. Tests permit one 8-bit channel step for rounding; the final hardware run observed zero difference.
- The texture-pool test checks view identity and every output pixel during 100 rotations, plus eviction and shutdown cleanup. Existing graphics tests check identity, tint, CRT, invalid frames and D3D11 validation. The interactive desktop test checks real capture, visible composition, resize, capture exclusion, click-through hit testing and three capture restarts.

## Reproduce

Build from the project folder with `./build.ps1`. Ordinary CTest runs include the image-equivalence/cache test using WARP; they do not assert timing thresholds.

Run the hardware benchmark from your normal Windows session:

```powershell
.\build\release\screenfx_performance_tests.exe --benchmark
```

The benchmark uses generated textures offscreen, writes no settings or images, and exits automatically. The reference shader and benchmark executable are test assets; they are not installed in `dist/bin`.
