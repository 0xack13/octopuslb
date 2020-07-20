/*
 * Octopus Load Balancer - Configuration file parser
 *
 * Copyright 2008-2011 Alistair Reay <alreay1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

char *trim_white_space(char *str)
{
	if (str == NULL)
	{
		return str;
	}
	char *end;

	// Trim leading space
	while(isspace(*str)) str++;

	if(*str == '\0')  // All spaces?
		return str;

	// Trim trailing space
	end = str + strlen(str) - 1;
	while(end > str && isspace(*end)) end--;

	// Write new null terminator
	*(end+1) = '\0';

	return str;
}

int parse_config_file(const char *conf_file) {
	int status;
	FILE *fp;
	char *directive;
	char *value;
	char buffer[1000];
	int inServerSection=0;
	int serverStatus=SERVER_STATE_ENABLED;
	int serverClone=SERVER_CLONE_FALSE;
	int standbyState=STANDBY_STATE_FALSE;
	int serverPort=80;
	int serverMaxc=balancer->default_maxc;
	float serverMaxl=balancer->default_maxl;
	struct in_addr serverIP;
	char serverName[SERVERNAME_MAX_LENGTH];

	int lineCounter=0;
	int v1;
	char *c1;
	errno=0;
	if((fp=fopen(conf_file,"r"))!=NULL) {
		while(fgets(buffer, 1000, fp)!=NULL) {
			lineCounter++;
			directive = trim_white_space(strtok(buffer,"="));
			value = trim_white_space(strtok(NULL," ="));
			if (inServerSection) {
				if (!strncmp(directive, "port",4)) {
					errno=0;
					serverPort=strtol(value, (char **)NULL, 10);
					if (errno !=0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parse_config_file: line %d: port value invalid: %s", lineCounter, strerror(errno));
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					if(!((serverPort > 0) && (serverPort < 65536))) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parse_config_file: line %d", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting port to: %d",serverPort);
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
				}
				if (!strncmp(directive, "maxc",4)) {
					errno=0;
					serverMaxc=strtol(value, (char **)NULL, 10);
					if (errno !=0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parse_config_file: line %d: maxc value invalid: %s", lineCounter, strerror(errno));
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					if(!(serverMaxc >= 0)) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parse_config_file: line %d: maxc value invalid", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting maxc to: %d",serverMaxc);
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
				}
				if (!strncmp(directive, "maxl",4)) {
					serverMaxl=atof(value);
					if(!(serverMaxl > 0.0)) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: maxl value invalid", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting maxl to: %f",serverMaxl);
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
				}
				if (!strncmp(directive, "status",5)) {
					if (!strncmp(value, "enabled",7)) {
						serverStatus=SERVER_STATE_ENABLED;
						if(balancer->debug_level > 0) {
							snprintf(log_string, OCTOPUS_LOG_LEN,"DEBUG: parse_config_file: setting status to: enabled");
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
						}
					}
					else if (!strncmp(value, "disabled",8)) {
						serverStatus=SERVER_STATE_DISABLED;
						if(balancer->debug_level > 0) {
							snprintf(log_string, OCTOPUS_LOG_LEN,"DEBUG: parse_config_file: setting status to: disabled");
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
						}
					}
					else {
						if (errno !=0) {
							snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parse_config_file: line %d: status value invalid: %s", lineCounter, value);
							write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
						}
					}
				}
				if (!strncmp(directive, "standby",7)) {
					if (!strncmp(value, "true",4)) {
						standbyState=STANDBY_STATE_TRUE;
						if(balancer->debug_level > 0) {
							snprintf(log_string, OCTOPUS_LOG_LEN,"DEBUG: parse_config_file: setting standby state to: true");
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
						}
					}
					else if (!strncmp(value, "false",5)) {
						standbyState=STANDBY_STATE_FALSE;
						if(balancer->debug_level > 0) {
							snprintf(log_string, OCTOPUS_LOG_LEN,"DEBUG: parse_config_file: setting standby state to: false");
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
						}
					}
					else {
						if (errno !=0) {
							snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parse_config_file: line %d: standby state value invalid: %s", lineCounter, value);
							write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
						}
					}
				}

				if (!strncmp(directive, "ip",2)) {
					status=inet_aton(value, &serverIP);
					if(status == 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parse_config_file: line %d: IP invalid", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN,"DEBUG: parse_config_file: setting IP to: %s", inet_ntoa(serverIP));
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
				}
				if (!strncmp(directive, "clone",5)) {
					if (!strncmp(value, "true",4)) {
						serverClone=SERVER_CLONE_TRUE;
						if(balancer->debug_level > 0) {
							snprintf(log_string, OCTOPUS_LOG_LEN,"DEBUG: parse_config_file: setting server type as: clone");
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
						}
					}
					else if (!strncmp(value, "false",5)) {
						standbyState=STANDBY_STATE_FALSE;
						if(balancer->debug_level > 0) {
							snprintf(log_string, OCTOPUS_LOG_LEN,"DEBUG: parse_config_file: setting server type as: member");
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
						}
					}
					else {
						if (errno !=0) {
							snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parse_config_file: line %d: server type value invalid: %s", lineCounter, value);
							write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
						}
					}
				}

			}

			if (!strncmp(directive, "[/",2)) {
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: found end-of-section: %s",directive);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
				inServerSection=0;
				status = create_balancer_server(serverClone, serverName, serverStatus, standbyState, serverPort, &serverIP, serverMaxc, serverMaxl);
				if(status < 0) {
					if(status==-1) {
						fprintf(stderr, "ERROR: memory allocation error\n");
					}
					if(status==-2) {
						fprintf(stderr, "ERROR: invalid tcp port number\n");
					}
					if(status==-3) {
						fprintf(stderr, "ERROR: invalid IP address\n");
					}
					fprintf(stderr, "FATAL ERROR: unable to create server %s\n", serverName);
					fprintf(stderr, "  -status  = %d\n", serverStatus);
					fprintf(stderr, "  -port    = %d\n", serverPort);
					fprintf(stderr, "  -IP      = %s", inet_ntoa(serverIP));
					fprintf(stderr, "  -Maxc    = %d\n", serverMaxc);
					fprintf(stderr, "  -Maxl = %f\n", serverMaxl);
					exit(1);
				}
				/* reset the values for the next server */
				serverStatus=1;
				serverPort=80;
				serverClone=SERVER_CLONE_FALSE;
				serverMaxc=balancer->default_maxc;
				serverMaxl=balancer->default_maxl;
				serverStatus=SERVER_STATE_ENABLED;
				standbyState=STANDBY_STATE_FALSE;
				continue;
			}
			if (!strncmp(directive, "[",1)) {
				inServerSection=1;
				memset(&serverName, '\0', SERVERNAME_MAX_LENGTH);
				memcpy(serverName, directive+1, strlen(directive)-2);
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: found start-of-section: %s",directive);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
				continue;
			}
			if (!strncmp(directive, "snmp_community_pw", 17)) {
#ifdef USE_SNMP
				balancer->snmp_community_pw = malloc(sizeof(u_char) * strlen(value) + 1);
				if(balancer->snmp_community_pw != NULL) {
					memcpy(balancer->snmp_community_pw, value, strlen(value));
					balancer->snmp_community_pw[strlen(value)]='\0';
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting snmp community password to \"%s\"", balancer->snmp_community_pw);
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
				}
				else {
					write_log(OCTOPUS_LOG_EXIT, "Unable to allocate memory for snmp_community_pw buffer!", SUPPRESS_OFF);
				}
#else
				snprintf(log_string, OCTOPUS_LOG_LEN, "NOTICE: parse_config_file: snmp_community_pw has no effect unless Octopus is compiled with SNMP development libraries");
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
#endif
			}

			if (!strncmp(directive, "snmp_enable", 11)) {
#ifdef USE_SNMP
				if(!strncmp(value, "on", 2)) {
					balancer->snmp_status=SNMP_ENABLED;
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting snmp status to enabled");
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
				}
				else {
					balancer->snmp_status=SNMP_DISABLED;
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting snmp status to disabled");
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
				}
#else
				snprintf(log_string, OCTOPUS_LOG_LEN, "NOTICE: parse_config_file: enabling snmp monitoring has no effect unless Octopus is compiled with SNMP development libraries");
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
#endif
			}


			if (!strncmp(directive, "shm_run_dir", 11)) {
				balancer->shm_run_dir= malloc(strlen(value) + 1);
				if(balancer->shm_run_dir != NULL) {
					memcpy(balancer->shm_run_dir, value, strlen(value));
					balancer->shm_run_dir[strlen(value)]='\0';
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting shm run dir to %s", balancer->shm_run_dir);
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
				}

				else {
					write_log(OCTOPUS_LOG_EXIT, "Unable to allocate memory for shm_run_dir buffer!", SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "session_weight", 14)) {
				balancer->session_weight=atof(value);
				if(!(balancer->session_weight >= 0.0)) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: session_weight value invalid", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}

				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting session_weight to %f", balancer->session_weight);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "algorithm",9)) {
				if (!strncmp(value, "RR", 2)) {
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting balancing algorithm to \"Round-Robin\"");
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
					balancer->algorithm= ALGORITHM_RR;
				}
				else if (!strncmp(value, "LC", 2)) {
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting balancing algorithm to \"Least-Connections\"");
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
					balancer->algorithm= ALGORITHM_LC;
				}
				else if (!strncmp(value, "LL", 2)) {
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting balancing algorithm to \"Least-Load\"");
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}

					#ifndef USE_SNMP
					fprintf(stderr, "ERROR: Cannot use algoritithm Least Load unless compiled with SNMP developlment libraries");
					exit(1);
					#endif
					balancer->algorithm= ALGORITHM_LL;
				}
				else if (!strncmp(value, "HASH", 4)) {
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting balancing algorithm to \"HTTP URI HASH\"");
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
					balancer->algorithm= ALGORITHM_HASH;
				}
				else if (!strncmp(value, "STATIC", 6)) {
					if(balancer->debug_level > 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting balancing algorithm to \"Static\"");
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
					balancer->algorithm= ALGORITHM_STATIC;
				}
				else {
					fprintf(stderr, "ERROR: parsing config file at line %d: unsupported allocation algorithm!!", lineCounter);
					exit(1);
				}
			}
			if (!strncmp(directive, "clone_mode",10)) {
				if(!strncmp(value, "disabled", 8)) {
					balancer->clone_mode=CLONE_MODE_OFF;
				}
				else if(!strncmp(value, "enabled",7)) {
					balancer->clone_mode=CLONE_MODE_ON;
				}
				else {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: clone_mode value invalid", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting clone_mode to: %d",balancer->clone_mode);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "overload_mode", 13)) {
				if(!strncmp(value, "STRICT", 5)) {
					balancer->overload_mode=OVERLOAD_MODE_STRICT;
				}
				else if(!strncmp(value, "RELAXED", 5)) {
					balancer->overload_mode=OVERLOAD_MODE_RELAXED;
				}
				else {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: overload_mode value invalid", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting overload_mode to: %d",balancer->overload_mode);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "default_maxc",12)) {
				balancer->default_maxc=strtol(value, (char **)NULL, 10);
				if (errno !=0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_logging: config file line %d: default_maxc value invalid: %s", lineCounter, strerror(errno));
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				serverMaxc=balancer->default_maxc;
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: initialize_logging: setting default_maxc value to: %d",balancer->default_maxc);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "default_maxl",12)) {
				balancer->default_maxl=atof(value);
				if (!(balancer->default_maxl >= 0.0)) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: initialize_logging: config file line %d: default_maxl must be greater than or equal to zero", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				serverMaxl=balancer->default_maxl;
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: initialize_logging: setting default_maxl value to: %f",balancer->default_maxl);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "hash_rebalance_threshold", 24)) {
				v1=strtol(value, &c1, 10);
	            if(value != c1) {
					if((v1 < 0) || (v1 >= 100)) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: hash_rebalance_threshold value invalid, must be greater than or equal to 0 and less than 100", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					else {
						balancer->hash_rebalance_threshold= v1;
					}
				}
				else {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: hash_rebalance_threshold value invalid, must be greater than or equal to 0 and less than 100", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting hash_rebalance_threshold to: %d",balancer->hash_rebalance_threshold);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "hash_rebalance_size", 19)) {
				v1=strtol(value, &c1, 10);
	            if(value != c1) {
					if((v1 < 0) || (v1 >= 100)) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: hash_rebalance_size value invalid, must be greater than or equal to 0 and less than 100", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
						continue;
					}
					else {
						balancer->hash_rebalance_size= v1;
					}
				}
				else {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: hash_rebalance_size value invalid, must be greater than or equal to 0 and less than 100", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting hash_rebalance_size to: %d",balancer->hash_rebalance_size);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "hash_rebalance_interval", 21)) {
				v1=strtol(value, &c1, 10);
	            if(value != c1) {
					if(v1 < 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: hash_rebalance_interval value invalid, must be greater than or equal to zero", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
						continue;
					}
					else {
						balancer->hash_rebalance_interval= v1;
					}
				}
				else {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: hash_rebalance_interval value invalid, must be greater than or equal to zero", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}

				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting hash_rebalance_interval to: %d",balancer->hash_rebalance_interval);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "shm_perms", 9)) {
				v1=strtol(value, &c1, 8);
				if(value != c1) {
					if(v1 <= 0 || v1 >= 777) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: shm_perms value invalid, must be between 0 and 777", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					else {
						balancer->shm_perms=v1;
					}
				}
				else {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: shm_perms value invalid, must be between 0 and 777", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting shm_perms to: %li",strtol(value, &c1, 10));
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "monitor_interval",16)) {
				v1=strtol(value, &c1, 10);
	            if(value != c1) {
					if(v1 < 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: monitor_interval value invalid, must be greater than or equal to 0", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					else {
						balancer->monitor_interval=v1;
					}
				}
				else {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: monitor_interval value invalid, must be greater than or equal to 0", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting monitor_interval to: %d",balancer->monitor_interval);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "connect_timeout",15)) {
				v1=strtol(value, &c1, 10);
	            if(value != c1) {
					if(v1 <= 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: connect_timeout value invalid, must be greater than zero", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					else {
						balancer->connect_timeout=v1;
					}
				}
				else {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: connect_timeout value invalid, must be greater than zero", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN,"DEBUG: parse_config_file: setting connect_timeout to: %d",balancer->connect_timeout);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "session_limit",13)) {
				v1=strtol(value, &c1, 10);
	            if(value != c1) {
					if(v1 < 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: session_limit value invalid, must be greater than or equal to 0 ", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					else {
						balancer->session_limit=v1;
					}
				}
				else {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: session_limit value invalid, must be greater than or equal to 0 ", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}

				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN,"DEBUG: parse_config_file: setting session_limit to: %d",balancer->connect_timeout);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "fd_limit",8)) {
				v1=strtol(value, &c1, 10);
	            if(value != c1) {
					if(v1 < 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: fd_limit value invalid, must be greater than or equal to 0", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					else {
						balancer->fd_limit=v1;
					}
				}
				else {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: fd_limit value invalid, must be greater than or equal to 0", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN,"DEBUG: parse_config_file: setting fd_limit to: %d",balancer->fd_limit);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}

			if (!strncmp(directive, "use_syslog",10)) {
				v1=strtol(value, &c1, 10);
	            if(value != c1) {
					if((v1 < 0) || (v1 >1)) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: use_syslog value invalid, must be either 0 or 1", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					else {
						balancer->use_syslog=v1;
					}
				}
				else {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: use_syslog value invalid, must be either 0 or 1", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN,"DEBUG: parse_config_file: setting use_syslog to: %d",balancer->use_syslog);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "binding_port",12)) {
				v1=strtol(value, &c1, 10);
	            if(value != c1) {
					if(v1 <= 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: binding_port value invalid, must be greater than 0", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
					}
					else {
						balancer->binding_port=v1;
					}
				}
				else {
						snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: binding_port value invalid, must be greater than 0", lineCounter);
						write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN,"DEBUG: parse_config_file: setting binding_port to: %d",balancer->binding_port);
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "binding_ip",10)) {
				if(inet_aton(value, &balancer->binding_ip) ==0) {
					snprintf(log_string, OCTOPUS_LOG_LEN,"ERROR: parsing config file at line %d: binding IP value invalid", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting binding_ip to: %s",inet_ntoa(balancer->binding_ip));
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "member_outbound_ip",18)) {
				if(inet_aton(value, &balancer->member_outbound_ip) ==0) {
					snprintf(log_string, OCTOPUS_LOG_LEN,"ERROR: parsing config file at line %d: member_outbound_ip value invalid", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				balancer->use_member_outbound_ip=1;
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting member_outbound_ip to: %s",inet_ntoa(balancer->member_outbound_ip));
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
			if (!strncmp(directive, "clone_outbound_ip",17)) {
				if(inet_aton(value, &balancer->clone_outbound_ip) ==0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: parsing config file at line %d: clone_outbound_ip value invalid", lineCounter);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				balancer->use_clone_outbound_ip=1;
				if(balancer->debug_level > 0) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: parse_config_file: setting clone_outbound_ip to: %s",inet_ntoa(balancer->clone_outbound_ip));
					write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				}
			}
		}
		fclose(fp);
		return 0;
	}
	else {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR! unable to open conf file! - %s", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	return -1;

}

