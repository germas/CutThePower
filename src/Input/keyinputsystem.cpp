/** @ingroup Input */
/*@{*/

/**
 * @date 2014/02/18
 *
 * @todo Implement keybindings
 * @todo Implement textfield editing and add more useable characters
 * 
 * @file keyinputsystem.cpp
 */
/*@}*/

#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_scancode.h>

#include "../world.h"
#include "components.h"
#include "../systems.h"
#include "../Graphics/text.h"

#define SYSTEM_MASK (COMPONENT_COMMAND) /**< Entities with a command component will be processed by the system. */

int GetScancode(char *character);

extern int textField; /**< This references the textField variable in the mouseinputsystem for the currently active textfield. */
int *command_keys; /**< This is the current keycodes mapped to each command. */
extern const char *character_map;
extern bool running;

/**
 * Polls the keyboard for input and performs the appropriate action.
 *
 * Current player commands:
 * <ul>
 *    <li><b>W</b> - Up</li>
 *    <li><b>A</b> - Left</li>
 *    <li><b>S</b> - Down</li>
 *    <li><b>D</b> - Right</li>
 *    <li><b>Space</b> - Action</li>
 * </ul>
 *
 * @param[in, out]	world 	Pointer to WORLD (structure containing "world" information, entities/components)
 * @param[out] 		running	Whether the GAME is running or still in menu
 *
 * @designer Vincent Lau
 * @author Jordan Marling
 *
 */
void KeyInputSystem(World *world)
{
    int entity;
    CommandComponent *command;

    SDL_Event event;
    
    //keyboard status
    static const Uint8 *currentKeyboardState = 0; 	//the current status	
    static Uint8 *prevKeyboardState = 0;			//the previous status
    Uint8 *p = 0;									//save the current status and assign prevKeyboardState to this after loop.
    static int numKeys = 0;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = false;
        }
    }
    
    currentKeyboardState = SDL_GetKeyboardState(&numKeys);
    p = (Uint8*)malloc(sizeof(Uint8) * numKeys);
	memcpy(p, currentKeyboardState, sizeof(Uint8) * numKeys);

	
    if (prevKeyboardState == 0) {
		if (prevKeyboardState == 0) {
			prevKeyboardState = (Uint8*)malloc(sizeof(Uint8) * numKeys);
		}
		memcpy(prevKeyboardState, p, sizeof(Uint8) * numKeys);
        return;
	}

    //If a textfield is focused
    if (textField != -1) {
		
        TextFieldComponent *text = &(world->text[textField]);
        SDL_Scancode code;
		
		if (currentKeyboardState[SDL_SCANCODE_BACKSPACE] &&
			!prevKeyboardState[SDL_SCANCODE_BACKSPACE]) {
			text->length--;
			text->text[text->length] = '\0';
			if (text->length < 0) {
				text->length = 0;
			}
		}
		else if (text->length < MAX_STRING) {
			
			for(int i = 0; i <= 512; i++) {
				
				code = SDL_GetScancodeFromName((char*)&i);
				
				if (currentKeyboardState[code] &&
					!prevKeyboardState[code]) {
					
					text->text[text->length] = (char)i;
					text->length++;
					
					break;
				}
			}
		}
    }

    for(entity = 0; entity < MAX_ENTITIES; entity++) {

        if ((world->mask[entity] & SYSTEM_MASK) == SYSTEM_MASK)
        {
            command = &(world->command[entity]);

            command->commands[C_UP] = (currentKeyboardState[command_keys[C_UP]] != 0);
            command->commands[C_LEFT] = (currentKeyboardState[command_keys[C_LEFT]] != 0);
            command->commands[C_DOWN] = (currentKeyboardState[command_keys[C_DOWN]] != 0);
            command->commands[C_RIGHT] = (currentKeyboardState[command_keys[C_RIGHT]] != 0);

			
			command->commands[C_ACTION] = (currentKeyboardState[command_keys[C_ACTION]] != 0);// && (prevKeyboardState[command_keys[C_ACTION]] == 0);
			
			//THIS IS VENDING MACHINE SIMULIATOR CODE
			//DELETE IF YOU DO NOT WANT TO PLACE VENDING MACHINES!!!!!
			if (command->commands[C_ACTION]) {
				
				//unsigned int mainframe = create_entity(world, COMPONENT_RENDER_PLAYER | COMPONENT_POSITION | COMPONENT_ANIMATION | COMPONENT_COLLISION);
				
				/*int x = (int)((world->position[entity].x + world->position[entity].width / 2) / TILE_WIDTH);
				int y = (int)((world->position[entity].y + world->position[entity].height / 2) / TILE_HEIGHT);
				
				world->position[mainframe].x = x * TILE_WIDTH;
				world->position[mainframe].y = y * TILE_HEIGHT;*/
				
				/*world->position[mainframe].x = (world->position[entity].x / TILE_WIDTH) * TILE_WIDTH;
				world->position[mainframe].y = (world->position[entity].y / TILE_HEIGHT) * TILE_HEIGHT;
				
				world->position[mainframe].width = TILE_WIDTH;
				world->position[mainframe].height = TILE_HEIGHT;
				
				world->renderPlayer[mainframe].width = TILE_WIDTH;
				world->renderPlayer[mainframe].height = TILE_HEIGHT;*/
				
				//world->collision[mainframe].type = COLLISION_SOLID;
				//world->collision[mainframe].active = true;
				//world->collision[mainframe].radius = 1;

				//load_animation("assets/Graphics/objects/computers/mainframe_5_animation.txt", world, mainframe);
				//play_animation(world, mainframe, "mainframe");
				
			}
			//END DELETE
			
        }
    }
    
    //set the previous to the temp. current keystate so we don't get updates we never handled.
    if (prevKeyboardState == 0) {
		prevKeyboardState = (Uint8*)malloc(sizeof(Uint8) * numKeys);
	}
	memcpy(prevKeyboardState, p, sizeof(Uint8) * numKeys);
	free(p);
}

/**
 * Loads the desired keyboard commands into the game array.
 *
 * Current player commands:
 * <ul>
 *    <li><b>C_UP</b> - Up</li>
 *    <li><b>C_LEFT</b> - Left</li>
 *    <li><b>C_DOWN</b> - Down</li>
 *    <li><b>C_RIGHT</b> - Right</li>
 *    <li><b>C_ACTION</b> - Action</li>
 * </ul>
 *
 * @param[in]		file 	The file to load the data.
 *
 * @designer Jordan Marling
 * @author Jordan Marling
 *
 */
int KeyMapInit(const char *file) 
{
	return KeyMapInitArray(file, (int**)&command_keys);
}

/**
 * Loads the desired keyboard commands.
 *
 * Current player commands:
 * <ul>
 *    <li><b>C_UP</b> - Up</li>
 *    <li><b>C_LEFT</b> - Left</li>
 *    <li><b>C_DOWN</b> - Down</li>
 *    <li><b>C_RIGHT</b> - Right</li>
 *    <li><b>C_ACTION</b> - Action</li>
 * </ul>
 *
 * @param[in]		file 	The file to load the data.
 *
 * @designer Jordan Marling
 * @author Jordan Marling
 *
 */
int KeyMapInitArray(const char *file, int **command_array) 
{
	
	FILE *fp;
	
	char command[64];
	char value[64];
	
	if (file == 0 || (fp = fopen(file, "r")) == 0) {
		printf("Error opening file: %s\n", file);
		return -1;
	}
	
	*command_array = (int*)malloc(sizeof(int) * NUM_COMMANDS);
	
	(*command_array)[C_UP] = SDL_SCANCODE_W;
	(*command_array)[C_LEFT] = SDL_SCANCODE_A;
	(*command_array)[C_DOWN] = SDL_SCANCODE_S;
	(*command_array)[C_RIGHT] = SDL_SCANCODE_D;
	(*command_array)[C_ACTION] = SDL_SCANCODE_SPACE;
	
	while (!feof(fp)) {
		
		if (fscanf(fp, "%s %s", command, value) != 2) {
			
			if (feof(fp)) {
				return 0;
			}
			
			printf("Error loading line.\n");
			return -1;
		}
		
		if (strcmp(command, "C_UP") == 0) { //C_UP
			(*command_array)[C_UP] = GetScancode(value);
		}
		else if (strcmp(command, "C_LEFT") == 0) { //C_LEFT
			(*command_array)[C_LEFT] = GetScancode(value);
		}
		else if (strcmp(command, "C_DOWN") == 0) { //C_DOWN
			(*command_array)[C_DOWN] = GetScancode(value);
		}
		else if (strcmp(command, "C_RIGHT") == 0) { //C_RIGHT
			(*command_array)[C_RIGHT] = GetScancode(value);
		}
		else if (strcmp(command, "C_ACTION") == 0) { //C_ACTION
			(*command_array)[C_ACTION] = GetScancode(value);
		}
		else {
			printf("Unable to load command: %s\n", command);
		}
	}
	
	return 0;
}


/**
 * Returns the keycode based on a character.
 *
 * @param[in]		character 	The character to be loaded
 *
 * @designer Jordan Marling
 * @author Jordan Marling
 *
 */
 int GetScancode(char *character)
 {
	if (character == 0) {
		return -1;
	}
	
	if (strcmp(character, "UP") == 0) {
		return SDL_SCANCODE_UP;
	}
	if (strcmp(character, "LEFT") == 0) {
		return SDL_SCANCODE_LEFT;
	}
	if (strcmp(character, "DOWN") == 0) {
		return SDL_SCANCODE_DOWN;
	}
	if (strcmp(character, "RIGHT") == 0) {
		return SDL_SCANCODE_RIGHT;
	}
	if (strcmp(character, "SPACE") == 0) {
		return SDL_SCANCODE_SPACE;
	}
	
	
	return SDL_GetScancodeFromName(character);
 }
