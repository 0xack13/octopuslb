/*
 * Octopus Load Balancer - Signal handler and shutdown function.
 *
 * Copyright 2008-2011 Alistair Reay <alreay1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

static void signal_handler(int signum) {
	switch(signum) {
		case SIGTERM:
			clean_exit();
		case SIGINT:
			clean_exit();
		case SIGSEGV:
			write_log(OCTOPUS_LOG_STD | OCTOPUS_LOG_SYSLOG, "ERROR! Received signal SIGSEGV!!", SUPPRESS_OFF);
			clean_exit();
	}
}

int clean_exit() {
	int status;
	pid_t p;

	/* the master process will wait for the monitor to quit */
	if(getpid() == balancer->master_pid) {
		write_log(OCTOPUS_LOG_STD | OCTOPUS_LOG_SYSLOG, "NOTICE: signal_handler: master: received signal SIGTERM. Shutting down...", SUPPRESS_OFF);
		balancer->alive=0;

		/* wait for monitoring process to quit first */
		p = waitpid(-1, &status, 0);
		if((p == -1) && (errno != ECHILD)) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: signal_handler: master: waitpid error: %s", strerror(errno));
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
		}
		write_log(OCTOPUS_LOG_STD, "NOTICE: signal_handler: master: killed monitoring process", SUPPRESS_OFF);



		/* remove the SHM file */
		if(balancer->shmid != -1) {
			status= unlink(balancer->shm_run_file);
			if(status != 0) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: signal_handler: master: Unable to remove SHM file!!: %s", strerror(errno));
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
			}
			if(balancer->debug_level > 0) {
				write_log(OCTOPUS_LOG_STD, "NOTICE: signal_handler: master: marking SHM segment for deletion", SUPPRESS_OFF);
			}
			struct shmid_ds *buf;
			buf = malloc(sizeof(struct shmid_ds));
			if(buf == NULL) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: signal_handler: master: malloc error: %s", strerror(errno));
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				exit(-1);
			}

			status=shmctl(balancer->shmid, IPC_RMID, buf);
			if(status !=0) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: signal_handler: master: SHM delete failed: %s", strerror(errno));
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
				exit(-1);
			}
			else {
				if(balancer->debug_level > 0) {
					write_log(OCTOPUS_LOG_STD, "NOTICE: signal_handler: master: marked SHM segment for deletion", SUPPRESS_OFF);
				}
			}
			fclose(balancer->log_file);
		}
		else {
				write_log(OCTOPUS_LOG_STD, "NOTICE: signal_handler: master: termination signal received, no SHM segment detected so not detaching", SUPPRESS_OFF);
		}
	}
	if(getpid() == balancer->monitor_pid) {
		if(balancer->shmid != -1) {
			if(balancer->debug_level > 0) {
				write_log(OCTOPUS_LOG_STD, "NOTICE: signal_handler: monitor: received signal SIGTERM. Shutting down...", SUPPRESS_OFF);
			}
			status=shmdt((const void *)balancer);
			if(status !=0) {
				snprintf(log_string, OCTOPUS_LOG_LEN, "ERROR: signal_handler: monitor: unable to detach SHM segment: %s", strerror(errno));
				write_log(OCTOPUS_LOG_STD, log_string, SUPPRESS_OFF);
			}
		}
		else {
			write_log(OCTOPUS_LOG_STD, "NOTICE: signal_handler: monitor: termination signal received, no SHM segment detected so not detaching", SUPPRESS_OFF);
		}
	}
	exit(0);
}

