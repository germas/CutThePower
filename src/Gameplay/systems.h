/** @ingroup Gameplay */
/** @{ */
/** @file systems.h */
/** @} */
#ifndef GAMEPLAY_SYSTEMS_H
#define GAMEPLAY_SYSTEMS_H

#include <SDL2/SDL.h>
#include "../Network/SendSystem.h"
#include "collision.h"

#include "../world.h"
void add_force(World* world, unsigned int entity, float magnitude, float dir);
void apply_force(World* world, unsigned int entity);
void movement_system(World* world, FPS fps, int sendpipe);
void update_system(World* world);
int handle_entity_collision(World* world, unsigned int entity, unsigned int entity_number, unsigned int tile_number, unsigned int hit_entity);
void rebuild_floor(World * world);

#endif
