/*
 * Octopus Load Balancer - Server binary.
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
#include "config.c"
#include "init.c"
#include "algorithms.c"
#include "monitor.c"
#include "signals.c"
#include "logging.c"
#include "connect.c"

/* acceptable command line parameters */
int usage(char *prog_name) {
	fprintf(stderr, "Octopus Load Balancer Server (version " OCTOPUS_VERSION " built " OCTOPUS_DATE")\n");
	fprintf(stderr, "Copyright 2011 Alistair Reay <alreay1@gmail.com>\n\n");
	fprintf(stderr, "Usage : %s [-hf] [-c config-file]\n", prog_name);
	fprintf(stderr, "	-h 		Print help message\n");
	fprintf(stderr, "	-f		Run server in foreground\n");
	fprintf(stderr, "	-c file		Use given config-file\n");
	fprintf(stderr, "	-d level	Set debugging level\n");
	fprintf(stderr, "\n");
	exit(0);
}

int main(int argc, char *argv[]) {
	int listenerfd = 0;
	int i = 0;
	struct sockaddr_in clientaddr;
	socklen_t size = sizeof(clientaddr);
	int incomingfd = 0;
	int nfds = 0;
	int status = 0;
	signal(SIGCHLD,signal_handler);
	signal(SIGUSR1,signal_handler);
	signal(SIGALRM,signal_handler);
	signal(SIGTERM,signal_handler);
	signal(SIGINT,signal_handler);
	signal(SIGSEGV,signal_handler);

	int param_foreground=FOREGROUND_OFF;
	char *param_conf_file = NULL;
	if(argc ==1) {
		usage(argv[0]);
		exit(1);
	}

	/* parse commandline arguments */
	while((i = getopt(argc, argv,"d:hc:f")) != -1) {
		switch (i) {
			/* help */
			case 'h':
				usage(argv[0]);
				break;
			/* configuration parameter */
			case 'c':
				param_conf_file= optarg;
				break;
			/* foreground */
			case 'f':
				param_foreground=FOREGROUND_ON;
				break;
			/* debug level */
			case 'd':
				temporary_debug_level= atoi(optarg);
				debug_level_override=1;
				break;
			/* everything else is undefined */
			case '?':
				if(isprint(optopt)) {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: main: Unknown option `-%c'.",optopt);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				else {
					snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: main: Unknown option character `\\x%x'.",optopt);
					write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
				}
				return 1;
			default:
				abort();
		}
	}
	/* catch any other cruft that the user may have input */
	for(i = optind; i < argc; i++) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: main: Non-option argument %s", argv[i]);
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
		exit(1);
	}

	/* allocate memory for the balancer and sets defaults settings */
	initialize_balancer();
	/*if we are running the server as a daemon then we don't want to suppress stderr error messages until after the server has started successfully */
	if(param_foreground == FOREGROUND_OFF) {
		balancer->foreground=FOREGROUND_INIT;
	}
	else {
		balancer->foreground=param_foreground;
	}

	/* Parse the configuration file looking for the logfile and balancer->debug_level directive */
	initialize_logging(param_conf_file);
	/* Parse the configuration file for all other settings */
	parse_config_file(param_conf_file);
	/* Create shared memory segment and copy BALANCER structure into segment */
	initialize_shm();
	/* initialization for the monitor process. It is forked from the main octopus-server binary */
	initialize_monitor(argc, argv);
	/* try to set file descriptor limits and then initialize FD structure array */
	initialize_fds();
	/* statically allocate all the memory required for handling client, member and ghost send/recv buffers */
	initialize_sessions();
	/* runs the server process which may involve running in the background (daemon mode) */
	initialize_process();
	/* standard listening socket creation for the load balancer */
	listenerfd = create_serversocket();

	/* create the epoll instance */
	epfd = epoll_create(balancer->fd_limit);
	/* configure our epoll event groups with the types of notifications we're interested in */
	null_ev.events = EPOLLERR | EPOLLHUP ;
	ro_ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
	wr_ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
	rw_ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP;
	/* at startup the only FD we care about is the listening socket's fd */
	ro_ev.data.fd = listenerfd;
	/* add the listening fd to the epoll instance */
  	if(epoll_ctl(epfd, EPOLL_CTL_ADD, listenerfd, &ro_ev) < 0) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: main: error adding listener fd to epoll: %s", strerror(errno));
		write_log(OCTOPUS_LOG_EXIT, log_string, SUPPRESS_OFF);
	}
	write_log(OCTOPUS_LOG_STD | OCTOPUS_LOG_SYSLOG, "STARTUP: main: octopus startup completed", SUPPRESS_OFF);
	/* startup has completed */

	/* this is the main loop */
	while(1) {
		/* most of the time the balancer will just be blocking here */
		nfds= epoll_wait(epfd, events, (balancer->session_limit * 3), -1);
		if(nfds <= 0) {
			if(errno==EINTR) {
				continue;
			}
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: epoll_wait returned error: %s", strerror(errno));
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
			continue;
		}
		/* if we're at a high-level of debug then we write out every epoll event */
		if(balancer->debug_level>5) {
			write_log(OCTOPUS_LOG_STD, "DEBUG: epoll_wait returned.", SUPPRESS_OFF);
			for(i=nfds; i>0; i--) {
				snprintf(log_string, OCTOPUS_LOG_LEN, " - event %d @ fd %i",events[i-1].events,events[i-1].data.u32);
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
			}
		}
		/* now we iterate through all of the epoll events */
		for(i=0; i <nfds; i++) {
			/* if we had activity on the server socket then we've probably got a new connection request */
			if ((events[i].data.fd == listenerfd)) {
				if((events[i].events & EPOLLIN) ) {
					/* try and accept the connection request */
					incomingfd = accept(listenerfd, (struct sockaddr *)&clientaddr, &size);
					/* handle accept errors */
					if (incomingfd < 0) {
						if(errno == EMFILE) {
							snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: cannot accept new connection, file descriptor limit reached!");
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
							continue;
						}
						else {
							snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: error accepting new connection: %s",strerror(errno));
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
							continue;
						}
					}
					/* set the accepted FD to nonblocking */
					if (fcntl(incomingfd, F_SETFL, O_NONBLOCK) < 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: error accepting new connection! cannot set NONBLOCK: %s", strerror(errno));
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
						shutdown(incomingfd, SHUT_RDWR);
						close(incomingfd);
						continue;
					}
					/* set no delay */
					if (setsockopt(incomingfd, IPPROTO_TCP, TCP_NODELAY, (char *) &yes, sizeof(yes)) < 0) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: error accepting new connection! cannot set TCP_NODELAY: %s", strerror(errno));
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
						shutdown(incomingfd, SHUT_RDWR);
						close(incomingfd);
						continue;
					}
					/* assign a new session to the accepted FD */
					fds[incomingfd].session= get_new_session();
					/* if we fail to allocate a session then we disconnect the punter */
					if(fds[incomingfd].session == NULL) {
						/* closedown the client */
						shutdown(incomingfd, SHUT_RDWR);
						close(incomingfd);
						snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: disconnected new request from host %s, port %d, fd %d as free session was not available", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), incomingfd);
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
						continue;
					}
					/* set all the session defaults */
					else {
						/* session state variables */
						fds[incomingfd].session->state |= STATE_CLI_CONNECTED;
						fds[incomingfd].session->state |= STATE_CLI_READ_READY;
						fds[incomingfd].session->clientfd=incomingfd;
						/* in debug mode we write a 'connect accepted' message */
						if(balancer->debug_level > 1) {
							snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: connect from host %s, port %d, fd %d", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), incomingfd);
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
						}
						/* add the accepted FD to a epoll group (read-only) at the moment */
						ro_ev.data.fd = incomingfd;
					    if(epoll_ctl(epfd, EPOLL_CTL_ADD, incomingfd, &ro_ev) < 0) {
				    		snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: error adding incomingfd to epoll set: %s", strerror(errno));
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
							delete_session(fds[incomingfd].session);
							continue;
					    }
					    /* we will connect the client to a server UNLESS we are using HTTP URI Hashing or Static (because we need to see client's requested URI before we can choose a server) */
					    else {
					    	if((balancer->algorithm != ALGORITHM_HASH) && (balancer->algorithm != ALGORITHM_STATIC)) {
					   			status = choose_server(fds[incomingfd].session);
					   			if(status == -1) {
				    				snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: rejecting connection attempt due to server selection not returning any servers!");
				    				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
					   				delete_session(fds[incomingfd].session);
					   				continue;
					   			}
					   			fds[incomingfd].session->state &= ~STATE_FRESH;
					   		}
					   	}
					}
				}
				/* we're should only see input events on the listener, everything else is an error */
				else  {
					write_log(OCTOPUS_LOG_STD, "WARNING: epoll returned unhandled event for listener fd", SUPPRESS_OFF);
				}
			}
			/* From here on in we're dealing with a FD that's already part of an established session.
			 * What happens now is that all the FDs are iterated and we add their respective sessions
			 * to a stack for later maintenance. Then we will copy data depending on what type of
			 * attention the socket is demanding.
			 */
			else {
				incomingfd = events[i].data.fd;
				/* A client FD has changed state */
				if (incomingfd == fds[incomingfd].session->clientfd) {
					/* check for error */
					if((events[i].events & EPOLLERR) ) {
						if(balancer->debug_level > 0) {
							snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: clientfd %d returned EPOLLERR", incomingfd);
							write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
						}
						/* mark session as having its' client disconnected */
						delete_session(fds[incomingfd].session);
						continue;
					}
					/* check for hang-up */
					if((events[i].events & EPOLLHUP) ) {
						if(balancer->debug_level > 0) {
							snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: clientfd %d returned EPOLLHUP", incomingfd);
							write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
						}
						/* mark session as having its' client disconnected */
						delete_session(fds[incomingfd].session);
						continue;
					}
					/* client wants to send us something */
					if((events[i].events & EPOLLIN) ) {
						client_read(incomingfd);
					}
					/* client is available for write */
					if(events[i].events & EPOLLOUT) {
						client_write(incomingfd);
					}
				}
				/* A member FD has changed state */
				else if (incomingfd == fds[incomingfd].session->memberfd) {
					/* check for error */
					if((events[i].events & EPOLLERR) ) {
						if(balancer->debug_level > 0) {
							snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: memberfd %d returned EPOLLERR", incomingfd);
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
						}
						/* member has disconnected */
						disconnect_member(fds[incomingfd].session);
						continue;
					}
					/* check for hang-up */
					if((events[i].events & EPOLLHUP) ) {
						if(balancer->debug_level>0) {
							snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: memberfd %d returned EPOLLHUP", incomingfd);
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
						}
						/* member has disconnected */
						disconnect_member(fds[incomingfd].session);
						continue;
					}
					/* member available for read */
					if((events[i].events & EPOLLIN) ) {
						member_read(incomingfd);
					}
					/* member available for write */
					if((events[i].events & EPOLLOUT) ) {
						member_write(incomingfd);
					}
				}
				/* A clone FD has changed state */
				else if (incomingfd == fds[incomingfd].session->clonefd)  {
					/* check for error */
					if((events[i].events & EPOLLERR) ) {
						if(balancer->debug_level>0) {
							snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: clonefd %d returned EPOLLERR", incomingfd);
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
						}
						/* mark the session as having its' clone disconnected */
						disconnect_clone(fds[incomingfd].session);
						continue;
					}
					/* check for hang-up */
					if((events[i].events & EPOLLHUP) ) {
						if(balancer->debug_level>0) {
							snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: clonefd %d returned EPOLLHUP", incomingfd);
							write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
						}
						/* mark the session as having its' clone disconnected */
						disconnect_clone(fds[incomingfd].session);
						continue;
					}
					/* clone is available for read */
					if((events[i].events & EPOLLIN) ) {
						clone_read(incomingfd);
					}
					/* clone is available for write */
					if((events[i].events & EPOLLOUT) ) {
						clone_write(incomingfd);
					}
				}
				/* If we get to here then we've been notified about an FD that's not part of any session.
				 * This can occur if during the iteration of updated FDs we have two FDs that have changed for a session
				 * (say clientfd closed and memberfd closed) and the clientfd is processed first then we'll kill the
				 * entire session (disassociating the fd from a session) so that when we get around to processing the
				 * memberfd we can't associate it with anything.
				 * This doesn't cause any problems but it should be captured here.   */
				else {
					if(balancer->debug_level > 2) {
						snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: Unassociated fd %d",incomingfd);
						write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
					}
				}
			}
		}
	}
	return 0;
}

/* this function handles reading data from the client */
int client_read(int fd) {
	int status;
	/* read the maximum amount of data possible into the client buffer appending to any data that hasn't already been passed to a member */
	nbytes= read(fd, (fds[fd].session->client_read_buffer + fds[fd].session->client_used_buffer), (MESSAGE_SIZE_LIMIT - fds[fd].session->client_used_buffer));
	/* if read returned an error */
	if (nbytes <= 0) {
		/* EOF? */
		if (nbytes == 0) {
			if(balancer->debug_level > 3) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: client_read: read() returned EOF from client @ fd %d",fd);
				write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
			}
		}
		/* otherwise socket error! */
		else {
			if(balancer->debug_level > 3) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: client_read: read() returned an error from client @ fd %d: %s",fd,strerror(errno));
				write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
			}
		}
		/* in any case, the client has disconnected */
		delete_session(fds[fd].session);
	}
	else {
		if(balancer->debug_level > 2) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: client_read: read %d bytes from client @ fd %d",(int)nbytes,fd);
			write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
		}
		/* increase the usage information of our read buffer */
		fds[fd].session->client_used_buffer += (int)nbytes;
		/* this check checks if the session has established a connection to a server and if not, connects it */
		if (fds[fd].session->state & STATE_FRESH) {
			status = choose_server(fds[fd].session);
			/* if we couldn't choose a server then we've got to disconnect the client */
			if(status == -1) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: rejecting connection attempt due to server selection not returning any servers!");
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_CONN_REJECT);
				delete_session(fds[fd].session);
				return -1;
			}
			fds[fd].session->state &= ~STATE_FRESH;
		}
		/* We maintain an extra buffer for passing client messages to clones. */
		if((fds[fd].session->state & STATE_CLO_CONNECTED) >0) {
			/* in the situation where the clone has not been able to be written to enough, we have to cut it loose to avoid any ugliness */
			if(nbytes > (MESSAGE_SIZE_LIMIT - fds[fd].session->clone_used_buffer)) {
				disconnect_clone(fds[fd].session);
			}
			/* append the data just read from the client into the buffer for the clone */
			else {
				strncpy(fds[fd].session->clone_write_buffer + fds[fd].session->clone_used_buffer, (fds[fd].session->client_read_buffer + fds[fd].session->client_used_buffer - nbytes), nbytes);
				fds[fd].session->clone_used_buffer += nbytes;
			}
		}
		/* update session to indicate we'd like to write to servers */
		fds[fd].session->state |= STATE_MEM_WRITE_READY;
		fds[fd].session->state |= STATE_CLO_WRITE_READY;
		/* client input buffer is full? */
		if(fds[fd].session->client_used_buffer == MESSAGE_SIZE_LIMIT) {
			fds[fd].session->state |= STATE_CLI_BUFF_FULL;
			fds[fd].session->state &= ~STATE_CLI_READ_READY;
		}

		/* SESSION SOCKET MAINTAIN */
		/* CLIENT */
		/*  read/write is possible */
		if( (fds[fd].session->state & STATE_CLI_READ_READY) && (fds[fd].session->state & STATE_CLI_WRITE_READY)) {
			rw_ev.data.fd=fds[fd].session->clientfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clientfd, &rw_ev);
		}
		/* only write is possible */
		else if (fds[fd].session->state & STATE_CLI_WRITE_READY) {
			wr_ev.data.fd=fds[fd].session->clientfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clientfd, &wr_ev);
		}
		/* only read is possible */
		else if (fds[fd].session->state & STATE_CLI_READ_READY) {
			ro_ev.data.fd=fds[fd].session->clientfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clientfd, &ro_ev);
		}
		/* otherwise we're not interested */
		else {
			null_ev.data.fd=fds[fd].session->clientfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clientfd, &null_ev);
		}
		/* MEMBER */
		if(fds[fd].session->state & STATE_MEM_READ_READY) {
			rw_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &rw_ev);
		}
		/* only write is possible */
		else {
			wr_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &wr_ev);
		}
		/* CLONE */
		if (fds[fd].session->state & STATE_CLO_CONNECTED) {
			rw_ev.data.fd=fds[fd].session->clonefd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clonefd, &rw_ev);
		}
	}
	return 0;
}

/* this function handles writing data to the client */
int client_write(int fd) {
	/* attempt to send everything we have to the client */
	nbytes = write(fd, fds[fd].session->member_read_buffer, fds[fd].session->member_used_buffer);
	if(balancer->debug_level > 2) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: client_write: wrote %d bytes to client @ fd %d",(int)nbytes,fd);
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}
	/* if write returned an error */
	if (nbytes < 0) {
		if(balancer->debug_level > 1) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: client_write: write() failed: %s",strerror(errno));
			write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
		}
		/* client has disconnected */
		delete_session(fds[fd].session);
	}
	else {
		/* update buffer usage */
		fds[fd].session->member_used_buffer -= (int)nbytes;
		/*no more data for client */
		if(fds[fd].session->member_used_buffer == 0) {
			fds[fd].session->state &= ~STATE_CLI_WRITE_READY;
			/* if the member has been disconnected, and there's no more data for the client, then we're done */
			if (!(fds[fd].session->state & STATE_MEM_CONNECTED)) {
				delete_session(fds[fd].session);
				return 0;
			}
		}
		/*there is still more to be written to the client! move any unwritten data to the front of the buffer */
		else {
			memcpy(fds[fd].session->member_read_buffer, (fds[fd].session->member_read_buffer + nbytes), fds[fd].session->member_used_buffer);
		}
		/* we just wrote some stuff to the client, the member buffer will have free space */
		if (fds[fd].session->state & STATE_MEM_BUFF_FULL) {
			fds[fd].session->state &= ~STATE_MEM_BUFF_FULL;
			fds[fd].session->state |= STATE_MEM_READ_READY;
		}

		/* SESSION SOCKET MAINTAIN */
		/* CLIENT */
		/*  read/write is possible */
		if( (fds[fd].session->state & STATE_CLI_READ_READY) && (fds[fd].session->state & STATE_CLI_WRITE_READY)) {
			rw_ev.data.fd=fds[fd].session->clientfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clientfd, &rw_ev);
		}
		/* only write is possible */
		else if (fds[fd].session->state & STATE_CLI_WRITE_READY) {
			wr_ev.data.fd=fds[fd].session->clientfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clientfd, &wr_ev);
		}
		/* only read is possible */
		else if (fds[fd].session->state & STATE_CLI_READ_READY) {
			ro_ev.data.fd=fds[fd].session->clientfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clientfd, &ro_ev);
		}
		/* otherwise we're not interested */
		else {
			null_ev.data.fd=fds[fd].session->clientfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clientfd, &null_ev);
		}
		/* MEMBER */
		if((fds[fd].session->state & STATE_MEM_WRITE_READY) && (fds[fd].session->state & STATE_MEM_READ_READY)) {
			rw_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &rw_ev);
		}
		/* only write is possible */
		else if(fds[fd].session->state & STATE_MEM_WRITE_READY) {
			wr_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &wr_ev);
		}
		/* only read is possible */
		else if(fds[fd].session->state & STATE_MEM_READ_READY) {
			ro_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &ro_ev);
		}
		/* otherwise we're not interested */
		else {
			null_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &null_ev);
		}

	}
	return 0;
}

/* this function handles reading data from the member */
int member_read(int fd) {
	/* read the maximum amount of data possible into the member buffer appending to any data that hasn't already been passed to the client */
	nbytes= read(fd, ((fds[fd].session->member_read_buffer) + fds[fd].session->member_used_buffer), (MESSAGE_SIZE_LIMIT - fds[fd].session->member_used_buffer));
	if(balancer->debug_level > 2) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: member_read: read %d bytes from server @ fd %d",(int)nbytes,fd);
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}
	/* if read returned an error */
	if (nbytes <= 0) {
		/* socket error! */
		if(nbytes < 0) {
			if(balancer->debug_level > 3) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: member_read: ERROR for fd @ %d",fd);
				write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
			}
			if (!(fds[fd].session->state & STATE_CLI_WRITE_READY)) {
				delete_session(fds[fd].session);
			}
			else {
				disconnect_member(fds[fd].session);
			}
		}
		/* EOF - this is normal */
		else {
			if(balancer->debug_level > 3) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: member_read: EOF for fd @ %d",fd);
				write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
			}
			if (!(fds[fd].session->state & STATE_CLI_WRITE_READY)) {
				delete_session(fds[fd].session);
			}
			else {
				disconnect_member(fds[fd].session);
			}
		}
	}
	else {
		/* update buffer and bytes accounting */
		fds[fd].session->member_used_buffer += (int)nbytes;
		fds[fd].session->member->brecv +=nbytes;
		/* reading data from a member will always mean we have to write to the client */
		fds[fd].session->state |= STATE_CLI_WRITE_READY;
		/* if out member buffer is full then the won't poll the server for updates until it's got some room */
		if(fds[fd].session->member_used_buffer == MESSAGE_SIZE_LIMIT) {
			if(balancer->debug_level > 3) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: member_read: server buffer full. Unsetting SRV_READ_READY for fd @ %d",fd);
				write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
			}
			fds[fd].session->state |= STATE_MEM_BUFF_FULL;
			fds[fd].session->state &= ~STATE_MEM_READ_READY;
		}
		/* SESSION SOCKET MAINTAIN */
		/* CLIENT */
		/*  read is possible */
		if(fds[fd].session->state & STATE_CLI_READ_READY) {
			rw_ev.data.fd=fds[fd].session->clientfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clientfd, &rw_ev);
		}
		/* only write is possible */
		else {
			wr_ev.data.fd=fds[fd].session->clientfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clientfd, &wr_ev);
		}

		/* MEMBER */
		if((fds[fd].session->state & STATE_MEM_WRITE_READY) && (fds[fd].session->state & STATE_MEM_READ_READY)) {
			rw_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &rw_ev);
		}
		/* only write is possible */
		else if(fds[fd].session->state & STATE_MEM_WRITE_READY) {
			wr_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &wr_ev);
		}
		/* only read is possible */
		else if(fds[fd].session->state & STATE_MEM_READ_READY) {
			ro_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &ro_ev);
		}
		/* otherwise we're not interested */
		else {
			null_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &null_ev);
		}
	}
	return 0;
}

/* this function handles writing data to the member */
int member_write(int fd) {
	/* attempt to send everything we have to the member */
	nbytes = write(fd, fds[fd].session->client_read_buffer, fds[fd].session->client_used_buffer);
	if(balancer->debug_level > 2) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: member_write: wrote %d bytes to server @ fd %d",(int)nbytes,fd);
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}
	/* if write returned an error */
	if (nbytes < 0) {
		if(balancer->debug_level > 3) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: member_write: ERROR, setting STATE_MEM_FAILED for fd @ %d",fd);
			write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
		}
		/* member has disconnected */
		disconnect_member(fds[fd].session);
	}
	else {
		/* update buffer usage and traffic accounting values */
		fds[fd].session->client_used_buffer -= (int)nbytes;
		fds[fd].session->member->bsent += (int)nbytes;
		/* after writing to a server we expect some sort of response */
		fds[fd].session->state |= STATE_MEM_READ_READY;
		/*if the buffer is now empty, then we don't need to monitor the server for write availability */
		if(fds[fd].session->client_used_buffer == 0) {
			if(balancer->debug_level > 3) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: member_write: server buffer empty. Unsetting STATE_MEM_WRITE_READY for fd @ %d",fd);
				write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
			}
			fds[fd].session->state &= ~STATE_MEM_WRITE_READY;
		}
		/*if the buffer was not fully emptied, move everything that wasn't written to the front of the buffer */
		else {
			memcpy(fds[fd].session->client_read_buffer, (fds[fd].session->client_read_buffer + nbytes), fds[fd].session->client_used_buffer);
		}
		/* we just wrote some stuff to the member, the client buffer will have free space */
		if(fds[fd].session->state & STATE_CLI_BUFF_FULL) {
			fds[fd].session->state &= ~STATE_CLI_BUFF_FULL;
			fds[fd].session->state |= STATE_CLI_READ_READY;
		}

		/* SESSION SOCKET MAINTAIN */
		/* CLIENT */
		/*  write is possible */
		if(fds[fd].session->state & STATE_CLI_WRITE_READY) {
			rw_ev.data.fd=fds[fd].session->clientfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clientfd, &rw_ev);
		}
		/* only read is possible */
		else {
			ro_ev.data.fd=fds[fd].session->clientfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clientfd, &ro_ev);
		}

		/* MEMBER */
		if((fds[fd].session->state & STATE_MEM_WRITE_READY) && (fds[fd].session->state & STATE_MEM_READ_READY)) {
			rw_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &rw_ev);
		}
		/* only write is possible */
		else if(fds[fd].session->state & STATE_MEM_WRITE_READY) {
			wr_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &wr_ev);
		}
		/* only read is possible */
		else if(fds[fd].session->state & STATE_MEM_READ_READY) {
			ro_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &ro_ev);
		}
		/* otherwise we're not interested */
		else {
			null_ev.data.fd=fds[fd].session->memberfd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->memberfd, &null_ev);
		}
	}
	return 0;
}

/* this function handles reading data from the clone */
int clone_read(int fd) {
	/* read the maximum amount of data possible into the waste buffer */
	nbytes= read(fd, waste_buffer, MESSAGE_SIZE_LIMIT);
	if(balancer->debug_level > 2) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: clone_read: read %d bytes from clone @ fd %d",(int)nbytes,fd);
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}
	/* if read returned an error */
	if (nbytes <= 0) {
		/* socket error !*/
		if(nbytes < 0) {
			if(balancer->debug_level > 3) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: clone_read: ERROR, setting STATE_CLO_FAILED for fd @ %d",fd);
				write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
			}
			disconnect_clone(fds[fd].session);
			return -1;
		}
		/* EOF - this is normal */
		if(balancer->debug_level > 3) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: clone_read: EOF, setting STATE_CLO_FAILED for fd @ %d",fd);
			write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
		}
		disconnect_clone(fds[fd].session);
	}
	else {
		/* bytes accounting */
		fds[fd].session->clone->brecv +=nbytes;

		/* CLONE SESSION SOCKET MAINTAIN */
		/* NONE NEEDED!
		 * We are always free to read more data from a clone
		 */
	}
	return 0;
}

/* this function handles writing data to the clone */
int clone_write(int fd) {
	/* attempt to send everything we have to the clone */
	nbytes =write(fd,fds[fd].session->clone_write_buffer, fds[fd].session->clone_used_buffer);
	if(balancer->debug_level > 2) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: clone_write: wrote %d bytes to clone @ fd %d",(int)nbytes,fd);
		write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
	}
	/* if write returned an error */
	if (nbytes <0) {
		if(balancer->debug_level > 3) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: clone_write: ERROR, setting STATE_CLO_FAILED for fd @ %d",fd);
			write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
		}
		/* clone has disconnected */
		disconnect_clone(fds[fd].session);
	}
	/* on a successful write */
	else {
		/* update buffer and bytes accounting */
		fds[fd].session->clone_used_buffer -= (int)nbytes;
		fds[fd].session->clone->bsent +=nbytes;
		/* after a write, we expect a response */
		fds[fd].session->state |= STATE_CLO_READ_READY;
		/*if the buffer is now empty, then we don't need to monitor the server for write availability */
		if(fds[fd].session->clone_used_buffer==0) {
			if(balancer->debug_level > 3) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: clone_write: clone buffer empty. Unsetting STATE_CLO_WRITE_READY for fd @ %d",fd);
				write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
			}
			fds[fd].session->state &= ~STATE_CLO_WRITE_READY;
		}
		/*if the buffer was not fully emptied, move everything that wasn't written to the front of the buffer */
		else {
			memcpy(fds[fd].session->clone_write_buffer, (fds[fd].session->clone_write_buffer + nbytes), fds[fd].session->clone_used_buffer);
		}

		/* CLONE SESSION SOCKET MAINTAIN */
		/* write is possible */
		if(fds[fd].session->state & STATE_CLO_WRITE_READY) {
			rw_ev.data.fd=fds[fd].session->clonefd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clonefd, &rw_ev);
		}
		/* otherwise just read */
		else {
			ro_ev.data.fd=fds[fd].session->clonefd;
			epoll_ctl(epfd, EPOLL_CTL_MOD, fds[fd].session->clonefd, &ro_ev);
		}
	}
	return 0;
}

/* returns a pointer to a unused session */
/* modified by Cheng Ren, 2012-9-30*/
SESSION* get_new_session() {

	if(unused_session_queue.head == unused_session_queue.rear)
	{
		write_log(OCTOPUS_LOG_STD, "WARNING: get_new_session: unable to supply new session!", SUPPRESS_OFF);
		return NULL;
	}
	// delete the session, and return it
	unused_session_queue.head = (unused_session_queue.head + 1) % unused_session_queue.head;
	return unused_session_queue.unused_sessions[unused_session_queue.head];
}

/*Added by Cheng Ren,2012-9-30*/
int add_unused_session(SESSION *session) {
	unused_session_queue.rear = (unused_session_queue.rear + 1) % balancer->session_limit;
	unused_session_queue.unused_sessions[unused_session_queue.rear] = session;
}

/*Added by Cheng Ren, 2012-9-30*/
int pop_unused_session() {
	if(unused_session_queue.head == unused_session_queue.rear)
	{
		write_log(OCTOPUS_LOG_STD, "WARNING: delete_unused_sessio: unable to delete session, no more free session available!", SUPPRESS_OFF);
	}
	
	if(balancer->debug_level > 3) {
		SESSION *session = unused_session_queue.unused_sessions[unused_session_queue.head];
		snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: deleting unused session with clientfd %d, memberfd %d, clonefd %d and state %d", session->clientfd, session->memberfd, session->clonefd, session->state);
		write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
	}

	unused_session_queue.head = (unused_session_queue.head + 1) % balancer->session_limit;
	return 0;
}

/* kills a session including shutting down sockets, closing FDs and resetting default session values */
int delete_session(SESSION *session) {
	if(balancer->debug_level > 3) {
		snprintf(log_string, OCTOPUS_LOG_LEN, "DEBUG: deleting session with clientfd %d, memberfd %d, clonefd %d and state %d", session->clientfd, session->memberfd, session->clonefd, session->state);
		write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
	}
	if (session->state & STATE_CLI_CONNECTED) {
		disconnect_client(session);
	}
	if (session->state & STATE_MEM_CONNECTED) {
		disconnect_member(session);
	}
	if (session->state & STATE_CLO_CONNECTED) {
		disconnect_clone(session);
	}
	/*session defaults */
	session->state=STATE_UNUSED;
	session->client_used_buffer=0;
	session->member_used_buffer=0;
	session->clone_used_buffer=0;
	add_unused_session(session); // added by Cheng Ren, 2012-10-15;
	return 0;
}


int disconnect_clone(SESSION *session) {
	int status=0;
	/* if the clone has a FD */
	if(session->clonefd >= 0) {
		session->state &= ~STATE_CLO_CONNECTED;
		session->clone->c -=1;
		session->clone->completed_c ++;
		shutdown(session->clonefd, SHUT_RDWR);
		status=close(session->clonefd);
		if (status!=0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: Failed to close clone fd: %d",session->clonefd);
			write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
		}
		session->clonefd=-1;
	}
	return 0;
}

int disconnect_member(SESSION *session) {
	int status=0;
	/* if the member has a FD */
	if(session->memberfd >= 0) {
		session->state &= ~STATE_MEM_CONNECTED;
		session->member->c -=1;
		session->member->completed_c ++;
		shutdown(session->memberfd, SHUT_RDWR);
		status=close(session->memberfd);
		if (status!=0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: Failed to close member fd %d",session->memberfd);
			write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
		}
		session->memberfd=-1;
	}
	return 0;
}

int disconnect_client(SESSION *session) {
	int status=0;
	/* if the client has a FD */
	if(session->clientfd >= 0) {
		session->state &= ~STATE_CLI_CONNECTED;
		shutdown(session->clientfd, SHUT_RDWR);
		status=close(session->clientfd);
		if (status!=0) {
			snprintf(log_string, OCTOPUS_LOG_LEN, "WARNING: Failed to close client fd %d",session->clientfd);
			write_log(OCTOPUS_LOG_STD,log_string, SUPPRESS_OFF);
		}
		session->clientfd=-1;
	}
	return 0;
}
