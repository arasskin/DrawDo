# Efficient text rendering for the native CRDT demo

Research checked 2026-10-21. The measured change is to preserve the native editable
buffer, replace only the differing UTF-16 range, and avoid resetting an unchanged
selection. The public C interface remains unchanged. Prefix/suffix boundaries are
expanded when necessary to avoid splitting surrogate pairs. Composition and stale
revision checks still run before mutation.

This preserves more of the native editor's existing state than assigning a new
whole string. It does not make the entire path incremental: the C bridge still
validates, copies and sends the complete UTF-8 string; the adapter scans it to find
the changed range. Multiple separated changes become one enclosing replacement.
For large documents, passing actual edit ranges through the bridge is a separate
potential improvement. It needs to preserve the existing revision/ACK contract.

## Approaches and their fit

| Approach | What it changes | Fit for this project |
|---|---|---|
| Incremental native editing | Retains the editable buffer and changes a range | Implemented; smallest change, retains native editing services |
| Viewport-based layout | Lays out visible paragraphs and nearby content | Already a design principle of TextKit 2; useful for large documents |
| Background text measurement | Computes glyph metrics before presenting a text block | Useful for read-only blocks; not a replacement for a mutable editing pipeline |
| Cached shaped runs and glyph atlas | Reuses layout and glyph images, draws only changed/visible content through Sokol | Plausible custom renderer, but requires more input, selection and accessibility integration |
| Signed distance fields / MSDF | Stores glyph outlines in textures that scale well | More relevant to zoomable canvas text; no demonstrated win for this fixed-size editor |
| Front-buffer rendering | Reduces buffering for small drawing updates | Specialized Android path; tearing tradeoff, no direct EditText switch |

### Native engines

Apple made UITextView use TextKit 2 by default starting with iOS 16. Accessing its
old `layoutManager` property can permanently switch that view into TextKit 1
compatibility mode. Our adapter uses the normal constructor and does not access
that property; the installed SDK's UITextView header agrees with this behavior.
This is a source/SDK audit, not a runtime assertion of the selected engine.
[Apple WWDC22](https://developer.apple.com/videos/play/wwdc2022/10090/).

TextKit 2 organizes layout around visible content rather than requiring a complete
layout from the beginning of a document. Avoid forcing layout outside the viewport.
That is especially relevant when text becomes much larger than the screen.
[Apple's TextKit 2 architecture](https://developer.apple.com/videos/play/wwdc2021/10061/).
Apple's newer discussion also describes custom rendering surfaces and viewport
hooks. Those newer hooks are absent from the installed iOS 26 SDK's UITextView
header, so they are not a shortcut we can deploy with this build environment.
[Apple's newer TextKit presentation](https://developer.apple.com/videos/play/wwdc2026/370/).

Android already has an editable-layout mechanism: DynamicLayout updates as text
changes, and Editable supports range replacement. We can use those capabilities
through EditText without building a new widget.
[DynamicLayout](https://developer.android.com/reference/android/text/DynamicLayout),
[Editable](https://developer.android.com/reference/android/text/Editable).

PrecomputedText can calculate text metrics on a worker thread for subsequent display
in TextView/StaticLayout. It implements Spannable rather than Editable. My assessment
is that it is a better candidate for static labels or read-only document blocks
than for repeatedly replacing the contents of this live editor; moving work to a
worker can also add a scheduling handoff to an update that needs to appear now.
[Android PrecomputedText](https://developer.android.com/reference/android/text/PrecomputedText).

### Owning the renderer

A custom renderer could cache shaped paragraph runs and glyph images, invalidate
only affected paragraphs, and draw visible glyphs through Sokol. This is a design
option, not a measured improvement. Text shaping must still handle ligatures,
combining characters, script direction, font fallback and emoji. HarfBuzz exposes
shaping and positioned glyph output; it is not a complete editing widget.
[HarfBuzz shaping](https://harfbuzz.github.io/shaping-and-shape-plans.html).

HarfBuzz is callable from C but introduces a C++ implementation dependency, as its
build requirements and `.cc` sources show. Continuing to use the existing platform
text APIs through C adapters would better preserve this project's current approach.
[HarfBuzz build documentation](https://harfbuzz.github.io/building.html).
Native editing services are substantial: UITextInput requires text ranges, hit
testing, selection geometry and composition integration, among other things.
A glyph renderer alone does not supply them.
[Apple UITextInput](https://developer.apple.com/documentation/uikit/uitextinput).

MSDF represents outlines in textures and preserves sharp corners while scaling.
The commonly used msdfgen generator is C++; it could run offline, leaving only a
texture/shader at runtime, but that would not solve dynamic font fallback, shaping
or editing. I would evaluate it for zoomable canvas labels, not replace this native
editor on its strength alone.
[msdfgen's own documentation](https://github.com/Chlumsky/msdfgen).

### Scheduling and presentation

Android's native controls already use hardware-accelerated rendering when attached
to an accelerated window. Our frame-commit probes only run in that case and produced
measurements. A custom GPU renderer therefore is not simply turning GPU rendering on.
[Android hardware acceleration](https://developer.android.com/topic/performance/views/hardware-accel-views).

Android's low-latency graphics library exposes front-buffer rendering from API 29,
which includes the Moto G7's OS. It is designed for small updates such as stylus
strokes, and the documentation warns about tearing and full-screen refreshes. It
requires a custom rendering path; it is not an EditText setting.
[Android advanced stylus rendering](https://developer.android.com/develop/ui/views/touch-and-input/stylus-input/advanced-stylus-features).

Once native update work is cheap, the remaining question is when our update meets
the platform's draw/compositor deadlines. Measure those stages before changing
threading or windows. Our current post-sync probe starts after Datafriend imports
an update, so it also excludes the wait for Sokol's next network-pump frame. An
end-to-end experiment should include socket arrival and that polling delay.

The [range-update measurements](benchmarks/crdt-native-range-2026-10-21.md) show
lower native update cost, but little change in median presentation/capture delay.
My recommendation is to keep the native editor and range updates, then measure
scheduling and larger-document behavior. A custom
renderer becomes worthwhile when those measurements or new product requirements
justify owning more of the editor.
