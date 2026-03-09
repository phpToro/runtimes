#ifndef PHPTORO_YOGA_H
#define PHPTORO_YOGA_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Text measurement callback — the native side (Swift/Kotlin) registers
 * this at startup so the C engine can measure text for layout.
 */
typedef void (*phptoro_measure_text_fn)(
    const char *text,
    float       fontSize,
    const char *fontWeight,
    float       maxWidth,
    float      *outWidth,
    float      *outHeight
);

/* Register the text measurement function. Call before phptoro_layout(). */
void phptoro_set_text_measure(phptoro_measure_text_fn fn);

/*
 * phptoro_layout() — Type-agnostic layout engine.
 *
 * Takes a JSON widget tree, runs Yoga flexbox layout, returns a flat
 * array of positioned frames with ALL non-layout properties forwarded.
 *
 * The engine does NOT know about element types. It only understands:
 *   - Layout properties (width, height, padding, margin, flex, gap, etc.)
 *   - "leaf": true    → this node has no children
 *   - "measure": "text" → use text measurement callback
 *   - "direction": "row" → horizontal layout (default: column)
 *
 * Everything else is forwarded as-is to the output frames.
 *
 * Input:  tree_json — JSON string of the widget tree
 *         width     — available width (screen width)
 *         height    — available height (NAN for auto)
 *
 * Output: JSON string — flat array of positioned frames:
 *         [{"type":"text","x":0,"y":0,"width":300,"height":20,"content":"Hello",...}, ...]
 *
 * Caller must free() the returned string.
 */
char *phptoro_layout(const char *tree_json, float width, float height);

#ifdef __cplusplus
}
#endif

#endif /* PHPTORO_YOGA_H */
