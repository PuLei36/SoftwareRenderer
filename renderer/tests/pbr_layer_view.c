#include <string.h>
#include "../core/api.h"
#include "../shaders/cache_helper.h"
#include "pbr_layer_view.h"

#define NUM_EDGES 5
#define EDGE_SPACE 0.15f
#define EDGE_START (1 - EDGE_SPACE * 0.5f)
#define EDGE_END (EDGE_SPACE * (NUM_EDGES - 0.5f))

static int above_layer_edge(int edge, vec2_t coord) {
    float offset = EDGE_SPACE * (float)edge;
    vec2_t start = vec2_new(EDGE_START - offset, 0);
    vec2_t end = vec2_new(EDGE_END - offset, 1);
    return vec2_edge(start, end, coord) > 0;
}

static int query_curr_layer(context_t *context, int prev_layer) {
    if (context->double_click) {
        if (prev_layer >= 0) {
            return -1;
        } else {
            return 0;
        }
    }
    if (context->single_click) {
        if (prev_layer > 0) {
            return 0;
        } else if (prev_layer == 0) {
            int edge;
            for (edge = 0; edge < NUM_EDGES; edge++) {
                if (above_layer_edge(edge, context->click_pos)) {
                    break;
                }
            }
            if (edge == 0) {
                return -1;
            } else {
                return edge;
            }
        }
    }
    return prev_layer;
}

static void draw_layer_edge(framebuffer_t *framebuffer, int edge) {
    vec4_t color = vec4_new(0.65f, 0.65f, 0.65f, 1);
    float offset = EDGE_SPACE * (float)edge;
    float start = EDGE_START - offset;
    float end = EDGE_END - offset;
    draw2d_draw_line(framebuffer, color, vec2_new(start, 0), vec2_new(end, 1));
}

static void draw_top_text(framebuffer_t *framebuffer, texture_t *labels[5],
                          int layer) {
    texture_t *label = labels[layer - 1];
    float ratio = (float)label->height / (float)framebuffer->height;
    vec2_t origin = vec2_new(0, 1 - ratio);
    draw2d_draw_texture(framebuffer, label, origin);
}

static void draw_bottom_text(framebuffer_t *framebuffer, texture_t *labels[5],
                             int layer) {
    texture_t *label = labels[layer - 1];
    float ratio = (float)label->width / (float)framebuffer->width;
    float center = EDGE_START - EDGE_SPACE * ((float)layer - 0.5f);
    vec2_t origin = vec2_new(center - ratio * 0.5f, 0);
    draw2d_draw_texture(framebuffer, label, origin);
}

static void draw_layer_view(framebuffer_t *framebuffer, pbr_layer_view_t *view) {
    if (view->layer == 0) {
        int edge;
        for (edge = 0; edge < NUM_EDGES; edge++) {
            draw_layer_edge(framebuffer, edge);
            draw_bottom_text(framebuffer, view->labels, edge + 1);
        }
    } else if (view->layer > 0) {
        draw_top_text(framebuffer, view->labels, view->layer);
    }
}

void pbr_layer_view_init(pbr_layer_view_t *view) {
    memset(view, 0, sizeof(pbr_layer_view_t));
    view->layer = -1;
}

void pbr_layer_view_acquire_labels(pbr_layer_view_t *view) {
    view->labels[0] = cache_acquire_texture("common/diffuse.tga",
                                            USAGE_LDR_COLOR);
    view->labels[1] = cache_acquire_texture("common/specular.tga",
                                            USAGE_LDR_COLOR);
    view->labels[2] = cache_acquire_texture("common/roughness.tga",
                                            USAGE_LDR_COLOR);
    view->labels[3] = cache_acquire_texture("common/occlusion.tga",
                                            USAGE_LDR_COLOR);
    view->labels[4] = cache_acquire_texture("common/normal.tga",
                                            USAGE_LDR_COLOR);
}

void pbr_layer_view_release_labels(pbr_layer_view_t *view) {
    cache_release_texture(view->labels[0]);
    cache_release_texture(view->labels[1]);
    cache_release_texture(view->labels[2]);
    cache_release_texture(view->labels[3]);
    cache_release_texture(view->labels[4]);
    view->labels[0] = NULL;
    view->labels[1] = NULL;
    view->labels[2] = NULL;
    view->labels[3] = NULL;
    view->labels[4] = NULL;
}

int pbr_layer_view_update(pbr_layer_view_t *view, context_t *context) {
    view->layer = query_curr_layer(context, view->layer);
    return view->layer;
}

void pbr_layer_view_draw(framebuffer_t *framebuffer, pbr_layer_view_t *view) {
    draw_layer_view(framebuffer, view);
}
