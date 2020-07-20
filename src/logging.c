/*
 * Octopus Load Balancer - Logging functions.
 *
 * Copyright 2008-2011 Alistair Reay <alreay1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

/* initialization and debugging info gets written from here */
int write_log(int options, char *description, int log_suppress) {
	errno =0;
	int status;
	time_t now;
	char *datebuf;
	now = time(NULL);
	datebuf = ctime(&now);
	datebuf[strlen(datebuf)-1]= '\0';

	/* we suppress some logging messages that could easily spam the logfile due to the likelihood of it being repeated at a high frequency */
	if((log_suppress == SUPPRESS_CONN_REJECT) && (balancer->connection_rejected_log_suppress == SUPPRESS_CONN_REJECT)) {
		return 0;
	}
	/* otherwise we set the flag so the next time something similar is logged it won't actually get logged... */
	if(log_suppress == SUPPRESS_CONN_REJECT) {
		balancer->connection_rejected_log_suppress = SUPPRESS_CONN_REJECT;
	}
	/*if the error is dire then after logging the msg send termination signals to monitor and master */
	if(options & OCTOPUS_LOG_EXIT) {
		if(balancer != NULL) {
			if((balancer->foreground == FOREGROUND_INIT) || (balancer->foreground == FOREGROUND_ON)) {
				fprintf(stderr, "%s\n", description);
			}
			/*write to syslog */
			if(balancer->use_syslog == 1) {
				openlog("octopus", LOG_PID, LOG_DAEMON);
				syslog(LOG_ALERT, "%s",description);
				closelog();
			}
			/*write to the logfile if it has been initialized */
			if( balancer->log_file != NULL) {
				fprintf(balancer->log_file, "%s - %s\n", datebuf, description);
				fflush(balancer->log_file);
			}
			if(balancer->monitor_pid > 0) {
				kill(balancer->monitor_pid, SIGTERM);
			}
			waitpid(balancer->monitor_pid, &status, 0);
			if(balancer->master_pid > 0) {
				kill(balancer->master_pid, SIGTERM);
			}
			waitpid(balancer->master_pid, &status, 0);
			exit(-1);
		}
		else {
			fprintf(stderr, "%s\n", description);
			exit(-1);
		}
	}

	/*write to normal log here */
	if(balancer != NULL) {
		/*write to syslog */
		if((options & OCTOPUS_LOG_SYSLOG) && (balancer->use_syslog == 1)) {
			openlog("octopus", LOG_PID, LOG_DAEMON);
			syslog(LOG_ALERT, "%s",description);
			closelog();
		}
		if( balancer->log_file == 0) {
			fprintf(stderr, "%s\n", description);
		}
		else {
			if (balancer->foreground == FOREGROUND_ON) {
				fprintf(stderr, "%s\n", description);
			}
			else if(balancer->log_file != NULL) {
				fprintf(balancer->log_file, "%s - %s\n", datebuf, description);
				fflush(balancer->log_file);
			}
		}
	}
	else {
		fprintf(stderr, "%s\n", description);
	}
	return 0;

}
