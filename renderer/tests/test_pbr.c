#include <stddef.h>
#include "../core/api.h"
#include "pbr_layer_view.h"
#include "test_helper.h"
#include "test_pbr.h"
#include "test_scenes.h"

typedef struct {
    scene_t *scene;
    pbr_layer_view_t layer_view;
} userdata_t;

static void tick_function(context_t *context, void *userdata_) {
    userdata_t *userdata = (userdata_t*)userdata_;
    perframe_t perframe = test_build_perframe(userdata->scene, context);
    perframe.layer_view = pbr_layer_view_update(&userdata->layer_view,
                                                context);
    test_draw_scene(userdata->scene, context->framebuffer, &perframe);
    pbr_layer_view_draw(context->framebuffer, &userdata->layer_view);
}

void test_pbr(int argc, char *argv[]) {
    const char *scene_name = argc > 2 ? argv[2] : NULL;
    scene_t *scene = test_create_scene(g_pbr_creators, scene_name);
    if (scene) {
        userdata_t userdata;
        userdata.scene = scene;
        pbr_layer_view_init(&userdata.layer_view);
        pbr_layer_view_acquire_labels(&userdata.layer_view);

        test_enter_mainloop(tick_function, &userdata);
        scene_release(scene);

        pbr_layer_view_release_labels(&userdata.layer_view);
    }
}
