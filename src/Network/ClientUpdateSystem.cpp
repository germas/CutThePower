/**
 * The client update system retrieves from the network module and
 * applies them to the world.
 *
 * @file client_update_system.cpp
 *
 * @todo Add "end of game" logic to client_udpate_objectives
 */
#include "Packets.h"
#include "GameplayCommunication.h"
#include "PipeUtils.h"
#include "../world.h"
#include "../systems.h"
#include "../Gameplay/collision.h"
#include "network_systems.h"
#include "../Input/chat.h"
typedef struct _objective_cache
{
	char  obj_state;
	short entity_no;
} objective_cache;

static int controllable_playerNo; /**< The entity number of the controllable player. */
extern int game_net_signalfd;     /**< An eventfd allowing the gameplay thread to request data from network router. */
extern int network_ready;         /**< Indicates whether the network has initialised. */
extern int player_team;           /**< The player's team (0, COPS or ROBBERS). */
int floor_change_flag = 0;        /**< Whether we just changed floors. */

unsigned int *player_table       = NULL; /**< A lookup table mapping server player numbers to client entities. */
objective_cache *objective_table = NULL; /**< A lookup table mapping server objective numbers to client entities. */

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
	 * @designer Clark Allenby
	 * @author Shane Spoor
 */
int client_update_system(World *world, int net_pipe) {
	void* 		packet;
	uint32_t 	type;
	uint32_t 	num_packets;
	uint64_t	signal = 1;
	unsigned	i;

    if(!network_ready) // Don't try to read the pipe until the network module has been initialised
        return -3;

	write(game_net_signalfd, &signal, sizeof(uint64_t));
	num_packets = read_type(net_pipe); // the function just reads a 32 bit value, so this works; semantically, not ideal

    if(num_packets == NET_SHUTDOWN) // network is shutting down; this is the only packet
    {
        char err_buf[128];
        uint32_t str_size;
        read_pipe(net_pipe, &str_size, sizeof(str_size));
        if(str_size)
        {
            read_pipe(net_pipe, err_buf, str_size);
            err_buf[str_size] = 0; // null terminate the string
            fprintf(stderr, "%s", err_buf);
        }
        memset(player_table, 255, MAX_PLAYERS);
        memset(objective_table, 255, MAX_OBJECTIVES);
        network_ready = 0;
        return -2;
    }

	for(i = 0; i < num_packets; ++i)
	{
		packet = read_data(net_pipe, &type);
		if(floor_change_flag == 1)
		{
			switch (type) 
			{ 
				case P_FLOOR_MOVE:
					client_update_floor(world, packet);
					break;
			}
		}
		else {
			switch (type) 
			{ 
				case P_CONNECT:
					if(client_update_info(world, packet) == CONNECT_CODE_DENIED)
					{
						return -1; // Pass error up to someone else to deal with
					}
					break;
				case G_STATUS:
					client_update_status(world, packet);
					break;
				case P_CHAT:
					client_update_chat(world, packet);
					break;
				case P_OBJCTV_LOC:
					client_update_objectives(world, packet);
					break;
				case 7: // undefined
					// Floor stuff
					break;
				case P_OBJSTATUS:
					client_update_objectives(world, packet);
					break;
				case G_ALLPOSUPDATE:
					client_update_pos(world, packet);
					break;
				case P_FLOOR_MOVE:
					client_update_floor(world, packet);
					break;
				default:
					break;
			}
		}
		free(packet);
	}

	return 0;
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
void client_update_chat(World *world, void *packet)
{
	PKT_SND_CHAT *snd_chat = (PKT_SND_CHAT*)packet;
	chat_add_line(snd_chat->message);
}

/**
 * Updates the the floor and position of the controllable player. If a player intiated
 * floor change occurs, will also set the floor change flag to 0 after setting the players
 * location.
 *
 * Revisions: <ul>
 *                <li>Shane Spoor - March 30th, 2014 - Added objective reconstruction on floor change.</li>
 *            </ul>
 *
 * @param[in, out]	world 	The world struct holding the data to be updated.
 * @param[in] 		packet	The packet containing update information.
 *
 * @designer Ramzi Chennafi
 * @author Ramzi Chennafi 
 */
void client_update_floor(World *world, void *packet)
{
	PKT_FLOOR_MOVE* floor_move = (PKT_FLOOR_MOVE*)packet;
	int obj_idx = (floor_move->new_floor - 1) * OBJECTIVES_PER_FLOOR; // Start at the correct offset for this floor (requires fixed number of objectives/floor)
	int i;

	world->position[player_table[controllable_playerNo]].level	= floor_move->new_floor;
	world->position[player_table[controllable_playerNo]].x		= floor_move->xPos;
	world->position[player_table[controllable_playerNo]].y		= floor_move->yPos;
	rebuild_floor(world, floor_move->new_floor);

	for(i = 0; i < MAX_ENTITIES; i++)
	{
		if(IN_THIS_COMPONENT(world->mask[i], COMPONENT_OBJECTIVE))
		{
			objective_table[obj_idx].entity_no = i;
			world->objective[i].status = objective_table[obj_idx].obj_state;
		}
	}

	floor_change_flag = 0;
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
	if(pos_update->floor == world->position[player_table[controllable_playerNo]].level){
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
	        if(i == controllable_playerNo)
				continue;
			
			if(player_table[i] != UNASSIGNED)
			{
				if(!pos_update->players_on_floor[i])
				{
					world->mask[player_table[i]] &= ~(COMPONENT_RENDER_PLAYER | COMPONENT_COLLISION); // If the player is no longer on the floor, turn off render and collision
				 	continue;
				}
				world->mask[player_table[i]] |= COMPONENT_RENDER_PLAYER | COMPONENT_COLLISION;
				world->movement[player_table[i]].movX	= pos_update->xVel[i];
				world->movement[player_table[i]].movY 	= pos_update->yVel[i];
				
				if(pos_update->xVel[i] < 0){
				 	world->movement[player_table[i]].lastDirection = DIRECTION_LEFT;
				}else if (pos_update->xVel[i] > 0){
					world->movement[player_table[i]].lastDirection = DIRECTION_RIGHT;
				}
				
				if(pos_update->yVel[i] < 0){
					world->movement[player_table[i]].lastDirection = DIRECTION_DOWN;
				}else if (pos_update->yVel[i] > 0){
					world->movement[player_table[i]].lastDirection = DIRECTION_UP;
				}
				
				world->position[player_table[i]].x		= pos_update->xPos[i];
				world->position[player_table[i]].y		= pos_update->yPos[i];
				world->position[player_table[i]].level	= pos_update->floor;
			}
		}
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
 *
 * @designer Shane Spoor
 * @author   Shane Spoor
 */
void client_update_objectives(World *world, void *packet)
{
	PKT_OBJECTIVE_STATUS *objective_update = (PKT_OBJECTIVE_STATUS *)packet;
	int obj_idx = (world->position[controllable_playerNo].level - 1) * OBJECTIVES_PER_FLOOR;

	if(objective_update->game_status == GAME_TEAM1_WIN || objective_update->game_status == GAME_TEAM2_WIN)
		player_team = 0;

	for(int i = 0; i < MAX_OBJECTIVES; ++i)
	{
	    if(objective_update->objectives_captured[i] == 0) // If the ojective is non-existent, then all following objectives are non-existent as well
	        break;
	    objective_table[i].obj_state = objective_update->objectives_captured[i];
	}
	
	for(int i = obj_idx; i < OBJECTIVES_PER_FLOOR + obj_idx; i++)
		world->objective[objective_table[i].entity_no].status = objective_table[i].obj_state;
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
 * @author Shane Spoor & Ramzi
 */
void client_update_status(World *world, void *packet)
{
	PKT_GAME_STATUS *status_update = (PKT_GAME_STATUS *)packet;

	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(status_update->player_valid[i] == true)
		{	
			if(player_table[i] == UNASSIGNED) // They're on the floor but haven't yet been created
	        {
	            player_table[i] = create_player(world, 400, 600, false, COLLISION_HACKER, i, status_update);	
	           	if(status_update->otherPlayers_teams[i] == COPS)
				{
					load_animation("assets/Graphics/player/p1/cop_animation.txt", world, player_table[i]);
				}
				else{
					setup_character_animation(world, status_update->characters[i], player_table[i]);
				}
	        }

	        else if(player_table[i] != UNASSIGNED && status_update->player_valid[i])
	        {
	        	if(status_update->otherPlayers_teams[i] == COPS)
	        	{
	        		change_player(world, COPS, status_update, i);
	        	}

	        	if(status_update->otherPlayers_teams[i] == ROBBERS)
	        	{
	        		change_player(world, ROBBERS, status_update, i);
	        	}

	        	if(status_update->otherPlayers_teams[i] == 0)
	        	{
			    	change_player(world, ROBBERS, status_update, i);
	        	}
	        }
		} 
		else
		{
			if(player_table[i] != UNASSIGNED)
			{
				destroy_entity(world, player_table[i]);
				player_table[i] = UNASSIGNED;
			}
		}
	}
}

void change_player(World * world, int type, PKT_GAME_STATUS * pkt, int playerNo)
{
	world->player[player_table[playerNo]].playerNo = playerNo;
	world->player[player_table[playerNo]].teamNo = pkt->otherPlayers_teams[playerNo];
	world->player[player_table[playerNo]].readyStatus = pkt->readystatus[playerNo];
	if(type == COPS)
	{
		world->collision[player_table[playerNo]].type = COLLISION_GUARD;
	}
	else{
		world->collision[player_table[playerNo]].type = COLLISION_HACKER;
	}
	if(type == COPS)
	{
		load_animation("assets/Graphics/player/p1/cop_animation.txt", world, player_table[playerNo]);
	}
	else{
		setup_character_animation(world, pkt->characters[playerNo], player_table[playerNo]);
	}
}

void setup_character_animation(World * world, int character, int entity)
{
	switch(character){
		case ABHISHEK:
			 load_animation("assets/Graphics/player/abhishek/animation.txt", world, entity);
		break;
		case AMAN:
			 load_animation("assets/Graphics/player/aman/animation.txt", world, entity);
		break;
		case ANDREW:
			 load_animation("assets/Graphics/player/andrew/animation.txt", world, entity);
		break;
		case CHRIS:
			 load_animation("assets/Graphics/player/chris/animation.txt", world, entity);
		break;
		case CORY:
			 load_animation("assets/Graphics/player/cory/animation.txt", world, entity);
		break;
		case DAMIEN:
			 load_animation("assets/Graphics/player/damien/animation.txt", world, entity);
		break;
		case CLARK:
		 	load_animation("assets/Graphics/player/clark/animation.txt", world, entity);
		break;
		case GERMAN:
			 load_animation("assets/Graphics/player/german/animation.txt", world, entity);
		break;
		case IAN:
			 load_animation("assets/Graphics/player/ian/animation.txt", world, entity);
		break;
		case JORDAN:
			 load_animation("assets/Graphics/player/jordan/animation.txt", world, entity);
		break;
		case JOSH:
			 load_animation("assets/Graphics/player/josh/animation.txt", world, entity);
		break;
		case KONST:
			 load_animation("assets/Graphics/player/konst/animation.txt", world, entity);
		break;
		case MAT:
			 load_animation("assets/Graphics/player/mat/animation.txt", world, entity);
		break;
		case RAMZI:
			 load_animation("assets/Graphics/player/ramzi/animation.txt", world, entity);
		break;
		case ROBIN:
			 load_animation("assets/Graphics/player/robin/animation.txt", world, entity);
		break;
		case SAM:
			 load_animation("assets/Graphics/player/sam/animation.txt", world, entity);
		break;
		case SHANE:
			 load_animation("assets/Graphics/player/shane/animation.txt", world, entity);
		break;
		case TIM:
			 load_animation("assets/Graphics/player/tim/animation.txt", world, entity);
		break;
		case VINCENT:
			 load_animation("assets/Graphics/player/vincent/animation.txt", world, entity);
		break;
		default:
		load_animation("assets/Graphics/player/p0/rob_animation.txt", world, entity);
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

	for (int i = 0; i < MAX_ENTITIES; i++)
	{
		if (IN_THIS_COMPONENT(world->mask[i], COMPONENT_MOVEMENT | COMPONENT_POSITION | COMPONENT_PLAYER | COMPONENT_CONTROLLABLE))
		{
			world->player[i].teamNo							= client_info->clients_team_number;
			world->player[i].playerNo						= client_info->clients_player_number;
			controllable_playerNo 							= client_info->clients_player_number;
			player_table[client_info->clients_player_number] = i;	
		}
	}

	return CONNECT_CODE_ACCEPTED;
}

/**
 * Initialises the lookup tables for players and objectives.
 *
 * @return 1 if the tables were initialised properly, or 0 if the memory couldn't be allocated.
 */
int init_client_update(World *world)
{
	objective_table = (objective_cache *)malloc(MAX_OBJECTIVES * sizeof(objective_cache));
	player_table = (unsigned int *)malloc(sizeof(unsigned int) * MAX_PLAYERS);

	if(!objective_table || !player_table)
	{
	    perror("init_client_update: malloc");
	    return 0;
	}

	memset(player_table, 255, MAX_PLAYERS * sizeof(unsigned int));
	memset(objective_table, 255, MAX_OBJECTIVES);
	return 1;
}


