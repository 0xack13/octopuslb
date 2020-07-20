/*
 * Octopus Load Balancer - Client binary.
 *
 * Copyright 2008-2011 Alistair Reay <alreay1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include "octopus.h"

#define MEMBER_ARG "m"
#define CLONE_ARG "c"
#define ADMIN_REQ_TRUE 0
#define ADMIN_REQ_FALSE 1
#define SINGLE_TARGET_CMD 0
#define MULTI_TARGET_CMD 1

int readonly=0;
int interactive=1;
int csv=0;
int extended_output_mode=1;
long int v1;
SERVER *subject[MAXSERVERS * 2];
int subject_count;
char *cli_input=NULL;
char *run_file = "";
char *run_dir = DEFAULT_SHM_RUN_DIR; 
int use_run_file=0;
int ignore_version_check=0;
int command_return_value=0;

/* function prototypes */
int command_prompt();
int cmd_scan_for_instances();
int cmd_create(char *, char *, char *, char *);
int cmd_algorithm(char *);
int cmd_hri(char *);
int cmd_hrs(char *);
int cmd_hrt(char *);
int cmd_monitor(char *);
int cmd_clone_mode(char *);
int cmd_overload_mode(char *);
int cmd_info();
int cmd_kill();
int cmd_standby(char *, char *);
int cmd_delete(char *, char *);
int cmd_maxl(char *, char *, char *);
int cmd_maxc(char *, char *, char *);
int cmd_enable(char *, char *);
int cmd_disable(char *, char *);
int cmd_reset(char *, char *);
int cmd_examples();
int cmd_help();
int cmd_snmp(char *);
int cmd_show();
int cmd_debug(char *);
int set_subject(char *, char *, int, int);

int usage(char *prog_name) {
	sockbufsize=0; /* just to suppress compiler 'unused variable' message */
	fprintf(stderr, "Octopus Load Balancer Admin Interface (version " OCTOPUS_VERSION " built " OCTOPUS_DATE")\n");
	fprintf(stderr, "Copyright 2011 Alistair Reay <alreay1@gmail.com>\n\n");
	fprintf(stderr, "Usage : %s  [-f SHM_FILE] [-d SHM_FILE_DIR] [-e CMD]\n", prog_name);
	fprintf(stderr, "	-h 		Print help message\n");
	fprintf(stderr, "	-f <file>	Location of SHM file for a running instance\n");
	fprintf(stderr, "	-d <directory>	Directory to look for SHM files. Defaults to %s\n",DEFAULT_SHM_RUN_DIR);
	fprintf(stderr, "	-e <cmd>	Run a command then exit. Non-interactive mode\n");
	fprintf(stderr, "	-i		Ignore admin/server version check. Dangerous option, allows a older/newer admin to connect to server\n");
	fprintf(stderr, "	-r		Attach to server in read-only mode. This will be attempted automatically if r/w mode can't be set\n");
	fprintf(stderr, "	-m		Return output in csv format. Intended for interfaces to Octopus like web admin interface. Should only be used with option -e\n");
	fprintf(stderr, "\n");
	exit(0);
}

/* this function does the work of actually connecting to the SHM segment of the server instance */
int connect_to_shm(char *run_file, int ignore_version_check) {
	struct stat stat_buffer;
	char *data = NULL;
	key_t key;
	int shmid;
	int status;
	status = stat(run_file, &stat_buffer);

	if(status == -1) {
		fprintf(stderr, "ERROR: Unable to find shm_run file. Check that server process has been started.\n");
		exit(-1);
	}
	/* generate the SHM key that is indicated by the requested run_file */
	if ((key = ftok(run_file, 'Z')) == -1) {
		fprintf(stderr, "ERROR: Unable to make SHM key\n");
		exit(-1);
	}
	/* get the identifer of the SHM segment associated with the key */
	if ((shmid = shmget(key, sizeof(BALANCER), 006)) == -1) {
		if(errno == EACCES) {
			if ((shmid = shmget(key, sizeof(BALANCER), 0)) == -1) {
				fprintf(stderr, "ERROR: Unable to connect to server process. Access error: %s\n", strerror(errno));
				exit(-1);
			}
			readonly=1;
		}
		else {
			fprintf(stderr, "ERROR: Unable to connect to server process. Check that server process has been started: %s\n", strerror(errno));
			exit(-1);
		}
	}
	/* attach to the SHM memory segment */
	if(readonly == 1) {
		/* in readonly mode we only ask for read permissions */
		data = shmat(shmid, (void *) 0, SHM_RDONLY);
	}
	else {
		data = shmat(shmid, (void *) 0, 0);
	}
	if (data == (char *) (-1)) {
		fprintf(stderr, "ERROR: Unable to attach to SHM segment: %s\n", strerror(errno));
		exit(-1);
	}
	/* we cast the SHM segment as a BALANCER struct */
	balancer = (BALANCER *)data;
	/* the very first thing we want to do is check for a version mismatch */
	if(!(strncmp(balancer->version, OCTOPUS_VERSION, OCTOPUS_VERSION_LEN))) {
		if(csv==1) {
			printf("%s,%s:%d\n", balancer->version, inet_ntoa(balancer->binding_ip),balancer->binding_port);
		}
		else {
			printf("Connected to balancer at %s:%d\n", inet_ntoa(balancer->binding_ip),balancer->binding_port);
		}
	}
	else {
		if(ignore_version_check > 0) {
			printf("WARNING! Version mismatch: server is version %s, admin is version %s. Continuing as admin was started with -i option\n", balancer->version, OCTOPUS_VERSION);
		}
		else {
			printf("ERROR! Version mismatch: server is version %s, admin is version %s\n", balancer->version, OCTOPUS_VERSION);
			exit(-1);
		}
	}
	/* then we want to make sure we've connected to a functioning LB */
	if(balancer->alive==0) {
		printf("ERROR! Connect succeeded but SHM segment has been marked as dead. Quitting\n");
		exit(-1);
	}
	return 0;
}


int main(int argc, char *argv[]) {
	/* process command line arguments */
	int i;
	while((i = getopt(argc, argv,"mirhd:f:e:")) != -1) {
		switch (i) {
			case 'd':
				run_dir = optarg;
				break;
			case 'f':
				run_file=optarg;
				use_run_file=1;
				break;
			case 'e':
				cli_input=optarg;
				interactive=-1;
				break;
			case 'h':
				usage(argv[0]);
				exit(0);
				break;
			case 'r':
				readonly=1;
				break;
			case 'i':
				ignore_version_check=1;
				break;
			case 'm':
				csv=1;
				break;
			case '?':
				if(isprint(optopt)) {
					fprintf(stderr, "ERROR: main: Unknown option `-%c'.\n",optopt);
				}
				else {
					fprintf(stderr, "ERROR: main: Unknown option character `\\x%x'.\n",optopt);
				}
				return 1;
			default:
				abort();
		}
	}
	for(i = optind; i < argc; i++) {
		fprintf(stderr, "ERROR: main: Non-option argument %s\n", argv[i]);
		exit(1);
	}
	/* if user has supplied a specific .shm run file location then we'll just use that and not look for any others */
	if(use_run_file > 0) {
		connect_to_shm(run_file, ignore_version_check);
	}
	else {
		/* if the user hasn't told us where to look then we'll scan the default location for shm_files */
		cmd_scan_for_instances();
	}
	/* then we'll give them the interactive shell prompt for octopus */
	command_prompt();
	return 0;
}

int command_prompt() {
	char input[ADMIN_MAX_INPUT];
	char *argument_1;
	char *argument_2;
	char *argument_3;
	char *argument_4;
	char *argument_5;
	int number_of_args=0;
	/* we only a welcome if we're in interactive mode */
	if(interactive == 1) {
		if(readonly == 1) {
			printf("Octopus Interactive Shell (read-only mode)\n");
		}
		else {
			printf("Octopus Interactive Shell\n");
		}
		printf("enter '?' for help\n");
	}

	while(1) {
		/* non-interactive shells want to run a single command then quit, we'll let 'em */
		/* this will only equal 0 after a non-interactive command has been executed */
		if(interactive==0) {
			exit(command_return_value);
		}
		/* here we are dealing with a real live human, let's give them a prompt */
		if(interactive==1) {
			if(readonly == 1) {
				printf("\noctopus (read-only mode)$ ");
			}
			else {
				printf("\noctopus $ ");
			}
		}
		/* here we are dealing with a non-interactive command */
		if(interactive == -1) {
			strncpy(input, cli_input, ADMIN_MAX_INPUT);
			if(strlen(input) >= ADMIN_MAX_INPUT - 1) {
				printf("ERROR: Too much input!\n");
				exit(1);
			}
			/* set this counter to zero so that after processing the current command the admin interface exits on the next loop */
			interactive=0;
		}
		/* admin utility spends most of its time waiting for user input here */
		else {
			if(fgets(input, ADMIN_MAX_INPUT, stdin) == NULL) {
				continue;
			}
		}
		/* the server process may have stopped or been restarted since the last valid input was gathered. We disconnect the admin if this is the case */
		if(balancer->alive != 1) {
			printf("NOTICE: Server process has quit. Exiting.\n");
			exit(0);
		}
		if(strlen(input) >= (ADMIN_MAX_INPUT -1) ) {
			printf("ERROR: Too much input!\n");
			exit(1);
		}
		/*break the user's input into separate pieces */
		argument_1 = strtok(input," ");
		argument_2 = strtok(NULL, " ");
		argument_3 = strtok(NULL, " ");
		argument_4 = strtok(NULL, " ");
		argument_5 = strtok(NULL, " ");
		/* if user just pressed enter, then we loop back to the prompt */
		if(!strcmp(argument_1, "\n")) {
			continue;
		}
		/* lets count the number of arguments that were passed in */
		number_of_args=0;
		if(argument_5 != NULL) {
			number_of_args=5;
		}
		else if(argument_4 != NULL) {
			number_of_args=4;
		}
		else if(argument_3 != NULL) {
			number_of_args=3;
		}
		else if(argument_2 != NULL) {
			number_of_args=2;
		}
		else if(argument_1 != NULL) {
			number_of_args=1;
		}

		if(!strncmp(argument_1, "scan", 4)) {
			/* when scanning we'll unset our readonly flag as we may want to connect in r/w mode
			 * this does not open a security hole to allow unpriv'd users to just 'scan' their
			 * way into r/w mode */
			readonly=0;
			command_return_value=cmd_scan_for_instances();
		}

		else if(!strncmp(argument_1, "standby", 7)) {
			if(number_of_args == 2) {
				command_return_value=cmd_standby(MEMBER_ARG, argument_2);
			}
			else if(number_of_args == 3) {
				command_return_value=cmd_standby(argument_2, argument_3);
			}
			else {
				printf("ERROR: not enough args\n");
				continue;
			}
		}
		/* SNMP command */
		else if(!strncmp(argument_1, "snmp", 4)) {
			command_return_value=cmd_snmp(argument_2);
		}
		/* CLONE mode command */
		else if(!strncmp(argument_1, "clone",5)) {
			if(number_of_args == 2) {
				command_return_value=cmd_clone_mode(argument_2);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* SHOW command */
		else if(!strncmp(argument_1, "s",1)) {
			command_return_value=cmd_show();
		}
		/* ? (HELP) command */
		else if(!strncmp(argument_1, "?",1)) {
			command_return_value=cmd_help();
		}
		/* EXIT command */
		else if(!strncmp(argument_1, "exit",4)) {
			printf("Bye!\n");
			exit(0);
		}
		/* QUIT command */
		else if(!strncmp(argument_1, "q",1)) {
			printf("Bye!\n");
			exit(0);
		}
		/* eXtended_output command */
		else if(!strncmp(argument_1, "x",1)) {
			if(extended_output_mode == 0) {
				extended_output_mode=1;
				printf("Extended output mode on\n");
			}
			else {
				extended_output_mode=0;
				printf("Extended output mode off\n");
			}
		}
		/* EXAMPLE command */
		else if(!strncmp(argument_1, "ex",2)) {
			command_return_value=cmd_examples();
		}
		/* DELETE command */
		else if(!strncmp(argument_1, "delete", 6)) {
			if(number_of_args == 2) {
				command_return_value=cmd_delete(MEMBER_ARG, argument_2);
			}
			else if(number_of_args == 3) {
				command_return_value=cmd_delete(argument_2, argument_3);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* MONITOR command */
		else if(!strncmp(argument_1, "monitor",7)) {
			if(number_of_args == 2) {
				command_return_value=cmd_monitor(argument_2);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* DEBUG command */
		else if(!strncmp(argument_1, "debug",5)) {
			if(number_of_args == 2) {
				command_return_value=cmd_debug(argument_2);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* HRT (Hash Rebalance Threshold) command */
		else if(!strncmp(argument_1, "hrt",3)) {
			if(number_of_args == 2) {
				command_return_value=cmd_hrt(argument_2);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* HRS (Hash Rebalance Size) command */
		else if(!strncmp(argument_1, "hrs",3)) {
			if(number_of_args == 2) {
				command_return_value=cmd_hrs(argument_2);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* HRI (Hash Rebalance Interval) command */
		else if(!strncmp(argument_1, "hri",3)) {
			if(number_of_args == 2) {
				command_return_value=cmd_hri(argument_2);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* INFO command */
		else if(!strncmp(argument_1, "i",1)) {
			command_return_value=cmd_info();
		}
		/* ALGORITHM command */
		else if(!strncmp(argument_1, "a",1)) {
			if(number_of_args == 2) {
				command_return_value=cmd_algorithm(argument_2);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* KILL command */
		else if(!strncmp(argument_1, "kill", 4)) {
			command_return_value=cmd_kill();
		}

		/* CREATE command */
		else if(!strncmp(argument_1, "c",1)) {
			if(number_of_args == 4) {
				command_return_value=cmd_create(MEMBER_ARG, argument_2, argument_3, argument_4);
			}
			else if(number_of_args == 5) {
				command_return_value=cmd_create(argument_2, argument_3, argument_4, argument_5);
			}
			else {
				printf("ERROR: not enough arguments\n");
				continue;
			}
		}
		/* MAXL command */
		else if(!strncmp(argument_1, "maxl",4)) {
			if(number_of_args == 3) {
				command_return_value=cmd_maxl(MEMBER_ARG, argument_2, argument_3);
			}
			else if(number_of_args == 4) {
				command_return_value=cmd_maxl(argument_2, argument_3, argument_4);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* MAXC command */
		else if(!strncmp(argument_1, "maxc",4)) {
			if(number_of_args == 3) {
				command_return_value=cmd_maxc(MEMBER_ARG, argument_2, argument_3);
			}
			else if(number_of_args == 4) {
				command_return_value=cmd_maxc(argument_2, argument_3, argument_4);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* RESET command */
		else if(!strncmp(argument_1, "r",1)) {
			if(number_of_args == 2) {
				command_return_value=cmd_reset(MEMBER_ARG, argument_2);
			}
			else if(number_of_args == 3) {
				command_return_value=cmd_reset(argument_2, argument_3);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* ENABLE command */
		else if(!strncmp(argument_1, "e",1)) {
			if(number_of_args == 2) {
				command_return_value=cmd_enable(MEMBER_ARG, argument_2);
			}
			else if(number_of_args == 3) {
				command_return_value=cmd_enable(argument_2, argument_3);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* DISABLE command */
		else if(!strncmp(argument_1, "d",1)) {
			if(number_of_args == 2) {
				command_return_value=cmd_disable(MEMBER_ARG, argument_2);
			}
			else if(number_of_args == 3) {
				command_return_value=cmd_disable(argument_2, argument_3);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
		/* OVERLOAD mode command */
		else if(!strncmp(argument_1, "o",1)) {
			if(number_of_args == 2) {
				command_return_value=cmd_overload_mode(argument_2);
			}
			else {
				printf("ERROR: incorrect arguments\n");
				continue;
			}
		}
	}
	return 0;
}
/* this function looks for .shm files in the supplied (or default) directory and then tries to connect admin to instance.
 * The idea is to build up a list of files that is then displayed so the the user can select what they want to connect to
 */
int cmd_scan_for_instances(){
	struct stat stat_buffer;
	int i=0;
	DIR *dp=NULL;
	struct dirent *ep=NULL;
	char *file_full_path='\0';
	int counter=0;
	errno=0;
	/* an array to store files referring to running instances of octopus */
	char *balancers[MAXBALANCERS];
	char input[64];

	/* open the run_dir requested */
	dp=opendir(run_dir);
	if(dp != NULL) {
		while((ep = readdir(dp))) {
			/* get the directory contents */
			i=(strlen(ep->d_name) + strlen(run_dir) + 1);
			file_full_path=malloc(i * sizeof(char));
			snprintf(file_full_path, i, "%s%s", run_dir, ep->d_name);
			stat(file_full_path, &stat_buffer);
			/* if it's a regular file then we'll use it */
			if(S_ISREG(stat_buffer.st_mode)) {
				balancers[counter]= malloc(strlen(file_full_path) + 1);
				strncpy(balancers[counter], file_full_path, (strlen(file_full_path) +1));
				counter++;
			}
			free(file_full_path);
		}
		closedir(dp);
	}
	else {
		printf("ERROR: Could not read dir %s : %s\n", run_dir, strerror(errno));
	}
	/* now we output a little list with options */
	if(counter > 1) {
		printf("select an instance...\n");
		for(i=0;i<counter; i++) {
			printf("%d. %s\n",i+1,balancers[i]);
		}
		printf("\n> ");
		while(fgets(input, 8, stdin) == NULL) {
			continue;
		}
		if((atoi(input) > counter) || (atoi(input) <= 0)) {
			printf("ERROR: Invalid instance %s\n", input);
			exit(1);
		}
		connect_to_shm(balancers[atoi(input)-1], ignore_version_check);

	}
	/* if there's only one file then we'll connect to it straight away */
	else if(counter == 1) {
		connect_to_shm(balancers[0], ignore_version_check);
	}
	else {
		printf("ERROR: Could not find any instances\n");
		exit(1);
	}

	return 0;
}

/* this command creates a server in the balancer */
int cmd_create(char *type, char *name, char *ip, char *port) {
	errno=0;
	int status;
	int cloneServer=SERVER_CLONE_FALSE;
	int serverStatus=SERVER_STATE_DISABLED;
	int serverPort=0;
	int serverMaxc=balancer->default_maxc;
	float servermaxl=balancer->default_maxl;
	char serverName[SERVERNAME_MAX_LENGTH];
	struct in_addr serverIP;
	memset(serverName,'\0',SERVERNAME_MAX_LENGTH);
	if(readonly==1) {
		printf("ERROR: This command not available in read-only mode!\n");
		return -1;
	}
	/*validate the inputs */
	if(strlen(ip) > 15) {
		printf("ERROR: ip value too long\n");
		return -1;
	}
	/*member, clone or invalid? */
	if(!strncmp(type,"m",1)) {
		cloneServer=SERVER_CLONE_FALSE;
	}
	else if(!strncmp(type,"c",1)) {
		cloneServer=SERVER_CLONE_TRUE;
	}
	else {
		printf("ERROR: server-type argument invalid\n");
		return -1;
	}
	strncpy(serverName, name, SERVERNAME_MAX_LENGTH);
	if(inet_aton(ip ,&serverIP) ==0) {
		printf("ERROR: invalid ip adress \"%s\"\n",ip);
		return -1;
	}
	serverPort=atoi(port);
	if (!(serverPort > 0)) {
		printf("ERROR: port value invalid\n");
		return -1;
	}
	/*ok we've validated our input now make a new server */
	status=create_balancer_server(cloneServer, serverName, serverStatus, STANDBY_STATE_FALSE, serverPort, &serverIP, serverMaxc, servermaxl);
	if(status==0) {
		if(cloneServer==SERVER_CLONE_FALSE) {
			printf("created member server \"%s\" successfully\n",serverName);
		}
		else {
			printf("created clone server \"%s\" successfully\n",serverName);
		}
		return 0;
	}
	else {
		printf("ERROR: unable to create server \"%s\"\n", serverName);
		return -1;
	}
}

/* this command changes the currently running balancing algorithm */
int cmd_algorithm(char *value) {
	if(readonly==1) {
		printf("ERROR: This command not available in read-only mode!\n");
		return -1;
	}
	else if(!strncmp(value, "rr",2)) {
		balancer->algorithm=ALGORITHM_RR;
		printf("Set balancing algorithm to roundrobin\n");
		return 0;
	}
	else if(!strncmp(value, "lc",2)) {
		balancer->algorithm=ALGORITHM_LC;
		printf("Set balancing algorithm to least connections\n");
		return 0;
	}
	else if(!strncmp(value, "ll",2)) {
		#ifdef USE_SNMP
		balancer->algorithm=ALGORITHM_LL;
		printf("Set balancing algorithm to least load\n");
		#else
		printf("ERROR: Cannot use Least Load algorithm unless SNMP support is enabled at compile time\n");
		return -1;
		#endif
		return 0;
	}
	else if(!strncmp(value, "hash",4)) {
		balancer->algorithm=ALGORITHM_HASH;
		printf("Set balancing algorithm to uri hashing\n");
		return 0;
	}
	else if(!strncmp(value, "static",5)) {
		balancer->algorithm=ALGORITHM_STATIC;
		printf("Set balancing algorithm to uri static hashing\n");
		return 0;
	}
	else {
		printf("ERROR: Argument 2 invalid\n");
		return 0;
	}
}

/* this command handles the hash rebalance interval setting */
int cmd_hri(char *value) {
	char *endptr;
	if(readonly==1) {
		printf("ERROR: This command not available in read-only mode!\n");
		return -1;
	}
	v1=strtol(value, &endptr, 10);
	if ((errno == ERANGE && (v1 == LONG_MAX || v1 == LONG_MIN)) || (errno != 0 && v1 == 0)) {
		printf("ERROR: Invalid parameter (interval)\n");
		return -1;
	}
	if(v1 < 0) {
    	printf("ERROR: invalid interval (< 0)\n");
		return -1;
	}
	balancer->hash_rebalance_interval=v1;
	if(v1==0) {
		printf("Disabled hash rebalancing\n");
	}
	else {
		printf("Set hash rebalance interval to %d seconds\n", balancer->hash_rebalance_interval);
	}
	return 0;
}

/* this command handles the hash rebalance size setting */
int cmd_hrs(char *value) {
	char *endptr;
	if(readonly==1) {
		printf("ERROR: This command not available in read-only mode!\n");
		return -1;
	}
	v1=strtol(value, &endptr, 10);
	if ((errno == ERANGE && (v1 == LONG_MAX || v1 == LONG_MIN)) || (errno != 0 && v1 == 0)) {
		printf("ERROR: Invalid parameter (size)\n");
		return -1;
	}
	if((v1 < 0) || (v1 > 100)) {
    	printf("ERROR: invalid size (> 100 or < 0)\n");
		return -1;
	}
	balancer->hash_rebalance_size=v1;
	printf("Set hash rebalance size to %d%%\n", balancer->hash_rebalance_size);
	return 0;
}

/* this command handles the hash rebalance threshold setting */
int cmd_hrt(char *value) {
	char *endptr;
	if(readonly==1) {
		printf("ERROR: This command not available in read-only mode!\n");
		return -1;
	}
	v1=strtol(value, &endptr, 10);
	if ((errno == ERANGE && (v1 == LONG_MAX || v1 == LONG_MIN)) || (errno != 0 && v1 == 0)) {
		printf("ERROR: Invalid parameter (threshold)\n");
		return -1;
	}
	if((v1 < 0) || (v1 > 100)) {
    	printf("ERROR: invalid threshold (> 100 or < 0)\n");
		return -1;
	}
	balancer->hash_rebalance_threshold=v1;
	printf("Set hash rebalance threshold to %d\n", balancer->hash_rebalance_threshold);
	return 0;
}

/* this command handles the monitor processes run interval setting */
int cmd_monitor(char *value) {
	char *endptr;
	if(readonly==1) {
		printf("ERROR: This command not available in read-only mode!\n");
		return -1;
	}
	v1=strtol(value, &endptr, 10);
	if ((errno == ERANGE && (v1 == LONG_MAX || v1 == LONG_MIN)) || (errno != 0 && v1 == 0)) {
		printf("ERROR: Invalid parameter (interval)\n");
		return -1;
	}
	else {
		balancer->monitor_interval=v1;
		printf("Set monitor interval to %d seconds\n", balancer->monitor_interval);
	}
	return 0;
}

/* this command sets the debug level */
int cmd_debug(char *value) {
	char *endptr;
	if(readonly==1) {
		printf("ERROR: This command not available in read-only mode!\n");
		return -1;
	}
	v1=strtol(value, &endptr, 10);
	if ((errno == ERANGE && (v1 == LONG_MAX || v1 == LONG_MIN)) || (errno != 0)) {
		printf("ERROR: Invalid parameter (interval)\n");
		return -1;
	}
	else {
		balancer->debug_level=v1;
		printf("Set debug level to %d\n", balancer->debug_level);
	}
	return 0;
}

/* this command sets the balancer's clone mode setting */
int cmd_clone_mode(char *value) {
	if(readonly==1) {
		printf("ERROR: This command not available in read-only mode!\n");
		return -1;
	}
	if(!strncmp(value, "e",1)) {
		balancer->clone_mode=CLONE_MODE_ON;
		printf("Enabling clone mode\n");
	}
	else if(!strncmp(value, "d",1)) {
		balancer->clone_mode=CLONE_MODE_OFF;
		printf("Disabling clone mode\n");
	}
	else {
		printf("ERROR: Argument 2 invalid\n");
		return -1;
	}
	set_cloned_state();
	return 0;
}

/* this command sets the balancer's overload setting */
int cmd_overload_mode(char *value) {
	if(readonly==1) {
		printf("ERROR: This command not available in read-only mode!\n");
		return -1;
	}
	if(!strncmp(value, "r",1)) {
		balancer->overload_mode=OVERLOAD_MODE_RELAXED;
		printf("Set overload mode to RELAXED\n");
	}
	else if(!strncmp(value, "s",1)) {
		balancer->overload_mode=OVERLOAD_MODE_STRICT;
		printf("Set overload mode to STRICT\n");
	}
	else {
		printf("ERROR: Argument 2 invalid\n");
		return -1;
	}
	return 0;
}

/* shows balancer information */
int cmd_info() {
	int i;
	printf("Process info\n");
	printf("============\n");
	printf("Octopus version :	%s\n", balancer->version);
	printf("Balancer PID:		%d\n", balancer->master_pid);
	printf("Monitor PID:		%d\n", balancer->monitor_pid);
	printf("Shared Memory ID:	%d\n", balancer->shmid);
	printf("Shared Memory file:	%s\n", balancer->shm_run_file_fullname);
	printf("Debug Level:		%d\n", balancer->debug_level);
	printf("\n");

	printf("Network info\n");
	printf("============\n");
	printf("Listening IP:		%s\n", inet_ntoa(balancer->binding_ip));
	printf("Listening TCP port:	%d\n", balancer->binding_port);
	printf("Member outbound IP:	%s\n", inet_ntoa(balancer->member_outbound_ip));
	printf("Clone outbound IP:	%s\n", inet_ntoa(balancer->clone_outbound_ip));
	printf("File Descriptor Limit:	%d\n", balancer->fd_limit);
	printf("Session Limit:		%d\n", balancer->session_limit);
	printf("\n");

	printf("Balancing algorithm info\n");
	printf("========================\n");
	printf("Algorithm:		%s\n", algorithm_status[balancer->algorithm - 1]);
	printf("Clone mode:		%s\n", cloning_status[balancer->clone_mode]);
	printf("Overload mode:		%s\n", overload_status[balancer->overload_mode]);
	printf("Session Weight:		%f\n", balancer->session_weight);
	printf("Default max conn limit:	%d\n", balancer->default_maxc);
	printf("Default max load limit:	%f\n", balancer->default_maxl);
	printf("\n");

	printf("Monitor info\n");
	printf("============\n");
	printf("Interval:		%d\n", balancer->monitor_interval);
	printf("Check timeout:		%d\n", balancer->connect_timeout);
	if(balancer->snmp_status == SNMP_ENABLED) {
		printf("SNMP status:		Enabled\n");
	}
	if(balancer->snmp_status == SNMP_DISABLED) {
		printf("SNMP status:		Disabled\n");
	}
	if(balancer->snmp_status == SNMP_NOT_INCLUDED) {
		printf("SNMP status:		Not compiled!\n");
	}
	printf("\n");

	printf("URI Hash info\n");
	printf("=============\n");
	printf("Table size:		%d\n", HASHTABLESIZE);
	printf("Rebalance threshold: 	%d%%\n", balancer->hash_rebalance_threshold);
	printf("Rebalance size:		%d%%\n", balancer->hash_rebalance_size);
	printf("Rebalance interval: 	%d seconds\n", balancer->hash_rebalance_interval);
	printf("%16s | %10s\n", "Server_Name", "URI_Assignments");
	for(i=0; i<balancer->nmembers; i++) {
		if(balancer->members[i].status != SERVER_STATE_FREE) {
			printf("%16s %8d\n", balancer->members[i].name, balancer->members[i].hash_table_usage);
		}
	}
	return 0;
}

/* kills the server */
int cmd_kill() {
	if(readonly==1) {
		printf("ERROR: This command not available in read-only mode!\n");
		return -1;
	}
	printf("Killing server with master pid %d and monitor pid %d\n", balancer->master_pid, balancer->monitor_pid);
	kill(balancer->monitor_pid, SIGTERM);
	kill(balancer->master_pid, SIGTERM);
	exit(0);
}

/* toggles a servers 'standby' flag */
int cmd_standby(char *type, char *id) {
	int status=0;
	status=set_subject(type, id, ADMIN_REQ_TRUE, SINGLE_TARGET_CMD);
	if (status != 0) {
		/* set_subject handles error notification */
		return -1;
	}
	if (subject[0]->standby_state== STANDBY_STATE_TRUE) {
		printf("Removing standby status for %s\n", subject[0]->name);
		subject[0]->standby_state= STANDBY_STATE_FALSE;
	}
	else {
		printf("Creating standby status for %s\n", subject[0]->name);
		subject[0]->standby_state= STANDBY_STATE_TRUE;
	}
	return 0;
}

/* remove a server from the LB */
int cmd_delete(char *type, char *id) {
	int status=0;
	status=set_subject(type, id, ADMIN_REQ_TRUE, SINGLE_TARGET_CMD);
	if (status != 0) {
		/* set_subject handles error notification */
		return -1;
	}
	subject[0]->status=SERVER_STATE_DELETED;
	printf("Deleting server \"%s\"\n", subject[0]->name);
	return 0;
}

/* sets a server's maximum load */
int cmd_maxl(char *type, char *id, char *value) {
	int status=0;
	status=set_subject(type, id, ADMIN_REQ_TRUE, SINGLE_TARGET_CMD);
	if (status != 0) {
		/* set_subject handles error notification */
		return -1;
	}
	subject[0]->maxl=atof(value);
	printf("Setting maximum load to %2.2f for server \"%s\"\n", subject[0]->maxl, subject[0]->name);
	return 0;
}

/* sets a server's maximum connections */
int cmd_maxc(char *type, char *id, char *value) {
	int status=0;
	status=set_subject(type, id, ADMIN_REQ_TRUE, SINGLE_TARGET_CMD);
	if (status != 0) {
		/* set_subject handles error notification */
		return -1;
	}
	subject[0]->maxc=atoi(value);
	printf("Setting maximum connections to %d for server \"%s\"\n", subject[0]->maxc, subject[0]->name);
	return 0;
}

/* enable a server */
int cmd_enable(char *type, char *id) {
	int i;
	int status=0;
	status=set_subject(type, id, ADMIN_REQ_TRUE, MULTI_TARGET_CMD);
	if (status != 0) {
		/* set_subject handles error notification */
		return -1;
	}
	for(i=0;i<subject_count;i++) {
		if(subject[i]->status==SERVER_STATE_DISABLED) {
			subject[i]->status=SERVER_STATE_ENABLED;
			printf("Enabling server \"%s\"\n",subject[i]->name);
		}
	}
	set_cloned_state();
	return 0;
}

/* disable a server */
int cmd_disable(char *type, char *id) {
	int i;
	int status=0;
	status=set_subject(type, id, ADMIN_REQ_TRUE, MULTI_TARGET_CMD);
	if (status != 0) {
		/* set_subject handles error notification */
		return -1;
	}
	for(i=0;i<subject_count;i++) {
		if((subject[i]->status==SERVER_STATE_ENABLED) || (subject[i]->status==SERVER_STATE_FAILED)) {
			subject[i]->status=SERVER_STATE_DISABLED;
			printf("Disabling server \"%s\"\n",subject[i]->name);
		}
	}
	set_cloned_state();
	return 0;
}

/* reset counters for one or all servers */
int cmd_reset(char *type, char *id) {
	int i;
	int status=0;
	status=set_subject(type, id, ADMIN_REQ_TRUE, MULTI_TARGET_CMD);
	if (status != 0) {
		/* set_subject handles error notification */
		return -1;
	}
	for(i=0;i<subject_count;i++) {
		subject[i]->bsent=0;
		subject[i]->brecv=0;
		subject[i]->completed_c=0;
	}
	if(subject_count > 1) {
		printf("Reset all counters\n");
	}
	else {
		printf("Reset counters for server \"%s\"\n", subject[0]->name);
	}
	return 0;
}

/* print out some examples */
int cmd_examples() {
	printf("Usage Examples:\n\n");
	printf("Showing the current configuration including server names, IP, port, server type, and id #\n");
	printf("$ show\n");
	printf("\n");
	printf("Creating a new member webserver named \"Hercules\" @ 192.168.1.102 on tcp/80:\n");
	printf("$ create member Hercules 192.168.1.102 80\n");
	printf("\n");
	printf("Setting the maximum number of connections we will allow to \"Hercules\" which has id # of 2\n");
	printf("$ maxc m 2 500\n");
	printf("\n");
	printf("Setting the maximum load we will allow for \"Hercules\" which has id # of 2\n");
	printf("$ maxl m 2 4.0\n");
	printf("\n");
	printf("Creating a new clone webserver named \"David\" @ 10.1.1.5 on tcp/80:\n");
	printf("$ create clone David 10.1.1.5 80\n");
	printf("\n");
	printf("Turning on request cloning in strict mode\n");
	printf("$ mode strict\n");
	printf("\n");
	printf("Setting overload to strict mode\n");
	printf("$ overload strict\n");
	printf("\n");
	printf("Dynamically changing the load balancing algorithm to \"Least Connections\"\n");
	printf("$ algorithm lc\n");
	printf("\n");
	return 0;
}

/* show available commands to the user */
int cmd_help() {
	if(readonly==1) {
		printf("available commands for read-only mode:\n");
		printf("[s]how						overview and statistics for all members and clones\n");
		printf("[i]nfo						overview of load balancer configuration\n");
		printf("[scan]						search for and connect to other instances of octopus running on same host\n");
		printf("[q]uit						quit the admin interface\n");
		printf("[ex]amples					show some usage examples\n");
		printf("[?]						this screen\n");
	}
	else {
		printf("available commands:\n");
		printf("[s]how						overview and statistics for all member and clone servers\n");
		printf("[c]reate <[c]lone/[m]ember> <name> <ip> <port>	create a new member or clone server\n");
		printf("[delete] <[c]lone/[m]ember> <#>			deletes specified member or clone server\n");
		printf("[d]isable [a]ll / (<[c]lone/[m]ember> <#>)	disables all (or specified members or clone) servers\n");
		printf("[e]nable [a]ll / (<[c]lone/[m]ember> <#>)	enables all (or specified member or clone) servers\n");
		printf("[maxc] <[c]lone/[m]ember> <#> <value>		sets the maximum number of connections for the specified member or clone\n");
		printf("[standby] <[c]lone/[m]ember> <#>		toggles the standby state of specified member or clone\n");
		if(balancer->snmp_status != SNMP_NOT_INCLUDED) {
			printf("[maxl] <[c]lone/[m]ember> <#> <value>		sets the maximum load for the specified member or clone\n");
			printf("[o]verload <[s]trict/[r]elaxed>			set the overload mode\n");
			printf("[a]lgorithm <[rr]/[lc]/[ll]/[hash]/[static]>	set the balancing algorithm\n");
			printf("[hrt] <value>	  				set the hash rebalance threshold to <value>%%\n");
			printf("[hrs] <value>  					set the hash rebalance size to <value>%%\n");
			printf("[hri] <value>	  				set the hash rebalance interval to <value> seconds\n");
			printf("[snmp] <on/off>					set snmp monitoring on or off\n");
		}
		else {
			printf("[a]lgorithm <[rr]/[lc]/[hash]>			set the balancing algorithm\n");
		}
		printf("[clone] <[e]nable/[d]isable>			set the cloning mode\n");
		printf("[monitor] <seconds>				set the time period between runs of the monitor process\n");
		printf("[r]eset <[a]ll> / <[c]lone/[m]ember> <#>	resets counters for member, clone or all servers\n");
		printf("[i]nfo						overview of load balancer configuration\n");
		printf("[scan]						search for and connect to other instances of octopus running on same host\n");
		printf("[debug] <level>					set the debug level. <level> is an integer ranging from 0 (no debug) to 6 (extremely verbose)\n");
		printf("[q]uit						quit the admin interface\n");
		printf("[kill]						kill server process (and admin)\n");
		printf("[ex]amples					show some usage examples\n");
		printf("[x]tended output mode toggle			toggles extra columns like bytes in/out, handled connections and raw SNMP load\n");
		printf("[?]						this screen\n");
	}
	return 0;
}

/* toggle SNMP monitoring */
int cmd_snmp(char *value) {
	if(readonly==1) {
		printf("ERROR: This command not available in read-only mode!\n");
		return -1;
	}
	if(balancer->snmp_status==SNMP_NOT_INCLUDED) {
		printf("ERROR: server instance does not have SNMP support compiled!\n");
		return -1;
	}
	else {
		if(value == NULL) {
			printf("ERROR: not enough args\n");
			return -1;
		}
		if(!strncmp(value, "on",2 )) {
			balancer->snmp_status=SNMP_ENABLED;
			printf("Set snmp status to ENABLED");
		}
		if(!strncmp(value, "off",3 )) {
			balancer->snmp_status=SNMP_DISABLED;
			printf("Set snmp status to DISABLED");
		}
	}
	return 0;
}

/* show current state of balancer */
int cmd_show() {
	int i;
	/* prints out CSV output when requested */
	if(csv==1) {
		for(i=0; i<balancer->nmembers; i++) {
			if(balancer->members[i].status == SERVER_STATE_FREE) {
				continue;
			}
			printf("%s,%d,%s,%s,%s,%s,%d,%d,%d,%lu,%lu,%lu,%f,%f,%d\n","Member", i, balancer->members[i].name, server_status[balancer->members[i].status], standby_status[balancer->members[i].standby_state], inet_ntoa(balancer->members[i].myaddr.sin_addr), balancer->members[i].port, balancer->members[i].c, balancer->members[i].maxc, balancer->members[i].completed_c, balancer->members[i].bsent, balancer->members[i].brecv, balancer->members[i].load, balancer->members[i].maxl, balancer->members[i].e_load);
		}
		for(i=0; i<balancer->nclones; i++) {
			if(balancer->clones[i].status == SERVER_STATE_FREE) {
				continue;
			}
			printf("%s,%d,%s,%s,%s,%s,%d,%d,%d,%lu,%lu,%lu,%f,%f,%d\n","Clone", i, balancer->clones[i].name, server_status[balancer->clones[i].status], standby_status[balancer->clones[i].standby_state], inet_ntoa(balancer->clones[i].myaddr.sin_addr), balancer->clones[i].port, balancer->clones[i].c, balancer->clones[i].maxc, balancer->clones[i].completed_c, balancer->clones[i].bsent, balancer->clones[i].brecv, balancer->clones[i].load, balancer->clones[i].maxl, balancer->clones[i].e_load );
		}
	}
	/* when the user asks for SHOW we suppress different column heads if they're not required (ie. load column is irrelevant is SNMP is not compiled in */
	else {

		/* GENERAL INFO */
		printf("%s:%d, Algorithm: %s, Cloning: %s", inet_ntoa(balancer->binding_ip), balancer->binding_port, algorithm_status[balancer->algorithm - 1], cloning_status[balancer->clone_mode]);
		/* we could use the snmp_status variable from BALANCER but there can exist the situation where no load values are present ie. all servers down, or octopus recently started */
		if(balancer->snmp_status==SNMP_ENABLED) {
			printf(", Overload: %s", overload_status[balancer->overload_mode]);
			if(balancer->overall_load >= 0) {
				printf(", Load @ %d%%", balancer->overall_load);
			}
		}
			printf("\n");

		/* COLUMN HEADINGS */
		printf("%9s %3s %16s  %8s %16s %4s %5s ","type", "#", "name", "status", "ip-address", "port", "c");

		if(extended_output_mode == 1) {
			printf("%5s %7s %12s %12s","maxc", "hc", "bsent", "brecv");
		}
		if(balancer->snmp_status==SNMP_ENABLED) {
			if(extended_output_mode == 1) {
				printf(" %5s  %5s ", "load", "maxl");
			}
			printf("%7s", "loading");
		}
		printf("\n");


		/* MEMBER SERVER INFO LINES */
		for(i=0; i<balancer->nmembers; i++) {
			if(balancer->members[i].status == SERVER_STATE_FREE) {
				continue;
			}
			printf("%3s%6s %3d %16s  %8s %16s %4d %5d ", standby_status[balancer->members[i].standby_state], "Member",i, balancer->members[i].name, server_status[balancer->members[i].status], inet_ntoa(balancer->members[i].myaddr.sin_addr), balancer->members[i].port, balancer->members[i].c);
			if(extended_output_mode == 1) {
				printf("%5d %7lu %12lu %12lu", balancer->members[i].maxc, balancer->members[i].completed_c, balancer->members[i].bsent, balancer->members[i].brecv);
			}
			if(balancer->snmp_status==SNMP_ENABLED) {
				if(extended_output_mode == 1) {
					if(balancer->members[i].load == SNMP_LOAD_NOT_INIT) {
						printf("     - ");
					}
					else if(balancer->members[i].load == SNMP_LOAD_FAILED) {
						printf("   ??? ");
					}
					else {
						printf(" % 2.2f ", balancer->members[i].load);
					}
					printf(" % 2.2f ", balancer->members[i].maxl);
				}
				if(balancer->members[i].load >= 0) {
					printf("   %3d%%", balancer->members[i].e_load);
				}
			}
			printf("\n");
		}
		/* CLONE SERVER INFO LINES */
		for(i=0; i<balancer->nclones; i++) {
			if(balancer->clones[i].status == SERVER_STATE_FREE) {
				continue;
			}
			printf(" %3s%5s %3d %16s  %8s %16s %4d %5d ", standby_status[balancer->clones[i].standby_state], "Clone",i, balancer->clones[i].name, server_status[balancer->clones[i].status], inet_ntoa(balancer->clones[i].myaddr.sin_addr), balancer->clones[i].port, balancer->clones[i].c);
			if(extended_output_mode == 1) {
				printf("%5d %7lu %12lu %12lu", balancer->clones[i].maxc, balancer->clones[i].completed_c, balancer->clones[i].bsent, balancer->clones[i].brecv);
			}
			if(balancer->snmp_status==SNMP_ENABLED) {
				if(extended_output_mode == 1) {
					if(balancer->clones[i].load == SNMP_LOAD_NOT_INIT) {
						printf("     - ");
					}
					else if(balancer->clones[i].load == SNMP_LOAD_FAILED) {
						printf("   ??? ");
					}
					else {
						printf(" % 2.2f ", balancer->clones[i].load);
					}
					printf(" % 2.2f ", balancer->clones[i].maxl);
				}
				if(balancer->clones[i].load >= 0) {
					printf("   %3d%%", balancer->clones[i].e_load);
				}
			}
			printf("\n");
		}
	}
	return 0;
}

/* most commands will act upon one or more servers, this function generates a list of servers and also validates some of the user's input */
int set_subject(char *type, char *id, int privilege_required, int number_of_targets) {
	errno=0;
	int i=0;
	char *endptr;
	int all_servers=0;
	subject_count=0;
	if(privilege_required==ADMIN_REQ_TRUE && readonly==1) {
		printf("ERROR: This command not available in read-only mode!\n");
		return -1;
	}
	/* user wants all servers! */
	if(!strncmp(id, "a",1)) {
		if(number_of_targets == MULTI_TARGET_CMD) {
			all_servers=1;
		}
		else {
			printf("ERROR: This command only accepts a single server as a target!\n");
			return -1;
		}
	}
	/* user has specified a particular server */
	else {
		v1=strtol(id, &endptr, 10);
		if ((errno == ERANGE && (v1 == LONG_MAX || v1 == LONG_MIN)) || (errno != 0 && v1 == 0)) {
			printf("ERROR: Invalid parameter (ID #)\n");
			return -1;
		}
	}
	/* is the server a member or a clone? */
	/* member here */
	if((!strncmp(type, "m",1) || all_servers == 1)) {
		if(all_servers==1) {
			for(i=0; i< balancer->nmembers;i++) {
				/* we won't allow deleted or empty slots to be manipulated */
				if((balancer->members[i].status != SERVER_STATE_FREE) && (balancer->members[i].status != SERVER_STATE_DELETED)) {
					//printf("adding server \"%s\" to slot %d\n", balancer->members[i].name, subject_count);
					subject[subject_count++] = &(balancer->members[i]);
				}
			}
		}
		else {
			if((balancer->members[v1].status != SERVER_STATE_FREE) && (balancer->members[v1].status != SERVER_STATE_DELETED) && (v1 < balancer->nmembers) && (v1 >= 0)) {
				//printf("adding server \"%s\" to slot %d\n", balancer->members[i].name, subject_count);
				subject[subject_count++] = &(balancer->members[v1]);
			}
			else {
				printf("ERROR: Invalid member ID!\n");
				return -1;
			}
		}
	}
	/* clone here */
	if((!strncmp(type, "c",1) || all_servers == 1)) {
		if(all_servers==1) {
			for(i=0; i< balancer->nclones;i++) {
				/* we won't allow deleted or empty slots to be manipulated */
				if((balancer->clones[i].status != SERVER_STATE_FREE) && (balancer->clones[i].status != SERVER_STATE_DELETED)) {
					//printf("adding server \"%s\" to slot %d\n", balancer->clones[i].name, subject_count);
					subject[subject_count++] = &(balancer->clones[i]);
				}
			}
		}
		else {
			if((balancer->clones[v1].status != SERVER_STATE_FREE) && (balancer->clones[v1].status != SERVER_STATE_DELETED) && (v1 < balancer->nclones) && (v1 >= 0)) {
				//printf("adding server \"%s\" to slot %d\n", balancer->clones[i].name, subject_count);
				subject[subject_count++] = &(balancer->clones[v1]);
			}
			else {
				printf("ERROR: Invalid clone ID!\n");
				return -1;
			}
		}
	}
	/* user has put something weird into the input */
	if ((strncmp(type, "c", 1)) && (strncmp(type, "m", 1)) && strncmp(type, "a", 1)) {
		printf("ERROR: Invalid type\n");
		return -1;
	}
	return 0;
}

