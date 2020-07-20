/*
 * Octopus Load Balancer - Monitor process.
 *
 * Copyright 2008-2011 Alistair Reay <alreay1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */


/* this function tries to create a connection to a server and sets its' state appropriately (enabled or failed) */
int check_server_state(SERVER *server) {
	int serverfd;
	int status=0;
	int select_status=0;
	int read_status=0;
	int err_status=0;
	struct sockaddr_in server_addr;
	char readbuffer[1];

	/* we are only interested if an ENABLED server has died or a FAILED server has revived */
	if((server->status != SERVER_STATE_ENABLED) && (server->status != SERVER_STATE_FAILED)) {
		return 0;
	}
	if(balancer->debug_level>2) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: monitor: checking state of server \"%s\"", server->name);
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}
	errno=0;
	if ((serverfd=socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		if(errno == ENFILE) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: monitor: max fds reached");
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
		}
		if(balancer->debug_level>1) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "NOTICE: monitor: cannot create socket to connect to member server: %s",strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
		}
		return -1;
	}
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons((uint16_t)(server->port));
	server_addr.sin_addr = server->myaddr.sin_addr;
	memset(server_addr.sin_zero, '\0', sizeof(server_addr.sin_zero));
	status =fcntl(serverfd, F_SETFL, O_NONBLOCK);
	if(status<0) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: monitor: cannot set socket to NONBLOCK: %s", strerror(errno));
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
		return -1;
	}
	status = setsockopt(serverfd, IPPROTO_TCP, TCP_NODELAY,(char *)&yes, (socklen_t)sizeof(yes));
	if(status<0) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: monitor: cannot set socket to NODELAY: %s", strerror(errno));
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
		return -1;
	}
	status = setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, (socklen_t)sizeof(yes));
		if(status<0) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: monitor: cannot set socket REUSEADDR: %s", strerror(errno));
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}
	status= setsockopt(serverfd, SOL_SOCKET, SO_KEEPALIVE, (char *)&yes, (socklen_t)sizeof(yes));
	if(status<0) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: monitor: cannot set socket KEEPALIVE: %s", strerror(errno));
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}
	/* connect to the server's designated tcp port. we connect in non-blocking mode then let select wait on it for
	 * some user-definable length of time. if the timeout occurs then we say the sever has failed. if select does
	 * return before the timeout occurs then we check to see that it wasn't refused (unreadable socket) or in
	 * some sort of error state and then say the server is alive
	 */
	status= connect(serverfd, (struct sockaddr *)&server_addr, (socklen_t)sizeof(server_addr));
	if((status ==0) || (errno == EINPROGRESS)) {
		struct timeval timeout;
		memset(&timeout, '\0', sizeof(struct timeval));
		timeout.tv_sec = balancer->connect_timeout;
		fd_set read_fd_set;
		fd_set write_fd_set;
		fd_set err_fd_set;

		FD_ZERO(&read_fd_set);
		FD_ZERO(&write_fd_set);
		FD_ZERO(&err_fd_set);
		FD_SET(serverfd, &read_fd_set);
		FD_SET(serverfd, &write_fd_set);
		FD_SET(serverfd, &err_fd_set);

		select_status = select(serverfd +1, &read_fd_set, &write_fd_set, &err_fd_set, &timeout);
		if(select_status >= 1) {
			/* this is where we detect TCP RST or 'connection refused'	 */
			if(FD_ISSET(serverfd, &read_fd_set)) {
				read_status = read(serverfd, readbuffer, sizeof(readbuffer));
				if((read_status == -1) && (balancer->debug_level>2)) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: monitor: server \"%s\" read failed: %s",server->name, strerror(errno));
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}

			}
			if(read_status != -1) {
				/* does select() think the socket came up with an error? */
				if(FD_ISSET(serverfd, &err_fd_set)) {
					err_status = -1;
					if((err_status == -1) && (balancer->debug_level>2)) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "NOTICE: monitor: server \"%s\" connect raised exception: %s", server->name,strerror(errno));
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
				}
				if(err_status != -1) {
					/* if the server was marked as failed but has now tested OK then we log something to that effect */
					if(server->status == SERVER_STATE_FAILED) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "NOTICE: monitor: server \"%s\" has revived", server->name);
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
					/* don't want to log this message (server alive) twice when in debugging mode */
					else {
						if(balancer->debug_level>2) {
							snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: monitor: setting server \"%s\" to enabled", server->name);
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
						}
					}
					server->status= SERVER_STATE_ENABLED;
				}
			}
		}
		/* check has timed out, or read returned an error, or sock was in select's error balancer; meaning server has failed */
		if((select_status <=0) || (read_status == -1) || (err_status == -1)) {
			if(server->status == SERVER_STATE_ENABLED) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "NOTICE: monitor: server \"%s\" has failed", server->name);
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
			}
			/* don't want to log this message (server failed) twice when in debugging mode */
			else {
				if(balancer->debug_level>2) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "NOTICE: monitor: setting server \"%s\" to failed", server->name);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			server->status= SERVER_STATE_FAILED;
			server->load=SNMP_LOAD_NOT_INIT;
			server->e_load=0;
		}
		set_cloned_state();
		shutdown(serverfd, SHUT_RDWR);
		close(serverfd);
	}
	return 0;
}

/* get the 1 minute load average from the server */
int check_server_snmp(SERVER *server) {
#ifdef USE_SNMP
/*
Load
1 minute Load: .1.3.6.1.4.1.2021.10.1.3.1
5 minute Load: .1.3.6.1.4.1.2021.10.1.3.2
15 minute Load: .1.3.6.1.4.1.2021.10.1.3.3
CPU
percentage of user CPU time: .1.3.6.1.4.1.2021.11.9.0
raw user cpu time: .1.3.6.1.4.1.2021.11.50.0
percentages of system CPU time: .1.3.6.1.4.1.2021.11.10.0
raw system cpu time: .1.3.6.1.4.1.2021.11.52.0
percentages of idle CPU time: .1.3.6.1.4.1.2021.11.11.0
raw idle cpu time: .1.3.6.1.4.1.2021.11.53.0
raw nice cpu time: .1.3.6.1.4.1.2021.11.51.0
Memory Statistics
Total Swap Size: .1.3.6.1.4.1.2021.4.3.0
Available Swap Space: .1.3.6.1.4.1.2021.4.4.0
Total RAM in machine: .1.3.6.1.4.1.2021.4.5.0
Total RAM used: .1.3.6.1.4.1.2021.4.6.0
Total RAM Free: .1.3.6.1.4.1.2021.4.11.0
Total RAM Shared: .1.3.6.1.4.1.2021.4.13.0
Total RAM Buffered: .1.3.6.1.4.1.2021.4.14.0
Total Cached Memory: .1.3.6.1.4.1.2021.4.15.0
*/
	struct snmp_session ss1;
	struct snmp_session *ss2;
	struct snmp_pdu *pdu1;
	struct snmp_pdu *pdu2;
	oid anOID[MAX_OID_LEN];
	size_t anOID_len = MAX_OID_LEN;
	struct variable_list *vars;
	int status;
	init_snmp("octopus");

	if((server->status == SERVER_STATE_ENABLED) || server->status == SERVER_STATE_DISABLED) {
		snmp_sess_init(&ss1);
		ss1.peername = inet_ntoa(server->myaddr.sin_addr);
		ss1.version = SNMP_VERSION_1;
		ss1.community = balancer->snmp_community_pw;
		ss1.community_len = strlen((const char *)ss1.community);
		ss2 = snmp_open(&ss1);
		if(!ss2) {
			server->load= SNMP_LOAD_FAILED;
			return -1;
		}
		pdu1 = snmp_pdu_create(SNMP_MSG_GET);
		read_objid(".1.3.6.1.4.1.2021.10.1.3.1", anOID, &anOID_len);
		snmp_add_null_var(pdu1, anOID, anOID_len);
		status = snmp_synch_response(ss2, pdu1, &pdu2);
		if (status == STAT_SUCCESS && pdu2->errstat == SNMP_ERR_NOERROR) {
			for(vars = pdu2->variables; vars; vars = vars->next_variable) {
				if(vars->type == ASN_OCTET_STR) {
					server->load= atof((const char *)vars->val.string);
				}
				else {
					server->load= SNMP_LOAD_FAILED;
				}

			}
		}
		else {
			server->load= SNMP_LOAD_FAILED;
			if(balancer->debug_level > 0) {
				if (status == STAT_SUCCESS) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: check_server_snmp: error in snmp packet: %s", snmp_errstring(pdu2->errstat));
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
				if (status == STAT_TIMEOUT) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: check_server_snmp: snmp timeout error for server: %s", server->name);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
		}
		if (pdu2) {
			snmp_free_pdu(pdu2);
			snmp_close(ss2);
		}
	}
#endif
	return 0;
}


/* Handles deleted servers. */
int handle_delete_servers() {
	int i;
	int j;
	for (i=0; i< balancer->nmembers; i++) {
		/* check if it is in state 'deleted' and has no connections active */
		if((balancer->members[i].status == SERVER_STATE_DELETED) && (balancer->members[i].c == 0)) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "NOTICE: Monitor: deleting member %s", balancer->members[i].name);
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
			/* reset any hash algorithm assignments from the hash_table */
			for(j=0; j < HASHTABLESIZE; j++) {
				if(balancer->hash_table[j] == i) {
					balancer->hash_table[j]=HASH_VAL_FREE;
				}
			}
			memset(&(balancer->members[i]), '\0', sizeof(SERVER));
			/* test if the server is at the end of the array in which case we don't have to do much */
			if(i == (balancer->nmembers -1)) {
				balancer->nmembers --;
			}
			else {
				balancer->members[i].status = SERVER_STATE_FREE;
			}
		}
	}
	for (i=0; i< balancer->nclones; i++) {
		/* check if it is in state 'deleted' and has no connections active */
		if((balancer->clones[i].status == SERVER_STATE_DELETED) && (balancer->clones[i].c == 0)) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "NOTICE: Monitor: deleting clone %s", balancer->clones[i].name);
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
			memset(&(balancer->clones[i]), '\0', sizeof(SERVER));
			/* test if the server is at the end of the array in which case we don't have to do much */
			if(i == (balancer->nclones -1)) {
				balancer->nclones --;
			}
			else {
				balancer->clones[i].status = SERVER_STATE_FREE;
			}
		}
	}
	return 0;
}

/* initialization for the monitor process. It is forked from the main octopus-server binary
 * The monitor's job is to
 * 1) Watch for servers that have been deleted and remove them when possible
 * 2) Watch for user instigated changes to load balancing algorithm and set them in the server process
 * 3) Monitor the health of member and clone servers and set state appropriately
 * 4) Rebalance the URI hashes assigned to each server when using URI HASH algorithm only
 */
int initialize_monitor(int argc, char *argv[]) {
	int childpid;
	int last_algorithm;
	int monitor_interval;
	int hash_rebalance_interval;
	int hash_rebalance_threshold;
	int hash_rebalance_size;
	unsigned short int i=0;;
	#ifdef USE_SNMP
	unsigned short int j=0;
	#endif
	if ((childpid = fork()) < 0) {
		snprintf(log_string, OCTOPUS_LOG_LEN,"ERROR: initialize_monitor: monitor process fork error: %s", strerror(errno));
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}
	/* child process */
	else if (childpid == 0) {
		snprintf(log_string, OCTOPUS_LOG_LEN,"STARTUP: initialize_monitor: monitor process started");
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
		balancer->monitor_pid= getpid();
		last_algorithm = balancer->algorithm;
		if(balancer->snmp_community_pw == NULL) {
			write_log(OCTOPUS_LOG_STD, "WARNING: initialize_monitor: SNMP password has not been set. Server load checks will be disabled!", SUPPRESS_OFF);
		}

		while(1) {
			monitor_interval = balancer->monitor_interval;
			hash_rebalance_interval = balancer->hash_rebalance_interval;
			hash_rebalance_threshold = balancer->hash_rebalance_threshold;
			hash_rebalance_size = balancer->hash_rebalance_size;
			balancer->connection_rejected_log_suppress=SUPPRESS_OFF;

			/* this loop just catches the situation where the user has requested monitor not to run. */
			if(monitor_interval <= 0) {
				sleep(5);
			}
			else {
				sleep(monitor_interval);
				handle_delete_servers();
				/* has the user changed the balancing algorithm since last run? */
				if(balancer->algorithm != last_algorithm) {
					if(balancer->algorithm == ALGORITHM_LL) {
						write_log(OCTOPUS_LOG_STD, "NOTICE: changing balancing algorithm to method: Least Load", SUPPRESS_OFF);
					}
					else if(balancer->algorithm == ALGORITHM_LC) {
						write_log(OCTOPUS_LOG_STD, "NOTICE: changing balancing algorithm to method: Least Connections", SUPPRESS_OFF);
					}
					else if(balancer->algorithm == ALGORITHM_RR) {
						write_log(OCTOPUS_LOG_STD, "NOTICE: changing balancing algorithm to method: Round Robin", SUPPRESS_OFF);
					}
					else if(balancer->algorithm == ALGORITHM_HASH) {
						write_log(OCTOPUS_LOG_STD, "NOTICE: changing balancing algorithm to method: URI Hash", SUPPRESS_OFF);
					}
					else if(balancer->algorithm == ALGORITHM_STATIC) {
						write_log(OCTOPUS_LOG_STD, "NOTICE: changing balancing algorithm to method: Static", SUPPRESS_OFF);
					}
					last_algorithm= balancer->algorithm;
				}

				/* this part tries connecting to the servers and sets states appropriately */
				for(i=0; i < balancer->nmembers; i++) {
					check_server_state(&(balancer->members[i]));
				}
				for(i=0; i < balancer->nclones; i++) {
					check_server_state(&(balancer->clones[i]));
				}
				#ifdef USE_SNMP
				if((balancer->snmp_status == SNMP_ENABLED) && (balancer->snmp_community_pw != NULL)) {
					/* if we're using SNMP then try to get 1 minute server load */
					for (i=0; i< balancer->nmembers; i++) {
						check_server_snmp(&(balancer->members[i]));
					}
					for (i=0; i< balancer->nclones; i++) {
						check_server_snmp(&(balancer->clones[i]));
					}
					calc_effective_load();

					/* decide whether or not to rebalance the server allocations of the HASH algorithm */
					if(balancer->algorithm == ALGORITHM_HASH) {
						if((hash_rebalance_interval > 0) && (hash_rebalance_threshold > 0) && (hash_rebalance_size >0)) {
							if(hash_rebalance_interval < monitor_interval) {
								write_log(OCTOPUS_LOG_STD, "NOTICE: changing hash_rebalance_interval as it is lower than monitor_interval", SUPPRESS_OFF);
								balancer->hash_rebalance_interval = monitor_interval;
								hash_rebalance_interval = monitor_interval;
							}
							// j is just a counter to figure out whether we should run a rebalance or not
							j++;
							if(j % (int)(hash_rebalance_interval / monitor_interval) == 0) {
								rebalance_hash();
								j=0;
							}
						}
					}
				}
				#endif
			}
		}
		return 0;
	}
	return 0;
}

/* this is just a simple SNMP load compare method used by the URI HASH algorithm */
static int cmp_member_load(const void *p1, const void *p2) {
#ifdef USE_SNMP
	if( ((SERVER *)p1)->e_load < ((SERVER *)p2)->e_load) {
		return -1;
	}
	if( ((SERVER *)p1)->e_load == ((SERVER *)p2)->e_load) {
		return 0;
	}
	if( ((SERVER *)p1)->e_load > ((SERVER *)p2)->e_load) {
		return 1;
	}
#endif
	return 0;
}


/* When using the HTTP URI HASH algorithm, and SNMP is enabled, we may need to move
 * some URIs from one server to another (due to overload or load difference between servers).
 * This function handles that task, it tries to normalize load across all servers.
 * The server with highest load and lowest load are found and their differences in effective load
 * are checked to see if they meet the threshold specified in the conf file (or admin command [hrt])
 * (hash rebalance threshold).
 * If the load difference threshold is met then [hrs] (hash rebalance size) percent of the highest loaded
 * server's URIs are shifted to the lowest.
 * for example: 100 URIs are mapped to server1 and 100 URIs to server2. Server1 is running at 90% and Server2 at 10%.
 * hrt is set at 30% and hrs at 5%. The threshold is met (difference between servers is 80% and threshold is
 * only 30%) so 5% of the 100 URIs allocated to Server1 are given to Server2. Therefore, after the rebalance,
 * Server1 has 95 URIs allocated to it and Server2 has 105.
 * The idea is that server1, now having less work to do in the future will start to "cool-down" and server2 will
 * start pulling it's weight by taking 5% of the URIs that are obviously causing Server1 to get heavily loaded.
 */
int rebalance_hash() {
#ifdef USE_SNMP
	SERVER subjects[balancer->nmembers];
	int i;
	int j;
	int hashes_to_move=0;
	unsigned short int low_load_server_id;
	unsigned short int high_load_server_id;
	/* j gets set with the number of enabled servers */
	j=0;
	for(i=0; i < balancer->nmembers; i++) {
		if((balancer->members[i].status == SERVER_STATE_ENABLED) && (balancer->members[i].standby_state != STANDBY_STATE_TRUE)) {
			subjects[j]=balancer->members[i];
			j++;
		}
	}
	/* if there is 1 or less servers then this is a waste of time */
	if(j <= 1) {
		return 0;
	}

	qsort(&subjects[0], j, sizeof(SERVER), cmp_member_load);

	low_load_server_id = subjects[0].id;
	high_load_server_id = subjects[j-1].id;

	/* calc e_load difference between most loaded server and least loaded */
	i = (balancer->members[high_load_server_id].e_load - balancer->members[low_load_server_id].e_load);
	/* we only want to rebalance if the difference between highest and lowest loaded servers is higher than user supplied threshold (hrt in admin) */
	if (i >= balancer->hash_rebalance_threshold) {
		hashes_to_move = (int) ((balancer->members[high_load_server_id].hash_table_usage * balancer->hash_rebalance_size) / 100);
		if(balancer->debug_level > 1) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "NOTICE: monitor: rebalance_hash: threshold met, trying to move %d uri hashes from %s to %s", hashes_to_move, balancer->members[high_load_server_id].name, balancer->members[low_load_server_id].name);
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
		}

		for(i=0; i < HASHTABLESIZE; i++) {
			if(hashes_to_move <= 0) {
				return 0;
			}
			if(balancer->hash_table[i] == high_load_server_id) {
				balancer->hash_table[i] = low_load_server_id;
				hashes_to_move --;
				balancer->members[high_load_server_id].hash_table_usage --;
				balancer->members[low_load_server_id].hash_table_usage ++;
			}
		}
	}
#endif
	return 0;
}


/* this function assigns a loading percentage to member and clone servers by dividing current SNMP load by max SNMP load */
int calc_effective_load() {
#ifdef USE_SNMP
	/* we don't want to divide using zero anywhere so get server vals into local vars to avoid any possible race conditions */
	float overall_maxl=0.0;
	float overall_l=0.0;
	int i;
	float server_load;
	float server_maxl;
	for(i=0; i < balancer->nmembers; i++) {
		server_load=balancer->members[i].load;
		server_maxl=balancer->members[i].maxl;
		if((server_load <= 0.0) || (server_maxl <= 0.0)) {
			balancer->members[i].e_load= 0;
		}
		else {
			balancer->members[i].e_load= ((server_load  / server_maxl) * 100);
			overall_l += balancer->members[i].load;
			overall_maxl += balancer->members[i].maxl;
		}
	}
	for(i=0; i < balancer->nclones; i++) {
		server_load=balancer->clones[i].load;
		server_maxl=balancer->clones[i].maxl;
		if((server_load < 0.0) || (server_maxl <= 0.0)) {
			balancer->clones[i].e_load= 0;
		}
		else {
			balancer->clones[i].e_load= ((server_load / server_maxl) * 100);
		}
	}
	if(overall_l >0) {
		balancer->overall_load = (overall_l / overall_maxl * 100);
	}
#endif
	return 0;
}

