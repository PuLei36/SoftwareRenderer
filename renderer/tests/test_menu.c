#include <stdio.h>
#include <string.h>
#include "../core/api.h"
#include "../shaders/cache_helper.h"
#include "pbr_layer_view.h"
#include "test_helper.h"
#include "test_menu.h"
#include "test_scenes.h"

#define MENU_MAX_SCENES 32
#define MENU_MAX_ITEMS 40
#define MENU_TITLE_TOP 0.95f
#define MENU_TITLE_GAP 0.04f
#define MENU_ITEM_GAP 0.015f

static const vec3_t MENU_CAMERA_POSITION = {0, 0, 1.5f};
static const vec3_t MENU_CAMERA_TARGET = {0, 0, 0};

typedef enum {
    MENU_RENDERER_NONE,
    MENU_RENDERER_BLINN,
    MENU_RENDERER_PBR
} menu_renderer_t;

typedef enum {
    MENU_RENDERER_SELECT,
    MENU_MODEL_SELECT,
    MENU_RENDER
} menu_state_t;

typedef struct {
    const char *scene_name;
    texture_t *label;
} scene_label_t;

typedef struct {
    texture_t *label;
    vec2_t origin;
    vec2_t size;
    int is_back;
    const char *scene_name;
    menu_renderer_t renderer;
} menu_item_t;

typedef struct {
    menu_state_t state;
    menu_renderer_t renderer;
    scene_t *scene;

    pbr_layer_view_t pbr_view;
    int pbr_view_active;

    texture_t *title_renderer;
    texture_t *title_model;
    texture_t *label_pbr;
    texture_t *label_blinn;
    texture_t *label_back;

    scene_label_t blinn_models[MENU_MAX_SCENES];
    scene_label_t pbr_models[MENU_MAX_SCENES];
    int blinn_model_count;
    int pbr_model_count;

    menu_item_t renderer_items[2];
    int renderer_item_count;
    menu_item_t model_items[MENU_MAX_ITEMS];
    int model_item_count;
    menu_item_t render_back_item;
    int reset_camera;
} menu_context_t;

static texture_t *acquire_ui_label(const char *name) {
    char path[PATH_SIZE];
    sprintf(path, "common/ui/%s.tga", name);
    return cache_acquire_texture(path, USAGE_LDR_COLOR);
}

static void load_scene_labels(scene_label_t *items, int *out_count,
                              creator_t creators[]) {
    int count = test_scene_count(creators);
    int i;
    if (count > MENU_MAX_SCENES) {
        count = MENU_MAX_SCENES;
    }
    for (i = 0; i < count; i++) {
        items[i].scene_name = creators[i].scene_name;
        items[i].label = acquire_ui_label(creators[i].scene_name);
    }
    *out_count = count;
}

static void build_renderer_items(menu_context_t *menu) {
    memset(menu->renderer_items, 0, sizeof(menu->renderer_items));
    menu->renderer_item_count = 2;

    menu->renderer_items[0].label = menu->label_pbr;
    menu->renderer_items[0].renderer = MENU_RENDERER_PBR;
    menu->renderer_items[1].label = menu->label_blinn;
    menu->renderer_items[1].renderer = MENU_RENDERER_BLINN;
}

static void build_model_items(menu_context_t *menu) {
    scene_label_t *models = NULL;
    int count = 0;
    int i;

    memset(menu->model_items, 0, sizeof(menu->model_items));
    menu->model_item_count = 0;

    menu->model_items[0].label = menu->label_back;
    menu->model_items[0].is_back = 1;
    menu->model_item_count = 1;

    if (menu->renderer == MENU_RENDERER_PBR) {
        models = menu->pbr_models;
        count = menu->pbr_model_count;
    } else if (menu->renderer == MENU_RENDERER_BLINN) {
        models = menu->blinn_models;
        count = menu->blinn_model_count;
    }

    for (i = 0; i < count && menu->model_item_count < MENU_MAX_ITEMS; i++) {
        int index = menu->model_item_count;
        menu->model_items[index].label = models[i].label;
        menu->model_items[index].scene_name = models[i].scene_name;
        menu->model_item_count += 1;
    }
}

static void layout_items(framebuffer_t *framebuffer, menu_item_t *items,
                         int count, float top_y, float gap) {
    float y = top_y;
    int i;
    for (i = 0; i < count; i++) {
        texture_t *label = items[i].label;
        float width = (float)label->width / (float)framebuffer->width;
        float height = (float)label->height / (float)framebuffer->height;
        float x = 0.5f - width * 0.5f;
        float y0 = y - height;
        items[i].origin = vec2_new(x, y0);
        items[i].size = vec2_new(width, height);
        y = y0 - gap;
    }
}

static void draw_item_box(framebuffer_t *framebuffer, menu_item_t *item) {
    vec4_t color = vec4_new(0.3f, 0.3f, 0.3f, 1);
    vec2_t p0 = item->origin;
    vec2_t p1 = vec2_new(item->origin.x + item->size.x, item->origin.y);
    vec2_t p2 = vec2_new(item->origin.x + item->size.x,
                         item->origin.y + item->size.y);
    vec2_t p3 = vec2_new(item->origin.x, item->origin.y + item->size.y);
    draw2d_draw_line(framebuffer, color, p0, p1);
    draw2d_draw_line(framebuffer, color, p1, p2);
    draw2d_draw_line(framebuffer, color, p2, p3);
    draw2d_draw_line(framebuffer, color, p3, p0);
}

static float draw_title(framebuffer_t *framebuffer, texture_t *title) {
    float width = (float)title->width / (float)framebuffer->width;
    float height = (float)title->height / (float)framebuffer->height;
    float x = 0.5f - width * 0.5f;
    float y0 = MENU_TITLE_TOP - height;
    draw2d_draw_texture(framebuffer, title, vec2_new(x, y0));
    return y0;
}

static void draw_items(framebuffer_t *framebuffer, menu_item_t *items,
                       int count) {
    int i;
    for (i = 0; i < count; i++) {
        draw_item_box(framebuffer, &items[i]);
        draw2d_draw_texture(framebuffer, items[i].label, items[i].origin);
    }
}

static int hit_item(menu_item_t *items, int count, vec2_t pos) {
    int i;
    for (i = 0; i < count; i++) {
        float x0 = items[i].origin.x;
        float y0 = items[i].origin.y;
        float x1 = x0 + items[i].size.x;
        float y1 = y0 + items[i].size.y;
        if (pos.x >= x0 && pos.x <= x1 && pos.y >= y0 && pos.y <= y1) {
            return i;
        }
    }
    return -1;
}

static void clear_menu(framebuffer_t *framebuffer) {
    vec4_t color = vec4_new(0.08f, 0.08f, 0.09f, 1);
    framebuffer_clear_color(framebuffer, color);
    framebuffer_clear_depth(framebuffer, 1);
}

static void enter_render_state(menu_context_t *menu, const char *scene_name) {
    creator_t *creators = NULL;
    if (menu->renderer == MENU_RENDERER_PBR) {
        creators = g_pbr_creators;
    } else if (menu->renderer == MENU_RENDERER_BLINN) {
        creators = g_blinn_creators;
    }
    if (creators == NULL) {
        return;
    }

    menu->scene = test_create_scene(creators, scene_name);
    if (menu->scene) {
        if (menu->renderer == MENU_RENDERER_PBR && !menu->pbr_view_active) {
            pbr_layer_view_init(&menu->pbr_view);
            pbr_layer_view_acquire_labels(&menu->pbr_view);
            menu->pbr_view_active = 1;
        }
        menu->state = MENU_RENDER;
        menu->reset_camera = 1;
    }
}

static void handle_renderer_click(menu_context_t *menu, vec2_t click_pos) {
    int hit = hit_item(menu->renderer_items, menu->renderer_item_count,
                       click_pos);
    if (hit >= 0) {
        menu->renderer = menu->renderer_items[hit].renderer;
        build_model_items(menu);
        menu->state = MENU_MODEL_SELECT;
    }
}

static void handle_model_click(menu_context_t *menu, vec2_t click_pos) {
    int hit = hit_item(menu->model_items, menu->model_item_count, click_pos);
    if (hit >= 0) {
        menu_item_t *item = &menu->model_items[hit];
        if (item->is_back) {
            menu->state = MENU_RENDERER_SELECT;
        } else if (item->scene_name) {
            enter_render_state(menu, item->scene_name);
        }
    }
}

static void render_menu_screen(menu_context_t *menu, context_t *context) {
    framebuffer_t *framebuffer = context->framebuffer;
    float title_bottom;
    clear_menu(framebuffer);

    if (menu->state == MENU_RENDERER_SELECT) {
        title_bottom = draw_title(framebuffer, menu->title_renderer);
        layout_items(framebuffer, menu->renderer_items,
                     menu->renderer_item_count,
                     title_bottom - MENU_TITLE_GAP, MENU_ITEM_GAP);
        if (context->single_click) {
            handle_renderer_click(menu, context->click_pos);
        }
        draw_items(framebuffer, menu->renderer_items,
                   menu->renderer_item_count);
    } else if (menu->state == MENU_MODEL_SELECT) {
        title_bottom = draw_title(framebuffer, menu->title_model);
        layout_items(framebuffer, menu->model_items,
                     menu->model_item_count,
                     title_bottom - MENU_TITLE_GAP, MENU_ITEM_GAP);
        if (context->single_click) {
            handle_model_click(menu, context->click_pos);
        }
        draw_items(framebuffer, menu->model_items, menu->model_item_count);
    }
}

static void leave_render_state(menu_context_t *menu) {
    if (menu->scene) {
        scene_release(menu->scene);
        menu->scene = NULL;
    }
    if (menu->renderer == MENU_RENDERER_PBR && menu->pbr_view_active) {
        pbr_layer_view_release_labels(&menu->pbr_view);
        menu->pbr_view_active = 0;
    }
    menu->state = MENU_MODEL_SELECT;
}

static void layout_render_back_item(framebuffer_t *framebuffer,
                                    menu_item_t *item) {
    texture_t *label = item->label;
    float width = (float)label->width / (float)framebuffer->width;
    float height = (float)label->height / (float)framebuffer->height;
    float x = 0.03f;
    float y0 = 0.97f - height;
    item->origin = vec2_new(x, y0);
    item->size = vec2_new(width, height);
}

static int handle_render_back_click(menu_context_t *menu, context_t *context) {
    menu_item_t *item = &menu->render_back_item;
    if (!item->label) {
        return 0;
    }

    layout_render_back_item(context->framebuffer, item);
    if (context->single_click) {
        if (hit_item(item, 1, context->click_pos) >= 0) {
            leave_render_state(menu);
            return 1;
        }
    }
    return 0;
}

static void draw_render_back(menu_context_t *menu, framebuffer_t *framebuffer) {
    menu_item_t *item = &menu->render_back_item;
    if (!item->label) {
        return;
    }
    layout_render_back_item(framebuffer, item);
    draw_item_box(framebuffer, item);
    draw2d_draw_texture(framebuffer, item->label, item->origin);
}

static void reset_camera_if_needed(menu_context_t *menu, context_t *context) {
    if (!menu->reset_camera) {
        return;
    }
    camera_set_transform(context->camera,
                         MENU_CAMERA_POSITION,
                         MENU_CAMERA_TARGET);
    menu->reset_camera = 0;
}

static void render_scene(menu_context_t *menu, context_t *context) {
    perframe_t perframe;
    if (handle_render_back_click(menu, context)) {
        render_menu_screen(menu, context);
        return;
    }
    reset_camera_if_needed(menu, context);
    perframe = test_build_perframe(menu->scene, context);
    if (menu->renderer == MENU_RENDERER_PBR) {
        perframe.layer_view = pbr_layer_view_update(&menu->pbr_view, context);
    }
    test_draw_scene(menu->scene, context->framebuffer, &perframe);
    if (menu->renderer == MENU_RENDERER_PBR) {
        pbr_layer_view_draw(context->framebuffer, &menu->pbr_view);
    }
    draw_render_back(menu, context->framebuffer);
}

static void tick_function(context_t *context, void *userdata) {
    menu_context_t *menu = (menu_context_t*)userdata;
    if (menu->state == MENU_RENDER && menu->scene != NULL) {
        render_scene(menu, context);
    } else {
        render_menu_screen(menu, context);
    }
}

static void release_scene_labels(scene_label_t *items, int count) {
    int i;
    for (i = 0; i < count; i++) {
        cache_release_texture(items[i].label);
        items[i].label = NULL;
    }
}

void test_menu(int argc, char *argv[]) {
    menu_context_t menu;

    UNUSED_VAR(argc);
    UNUSED_VAR(argv);

    memset(&menu, 0, sizeof(menu_context_t));
    menu.state = MENU_RENDERER_SELECT;
    menu.renderer = MENU_RENDERER_NONE;

    menu.title_renderer = acquire_ui_label("select-renderer");
    menu.title_model = acquire_ui_label("select-model");
    menu.label_pbr = acquire_ui_label("pbr");
    menu.label_blinn = acquire_ui_label("blinn-phong");
    menu.label_back = acquire_ui_label("back");
    menu.render_back_item.label = menu.label_back;
    menu.render_back_item.is_back = 1;

    load_scene_labels(menu.blinn_models, &menu.blinn_model_count,
                      g_blinn_creators);
    load_scene_labels(menu.pbr_models, &menu.pbr_model_count,
                      g_pbr_creators);

    build_renderer_items(&menu);
    build_model_items(&menu);

    test_enter_mainloop(tick_function, &menu);

    if (menu.scene) {
        scene_release(menu.scene);
        menu.scene = NULL;
    }
    if (menu.pbr_view_active) {
        pbr_layer_view_release_labels(&menu.pbr_view);
        menu.pbr_view_active = 0;
    }

    cache_release_texture(menu.title_renderer);
    cache_release_texture(menu.title_model);
    cache_release_texture(menu.label_pbr);
    cache_release_texture(menu.label_blinn);
    cache_release_texture(menu.label_back);

    release_scene_labels(menu.blinn_models, menu.blinn_model_count);
    release_scene_labels(menu.pbr_models, menu.pbr_model_count);
}
