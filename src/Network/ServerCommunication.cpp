/** @ingroup Network */
/** @{ */

/**
 * This file contains all methods responsible for communication with the server.
 *
 * @todo Get rid of magic numbers in grab_send_packet
 * @todo Make writing to cnt_errno thread safe
 * @todo Write to network router requesting that keep alive be sent
 * 
 * @file ServerCommunication.cpp
 */

/** @} */

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "NetworkRouter.h"
#include "ServerCommunication.h"
#include "GameplayCommunication.h"
#include "PipeUtils.h"
#include "Packets.h"

extern uint32_t packet_sizes[NUM_PACKETS];
static int cnt_errno = -1;
extern sem_t err_sem;
static uint64_t tcp_seq_num = 0;

/**
 * Monitors sockets to receive data from the server.
 *
 * Upon receiving data, the thread writes to the pipe connected to the network
 * router thread. The thread will return in case of any error condition (wrapper functions
 * are responsible for minor error handling; if the thread returns, network should stop
 * running altogether).
 *
 * @param[in] ndata Pointer to a NETWORK_DATA struct containing socket descriptors and
 *                  the pipe end to the Network Router thread.
 *
 * @return NULL upon termination.
 *
 * @designer Shane Spoor
 * @author   Shane Spoor
 *
 * @date February 16, 2014
 *
 */
 void *recv_thread_func(void *ndata)
 {
 	NDATA				recv_data = (NDATA)ndata;
 	int 				numready;
 	SDLNet_SocketSet 	set = make_socket_set(2, recv_data->tcp_sock, recv_data->udp_sock);
	int 				res = 0;
 	if(!set)
 		return NULL;
 	
 	while(1)
 	{
 		if((numready = check_sockets(set)) == -1)
        {
            // write error message to the network router
     	    break;
        }
 
        else if(numready == 0) // Timed out; tell network router to check if server is down
        {
            
        }

		else
		{
			if(SDLNet_SocketReady(recv_data->tcp_sock))
			{
				if((res = handle_tcp_in(recv_data->write_pipe, recv_data->tcp_sock)) == -1)
				{
                    break;
                }
                if(res == -2)
                {
                	continue;
            	}	
            }

    		if(SDLNet_SocketReady(recv_data->udp_sock))
    		{
				if((res = handle_udp_in(recv_data->write_pipe, recv_data->udp_sock)) == -1)
				{
                    break;
                }
                if(res == -2)
                {
                	continue;
				}
			}
    	}
 	}

    SDLNet_FreeSocketSet(set);
    return NULL;
}

/**
 * Sends data received from the network router pipe to the server.
 * 
 * The thread gets the data from the pipe and determines the protocol (UDP or TCP)
 * to use, then sends the packet on corresponding socket.
 *
 * @param[in] ndata NETWORK_DATA containing a tcp socket, udp socket and a file
 *                  descriptor to the network router send pipe.
 *
 * @return  NULL upon termination
 *
 * @designer Ramzi Chennafi
 * @author   Ramzi Chennafi
 * 
 * @date Febuary 13 2014
 */
void* send_thread_func(void* ndata){

	NDATA snd_data = (NDATA) ndata;

	int protocol = 0;
	uint32_t type = 0;
	void * data;
	int ret = -1;

	while(1){	
    	data = grab_send_packet(&type, snd_data->read_pipe, &ret);
    	if(ret != 1){
			continue;
		}
		
		protocol = get_protocol(type);
		if(protocol == TCP)
		{
			send_tcp(&type, snd_data->tcp_sock, sizeof(uint32_t));
			send_tcp(data, snd_data->tcp_sock, packet_sizes[type - 1]);
		}
		else if(protocol == UDP)
		{
			send_udp(data, &type, snd_data->udp_sock, packet_sizes[type - 1] + sizeof(uint32_t));
		}
	}
	return NULL;
}

/**
 * Sends the packet data over the established TCP connection.
 *
 * @param[in] data Pointer to the data packet to send over TCP.
 * @param[in] sock The socket on which to send the data.
 * @param[in] size The size of the packet being sent.
 *
 * @return <ul>
 *              <li>Returns 0 on success.</li>
 *				<li>Returns -1 if there's an error on send.</li>
 *          </ul> 
 *
 * @designer Ramzi Chennafi
 * @author   Ramzi Chennafi
 *
 * @date January 20, 2014
 */
int send_tcp(void * data, TCPsocket sock, uint32_t size){

	int result=SDLNet_TCP_Send(sock, data, size);
	if(result <= 0) {
    	fprintf(stderr, "SDLNet_TCP_Send: %s\n", SDLNet_GetError());
    	return -1;
	}

	return 0;
}

/**
 * Sends the specified data over a UDP socket.
 *
 * Allocates the UDP packet, sends it, and frees the packet upon completion.
 * If sending the packet was unsuccessful, the function prints an error message.
 *
 * @param[in] data Pointer to the data packet to send over UDP.
 * @param[in] type The type of packet to send.
 * @param[in] sock The socket on which to send the data.
 * @param[in] size The size of the packet.
 *
 * @return <ul>
 *              <li>Returns 0 on success.</li>
 *				<li>Returns -1 if there's an error on send.</li>
 *         </ul>
 *
 * @designer Ramzi Chennafi
 * @author   Ramzi Chennafi
 *
 * @date Febuary 15, 2014
 */
int send_udp(void * data, uint32_t * type, UDPsocket sock, uint32_t size){

	int numsent;
	UDPpacket *pktdata = alloc_packet(size);
	memcpy(pktdata->data, type, sizeof(uint32_t));
	memcpy(pktdata->data + sizeof(uint32_t), data, size - sizeof(uint32_t));
	pktdata->len = size;

	numsent=SDLNet_UDP_Send(sock, pktdata->channel, pktdata);
	if(numsent < 0) {
    	fprintf(stderr,"SDLNet_UDP_Send: %s\n", SDLNet_GetError());
    	return -1;
	}

	SDLNet_FreePacket(pktdata);
	return 0;
}

/**
 * Handles the receipt of TCP data.
 *
 * Receives the TCP packet, if any, and writes it to the network router. Keep alive packets are
 * ignored. On error, it writes an error message (preceded by an packet type indicating an error)
 * and returns -1.
 *
 * @param[in] router_pipe_fd Descriptor for the write end of the pipe to the network router thread.
 * @param[in] tcp_sock       The TCP socket from which to receive data.
 *
 * @return 0 on success, or -1 if an error occurred. 
 *
 * @designer Shane Spoor
 * @author   Shane Spoor
 *
 */
int handle_tcp_in(int router_pipe_fd, TCPsocket tcp_sock)
{
    void *game_packet;
    uint32_t packet_type;
    uint64_t timestamp;
    
    if((game_packet = recv_tcp_packet(tcp_sock, &packet_type, &timestamp)) == NULL)
    {
        // if(packet_type != P_KEEPALIVE)
        // {
            if(cnt_errno == ERR_TCP_RECV_FAIL)
            {
                fprintf(stderr, "Failure in TCP receive : %s \n", SDLNet_GetError());
                return -1;
            }

            if(cnt_errno == ERR_CONN_CLOSED)
            {
                fprintf(stderr, "Server closed the connection.\n");
                //close_connections(set, recv_data->tcp_sock, recv_data->udp_sock);
                return -1;
            }


	        if(cnt_errno == ERR_CORRUPTED)
	        {
	        	return -2;
	        }
        //}
        // else if(write_pipe(router_pipe_fd, &packet_type, sizeof(packet_type)) == -1)
        //     return -1;
    }
    
    printf("Received TCP packet: %u\n", packet_type);
    if(write_packet(router_pipe_fd, packet_type, game_packet) == -1 ||
        write_pipe(router_pipe_fd, &timestamp, sizeof(timestamp)) == -1)
    {
        fprintf(stderr, "TCP>Router: Error in write packet, flushing pipe");
        fflush((FILE*)&router_pipe_fd);
	}
    free(game_packet);
    return 0;
}

/**
 * Handles the receipt of UDP packets.
 *
 * Receives the UDP packet and writes it to the network router. On error, it writes an error message 
 * (preceded by an packet type indicating an error) and returns -1.
 *
 * @param[in] router_pipe_fd Descriptor for the write end of the pipe to the network router thread.
 * @param[in] udp_sock       The UDP socket from which to receive data.
 * 
 * @return 0 on success, or -1 if an error occurred. 
 *
 * @designer Shane Spoor
 * @author   Shane Spoor
 *
 * @date March 12, 2014
 *
 */
int handle_udp_in(int router_pipe_fd, UDPsocket udp_sock)
{
    void *game_packet;
    uint32_t packet_type;
    uint64_t timestamp;

    if((game_packet = recv_udp_packet(udp_sock, &packet_type, &timestamp)) == NULL)
    {
        if(cnt_errno == ERR_UDP_RECV_FAIL)
        {
            fprintf(stderr, "Failure in UDP receive: %s ", SDLNet_GetError());
            return -1;
        }

        if(cnt_errno == ERR_CORRUPTED)
        {
        	return -2;
        }
    }
    printf("Received UDP packet: %u\n", packet_type);	
    if(write_packet(router_pipe_fd, packet_type, game_packet) == -1 ||
        write_pipe(router_pipe_fd, &timestamp, sizeof(timestamp)) == -1)
    {
        fprintf(stderr, "UDP>Router: Error in write packet, flushing pipe");
        fflush((FILE*)&router_pipe_fd);
    }
    free(game_packet);
    return 0;
}

/**
 * Reads a packet from the specified TCP socket.
 *
 * The function stores the packet in an allocated buffer if it's read successfully
 * and stores the packet type and timestamp in the variables passed by the caller.
 *
 * @param[in]  sock        The TCP socket from which to read.
 * @param[out] packet_type Holds the packet type on successful return. It may hold an invalid
 *                         packet type on failure.
 * @param[out] timestamp   Holds the timestamp on successful return.
 *
 * @return  A data buffer containing the packet on success and NULL on failure.
 *
 * @designer Shane Spoor
 * @author   Shane Spoor
 *
 * @date February 18th, 2014
 */
void *recv_tcp_packet(TCPsocket sock, uint32_t *packet_type, uint64_t *timestamp)
{
	void     *packet;
	int      numread;
    uint32_t packet_size;

	numread = recv_tcp(sock, packet_type, sizeof(uint32_t));
	if(numread < 0){
		set_error(ERR_TCP_RECV_FAIL);
		return NULL;
	}

	if(numread == 0){
		set_error(ERR_CONN_CLOSED);
		return NULL;
	}

    if(*packet_type == P_KEEPALIVE)
        return NULL;

	if((*packet_type <= 0 || *packet_type > NUM_PACKETS) && *packet_type)
	{
		fprintf(stderr, "recv_tcp_packet: Received Invalid Packet Type!\n");
		set_error(ERR_CORRUPTED);
		return NULL; 
	}

	packet_size = packet_sizes[(*packet_type) - 1];

	if((packet = malloc(packet_size)) == NULL)
	{
		perror("recv_ tcp_packet: malloc");
		set_error(ERR_NO_MEM);
		return NULL;
	}

	numread = recv_tcp(sock, packet, packet_size);
	*timestamp = tcp_seq_num++;
	return packet;
}

/**
 * Receives and processes a UDP packet containing a packet type, game data,
 * and a timestamp.
 *
 * The packet type and timestamp will be stored in @a packet_type and @a timestamp
 * if the function completes successfully. @a packet_type will contain an invalid
 * packet type if one is read; similarly, the contents of timestamp will be invalid
 * if the function receives an invalid timestamp. Note that the timestamp is not checked
 * for validity.
 *
 * @param[in]  sock        The UDP socket to receive data from.
 * @param[out] packet_type Receives the packet type.
 * @param[out] timestamp   Receives the timestamp.
 *
 * @return <ul>
 *              <li>Returns a data buffer containing the packet on success.</li>
 *				<li>Returns NULL on failure.</li>
 *         </ul>
 *
 * @designer Shane Spoor
 * @designer Ramzi Chennafi
 * @author   Shane Spoor
 *
 * @date Febuary 15, 2014
 */
void *recv_udp_packet(UDPsocket sock, uint32_t *packet_type, uint64_t *timestamp)
{
	void      *packet;
	uint32_t  packet_size;
    UDPpacket *pktdata = SDLNet_AllocPacket(MAX_UDP_RECV + sizeof(*packet_type) + sizeof(*timestamp));

	*packet_type = 1;

	if(recv_udp(sock, pktdata) == -1)
		return NULL;

	*packet_type 	= *((uint32_t *)pktdata->data);
    if(*packet_type < 1 || *packet_type > NUM_PACKETS) // Impossible packet type; packet is corrupted
    {
		fprintf(stderr, "recv_udp_packet: Received Invalid Packet Type!\n");
        cnt_errno = ERR_CORRUPTED;
        return NULL;
    }

	packet_size 	= packet_sizes[(*packet_type) - 1];
	packet			= malloc(packet_size);

	if(!packet)
	{
		cnt_errno = errno; // For printing later
		return NULL;
	}
	
	memcpy(packet, pktdata->data + sizeof(uint32_t), packet_size);
    memcpy(timestamp, pktdata->data + packet_size + sizeof(uint32_t), sizeof(*timestamp));
	*timestamp = *((uint64_t *)(pktdata->data + packet_size + sizeof(uint32_t)));

	SDLNet_FreePacket(pktdata);
	return packet;
}

/**
 * Reads the specified amount of data from a TCP socket.
 *
 * @param[in]  tcp_socket The TCP socket from which to receive data.
 * @param[out] buf        The buffer into which the data will be read.
 * @param[in]  bufsize    The size (in bytes) of buf.cd
 *
 * @return ERR_RECV_FAILED if SDLNet_TCP_Recv returns an error, and ERR_CONN_CLOSED if no
 *         data was read (i.e., received a RST or a FIN). Returns 0 on success.
 *
 * @designer Shane Spoor
 * @author   Shane Spoor
 *
 * @date Febuary 21, 2014
 */
int recv_tcp(TCPsocket sock, void *buf, size_t bufsize)
{
	int numread = SDLNet_TCP_Recv(sock, buf, bufsize);
	
	if(numread == -1)
	{
    	fprintf(stderr, "SDLNet_TCP_Recv: %s\n", SDLNet_GetError());
    	return cnt_errno = ERR_TCP_RECV_FAIL;
	}
	else if(numread == 0){
        fprintf(stderr, "recv_tcp: Connection closed or reset.\n");
		return cnt_errno = ERR_CONN_CLOSED;
	}
	
	return numread;
}

/**
 * Reads a packet into the buffer pointed to by @a udp_packet.
 *
 * @param[in]  sock       The socket from which to read.
 * @param[out] udp_packet The udp packet buffer to hold the data. 
 *
 * @return -1 on failure (cnt_errno is set to ERR_RECV_FAILED) and 0 on success.
 *
 * @designer Shane Spoor
 * @author   Shane Spoor
 *
 * @date Febuary 21, 2014
 */
int recv_udp (UDPsocket sock, UDPpacket *udp_packet)
{
	if(SDLNet_UDP_Recv(sock, udp_packet) == -1)
	{
		fprintf(stderr, "SDLNet_UDP_Recv: %s\n", SDLNet_GetError());
        cnt_errno = ERR_UDP_RECV_FAIL;
		return -1;
	}
	
	return 0;
}

/**
 * Grabs the first packet on the pipe to be sent by the send thread.
 *
 * @param[out] type Receives the type of packet to send.
 * @param[in]  fd   The read descriptor for the pipe between network router/send thread.
 * @param[out] ret  If the function is successful, this holds 1; otherwise, it will hold -1.
 *
 * @return The packet on success or NULL on failure.
 *
 * @designer Ramzi Chennafi
 * @author   Ramzi Chennafi
 *
 * @date Febuary 20 2014
 */
void *grab_send_packet(uint32_t *type, int fd, int *ret){

	*type = read_type(fd);
	if(*type >= 90){
		*ret = -1;
		return NULL;
	}

	uint32_t size = packet_sizes[*type - 1];

	void * data = (void*) malloc(sizeof(size));

	data = read_packet(fd, size); // reads data

	*ret = 1;

	return data;
}

/**
 * Creates a UDP packet of size @a size.
 *
 * If the function fails, it prints an error message before returning. 
 *
 * @param[in] size The size of the packet to create.
 *
 * @return A UDPpacket pointer on success or NULL on failure.
 *
 * @designer Ramzi Chennafi
 * @author Ramzi Chennafi
 *
 * @date Febuary 15, 2014
 */
UDPpacket *alloc_packet(uint32_t size){

	UDPpacket *pktdata = SDLNet_AllocPacket(size);

	if(!pktdata) {
	    fprintf(stderr, "SDLNet_AllocPacket: %s\n", SDLNet_GetError());
	    return NULL;
	}

	return pktdata;
}
/**
 * Creates an IPaddress struct holding the IP address and port information for the SDL network functions
 *
 * @param[out] ip_addr        Pointer to the IPaddress struct which will receive the address information.
 * @param[in]  port           The port to use for sending/receiving on the socket.
 * @param[in]  host_ip_string The hostname or IP address (in dotted string form) to resolve.
 *
 * @return -1 on failure, or 0 on success.
 *
 * @designer Shane Spoor
 * @author Shane Spoor
 *
 * @date Febuary 15, 2014
 */
int resolve_host(IPaddress *ip_addr, const uint16_t port, const char *host_ip_string)
{
    if(SDLNet_ResolveHost(ip_addr, host_ip_string, port) == -1)
    {
        fprintf(stderr, "SDLNet_ResolveHost: %s\n", SDLNet_GetError());
        return -1;
    }
    return 0;
}

/**
 * Creates an IPaddress struct holding the IP address and port information for the SDL network functions.
 *
 * @param[in] num_sockets The number of sockets to be added to the set.
 * @param[in] ...         A list of size @a num_sockets of TCPsocket and UDPsocket structs 
 *                        to add to the set.
 *
 * @return An SDLNet_SocketSet pointer on success, or NULL on failure.
 *
 * @designer Shane Spoor
 * @author Shane Spoor
 *
 * @date Febuary 15, 2014
 */
SDLNet_SocketSet make_socket_set(int num_sockets, ...)
{
    int i;
	va_list socket_list; 	
	SDLNet_SocketSet set = SDLNet_AllocSocketSet(num_sockets);

	if(!set)
	{
		fprintf(stderr, "SDLNet_AllocSocketSet: %s\n", SDLNet_GetError());
		return NULL;
	}
	
	va_start(socket_list, num_sockets);
	for(i = 0; i < num_sockets; i++)
	{
		if(SDLNet_AddSocket(set, va_arg(socket_list, SDLNet_GenericSocket)) == -1)
		{
			fprintf(stderr, "SDLNet_AddSocket: %s\n", SDLNet_GetError());
			return NULL;
		}
	}

	va_end(socket_list);
	return set;
}

/**
 * Runs select to determine whether the sockets have data to receive.
 *
 * @param[in] set The set to monitor for activity.
 *
 * @return The number of sockets ready on success, or -1 on failure.
 *
 * @designer Shane Spoor
 * @author Shane Spoor
 *
 * @date Febuary 21, 2014
 */
int check_sockets(SDLNet_SocketSet set)
{
	int numready = SDLNet_CheckSockets(set, 100000);

	if(numready == -1)
	{
		fprintf(stderr, "SDLNet_CheckSockets: %s\n", SDLNet_GetError());
		perror("SDLNet_CheckSockets");
	}

	return numready;
}

/**
 * Grabs the correct protocol for the specified packet.
 *
 * @param[in] type The type of packet to get the protocol for.
 *
 * @return The correct protocol for the specified packet type.
 *
 * @designer Ramzi Chennafi
 * @author   Ramzi Chennafi
 *
 * @date Febuary 21, 2014
 */
int get_protocol(uint32_t type)
{
	int protocol;

	switch(type)
	{
		case P_NAME:
		case P_CONNECT:
		case G_STATUS:
		case P_CHAT:
		case P_CLNT_LOBBY:
		case P_OBJCTV_LOC:
		case P_UNDEF:
		case P_KEEPALIVE:
		case P_OBJSTATUS:
			protocol = TCP;
			break;
		case P_POSUPDATE:
		case P_FLOOR_MOVE_REQ:
		case P_FLOOR_MOVE:
		case P_TAGGING:
			protocol = UDP;	
			break;
	}

	return protocol;
}

/**
 * Removes sockets from set and closes open sockets.
 *
 * If removing either or both of the sockets fails, the function prints
 * an error message.
 *
 * @param[in] set     The set to free.
 * @param[in] tcpsock The TCP socket to close.
 * @param[in] udpsock The UDP socket to close.
 *
 * @designer Ramzi Chennafi
 * @author   Ramzi Chennafi
 *
 * @date Febuary 21, 2014
 */
void close_connections(SDLNet_SocketSet set, TCPsocket tcpsock, UDPsocket udpsock)
{
	int numused;

	numused=SDLNet_UDP_DelSocket(set,udpsock);
	if(numused==-1) {
	    printf("SDLNet_DelSocket: %s\n", SDLNet_GetError());
	}
	numused=SDLNet_TCP_DelSocket(set,tcpsock);
	if(numused==-1) {
	    printf("SDLNet_DelSocket: %s\n", SDLNet_GetError());
	}

	SDLNet_TCP_Close(tcpsock);
	SDLNet_UDP_Close(udpsock);
}

/**
 * Sets cnt_errno to the specified value.
 *
 * The function uses a semaphore to ensure thread safety. If a thread can't acquire
 * the semaphore to write to cnt_errno, it does not block; any error reported this
 * way will require the network threads to shut down, and all errors will be logged
 * regardless.
 *
 * This behaviour may change in the future (by creating a linked list of errors, for
 * example).
 *
 * @param[in] error The error number to store in cnt_errno.
 *
 * @designer Shane Spoor
 * @author   Shane Spoor
 *
 * @date March 12, 2014
 */
void set_error(int error)
{
    int ret = sem_trywait(&err_sem);
    if(ret != -1) // If we got the semaphore
    {
        cnt_errno = error;
        sem_post(&err_sem);
    }
}

/**
 * Retrieves the error string for the current value of cnt_errno.
 *
 * The function uses a semaphore to ensure thread safety and will block if another
 * thread owns the semaphore.
 *
 * The function may be extended to return a list of errors.
 *
 * @return The error string corresponding to the value of cnt_errno.
 *
 * @designer Shane Spoor
 * @author   Shane Spoor
 *
 * @date March 12, 2014
 */
const char *get_error_string()
{
    int err;
    static const char *error_strings[] = {
        "Could not open the connection.",
        "The server closed the connection.",
        "Failed to receive TCP data.",
        "Failed to receive a UDP packet.",
        "Failed to send TCP data.",
        "Failed to send a UDP packet.",
        "Received corrupted data.",
        "The remote host could not be resolved. Ensure the host name or IP address is valid.",
        "The program could not allocate enough memory.",
        "Could not write to a pipe.",
        "Could not acquire a semaphore.",
        "Could not remove socket from socket set.",
        "Could not allocate a socket set.",
        "Network router thread failed to initialise."  
    };

    if(sem_wait(&err_sem) == -1)
        return error_strings[(ERR_NO_SEM * -1) - 1];
    else
    {
        err = cnt_errno;
        sem_post(&err_sem);
        if(!err)
            return NULL;

        err *= -1;
        err -= 1;
        return error_strings[err];
    }
}    
