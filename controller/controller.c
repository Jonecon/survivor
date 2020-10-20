/* CLIENT = CONTROLLER */
/* make all PORT=tap3 ROBOT_ADDRESSES=fe80::b4e1:7aff:fe26:3b28 term */

#include <stdio.h>
#include "net/sock/udp.h"
#include "shell.h"
#include "thread.h"
#include "xtimer.h"
#include "msg.h"

#include "net/af.h"
#include "net/protnum.h"
#include "net/ipv6/addr.h"

#define DIRECTION_UP 1;
#define DIRECTION_DOWN 2;
#define DIRECTION_LEFT 3;
#define DIRECTION_RIGHT 4;
#define DIRECTION_POS 5;
#define DIRECTION_STOP 0;

// DECLARING FUNCTIONS
extern int _gnrc_netif_config(int argc, char **argv);
extern int controller_cmd(int argc, char **argv);
extern int getSta_cmd_remote(char* response);

struct Point {
	int x;
	int y;
};

typedef struct {
    int status;
	int energy;
	struct Point position;
	//Hard coded need to change.
	struct Point survivors_found[3];
	mutex_t lock;
} robot_data;

// DECLARING VARIABLES
char robot_addresses[MAX_ROBOTS][24];
char control_thread_stack[MAX_ROBOTS][THREAD_STACKSIZE_MAIN];
char auto_thread_stack[MAX_ROBOTS][THREAD_STACKSIZE_MAIN];
char listener_thread_stack[THREAD_STACKSIZE_MAIN];
char message[MAX_ROBOTS][20];

uint8_t buf[255];

int robotID = 0;
int numRobots = 0;

struct Point border;
struct Point position;
static robot_data robots[MAX_ROBOTS];

/* DEFINING SETS OF COMMANDS FOR THE CONTROLLER */
static const shell_command_t shell_commands[] = {
	{"sUp", "Send move up instruction to a robot", controller_cmd},
	{"sDown", "Send move down instruction to a robot", controller_cmd},
	{"sLeft", "Send move left instruction to a robot", controller_cmd},
	{"sRight", "Send move right instruction to a robot", controller_cmd},
	{"stop", "Send stop instruction to a robot", controller_cmd},
	{"getSta", "Send get status instruction to a robot", controller_cmd},
	{"pos", "Send pos instruction to a robot", controller_cmd},
	// {"getList", "List robot status", getList_cmd},
	{NULL, NULL, NULL}
};

/* THREAD HANDLER */

void *controller_thread_handler(void *arg) {
	(void)arg;

	int id = robotID;

	// CONFIGURING local.port TO WAIT FOR MESSAGES TRANSMITED TOWARDS THE CONTROLLER_PORT
	// THEN CREATING UDP SOCK BOUND TO THE LOCAL ADDRESS
	sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
	sock_udp_t sock;

	local.port = CONTROLLER_PORT;

	/* IF THERE'S AN ERROR CREATING UDP SOCK
	if (sock_udp_create(&sock, &local, NULL, 0) < 0) {
		puts("Error creating UDP sock");
		return NULL;
	}*/

	// PREPARE A REMOTE ADDRESS WHICH CORRESPOND TO THE ROBOT
	sock_udp_ep_t remote = {.family = AF_INET6};
	if (ipv6_addr_from_str((ipv6_addr_t *)&remote.addr.ipv6, robot_addresses[id]) == NULL) {
		puts("Cannot convert server address");
		sock_udp_close(&sock);
		return NULL;
	}

	// SETTING THE PORT OF THE CONTROLLER
	remote.port = CONTROLLER_PORT + id;

	// configure the underlying network such that all packets transmitted will reach a server
	ipv6_addr_set_all_nodes_multicast((ipv6_addr_t *)&remote.addr.ipv6, IPV6_ADDR_MCAST_SCP_LINK_LOCAL);

	while (1) {

		// IF THERE IS NO COMMAND
		if (strlen(message[id]) == 0) {
			xtimer_sleep(1);
			continue;

		} else {
			ssize_t res;

			// IF THERE'S AN ERROR CREATING UDP SOCK
			if (sock_udp_create(&sock, &local, NULL, 0) < 0) {
				puts("Error creating UDP sock");
				return NULL;
			}
			
			printf("\nSending: %s to robot id: %d", message[id], id);

			// TRANSMITTING THE MESSAGE TO THE ROBOT[id] WHILE CHECKING
			if (sock_udp_send(&sock, message[id], strlen(message[id]) + 1, &remote) < 0) {
				puts("Error sending message");
				sock_udp_close(&sock);
				return NULL;
			}

			// WAITING FOR A RESPONSE
			sock_udp_ep_t remote;
			uint8_t buf[255];
			res = sock_udp_recv(&sock, buf, sizeof(buf), 3 * US_PER_SEC, &remote);

			if (res < 0) {
				if (res == -ETIMEDOUT) {
					puts("Timed out, no response. Message may be lost or delayed.");
				} else {
					puts("Error receiving message. This should not happen.");
				}
			}
			else {
				// CONVERT ROBOT ADDRESS FROM ipv6_addr_t TO STRING
				char ipv6_addr[IPV6_ADDR_MAX_STR_LEN];
				if (ipv6_addr_to_str(ipv6_addr, (ipv6_addr_t *)&remote.addr.ipv6, IPV6_ADDR_MAX_STR_LEN ) == NULL) {
					printf("\nCannot convert server address\n");
					strcpy(ipv6_addr, "???");
				}

				// ensure a null-terminated string
				buf[res] = 0;

				/* Decide what to do with this information;
					-maybe update info on robot for the logic thread
					-Don't need to reply in this thread now
				*/


				//printf("Received from server (%s, %d): \"%s\"\n", ipv6_addr, remote.port, buf);
				strcpy(message[id], "");
			}
			sock_udp_close(&sock);
		}
	}

	return NULL;
}

void *listener_thread_handler(void* arg){

	//Here so we can display all messages recieved and are able to recieve spontaneous messages.
	/* if the port is the same as this threads robot
				Then we have a message to proccess and display
				if this message
					THEN
				etc
	*/
	(void) arg;
	sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
	sock_udp_t sock;

	local.port = CONTROLLER_PORT;

	// IF THERE'S AN ERROR CREATING UDP SOCK
	if (sock_udp_create(&sock, &local, NULL, 0) < 0) {
		puts("Error creating UDP sock");
		return NULL;
	}

	while (1){

		ssize_t res;
		sock_udp_ep_t remote;
		uint8_t buf[255];
		res = sock_udp_recv(&sock, buf, sizeof(buf), 3 * US_PER_SEC, &remote);

		if (res < 0) {
			if (res == -ETIMEDOUT) {
			} else {
				puts("Error receiving message. This should not happen.");
			}
		}
		else {
			// CONVERT ROBOT ADDRESS FROM ipv6_addr_t TO STRING
			char ipv6_addr[IPV6_ADDR_MAX_STR_LEN];
			if (ipv6_addr_to_str(ipv6_addr, (ipv6_addr_t *)&remote.addr.ipv6, IPV6_ADDR_MAX_STR_LEN ) == NULL) {
				printf("\nCannot convert server address\n");
				strcpy(ipv6_addr, "???");
			}

			// ensure a null-terminated string
			buf[res] = 0;
			// printf("Received from robot (%s, %d): \"%s\"\n", ipv6_addr, remote.port, buf);
			printf("\nReceived from robot (%d): \"%s\"\n", remote.port, buf);

			int id, energy, x, y, status;

			sscanf((char*) buf, "STATUS id: %d energy: %d position: (%d, %d) direction: %d", &id, &energy, &x, &y, &status);

			robots[id].status = status;
			robots[id].energy = energy;
			robots[id].position.x = x;
			robots[id].position.y = y;
		}
	}
}

void *logic_thread_handler(void *arg) {
	(void)arg;

	// DECLARING VARIABLES
	border.x = NUM_LINES + 1;
	border.y = NUM_COLUMNS + 1;
	
	// MOVING THE ROBOT TO ITS INITIAL POSITION
	position.x = 0;
	for (int i = 0; i < numRobots; ++i) {

		position.y = ((border.y/numRobots)*i);

		//SET MESSAGE[robot_id] WITH THE COMMAND TO BE SENT
		sprintf(message[i], "pos %d %d", position.x, position.y);

		/* TO BE DELETED ? */
		printf("Robot %d position (%d, %d)\n",i, position.x, position.y);
	}

	while(1) {

		for (int i = 0; i < (numRobots + 1); ++i) {

			// START MOVING RIGHT THEN WAIT FOR A MSG WHEN A ROBOT IS STOP
			if (robots[i].status == 5)
			{
				//SET MESSAGE[robot_id] WITH THE COMMAND TO BE SENT
				strcpy(message[i], "sRight");

				/* TO BE DELETED ? */
				printf("Robot %d position (%d, %d)\n",i, position.x, position.y);

				
				xtimer_sleep((border.y/numRobots) + 1);
			}
			
		// 	if (robots[i].status == 0) {

		// 		// CHECK IF ITS STOP BECAUSE IT REACHED Y BORDER 
		// 		if ((robots[i].position.y == 0) || (robots[i].position.y == NUM_LINES)) {
					
		// 			//SET MESSAGE[robot_id] WITH THE COMMAND TO BE SENT
		// 			sprintf(message[i], "pos %d %d", position.x, (position.y + 1));

		// 			if (robots[i].status == 5)
		// 			{
		// 				// IF IT REACHED MAX LEFT
		// 				if (robots[i].position.y == 0) {

		// 					//SET MESSAGE[robot_id] WITH THE COMMAND TO BE SENT
		// 					strcpy(message[i], "sRight");
		// 				}
		// 				// IF IT REACHED MAX RIGHT
		// 				if (robots[i].position.y == NUM_LINES) {

		// 					//SET MESSAGE[robot_id] WITH THE COMMAND TO BE SENT
		// 					strcpy(message[i], "sLeft");
		// 				}
		// 			}
		// 		}
		// 	}
		}

		xtimer_sleep(1);
	}
}

int main(void) {

	// GETTING ALL ROBOT IPv6 ADDRESSES
	char str[24*MAX_ROBOTS];
	strcpy(str, ROBOT_ADDRESSES);
	char* token = strtok(str, ",");

	// SETTING UP COMMUNICATION THREADS FOR EACH ROBOTS
	while (token != NULL) {
		strcpy(robot_addresses[robotID], token);

		// CREATING THREAD PASSING ID AS PARAMETER
		thread_create(control_thread_stack[robotID], sizeof(control_thread_stack[robotID]), THREAD_PRIORITY_MAIN - 1,
			THREAD_CREATE_STACKTEST, controller_thread_handler, NULL, "Control Thread");

		numRobots++;
		robotID++;
		token = strtok(NULL, ",");
	}

	thread_create(auto_thread_stack[robotID], sizeof(auto_thread_stack[robotID]), THREAD_PRIORITY_MAIN - 1,
			THREAD_CREATE_STACKTEST, logic_thread_handler, NULL, "Logic Thread");

	thread_create(listener_thread_stack, sizeof(listener_thread_stack), THREAD_PRIORITY_MAIN - 1,
		THREAD_CREATE_STACKTEST, listener_thread_handler, NULL, "Listener Thread");

	/*
	for(unsigned int i = 0; i < (sizeof(robot_addresses)/sizeof(robot_addresses[0])); i++){
		printf("robot %d address: %s\n", i, robot_addresses[i]);
	}*/

	// STARTING SHELL
	puts("Shell running");
	char line_buf[SHELL_DEFAULT_BUFSIZE];
	shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

	// SHOULD NEVER BE REACHED
	return 0;
}

/* DECLARING FUNCTIONS */
int controller_cmd(int argc, char **argv) {
	if (argc == 4){
		int robotid = atoi(argv[1]);
		if (robotid <= MAX_ROBOTS - 1) {
		//SET MESSAGE[robot_id] WITH THE COMMAND TO BE SENT
		sprintf(message[robotid], "%s %s %s", argv[0], argv[2], argv[3]);
		return 0;
		}
	}
	

	//IF USER DID NOT PUT ROBOT ID
	if ((argc != 2) && (argc != 4)) {
		printf("usage: %s robot_id\n", argv[0]);
		return 1;
	}

	int robotid = atoi(argv[1]);
	if (robotid <= MAX_ROBOTS - 1) {
		//SET MESSAGE[robot_id] WITH THE COMMAND TO BE SENT
		strcpy(message[robotid], argv[0]);
	} else {
		printf("No robot with this ID\n");
		return 1;
	}

	return 0;
}
