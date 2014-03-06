/**
 * The client update system retrieves from the network module and
 * applies them to the world.
 *
 * @file client_update_system.cpp
 */

#include <stdlib.h>
#include <string.h>
#include "Packets.h"
#include "network_systems.h"
#include "GameplayCommunication.h"
#include "PipeUtils.h"

/**
 * TODO:	Rearrange the switch statement to put the most likely packets first. 
 *			This would be, in order: 11, 8, 4, 7 (?), and we'll have to decide the rest later
 */

extern int game_net_signalfd, game_net_lockfd;
static unsigned int *player_table = NULL; /**< A lookup table mapping server player numbers to client entities. */

/**
 * Receives all updates from the server and applies them to the world.
 *
 * The function updates all relevant networking information: movement data from other players,
 * objective updates, floor changes, and the initial player information (names, team numbers and
 * player numbers).
 *
 * @param[in] world 	The world struct to be updated.
 * @param[in] net_pipe	The read end of a pipe connected to the network module.
 *
 * @designer Shane Spoor
 * @author Shane Spoor
 */
void client_update_system(World *world, int net_pipe) {
	void* 		packet;
	uint32_t 	type;
	uint32_t 	num_packets;
	unsigned	i;
	
	if(!player_table)
	{
		player_table = (unsigned int *)malloc(sizeof(unsigned int) * MAX_PLAYERS);
		memset(player_table, 255, MAX_PLAYERS * sizeof(unsigned int)); 
	}
	
	write(
	// Wait on the other semaphore
	
	num_packets = read_type(net_pipe); // the function just reads a 32 bit value, so this works; semantically, not ideal
	
	for(i = 0; i < num_packets; ++i)
	{
		packet = read_data(net_pipe, &type);
		if(!packet)
			return; // might need to check errno here and log error if there was one
	
		switch (type) {
			case 2:
				if(client_update_info(world, packet) == CONNECT_CODE_DENIED)
					return; // Pass error up to someone else to deal with
				break;
			case 3:
				client_update_status(world, packet);
				break;
			case 4:
				// Chat data
				break;
			case 6:
				// Map info
				break;
			case 7:
				// Floor stuff
				break;
			case 8:
				// Objective update (game_status is significant here)
				break;
			case 11:
				client_update_pos(world, packet);
				break;
		
			// Should never receive a packet outside the above range (the rest are unpurposed or client->server packets); 
			// discard it (and maybe log an error) if we get one
			case 1:
			case 5:
			case 9:
			case 10:
			case 12:
			default:
				break;
		}
		free(packet);
	}
}

/**
 * Updates the positions and movement properties of every other player.
 *
 * The function will ignore players that aren't on the current floor and the client's
 * own player, since they're said to be authoritative over their own position (except
 * for their floor).
 *
 * @param[in, out]	world 	The world struct holding the data to be updated.
 * @param[in] 		packet	The packet containing update information.
 *
 * @designer Shane Spoor
 * @author Shane Spoor
 */
void client_update_pos(World *world, void *packet)
{
	PKT_ALL_POS_UPDATE *pos_update = (PKT_ALL_POS_UPDATE *)packet;
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (!pos_update->players_on_floor[i] || player_table[i] == CLIENT_PLAYER) // If they're not on this floor or it's our player
			continue;

		world->movement[player_table[i]].movX	= pos_update->xVel[i];
		world->movement[player_table[i]].movY 	= pos_update->yVel[i];
		world->position[player_table[i]].x		= pos_update->xPos[i];
		world->position[player_table[i]].y		= pos_update->yPos[i];
		world->position[player_table[i]].level	= pos_update->floor;
	}
}

/**
 * Udpates the objective statuses and the game state.
 *
 * If all objectives have been captured, the game is over. The server will
 * indicate this using the game status property.
 *
 * @param[out]	world 	The world struct containing the ojective states to be updated.
 * @param[in]	packet	The packet containing objective update information.
 */
void client_update_objectives(World *world, void *packet)
{
	PKT_OBJECTIVE_STATUS *objective_update = (PKT_OBJECTIVE_STATUS *)packet;
	for(int i = 0; i < MAX_OBJECTIVES; ++i)
	{
		// Code to update (currently non-existing) objectives	
	}
}

/**
 * Updates the status and team details of all other players.
 *
 * The client receives a separate packet containg this information for it specifically, so
 * it ignores its own information.
 *
 * @param[in, out] 	world	The world struct holding the details to be updated.
 * @param[in] 		packet	The packet holding the update information.
 *
 * @designer Shane Spoor
 * @author Shane Spoor
 */
void client_update_status(World *world, void *packet)
{
	PKT_GAME_STATUS *status_update = (PKT_GAME_STATUS *)packet;
	unsigned int entity;
	for(int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(!status_update->player_valid[i] || player_table[i] == CLIENT_PLAYER) // Don't update if they're not valid or it's our player
			continue;
		
		if(player_table[i] == UNASSIGNED)
		{
			entity 								= create_player(*world, 0, 0, false);
			world->player[entity].teamNo 		= status_update->otherPlayers_teams[i];
			world->player[entity].playerNo 		= i + 1;
			player_table[i] 					= entity;
			strcpy(world->player[entity].name, status_update->otherPlayers_name[i]);
		}
		
		world->player[entity].readyStatus = status_update->readystatus[i];
	}
}

/**
 * Updates the client's player number and team details.
 *
 * The client should only receive this packet once at the beginning of each game.
 *
 * @param[out] 	world	The world struct to hold the client's information.
 * @param[in]	packet	The packet containing the client's team and player details.
 *
 * @return 	CONNECT_CODE_DENIED if the client's connection attempt was for some reason
 *			denied by the server, or CONNECT_CODE_ACCEPTED otherwise.
 *
 * @designer Shane Spoor
 * @author Shane Spoor
 */
int client_update_info(World *world, void *packet)
{
	PKT_PLAYER_CONNECT *client_info = (PKT_PLAYER_CONNECT *)packet;
	if(client_info->connect_code == CONNECT_CODE_DENIED)
		return CONNECT_CODE_DENIED;

	world->player[CLIENT_PLAYER].teamNo					= client_info->clients_team_number;
	world->player[CLIENT_PLAYER].playerNo				= client_info->clients_player_number;
	player_table[client_info->clients_player_number]	= CLIENT_PLAYER; // Or whatever the client's player entity is assigned
	
	return CONNECT_CODE_ACCEPTED;
}

