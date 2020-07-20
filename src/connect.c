/*
 * Octopus Load Balancer - Server connection functions.
 *
 * Copyright 2008-2011 Alistair Reay <alreay1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */


/* connects a session to the server specified by next_member and next_clone variables */
/* returns 0 on success */
/* returns -1 if connection to member server failed */
/* returns 1 if clone connection could not be established but was requested */
int connect_server(SESSION *session) {
	struct sockaddr_in member_addr;
	struct sockaddr_in clone_addr;
	struct sockaddr_in member_outbound_addr;
	struct sockaddr_in clone_outbound_addr;
	int serverfd;
	int clonefd;
	int status;
	errno=0;

	/*check that the session does not already have a server fd. */
	/*connect may have been called on this session because the clone read/write failed and it needs to be reconnected */
	if (session->memberfd == -1) {
		if ((serverfd=socket(PF_INET, SOCK_STREAM, 0)) < 0) {
			if(errno == EMFILE) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: file descriptor limit reached!");
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			}
			else {
				snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot create new socket for member %s: %s", balancer->members[next_member].name, strerror(errno));
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			}
			return -1;

		}
		member_addr.sin_family = AF_INET;
		member_addr.sin_port = htons((uint16_t)(balancer->members[next_member].port));
		member_addr.sin_addr = balancer->members[next_member].myaddr.sin_addr;
		memset(member_addr.sin_zero, '\0', sizeof(member_addr.sin_zero));

		if (fcntl(serverfd, F_SETFL, O_NONBLOCK)==-1) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: connect_server: cannot set socket to NONBLOCK: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			return -1;
		}
		status = setsockopt(serverfd, IPPROTO_TCP, TCP_NODELAY,(char *) &yes, (socklen_t)sizeof(yes));
		if(status<0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot set socket to NODELAY: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
		}
		status = setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes));
		if(status<0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot set socket REUSEADDR: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
		}
		/* use specified outbound ip for member servers */
		bzero(&member_outbound_addr, sizeof(member_outbound_addr));
		member_outbound_addr.sin_family = AF_INET;
		memcpy(&member_outbound_addr.sin_addr, &balancer->member_outbound_ip, sizeof(struct in_addr));
		status= bind(serverfd, (struct sockaddr *)&member_outbound_addr, (socklen_t)sizeof(member_outbound_addr));
		if(status<0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: Unable to bind connection to requested member outbound IP: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			return -1;
		}
		/* connect to the appropriate member */
		errno=0;
		status= connect(serverfd, (struct sockaddr *)&member_addr, (socklen_t)sizeof(member_addr));
		if(status != 0) {
			if(errno != EINPROGRESS) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot connect to member %s: %s", balancer->members[next_member].name, strerror(errno));
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
				return -1;
			}
		}
		/* associate the session with the server, set session state and put it into a epoll balancer */
		session->member=&(balancer->members[next_member]);
		session->memberfd=serverfd;
		fds[serverfd].session=session;
		ro_ev.data.fd=serverfd;
		if(epoll_ctl(epfd, EPOLL_CTL_ADD, serverfd, &ro_ev) <0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot add server to epoll fd: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			return -1;
		}
		fds[serverfd].session->member->c +=1;
		fds[serverfd].session->state |= STATE_MEM_CONNECTED;
		fds[serverfd].session->state |= STATE_MEM_READ_READY;
		if(balancer->debug_level >1) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: connected to member server %s @ fd %d", balancer->members[next_member].name, serverfd);
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
		}
	}
	/* if we are cloning connections... */
	if((use_clone == 1) && (session->clonefd == -1)) {
		errno=0;
		if ((clonefd=socket(PF_INET, SOCK_STREAM, 0)) < 0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot create new socket for clone %s: %s", balancer->clones[next_clone].name, strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			return 1;
		}
		clone_addr.sin_family = AF_INET;
		clone_addr.sin_port = htons((uint16_t)(balancer->clones[next_clone].port));
		clone_addr.sin_addr = balancer->clones[next_clone].myaddr.sin_addr;
		memset(clone_addr.sin_zero, '\0', sizeof(clone_addr.sin_zero));
		status =fcntl(clonefd, F_SETFL, O_NONBLOCK);
		if(status<0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot set socket to NONBLOCK: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			return 1;
		}
		status = setsockopt(clonefd, IPPROTO_TCP, TCP_NODELAY,(char *) &yes, (socklen_t)sizeof(yes));
		if(status<0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot set socket to NODELAY: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
		}
		status = setsockopt(clonefd, SOL_SOCKET, SO_SNDBUF, &sockbufsize, (socklen_t)sizeof(sockbufsize));
		if(status<0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot set socket SNDBUF: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			return 1;
		}
		status = setsockopt(clonefd, SOL_SOCKET, SO_RCVBUF, &sockbufsize, (socklen_t)sizeof(sockbufsize));
		if(status<0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot set socket RCVBUF: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			return 1;
		}
		status = setsockopt(clonefd, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes));
		if(status<0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot set socket to REUSEADDR: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			return 1;
		}
		status= setsockopt(clonefd, SOL_SOCKET, SO_KEEPALIVE, (char *)&yes, (socklen_t)sizeof(yes));
		if(status<0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot set socket to KEEPALIVE: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			return 1;
		}

		/* use specified outbound ip for clone servers */
		bzero(&clone_outbound_addr, sizeof(clone_outbound_addr));
		clone_outbound_addr.sin_family = AF_INET;
		memcpy(&clone_outbound_addr.sin_addr, &balancer->clone_outbound_ip, sizeof(struct in_addr));
		status= bind(clonefd, (struct sockaddr *)&clone_outbound_addr, (socklen_t)sizeof(clone_outbound_addr));
		if(status<0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: Unable to bind connection to requested outbound clone IP: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			return -1;
		}


		/* connect to the appropriate clone */
		errno=0;
		status= (connect(clonefd, (struct sockaddr *)&clone_addr, (socklen_t)sizeof(clone_addr)) == -1);
		if(status != 0) {
			if(errno != EINPROGRESS) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot connect to clone %s: %s", balancer->clones[next_clone].name, strerror(errno));
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
				return 1;
			}
		}
		/* associate the session with the server, set session state and put it into a epoll balancer */
		session->clone=&(balancer->clones[next_clone]);
		session->clonefd=clonefd;
		fds[clonefd].session=session;
		ro_ev.data.fd=clonefd;
		if(epoll_ctl(epfd, EPOLL_CTL_ADD, clonefd, &ro_ev) <0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: connect_server: cannot add clone to epoll fd: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
			return 1;
		}
		fds[clonefd].session->clone->c +=1;
		fds[clonefd].session->state |= STATE_CLO_CONNECTED;
		fds[clonefd].session->state |= STATE_CLO_READ_READY;
		if(balancer->debug_level >1) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: Connected to clone server %s @ fd %d", balancer->clones[next_clone].name, clonefd);
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
		}
	}
	return 0;
}


