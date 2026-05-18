#include <stddef.h>

#include "../scenes/blinn_scenes.h"
#include "../scenes/pbr_scenes.h"
#include "test_scenes.h"

creator_t g_blinn_creators[] = {
    {"azura", blinn_azura_scene},
    {"centaur", blinn_centaur_scene},
    {"craftsman", blinn_craftsman_scene},
    {"elfgirl", blinn_elfgirl_scene},
    {"kgirl", blinn_kgirl_scene},
    {"lighthouse", blinn_lighthouse_scene},
    {"mccree", blinn_mccree_scene},
    {"nier2b", blinn_nier2b_scene},
    {"phoenix", blinn_phoenix_scene},
    {"vivi", blinn_vivi_scene},
    {"whip", blinn_whip_scene},
    {"witch", blinn_witch_scene},
    {0, 0},
};

creator_t g_pbr_creators[] = {
    {"assassin", pbr_assassin_scene},
    {"buster", pbr_buster_scene},
    {"crab", pbr_crab_scene},
    {"dieselpunk", pbr_dieselpunk_scene},
    {"drone", pbr_drone_scene},
    {"helmet", pbr_helmet_scene},
    {"horse", pbr_horse_scene},
    {"junkrat", pbr_junkrat_scene},
    {"ornitier", pbr_ornitier_scene},
    {"ponycar", pbr_ponycar_scene},
    {"robot", pbr_robot_scene},
    {"sphere", pbr_sphere_scene},
    {"spheres", pbr_spheres_scene},
    {0, 0},
};

int test_scene_count(creator_t creators[]) {
    int count = 0;
    while (creators[count].scene_name != NULL) {
        count += 1;
    }
    return count;
}
