/*
 * Octopus Load Balancer - Balancing algorithms and server connections
 *
 * Copyright 2008-2011 Alistair Reay <alreay1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */


int get_available_servers() {
	int i=0;
	int status;
	using_member_standby=0;
	using_clone_standby=0;
	available_members_count=0;
	available_clones_count=0;
	use_clone=0;

	for (i=0; i < balancer->nmembers; i++) {
		/*if the candidate has exceeded its maximum load, is a standby server, or is invalid, then don't consider it */
		if((balancer->members[i].c >= balancer->members[i].maxc) || (balancer->members[i].status != SERVER_STATE_ENABLED) || (balancer->members[i].standby_state == STANDBY_STATE_TRUE)) {
			continue;
		}
		if((balancer->overload_mode == OVERLOAD_MODE_STRICT) && (balancer->members[i].e_load > 100)) {
			continue;
		}
		available_members[available_members_count++] = i;
	}
	/* if there were no ready servers then we scan again but this time relaxing the "standby" condition as this is what standbys are for */
	if (available_members_count == 0) {
		for (i=0; i < balancer->nmembers; i++) {
			if((balancer->members[i].c >= balancer->members[i].maxc) || (balancer->members[i].status != SERVER_STATE_ENABLED)) {
				continue;
			}
			if((balancer->overload_mode == OVERLOAD_MODE_STRICT) && (balancer->members[i].e_load > 100)) {
				continue;
			}
			available_members[available_members_count++] = i;
		}
		/* if we still can't get a server then we return an error */
		if (available_members_count == 0) {
			return -1;
		}
		else {
			using_member_standby=1;
		}
	}
	status=verify_server(&(balancer->members[next_member]));
	/* if the current next_member is invalid then set the first valid server we have */
	if(status == -1) {
		next_member=available_members[0];
	}
	/* if the current next_member is a standby but we have normal members available then use the normal one */
	if((status == 1) && (using_member_standby == 0)) {
		next_member=available_members[0];
	}

	if(balancer->clone_mode == CLONE_MODE_ON) {
		for (i=0; i < balancer->nclones; i++) {
			/*if the candidate has exceeded its maximum load, is a standby server, or is invalid, then don't consider it */
			if((balancer->clones[i].c >= balancer->clones[i].maxc) || (balancer->clones[i].status != SERVER_STATE_ENABLED) || (balancer->clones[i].standby_state == STANDBY_STATE_TRUE)) {
				continue;
			}
			if((balancer->overload_mode == OVERLOAD_MODE_STRICT) && (balancer->clones[i].e_load > 100)) {
				continue;
			}
			available_clones[available_clones_count++] = i;
		}
		/* if there were no ready servers then we scan again but this time relaxing the "standby" condition as this is what standbys are for */
		if (available_clones_count == 0) {
			for (i=0; i < balancer->nclones; i++) {
				if((balancer->clones[i].c >= balancer->clones[i].maxc) || (balancer->clones[i].status != SERVER_STATE_ENABLED)) {
					continue;
				}
				if((balancer->overload_mode == OVERLOAD_MODE_STRICT) && (balancer->clones[i].e_load > 100)) {
					continue;
				}
				available_clones[available_clones_count++] = i;
				using_clone_standby=1;
			}
		}
		if (available_clones_count > 0) {
			use_clone=1;
			status=verify_server(&(balancer->clones[next_clone]));
			if(status == -1) {
				next_clone=available_clones[0];
			}
			/* if the current next_clone is a standby but we have normal members available then use the normal one */
			if((status == 1) && (using_member_standby == 0)) {
				next_member=available_members[0];
			}
		}
	}
	return 0;
}

/* verifies that the selected member and clone are truly valid.
returns 0 if server is valid
returns -1 if server is invalid
returns 1 if server is valid but is a standby
*/
int verify_server(SERVER *server) {
    if(balancer->overload_mode == OVERLOAD_MODE_STRICT) {
        if( (server->status != SERVER_STATE_ENABLED) || (server->e_load > 100) || (server->c >= server->maxc) ) {
            return -1;
        }
    }
    if((server->status != SERVER_STATE_ENABLED) || (server->c >= server->maxc)) {
    	return -1;
    }
    if(server->standby_state==STANDBY_STATE_TRUE) {
    	return 1;
    }
    return 0;
}

/* turns a request URI into an integer */
unsigned short int DJBHash(char* str, unsigned int len) {
	unsigned int hash = 5381;
	unsigned int i    = 0;

	for(i = 0; i < len; str++, i++)	{
		hash = ((hash << 5) + hash) + (*str);
	}
	return (unsigned short int)(hash);
}

int choose_server(SESSION *session) {
	int status;
	status=get_available_servers();
	if(status != 0) {
		return -1;
	}
	if(balancer->algorithm == ALGORITHM_RR) {
		set_rr_server();
	}
	else if(balancer->algorithm == ALGORITHM_LL) {
		set_ll_server();
	}
	else if(balancer->algorithm == ALGORITHM_LC) {
		set_lc_server();
	}
	else if(balancer->algorithm == ALGORITHM_HASH) {
		set_hash_server(session);
	}
	else if(balancer->algorithm == ALGORITHM_STATIC) {
		set_static_server(session);
	}
	status=connect_server(session);
	return status;
}

int set_static_server(SESSION *session) {
	unsigned short int hash;
	char request_line[URI_LINE_LEN];
	memset(request_line, '\0', URI_LINE_LEN);
	char *uri_start = NULL;
	char *uri = NULL;

	/* copy up to URI_LINE_LEN characters from the first line of the request */
	memccpy(request_line, session->client_read_buffer, '\n', URI_LINE_LEN);
	/* get a pointer to the second word of the request */
	uri_start=strchr(request_line, ' ');
	if(uri_start != NULL) {
		/* then get that second word of the request (this is the uri) */
		uri = strtok(uri_start," ");
		/* if the strtok function returns NULL it is because the request is not a valid http request (ie. does not comform to "VERB NOUN" format) , use LC method */
		if(uri == NULL) {
			set_lc_server();
			return 0;
		}
		/* strip off query term when hashing uri */
		if(strchr(uri, '?') != NULL) {
			char *result = NULL;
			/* and then move the requested URL (minus the query string) into a temporary buffer */
			result = strtok(uri, "?");
			/* hash the stripped URI to get a number between 0 and HASHTABLESIZE */
			hash=DJBHash(result, (unsigned int)strlen(result));
			hash= hash % (HASHTABLESIZE -1);
		}
		else {
			/* if there is no query term, hash the full URI to get a number between 0 and HASHTABLESIZE */
			hash=DJBHash(uri, (unsigned int)strlen(uri));
			hash= hash % (HASHTABLESIZE -1);
		}
		next_member=available_members[hash % available_members_count];
		if(use_clone==1) {
			next_clone=available_clones[hash % available_clones_count];
		}
	}
	/* if the strchr function returns NULL it is because the request is not a valid http request (ie. does not comform to "VERB NOUN" format) , use LC method */
	else {
		set_lc_server();
	}
	return 0;
}

/* hashes the request URI to an integer between 0 and balancer->nmembers and then stores it so the next time the uri is requested it goes back to the same server. this algorithm is designed for HTTP connections only */
int set_hash_server(SESSION *session) {
	int status;
	unsigned short int hash;
	SERVER *candidate;
	char request_line[URI_LINE_LEN];
	memset(request_line, '\0', URI_LINE_LEN);
	char *uri_start = NULL;
	char *uri = NULL;

	/* copy up to URI_LINE_LEN characters from the first line of the request */
	memccpy(request_line, session->client_read_buffer, '\n', URI_LINE_LEN);
	/* get a pointer to the second word of the request */
	uri_start=strchr(request_line, ' ');
	if(uri_start != NULL) {
		/* then get that second word of the request (this is the uri) */
		uri = strtok(uri_start," ");
		/* if the strtok function returns NULL it is because the request is not a valid http request (ie. does not comform to "VERB NOUN" format) , use LC method */
		if(uri == NULL) {
			set_lc_server();
			return 0;
		}
		/* strip off query term when hashing uri */
		if(strchr(uri, '?') != NULL) {
			char *result = NULL;
			/* and then move the requested URL (minus the query string) into a temporary buffer */
			result = strtok(uri, "?");
			/* hash the stripped URI to get a number between 0 and HASHTABLESIZE */
			hash=DJBHash(result, (unsigned int)strlen(result));
			hash=hash % (HASHTABLESIZE -1);
		}
		else {
			/* if there is no query term, hash the full URI to get a number between 0 and HASHTABLESIZE */
			hash=DJBHash(uri, (unsigned int)strlen(uri));
			hash= hash % (HASHTABLESIZE -1);
		}

	}
	/* if the strchr function returns NULL it is because the request is not a valid http request, use LC method */
	else {
		set_lc_server();
		return 0;
	}

	/* HASH HIT: if this hash has a server associated with it, set member */
	if (balancer->hash_table[hash] != HASH_VAL_FREE) {
		if (balancer->debug_level > 1) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: algorithm HASH: hash hit for uri %s", uri);
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
		}
		candidate=&(balancer->members[balancer->hash_table[hash]]);
		/* check that the server we will use is alive and not overloaded */
		status=verify_server(candidate);
		/* if we've got a valid member or we're being forced to use a standby then continue normally */
		if((status == 0) || ((status == 1) && (using_member_standby == 1)) ) {
			next_member=candidate->id;
			if(use_clone==1) {
				next_clone=available_clones[hash % available_clones_count];
			}
			return 0;
		}

		/* server is not available or the current hash refers to a standby but we have valid members available. we will permanently put that hash onto another server */
		else {
			/* then choose a new next_member */
			set_lc_server();
			/* for accounting, decrement the dead server's amount of hashes */
			candidate->hash_table_usage --;
			/* for accounting, the new server gets assigned another hash */
			balancer->members[next_member].hash_table_usage ++;
			/* finally, update the actual hash table */
			balancer->hash_table[hash]=next_member;

		}
		return 0;
	}
	/* HASH MISS: we haven't seen this hash val before, assign the least connected server */
	else {
		if(balancer->debug_level > 1) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: algorithm HASH: hash miss for uri %s", uri);
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
		}
		/* when we have to choose a server, use LC as the selection algorithm. */
		set_lc_server();
		/*for accounting, the new server gets assigned another hash */
		balancer->members[next_member].hash_table_usage ++;
		/*finally, update the actual hash table */
		balancer->hash_table[hash]=next_member;
	}
	return 0;
}

int set_ll_server() {
	unsigned short int i;
	SERVER *candidate;
	for (i=0; i < available_members_count; i++) {
		candidate=&(balancer->members[available_members[i]]);
		/*if the candidate has less load than the current next_member then it becomes the new next_member */
		/*e_load is not updated regularly enough to be useful here */
		if((candidate->load + (candidate->c * balancer->session_weight)) < (balancer->members[next_member].load + balancer->members[next_member].c * balancer->session_weight)) {
			next_member=candidate->id;
		}
	}
	if(use_clone==1) {
		for (i=0; i < available_clones_count; i++) {
			candidate=&(balancer->clones[available_clones[i]]);
			/*if the candidate has less load than the current next_clone then it becomes the new next_clone */
			/*e_load is not updated regularly enough to be useful here */
			if((candidate->load + (candidate->c * balancer->session_weight)) < (balancer->clones[next_clone].load + balancer->clones[next_clone].c * balancer->session_weight)) {
				next_clone=candidate->id;
			}
		}
	}
	return 0;
}

/* sets the next_member and next_clone variables for the least connection algorithm */
/* the member and clone with the lowest number of active connections is the next_member and next_ghost respectively */
int set_lc_server() {
	unsigned short int i;
	/* use roundrobin to set the next_member to the next server in list, this is useful when sessions are at zero or are even. It spreads out
	 * the requests which looks nicer in the interface to prevent scary looking skewing and also provides some better server load balancing
	 * even when SNMP monitoring/balancing isn't being used.
	 */
	set_rr_server();
	SERVER *candidate;
	for (i=0; i < available_members_count; i++) {
		candidate=&(balancer->members[available_members[i]]);
		/*if the candidate has less active connections than the current next_member and it is alive, then it becomes the new next_member */
		if(candidate->c < balancer->members[next_member].c) {
			next_member=candidate->id;
		}
	}
	if(use_clone==1) {
		for (i=0; i < available_clones_count; i++) {
			candidate=&(balancer->clones[available_clones[i]]);
			/*if the candidate has less active connections than the current next_clone and it is alive, then it becomes the new next_clone */
			if(candidate->c < balancer->clones[next_clone].c) {
				next_clone=candidate->id;
			}
		}
	}
	return 0;
}

/* sets the next_member and next_clone variables for the round robin algorithm */
/* returns 0 if both member and clone are valid */
/* returns 1 if the member is valid but the clone is not */
/* returns -1 if member is invalid */
int set_rr_server() {
	int i;
	for (i=0; i < available_members_count; i++) {
		if (available_members[i]==next_member) {
			next_member=balancer->members[(available_members[((i+1) % available_members_count)])].id;
			break;
		}
	}
	if(use_clone==1) {
		for (i=0; i < available_clones_count; i++) {
			if (available_clones[i]==next_clone) {
				next_clone=balancer->clones[(available_clones[((i+1) % available_clones_count)])].id;
				break;
			}
		}

	}
	return 0;
}
