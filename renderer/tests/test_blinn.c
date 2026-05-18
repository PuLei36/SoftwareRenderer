#include <stddef.h>
#include "../core/api.h"
#include "test_blinn.h"
#include "test_helper.h"
#include "test_scenes.h"

static void tick_function(context_t *context, void *userdata) {
    scene_t *scene = (scene_t*)userdata;
    perframe_t perframe = test_build_perframe(scene, context);
    test_draw_scene(scene, context->framebuffer, &perframe);
}

void test_blinn(int argc, char *argv[]) {
    const char *scene_name = argc > 2 ? argv[2] : NULL;
    scene_t *scene = test_create_scene(g_blinn_creators, scene_name);
    if (scene) {
        test_enter_mainloop(tick_function, scene);
        scene_release(scene);
    }
}
