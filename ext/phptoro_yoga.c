/*
 * phptoro_yoga.c — Type-agnostic Yoga layout engine.
 *
 * This engine knows about LAYOUT, not element types. It reads:
 *   - Layout properties: width, height, padding, margin, flex, gap, etc.
 *   - "leaf": true       → node has no children (don't recurse)
 *   - "measure": "text"  → attach text measurement callback
 *   - "direction": "row" → horizontal flex direction (default: column)
 *
 * ALL other properties are forwarded as-is to the output frames.
 * Adding a new element type requires ZERO changes to this file.
 */

#include "phptoro_yoga.h"
#include "cJSON.h"
#include <yoga/Yoga.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Text measurement ──────────────────────────────────────────────── */

static phptoro_measure_text_fn g_measure_text = NULL;

void phptoro_set_text_measure(phptoro_measure_text_fn fn) {
    g_measure_text = fn;
}

typedef struct {
    char *text;
    float fontSize;
    char *fontWeight;
} TextMeasureCtx;

static YGSize text_measure_func(
    YGNodeConstRef node,
    float width,
    YGMeasureMode widthMode,
    float height,
    YGMeasureMode heightMode
) {
    YGSize zero = {0, 0};
    TextMeasureCtx *ctx = (TextMeasureCtx *)YGNodeGetContext(node);
    if (!ctx || !g_measure_text) return zero;

    float maxWidth = (widthMode == YGMeasureModeUndefined) ? 100000.0f : width;
    float outW = 0, outH = 0;

    g_measure_text(ctx->text, ctx->fontSize, ctx->fontWeight, maxWidth, &outW, &outH);

    YGSize result;
    switch (widthMode) {
        case YGMeasureModeExactly: result.width = width; break;
        case YGMeasureModeAtMost:  result.width = outW < width ? outW : width; break;
        default:                   result.width = outW; break;
    }
    switch (heightMode) {
        case YGMeasureModeExactly: result.height = height; break;
        case YGMeasureModeAtMost:  result.height = outH < height ? outH : height; break;
        default:                   result.height = outH; break;
    }
    return result;
}

static void free_text_ctx(YGNodeRef yg) {
    TextMeasureCtx *ctx = (TextMeasureCtx *)YGNodeGetContext(yg);
    if (ctx) {
        free(ctx->text);
        free(ctx->fontWeight);
        free(ctx);
        YGNodeSetContext(yg, NULL);
    }
}

/* ── JSON helpers ──────────────────────────────────────────────────── */

static float json_float(const cJSON *obj, const char *key, float def) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) return (float)item->valuedouble;
    return def;
}

static const char *json_string(const cJSON *obj, const char *key, const char *def) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) return item->valuestring;
    return def;
}

static int json_bool(const cJSON *obj, const char *key, int def) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item);
    return def;
}

/* ── Layout property names (consumed by C, NOT forwarded to Swift) ── */

static const char *LAYOUT_PROPS[] = {
    "children", "leaf", "measure", "direction",
    "width", "height", "min-width", "max-width", "min-height", "max-height",
    "flex", "flex-grow", "flex-shrink", "flex-basis",
    "padding", "padding-x", "padding-y",
    "padding-top", "padding-bottom", "padding-left", "padding-right",
    "margin", "margin-x", "margin-y",
    "margin-top", "margin-bottom", "margin-left", "margin-right",
    "gap", "align-items", "justify-content", "align-self",
    "wrap", "aspect-ratio", "position",
    "top", "left", "right", "bottom",
    "overflow", "display", "distribution",
    NULL
};

static int is_layout_prop(const char *key) {
    for (int i = 0; LAYOUT_PROPS[i]; i++) {
        if (strcmp(key, LAYOUT_PROPS[i]) == 0) return 1;
    }
    return 0;
}

/* ── Apply layout properties to a Yoga node ────────────────────────── */

static YGAlign parse_align(const char *val) {
    if (!val || !*val) return YGAlignStretch;
    if (strcmp(val, "center") == 0)       return YGAlignCenter;
    if (strcmp(val, "flex-start") == 0)   return YGAlignFlexStart;
    if (strcmp(val, "start") == 0)        return YGAlignFlexStart;
    if (strcmp(val, "flex-end") == 0)     return YGAlignFlexEnd;
    if (strcmp(val, "end") == 0)          return YGAlignFlexEnd;
    if (strcmp(val, "stretch") == 0)      return YGAlignStretch;
    if (strcmp(val, "baseline") == 0)     return YGAlignBaseline;
    return YGAlignStretch;
}

static YGJustify parse_justify(const char *val) {
    if (!val || !*val) return YGJustifyFlexStart;
    if (strcmp(val, "center") == 0)        return YGJustifyCenter;
    if (strcmp(val, "flex-start") == 0)    return YGJustifyFlexStart;
    if (strcmp(val, "start") == 0)         return YGJustifyFlexStart;
    if (strcmp(val, "flex-end") == 0)      return YGJustifyFlexEnd;
    if (strcmp(val, "end") == 0)           return YGJustifyFlexEnd;
    if (strcmp(val, "space-between") == 0) return YGJustifySpaceBetween;
    if (strcmp(val, "between") == 0)       return YGJustifySpaceBetween;
    if (strcmp(val, "space-around") == 0)  return YGJustifySpaceAround;
    if (strcmp(val, "around") == 0)        return YGJustifySpaceAround;
    if (strcmp(val, "space-evenly") == 0)  return YGJustifySpaceEvenly;
    if (strcmp(val, "evenly") == 0)        return YGJustifySpaceEvenly;
    return YGJustifyFlexStart;
}

static void apply_layout_props(YGNodeRef yg, const cJSON *node) {
    /* Direction */
    const char *direction = json_string(node, "direction", NULL);
    if (direction && strcmp(direction, "row") == 0) {
        YGNodeStyleSetFlexDirection(yg, YGFlexDirectionRow);
    } else {
        YGNodeStyleSetFlexDirection(yg, YGFlexDirectionColumn);
    }

    /* Dimensions */
    float w = json_float(node, "width", 0);
    float h = json_float(node, "height", 0);
    if (w > 0) YGNodeStyleSetWidth(yg, w);
    if (h > 0) YGNodeStyleSetHeight(yg, h);

    float minW = json_float(node, "min-width", 0);
    float maxW = json_float(node, "max-width", 0);
    float minH = json_float(node, "min-height", 0);
    float maxH = json_float(node, "max-height", 0);
    if (minW > 0) YGNodeStyleSetMinWidth(yg, minW);
    if (maxW > 0) YGNodeStyleSetMaxWidth(yg, maxW);
    if (minH > 0) YGNodeStyleSetMinHeight(yg, minH);
    if (maxH > 0) YGNodeStyleSetMaxHeight(yg, maxH);

    /* Flex */
    float flex = json_float(node, "flex", 0);
    if (flex > 0) YGNodeStyleSetFlex(yg, flex);

    float flexGrow = json_float(node, "flex-grow", -1);
    if (flexGrow >= 0) YGNodeStyleSetFlexGrow(yg, flexGrow);

    float flexShrink = json_float(node, "flex-shrink", -1);
    if (flexShrink >= 0) YGNodeStyleSetFlexShrink(yg, flexShrink);

    float flexBasis = json_float(node, "flex-basis", -1);
    if (flexBasis >= 0) YGNodeStyleSetFlexBasis(yg, flexBasis);

    /* Gap */
    float gap = json_float(node, "gap", 0);
    if (gap > 0) YGNodeStyleSetGap(yg, YGGutterAll, gap);

    /* Padding */
    float p = json_float(node, "padding", 0);
    if (p > 0) YGNodeStyleSetPadding(yg, YGEdgeAll, p);

    float px = json_float(node, "padding-x", 0);
    if (px > 0) YGNodeStyleSetPadding(yg, YGEdgeHorizontal, px);

    float py = json_float(node, "padding-y", 0);
    if (py > 0) YGNodeStyleSetPadding(yg, YGEdgeVertical, py);

    float pt = json_float(node, "padding-top", 0);
    if (pt > 0) YGNodeStyleSetPadding(yg, YGEdgeTop, pt);

    float pb = json_float(node, "padding-bottom", 0);
    if (pb > 0) YGNodeStyleSetPadding(yg, YGEdgeBottom, pb);

    float pl = json_float(node, "padding-left", 0);
    if (pl > 0) YGNodeStyleSetPadding(yg, YGEdgeLeft, pl);

    float pr = json_float(node, "padding-right", 0);
    if (pr > 0) YGNodeStyleSetPadding(yg, YGEdgeRight, pr);

    /* Margin */
    float m = json_float(node, "margin", 0);
    if (m > 0) YGNodeStyleSetMargin(yg, YGEdgeAll, m);

    float mx = json_float(node, "margin-x", 0);
    if (mx > 0) YGNodeStyleSetMargin(yg, YGEdgeHorizontal, mx);

    float my = json_float(node, "margin-y", 0);
    if (my > 0) YGNodeStyleSetMargin(yg, YGEdgeVertical, my);

    float mt = json_float(node, "margin-top", 0);
    if (mt > 0) YGNodeStyleSetMargin(yg, YGEdgeTop, mt);

    float mb = json_float(node, "margin-bottom", 0);
    if (mb > 0) YGNodeStyleSetMargin(yg, YGEdgeBottom, mb);

    float ml = json_float(node, "margin-left", 0);
    if (ml > 0) YGNodeStyleSetMargin(yg, YGEdgeLeft, ml);

    float mr = json_float(node, "margin-right", 0);
    if (mr > 0) YGNodeStyleSetMargin(yg, YGEdgeRight, mr);

    /* Alignment */
    const char *alignItems = json_string(node, "align-items", NULL);
    if (alignItems) YGNodeStyleSetAlignItems(yg, parse_align(alignItems));

    const char *justifyContent = json_string(node, "justify-content", NULL);
    if (justifyContent) YGNodeStyleSetJustifyContent(yg, parse_justify(justifyContent));

    const char *alignSelf = json_string(node, "align-self", NULL);
    if (alignSelf) YGNodeStyleSetAlignSelf(yg, parse_align(alignSelf));

    /* Wrap */
    int wrap = json_bool(node, "wrap", 0);
    if (wrap) YGNodeStyleSetFlexWrap(yg, YGWrapWrap);

    /* Aspect ratio */
    float ar = json_float(node, "aspect-ratio", 0);
    if (ar > 0) YGNodeStyleSetAspectRatio(yg, ar);

    /* Absolute positioning */
    const char *position = json_string(node, "position", NULL);
    if (position && strcmp(position, "absolute") == 0) {
        YGNodeStyleSetPositionType(yg, YGPositionTypeAbsolute);
    }

    float posTop = json_float(node, "top", NAN);
    float posLeft = json_float(node, "left", NAN);
    float posRight = json_float(node, "right", NAN);
    float posBottom = json_float(node, "bottom", NAN);
    if (!isnan(posTop))    YGNodeStyleSetPosition(yg, YGEdgeTop, posTop);
    if (!isnan(posLeft))   YGNodeStyleSetPosition(yg, YGEdgeLeft, posLeft);
    if (!isnan(posRight))  YGNodeStyleSetPosition(yg, YGEdgeRight, posRight);
    if (!isnan(posBottom)) YGNodeStyleSetPosition(yg, YGEdgeBottom, posBottom);

    /* Overflow */
    const char *overflow = json_string(node, "overflow", NULL);
    if (overflow) {
        if (strcmp(overflow, "hidden") == 0)  YGNodeStyleSetOverflow(yg, YGOverflowHidden);
        else if (strcmp(overflow, "scroll") == 0) YGNodeStyleSetOverflow(yg, YGOverflowScroll);
    }

    /* Display */
    const char *display = json_string(node, "display", NULL);
    if (display && strcmp(display, "none") == 0) {
        YGNodeStyleSetDisplay(yg, YGDisplayNone);
    }
}

/* ── Build Yoga node tree ──────────────────────────────────────────── */

static YGNodeRef build_node(const cJSON *node) {
    YGNodeRef yg = YGNodeNew();

    /* Apply all layout properties */
    apply_layout_props(yg, node);

    /* Check if this is a leaf node */
    int is_leaf = json_bool(node, "leaf", 0);

    if (is_leaf) {
        /* Check if text measurement is needed */
        const char *measure = json_string(node, "measure", NULL);
        if (measure && strcmp(measure, "text") == 0) {
            TextMeasureCtx *ctx = (TextMeasureCtx *)malloc(sizeof(TextMeasureCtx));
            ctx->text = strdup(json_string(node, "content", ""));
            ctx->fontSize = json_float(node, "font-size", 17);
            ctx->fontWeight = strdup(json_string(node, "font-weight", "regular"));
            YGNodeSetContext(yg, ctx);
            YGNodeSetMeasureFunc(yg, text_measure_func);
        }
        /* Leaf — no children */
        return yg;
    }

    /* Container — recurse into children */
    const char *distribution = json_string(node, "distribution", NULL);
    int is_equal = (distribution && strcmp(distribution, "equal") == 0);

    const cJSON *children = cJSON_GetObjectItemCaseSensitive(node, "children");
    if (cJSON_IsArray(children)) {
        int idx = 0;
        const cJSON *child;
        cJSON_ArrayForEach(child, children) {
            YGNodeRef childYG = build_node(child);
            if (is_equal) YGNodeStyleSetFlex(childYG, 1);
            YGNodeInsertChild(yg, childYG, idx++);
        }
    }

    return yg;
}

/* ── Extract flat frames ───────────────────────────────────────────── */

static void extract_frames(
    YGNodeRef yg,
    const cJSON *node,
    float parentX,
    float parentY,
    cJSON *output
) {
    float x = YGNodeLayoutGetLeft(yg) + parentX;
    float y = YGNodeLayoutGetTop(yg) + parentY;
    float w = YGNodeLayoutGetWidth(yg);
    float h = YGNodeLayoutGetHeight(yg);

    /* Create output frame */
    cJSON *frame = cJSON_CreateObject();
    cJSON_AddNumberToObject(frame, "x", x);
    cJSON_AddNumberToObject(frame, "y", y);
    cJSON_AddNumberToObject(frame, "width", w);
    cJSON_AddNumberToObject(frame, "height", h);

    /* Forward ALL non-layout properties as-is */
    cJSON *item = node->child;
    while (item) {
        if (!is_layout_prop(item->string)) {
            cJSON_AddItemToObject(frame, item->string, cJSON_Duplicate(item, 1));
        }
        item = item->next;
    }

    cJSON_AddItemToArray(output, frame);

    /* Free text measurement context if present */
    TextMeasureCtx *ctx = (TextMeasureCtx *)YGNodeGetContext(yg);
    if (ctx) free_text_ctx(yg);

    /* Recurse children */
    int is_leaf = json_bool(node, "leaf", 0);
    if (is_leaf) return;

    const cJSON *children = cJSON_GetObjectItemCaseSensitive(node, "children");
    if (cJSON_IsArray(children)) {
        int idx = 0;
        const cJSON *child;
        cJSON_ArrayForEach(child, children) {
            YGNodeRef childYG = YGNodeGetChild(yg, idx++);
            if (childYG) extract_frames(childYG, child, x, y, output);
        }
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

char *phptoro_layout(const char *tree_json, float width, float height) {
    cJSON *tree = cJSON_Parse(tree_json);
    if (!tree) return strdup("[]");

    /* Build Yoga tree */
    YGNodeRef root = build_node(tree);

    /* Calculate layout */
    float h = isnan(height) ? YGUndefined : height;
    YGNodeCalculateLayout(root, width, h, YGDirectionLTR);

    /* Extract flat frame list */
    cJSON *output = cJSON_CreateArray();
    extract_frames(root, tree, 0, 0, output);

    /* Serialize */
    char *result = cJSON_PrintUnformatted(output);

    /* Cleanup */
    cJSON_Delete(output);
    YGNodeFreeRecursive(root);
    cJSON_Delete(tree);

    return result ? result : strdup("[]");
}
