/*
 * Octopus Load Balancer - Initialization functions.
 *
 * Copyright 2008-2011 Alistair Reay <alreay1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

/* runs the server process which may involve running in the background (daemon mode) */
int initialize_process() {
	/* if we're not going to be running in the foreground then it's daemon time */
	if(balancer->foreground == FOREGROUND_INIT) {
		pid_t pid;
		pid_t sid;
		/* fork off */
		if ((pid = fork()) < 0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_process: Cannot fork a daemon - %s\n", strerror(errno));
			write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
		}
		/* kill the original (console attached) process */
		else if(pid != 0) {
			exit(0);
		}
		umask(0);
		/* we're running in a new session */
		sid = setsid();
		if (sid < 0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_process: Cannot set sid - %s\n", strerror(errno));
			write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
		}
		/* change running dir to / */
		if((chdir("/")) < 0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_process: Cannot chdir to / - %s\n", strerror(errno));
			write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
		}
		/* close the terminal in/out streams */
		fclose(stdin);
		fclose(stdout);
		fclose(stderr);
		/* we're now in full daemon mode */
		balancer->foreground=FOREGROUND_OFF;
	}

	balancer->master_pid= getpid();
	return 0;
}

/* Parse the configuration file looking for the logfile and balancer->debug_level directive */
int initialize_logging(char *conf_file) {
	FILE *fp;
	char *directive;
	char *value;
	char buffer[MAX_CONF_LINE_LEN];
	int lineCounter=0;
	errno=0;

	if((fp=fopen(conf_file,"r"))!=NULL) {
		while(fgets(buffer, MAX_CONF_LINE_LEN, fp)!=NULL) {
			lineCounter++;
			//directive = strtok(buffer,"=");
			//value = strtok(NULL," =");
			directive = trim_white_space(strtok(buffer,"="));
			value = trim_white_space(strtok(NULL," ="));
			/* compare each line looking for "log_file" */
			if (!strncmp(directive, "log_file",8)) {
				/* if we find it, allocate some space for the string */
				balancer->log_file_path=malloc(strlen(value) + 1);
				if(balancer->log_file_path == NULL) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_logging: Unable to allocate memory for logfile string - %s", strerror(errno));
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				memcpy(balancer->log_file_path, value, strlen(value));
				/* null terminate */
				balancer->log_file_path[strlen(value)]= '\0';
			}
			if (!strncmp(directive, "debug_level",11)) {
				/* if the user has overridden the balancer->debug_level setting then let it be */
				if (debug_level_override==1) {
					continue;
				}
				balancer->debug_level=strtol(value, (char **)NULL, 10);
				if (errno !=0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_logging: config file line %d: balancer->debug_level value invalid: %s", lineCounter, strerror(errno));
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(!((balancer->debug_level >= 0) && (balancer->debug_level <= 6))) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_logging: config file line %d: balancer->debug_level value invalid: %s", lineCounter, strerror(errno));
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(balancer->debug_level > 0 && balancer->foreground == FOREGROUND_ON) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: initialize_logging: setting balancer->debug_level value to: %d",balancer->debug_level);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
		}
		fclose(fp);

		if(balancer->log_file_path == NULL) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR! log_file directive not found in conf file");
			write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
		}

		/* try to open the logfile for writing */
		balancer->log_file= fopen(balancer->log_file_path, "a");
		if(balancer->log_file == NULL) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_logging: Unable to open log file for writing - %s- %s", balancer->log_file_path, strerror(errno));
			write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
		}
		write_log(OCTOPUS_LOG_STD, "STARTUP: logging subsystem initialized", SUPPRESS_OFF);
	}
	else {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR! unable to open conf file! - %s\n", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	return 0;
}


/* statically allocate all the memory required for handling client, member and ghost send/recv buffers
 * static allocation is memory hungry but it pays off in terms of stability and performance (no malloc required ondemand)
 */
SESSION* initialize_sessions() {
	int i;
	int supportable_sessions=0;

	/* work out how many sessions we can support max with this amount of fds
	 * we divide by 3 because each session requires client, server,and clone FDs */
	supportable_sessions = (balancer->fd_limit - 4) / 3;
	snprintf(log_string, OCTOPUS_LOG_LEN, "STARTUP: initialize_sessions: With %d fds we can support %d sessions", balancer->fd_limit, supportable_sessions);
	write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	/* zero means we'll use the max setting */
	if(balancer->session_limit == 0) {
		balancer->session_limit = supportable_sessions;
	}
	/*if the user has set the session limit higher than the supportable amount then change it and notify via logfile */
	else if(balancer->session_limit > supportable_sessions) {
		balancer->session_limit = supportable_sessions;
		snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: initialize_sessions: User requested more sessions than we can support, setting session limit to %d", balancer->session_limit);
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}
	else {
		snprintf(log_string, OCTOPUS_LOG_LEN, "STARTUP: initialize_sessions: Using user defined session limit of %d sessions", balancer->session_limit);
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}

	/* now we allocate enough memory for all the sessions (static allocation) */
	sessions = malloc(sizeof(SESSION) * balancer->session_limit);
	if(sessions == NULL) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_sessions: Unable to allocate memory for sessions - %s", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	/* initialize all sessions with default (unused) values */
	memset(sessions,'\0',(sizeof(SESSION) * balancer->session_limit));
	for(i=0; i<balancer->session_limit;i++) {
		sessions[i].id=i;
		sessions[i].state=STATE_UNUSED;
		sessions[i].clientfd=-1;
		sessions[i].memberfd=-1;
		sessions[i].clonefd=-1;
		sessions[i].client_used_buffer=0;
		sessions[i].member_used_buffer=0;
		sessions[i].clone_used_buffer=0;
	}
	snprintf(log_string, OCTOPUS_LOG_LEN, "STARTUP: initialize_sessions: initialized %d sessions", balancer->session_limit);
	write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	initialize_unused_session(); // added by Cheng Ren, 2012-9-30
	return 0;
}

/*Begin---- by Cheng Ren, 2012-9-27*/
/*allocate memory for the unused session list*/
int initialize_unused_session() {

	if(sessions == NULL) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_unused_sessions: no available sessions  - %s", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	
	unused_session_queue.unused_sessions = malloc(sizeof(SESSION) * balancer->session_limit);
	unused_session_queue.head = 0;
	unused_session_queue.rear = 0;

	if(unused_session_queue.unused_sessions == NULL) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_unused_session: Unable to allocate memory for unused sessions - %s", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}

	snprintf(log_string, OCTOPUS_LOG_LEN, "STARTUP: initialize_unused_session: Using user defined session limit as the number of free sessions", balancer->session_limit);
	write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	
	/* add all of the unused sessions to the free list */
	int i;
	for(i=0; i< balancer->session_limit; i++) {
		if ((sessions[i].state) == STATE_UNUSED) {
			unused_session_queue.unused_sessions[unused_session_queue.rear] = &sessions[i];
			unused_session_queue.rear = (unused_session_queue.rear+1) % balancer->session_limit;  
		}
	}

	snprintf(log_string, OCTOPUS_LOG_LEN, "STARTUP: initialize_unused_session: initialized %d sessions", balancer->session_limit);
	write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	return 0;
}
/*End*/

/* try to set file descriptor limits and then initialize FD structure array */
int initialize_fds() {
	int status;
	/* this struct has a soft limit and hard limit field within it */
	struct rlimit limit;
	/* populate with the user defined limits */
	if(balancer->fd_limit > 0) {
		limit.rlim_max=balancer->fd_limit;
		limit.rlim_cur=balancer->fd_limit;
		/* try and set the system accordingly */
		status=setrlimit(RLIMIT_NOFILE, &limit);
		if(status!=0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: set_fd_limits: Unable to set fd limit");
		}
		else {
			snprintf(log_string, OCTOPUS_LOG_LEN, "STARTUP: set_fd_limits: Set fd limit to %d", balancer->fd_limit);
		}
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}

	/* regardless of what the user asked for we have to work with what the
	 * system has given us. */
	status=getrlimit(RLIMIT_NOFILE, &limit);
	if(status==0) {
		balancer->fd_limit=limit.rlim_cur;
	}
	else {
		balancer->fd_limit=0;
		snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: set_fd_limits: Unable to get fd limit");
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}

	/* allocate enough memory for all FDs (static allocation) */
	fds = malloc(sizeof(FD) * balancer->fd_limit);
	if(fds == NULL) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_fds: Unable to initialize file descriptor set  - %s", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	/* initialize with default values */
	memset(fds,'\0',(sizeof(FD) * balancer->fd_limit));
	snprintf(log_string, OCTOPUS_LOG_LEN, "STARTUP: initialize_fds: initialized file descriptor set with %d elements", balancer->fd_limit);
	write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	return 0;
}

/* allocate memory for the balancer and sets defaults settings */
int initialize_balancer() {
	int i;
	balancer = malloc(sizeof(BALANCER));
	if(balancer == NULL) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: Unable to allocate memory for balancer: %s", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	memset(balancer, '\0', sizeof(BALANCER));
	balancer->alive=1;
	balancer->nmembers=0;
	balancer->nclones=0;
	balancer->monitor_interval=DEFAULT_MONITOR_INTERVAL;
	balancer->connect_timeout=DEFAULT_CONNECT_TIMEOUT;
	balancer->algorithm=ALGORITHM_LC;
	balancer->clone_mode=CLONE_MODE_OFF;
	balancer->overload_mode=OVERLOAD_MODE_RELAXED;
	balancer->shmid=-1;
	balancer->use_syslog=0;
#ifdef USE_SNMP
	balancer->snmp_status=SNMP_DISABLED;
	balancer->overall_load=-1;
#else
	balancer->snmp_status=SNMP_NOT_INCLUDED;
#endif
	balancer->hash_rebalance_threshold=DEFAULT_REBALANCE_THRESHOLD;
	balancer->hash_rebalance_size=DEFAULT_REBALANCE_SIZE;
	balancer->hash_rebalance_interval=DEFAULT_REBALANCE_INTERVAL;
	balancer->use_member_outbound_ip=0;
	balancer->use_clone_outbound_ip=0;
	balancer->default_maxc=DEFAULT_MAXC;
	balancer->default_maxl=DEFAULT_MAXL;
	balancer->session_limit=DEFAULT_SESSION_LIMIT;
	balancer->fd_limit=DEFAULT_FD_LIMIT;
	balancer->binding_port=DEFAULT_PORT;
	balancer->session_weight=atof(DEFAULT_SESSION_WEIGHT);
	balancer->shm_perms=DEFAULT_SHM_PERMS;
	balancer->connection_rejected_log_suppress=0;
	balancer->debug_level=temporary_debug_level;
	inet_aton(DEFAULT_ADDRESS, &balancer->binding_ip);

	strncpy(balancer->version, OCTOPUS_VERSION, OCTOPUS_VERSION_LEN);
	for (i=0; i<HASHTABLESIZE; i++) {
		balancer->hash_table[i]=HASH_VAL_FREE;
	}
	return 0;
}

/* Create shared memory segment and copy BALANCER structure into segment */
int initialize_shm() {
	int status;
	int fd;
	char *data = NULL;
	key_t key;
	int shmid;
	umask(0);
	struct stat stat_buffer;
	char temp_name[512];
	/* test that a shm_run_dir has been defined */
	if(balancer->shm_run_dir == NULL) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_shm: No shm_run_dir specified! Check config file");
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	/* setup the shm_run_file attribute of the balancer struct */
	snprintf(temp_name, 512, "%s/%s-%s-%s-%d", balancer->shm_run_dir, "octopus", OCTOPUS_VERSION, inet_ntoa(balancer->binding_ip), balancer->binding_port);
	balancer->shm_run_file = malloc((strlen(temp_name) + 1) * sizeof(char));
	if(balancer->shm_run_file == NULL) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_shm: Unable to allocate memory for shm_run_file: %s", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	strncpy(balancer->shm_run_file, temp_name, strlen(temp_name) +1);
	strncpy(balancer->shm_run_file_fullname, temp_name, SHM_FILE_FULLNAME_MAX_LENGTH);

	/* the RUN_DIR directory should exist, if not, try to create it */
	status = stat(balancer->shm_run_dir, &stat_buffer);
	if(!S_ISDIR(stat_buffer.st_mode)) {
		if(mkdir(balancer->shm_run_dir, 0755) < 0) {
			if(errno != EEXIST) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_shm: Unable to create dir %s: %s", balancer->shm_run_dir, strerror(errno));
				write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
			}
		}
	}

	/* we should be creating a new .shm file in RUN_DIR. If the file already
	 * exists then there is already an instance running on that ip and port combo
	 */
	status = stat(balancer->shm_run_file, &stat_buffer);
	if(status == -1) {
		if ((fd = open(balancer->shm_run_file, O_CREAT | O_RDWR, balancer->shm_perms)) == -1) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_shm: Unable to create file %s: %s", balancer->shm_run_file, strerror(errno));
			write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
		}
	}
	else {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: Unable to create balancer. File %s already exists. Maybe a server instance is already running?", balancer->shm_run_file);
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}

	/* create a SHM key using a pathname and a project identifier */
	if ((key = ftok(balancer->shm_run_file, 'Z')) == -1) {
      	snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_shm: Unable to make SHM key - %s", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	snprintf(log_string, OCTOPUS_LOG_LEN, "STARTUP: initialize_shm: SHM key= %d",key);
	write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);

	/* allocate a SHM memory segment for storing the balancer struct */
	if ((shmid = shmget(key, sizeof(BALANCER), balancer->shm_perms | IPC_CREAT )) == -1) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_shm: Unable to create server SHM segment - %s", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	balancer->shmid=shmid;

	/* attach the SHM memory segment to the process' address space */
	data = shmat(shmid, (void *) 0, 0);
	if (data == (char *) (-1)) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_shm: Unable to attach to SHM segment - %s", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}

	/* copy all of the balancer struct into this address space */
	memcpy(data, balancer, sizeof(BALANCER));
	/* then free the original pointer */
	free(balancer);
	/* now make the pointer refer to the SHM segment */
	balancer=(BALANCER *)data;
	return 0;
}

/* standard listening socket creation for the load balancer */
int create_serversocket() {
	int listener;
	int status;
	struct sockaddr_in srvaddr;
	int yes=1;

	/* setup a IP address and TCP port struct */
	bzero(&srvaddr, sizeof(srvaddr));
	srvaddr.sin_family = AF_INET;
	memcpy(&srvaddr.sin_addr, &balancer->binding_ip, sizeof(struct in_addr));
	srvaddr.sin_port = htons((uint16_t)balancer->binding_port);

	/* get a socket file descriptor */
	listener = socket(AF_INET, SOCK_STREAM, 0);
	if(listener<0) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: create_server_socket: Unable to create server socket - %s", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	/* make it non-blocking */
	status = fcntl(listener, F_SETFL, O_NONBLOCK);
	if(status<0) {
		snprintf(log_string, OCTOPUS_LOG_LEN ,"ERROR: Unable to set server socket to NONBLOCK");
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	/* set nodelay */
	status = setsockopt(listener, IPPROTO_TCP, TCP_NODELAY,(char *) &yes, (socklen_t)sizeof(yes));
	if(status<0) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: Unable to set TCP_NODELAY on server socket");
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}
	/* set reuseaddr */
	status = setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes));
	if(status<0) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: Unable to set REUSEADDR on server socket");
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}
	/* bind the socket fd to the ip/tcp address */
	status = bind(listener, (struct sockaddr *)&srvaddr, (socklen_t)sizeof(srvaddr));
	if(status<0) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: Unable to bind to port. Check nothing else is bound on port %d at ip %s", balancer->binding_port, inet_ntoa(balancer->binding_ip));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	/* make the socket accept connections */
	status= listen(listener, 1024);
	if(status<0) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: Unable to listen() on server socket");
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	return listener;
}
