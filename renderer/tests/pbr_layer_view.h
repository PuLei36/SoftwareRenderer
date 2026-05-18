#ifndef PBR_LAYER_VIEW_H
#define PBR_LAYER_VIEW_H

#include "test_helper.h"

typedef struct {
    int layer;
    texture_t *labels[5];
} pbr_layer_view_t;

void pbr_layer_view_init(pbr_layer_view_t *view);
void pbr_layer_view_acquire_labels(pbr_layer_view_t *view);
void pbr_layer_view_release_labels(pbr_layer_view_t *view);
int pbr_layer_view_update(pbr_layer_view_t *view, context_t *context);
void pbr_layer_view_draw(framebuffer_t *framebuffer, pbr_layer_view_t *view);

#endif
