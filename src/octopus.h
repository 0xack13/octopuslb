/*
 * Octopus Load Balancer - Main header file.
 *
 * Copyright 2008-2011 Alistair Reay <alreay1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/shm.h>
#include <sys/epoll.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <sys/syslog.h>
#include <strings.h>
#include <unistd.h>
#ifdef USE_SNMP
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/session_api.h>
#endif

/* application versioning */
#define OCTOPUS_VERSION "1.14"
#define OCTOPUS_DATE "29/11/2011"
#define OCTOPUS_VERSION_LEN 16

/* the server may be started in the foreground of the default daemon mode.
 * In both cases we DO run in the foreground up until the point that the
 * server starts successfully. This intermediate state is the INIT state.
 */
#define FOREGROUND_OFF 0
#define FOREGROUND_ON 1
#define FOREGROUND_INIT 2

/* Default values that we assume unless overwritten by user config */
#define DEFAULT_PORT 1080
#define DEFAULT_ADDRESS "0.0.0.0"
#define DEFAULT_SESSION_WEIGHT "0.1"
#define DEFAULT_MONITOR_INTERVAL 5
#define DEFAULT_CONNECT_TIMEOUT 5
#define DEFAULT_MAXC 500
#define DEFAULT_MAXL 4.0
#define DEFAULT_REBALANCE_THRESHOLD 30
#define DEFAULT_REBALANCE_SIZE 5
#define DEFAULT_REBALANCE_INTERVAL 30


/* used to determing if SNMP has been compiled into octopus-server and if so, what state it is in */
#define SNMP_NOT_INCLUDED 0
#define SNMP_DISABLED 1
#define SNMP_ENABLED 2

/* maximum length of input we accept from the admin binary */
#define ADMIN_MAX_INPUT 128

/* the admin interface assumes that there are _only_ 512 instances running */
#define MAXBALANCERS 512

/* these values are used when logging a message to determine what should happen after logging, eg. quit after logging, write to syslog*/
#define OCTOPUS_LOG_STD 0
#define OCTOPUS_LOG_EXIT 1
#define OCTOPUS_LOG_SYSLOG 2
/* maximum log entry length */
#define OCTOPUS_LOG_LEN 512

/* these are the various states that a server may be in */
#define SERVER_STATE_FREE 0
#define SERVER_STATE_DELETED 1
#define SERVER_STATE_FAILED 2
#define SERVER_STATE_DISABLED 3
#define SERVER_STATE_ENABLED 4

/* a server will either be a standby server or not */
#define STANDBY_STATE_TRUE 0
#define STANDBY_STATE_FALSE 1

/* a server will either be a clone or not */
#define SERVER_CLONE_TRUE 0
#define SERVER_CLONE_FALSE 1

/* a balancer will contain a maximum of 512 servers */
#define MAXSERVERS 512

/* the amount of data stored temporarily in a SESSION will be 4KB
 * As memory is allocated statically in octopus increasing this
 * increases the memory footprint linearly
 */
#define MESSAGE_SIZE_LIMIT 4096

/* this is the size of the hash table used by the HASH balancing algorithm.
 * the size determines the number of values that a URL can hash to and
 * then be mapped (pinned) to a particular server
 */
#define HASHTABLESIZE 65535

/* when creating a shm_file we create with the following UNIX permissions
 * this allows others read-only access
 */
#define DEFAULT_SHM_PERMS 0644

/* if a user does not specify the maximum number of sessions or
 * file descriptors that octopus can use then we assume that we
 * should use the maximum allowed.
 */
#define DEFAULT_FD_LIMIT 0
/* for the session limit, zero means auto configure based on available fds */
#define DEFAULT_SESSION_LIMIT 0

/* the various session states */
#define STATE_UNUSED 0
#define STATE_MEM_CONNECTED 2
#define STATE_CLI_CONNECTED 4
#define STATE_CLO_CONNECTED 8
#define STATE_FRESH 16
#define STATE_MEM_READ_READY 32
#define STATE_CLI_READ_READY 64
#define STATE_CLO_READ_READY 128
#define STATE_MEM_WRITE_READY 256
#define STATE_CLI_WRITE_READY 512
#define STATE_CLO_WRITE_READY 1024
#define STATE_MEM_BUFF_FULL 2048
#define STATE_CLI_BUFF_FULL 4096

/* this is where we will place the shm_file */
#define DEFAULT_SHM_RUN_DIR "/var/run/octopuslb/"

/* we populate the hash table with this value to denote that it is unused
 * or unassigned.
 */
#define HASH_VAL_FREE -1

/* we allow a full shm_file path of 2048 characters, that's enough */
#define SHM_FILE_FULLNAME_MAX_LENGTH 2048

/* the cloning mode of the balancer can be off, fast-mode or strict mode.
 * it may also be failed due to the fact the the requisite clones are dead
 */
#define CLONE_MODE_OFF 0
#define CLONE_MODE_ON 1
#define CLONE_MODE_FAILED 2

/* overload mode can be 'relaxed' where we balance to servers that may be
 * overloaded. Or 'strict' where we stop allocating to servers that have
 * exeeded 100% loading
 */
#define OVERLOAD_MODE_RELAXED 0
#define OVERLOAD_MODE_STRICT 1

/* the various load balancing algorithms */
#define ALGORITHM_RR 1
#define ALGORITHM_LC 2
#define ALGORITHM_LL 3
#define ALGORITHM_HASH 4
#define ALGORITHM_STATIC 5

/* servernames must be 16 chars or less */
#define SERVERNAME_MAX_LENGTH 16

/* lines in the octopus.conf file will only be read up to 100 characters */
#define MAX_CONF_LINE_LEN 100

/* we only analyze the first 200 chars of a URL when using the HASH or
 * STATIC algorithms
 */
#define URI_LINE_LEN 200

/* values of SERVER->load that are assumed on server launch or SNMP check
 * failure
 */
#define SNMP_LOAD_NOT_INIT -1.00
#define SNMP_LOAD_FAILED -2.00

/* parameters used when logging to specify if a command should be suppressed
 * in order to stop spamming the log file when there's a chance that the
 * same error might be logged many times in quick succession
 */
#define SUPPRESS_OFF 0
#define SUPPRESS_CONN_REJECT 1

/* this struct stores all the information associated with a particular
 * server. IP, tcp port, state, counters and load
 */
typedef struct {
	unsigned short int id;
	char name[SERVERNAME_MAX_LENGTH];	/* servername */
	int status;	/* enabled, disabled or failed */
	int standby_state; /* true or false */
	int port;	/* which port do we connect to for this memeber? */
	struct sockaddr_in myaddr;	/* socket for server */
	int c;	/* current number of connections */
	int maxc;	/* maximum numbre of connections */
	unsigned long int completed_c;	/* completed number of connections */
	unsigned long bsent;	/* bytes sent */
	unsigned long brecv;	/* bytes received */
	float load;	/* snmp reported load */
	float maxl;	/* user specified max load */
	int e_load; /* effective loading, maxl/load * 100 */
	unsigned short int hash_table_usage;
} SERVER;

/* this struct stores information about an active session;
 * file descriptors, buffers and pointers to the associated
 * SERVER struct
 */
typedef struct {
	unsigned int id;
	unsigned int state;
	int clientfd;
	int memberfd;
	int clonefd;
	char member_read_buffer[MESSAGE_SIZE_LIMIT];
	char client_read_buffer[MESSAGE_SIZE_LIMIT];
	char clone_write_buffer[MESSAGE_SIZE_LIMIT];
	int member_used_buffer;
	int client_used_buffer;
	int clone_used_buffer;
	SERVER *member;
	SERVER *clone;
} SESSION;

/* This struct is used as a lookup to the SESSION struct */
typedef struct {
	SESSION *session;
} FD;

/*---Begin---by Cheng Ren, 2012-9-27 */
/*This struct is the queue of unused sessions*/
typedef struct{
	SESSION **unused_sessions;
	int head;
	int rear;
} UNUSED_SESSION_QUEUE;
/*End*/

/* This struct is the master brain of the load balancer.
 * It contains the SERVERs, accounting information, currently set balancing
 * algorithm, bound tcp port, SNMP password, log file and shm file details
 */
typedef struct {
	short int hash_table[HASHTABLESIZE];
	unsigned short int nmembers;
	unsigned short int nclones;
	unsigned short int monitor_interval;
	int alive;
	int algorithm;
	int clone_mode;
	int overload_mode;
	SERVER members[MAXSERVERS];
	SERVER clones[MAXSERVERS];
	u_char *snmp_community_pw;
	int snmp_status;
	int binding_port;
	struct in_addr binding_ip;
	struct in_addr member_outbound_ip;
	struct in_addr clone_outbound_ip;
	int connect_timeout;
	pid_t monitor_pid;
	pid_t master_pid;
	int shmid;
	int foreground;
	FILE *log_file;
	char *log_file_path;
	float session_weight;
	int hash_rebalance_threshold; /* if any two servers have e_load difference of this percentage then move some hashes to lowest loaded server */
	int hash_rebalance_size; /* move this percentage of currently assigned hashes per go */
	int hash_rebalance_interval; /* do rebalancing every X seconds */
	int use_member_outbound_ip;
	int use_clone_outbound_ip;
	char *shm_run_dir;
	char *shm_run_file;
	char shm_run_file_fullname[SHM_FILE_FULLNAME_MAX_LENGTH];
	int default_maxc;
	float default_maxl;
	char version[OCTOPUS_VERSION_LEN];
	int fd_limit;
	int session_limit;
	int use_syslog;
	int shm_perms;
	int overall_load;
	int connection_rejected_log_suppress;
	int debug_level;
} BALANCER;

/* function prototypes */
int handle_connection(int client_fd, char *buffer, size_t buffer_size);
int create_balancer_server(int clone_server, char *serverName, int serverStatus, int standbyState, int serverPort, struct in_addr *serverIP, int serverMaxc, float servermaxl);
SESSION* get_new_session();
int member_read(int fd);
int member_write(int fd);
int client_read(int fd);
int client_write(int fd);
int clone_read(int fd);
int clone_write(int fd);
int delete_session(SESSION *session);
int disconnect_client(SESSION *session);
int disconnect_member(SESSION *session);
int disconnect_clone(SESSION *session);
int get_available_servers();
int verify_server(SERVER *server);
int choose_server(SESSION *session);
int set_lc_server();
int set_ll_server();
int set_rr_server();
int set_hash_server(SESSION *session);
int set_static_server(SESSION *session);
int connect_server(SESSION *session);
int calc_effective_load();
int connect_to_shm(char *run_file, int ignore_version_check);
int rebalance_hash();
int clean_exit();
int write_log(int options, char *description, int log_suppress);

/* some global variables */
BALANCER *balancer;
SESSION *sessions;
UNUSED_SESSION_QUEUE unused_session_queue; /*added by Cheng Ren, 2012-9-27*/
FD *fds;
int epfd;
struct epoll_event null_ev;
struct epoll_event ro_ev;
struct epoll_event wr_ev;
struct epoll_event rw_ev;
struct epoll_event events[20000];
unsigned short int next_member=0;
unsigned short int next_clone=0;
static int sockbufsize = MESSAGE_SIZE_LIMIT;
ssize_t nbytes;
int yes=1;
char waste_buffer[MESSAGE_SIZE_LIMIT];
int temporary_debug_level=0;
int debug_level_override=0;
char *server_status[5] = {"Free", "Deleted", "Failed", "Disabled", "Enabled"};
char *cloning_status[3] = {"Disabled", "Enabled", "Failed"};
char *overload_status[2] = {"Relaxed", "Strict"};
char *algorithm_status[5] = {"Round Robin", "Least Connections", "Least Load", "Hash", "Static"};
char *standby_status[2] = {"(S)",""};
char log_string[OCTOPUS_LOG_LEN];
int available_members[MAXSERVERS];
int available_members_count=0;
int available_clones[MAXSERVERS];
int available_clones_count=0;
int use_clone=0;
int using_member_standby=0;
int using_clone_standby=0;

/* this function is used by admin and monitor where changing server states may change the cloning status */
int set_cloned_state() {
	int temp=0;
	int i;
	/* if clone mode is in on then we make sure that all the clones are enabled otherwise we're our clone mode is failed */
	if(balancer->clone_mode == CLONE_MODE_ON) {
		for (i=0; i< balancer->nclones; i++) {
			if(balancer->clones[i].status != SERVER_STATE_ENABLED) {
				temp++;
			}
		}
		if(temp == balancer->nclones) {
			balancer->clone_mode= CLONE_MODE_FAILED;
		}
	}
	/* if clone mode is in the failed state then we will scan for at least one enabled clone */
	else if(balancer->clone_mode == CLONE_MODE_FAILED) {
		for (i=0; i< balancer->nclones; i++) {
			if(balancer->clones[i].status == SERVER_STATE_ENABLED) {
				balancer->clone_mode= CLONE_MODE_ON;
				break;
			}
		}
	}
	return 0;
}

/*
tries to create a new server
this function is used by the admin too so can't write to log directly from here, have to get admin and server to do stuff according to ret val
returns 0 on success
returns -1 when malloc fails
returns -2 when tcp port is invalid
returns -3 when IP address is invalid
*/
int create_balancer_server(int clone_server, char *serverName, int serverStatus, int standbyState, int serverPort, struct in_addr *serverIP, int serverMaxc, float servermaxl) {
	int i;
	SERVER *s;
	s = malloc(sizeof(SERVER));
	if(s == NULL) {
		return -1;
	}
	memset(s,'\0', sizeof(SERVER));
	strncpy(s->name, serverName, SERVERNAME_MAX_LENGTH);
	s->status=serverStatus;
	s->standby_state=standbyState;
	s->port=serverPort;
	s->maxc=serverMaxc;
	s->load=SNMP_LOAD_NOT_INIT;
	s->maxl=servermaxl;
	s->bsent=0;
	s->brecv=0;
	s->e_load=0;
	s->hash_table_usage=0;
	s->myaddr.sin_family = AF_INET;
	if(htons((uint16_t)(serverPort)) > 0) {
		s->myaddr.sin_port = htons((uint16_t)(serverPort));
	}
	else {
		free(s);
		return -2;
	}
	memcpy(&s->myaddr.sin_addr, serverIP, sizeof(struct in_addr));
	/*for a member */
	if(clone_server==SERVER_CLONE_FALSE) {
		/*look for free slots first */
		for(i=0; i < balancer->nmembers; i++) {
			if(balancer->members[i].status == SERVER_STATE_FREE) {
				memcpy((void *)&balancer->members[i], (void *)s, sizeof(SERVER));
				balancer->members[i].id=i;
				free(s);
				return 0;
			}
		}
		/*otherwise just copy to end of members array and increment count */
		memcpy((void *)&balancer->members[(balancer->nmembers)], (void *)s, sizeof(SERVER));
		balancer->members[balancer->nmembers].id=balancer->nmembers;
		balancer->nmembers++;
	}
	/*for a clone */
	else {
		/*look for free slots first */
		for(i=0; i < balancer->nclones; i++) {
			if(balancer->clones[i].status == SERVER_STATE_FREE) {
				memcpy((void *)&balancer->clones[i], (void *)s, sizeof(SERVER));
				balancer->clones[i].id=i;
				free(s);
				return 0;
			}
		}
		/*otherwise just copy to end of clones array and increment count */
		memcpy((void *)&balancer->clones[(balancer->nclones)], (void *)s, sizeof(SERVER));
		balancer->clones[balancer->nclones].id=balancer->nclones;
		balancer->nclones++;
	}
	free(s);
	return 0;
}
