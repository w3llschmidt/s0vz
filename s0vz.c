/**************************************************************************

S0/Impulse to Volkszaehler 'RaspberryPI deamon'.

Version 0.2

sudo gcc -o /usr/sbin/s0vz /tmp/s0vz.c -lconfig -lcurl´

https://github.com/w3llschmidt/s0vz.git
https://github.com/volkszaehler/volkszaehler.org.git

Henrik Wellschmidt  <w3llschmidt@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

**************************************************************************/

#include <stdio.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <libconfig.h>
#include <stddef.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <curl/curl.h>

#define DAEMON_NAME "s0vz"
#define DAEMON_VERSION "0.2"

#define BUF_LEN 32

void daemonShutdown();
void signal_handler(int sig);
void daemonize(char *rundir, char *pidfile);

int pidFilehandle, vzport, i;

const char *vzserver, *vzpath;

const char *vzuuid[6];

// GPIOs festlegen und Anzahl berrechnen
char gpio_pin_id[] = { 17, 18, 21, 22, 23, 24 };
int inputs = sizeof(gpio_pin_id)/sizeof(gpio_pin_id[0]);

void signal_handler(int sig)
{
	switch(sig)
	{
		case SIGHUP:
		syslog(LOG_WARNING, "Received SIGHUP signal.");
		break;
		case SIGINT:
		case SIGTERM:
		syslog(LOG_INFO, "Daemon exiting");
		daemonShutdown();
		exit(EXIT_SUCCESS);
		break;
		default:
		syslog(LOG_WARNING, "Unhandled signal %s", strsignal(sig));
		break;
	}
}

void daemonShutdown()
{
		close(pidFilehandle);
		remove("/tmp/1wirevz.pid");
}

void daemonize(char *rundir, char *pidfile)
{
	int pid, sid, i;
	char str[10];
	struct sigaction newSigAction;
	sigset_t newSigSet;

	if (getppid() == 1)
	{
		return;
	}

	/* Signal mask - block */
	sigemptyset(&newSigSet);
	sigaddset(&newSigSet, SIGCHLD);
	sigaddset(&newSigSet, SIGTSTP);
	sigaddset(&newSigSet, SIGTTOU);
	sigaddset(&newSigSet, SIGTTIN);
	sigprocmask(SIG_BLOCK, &newSigSet, NULL);

	/* Signal handler */
	newSigAction.sa_handler = signal_handler;
	sigemptyset(&newSigAction.sa_mask);
	newSigAction.sa_flags = 0;

	/* Signals to handle */
	sigaction(SIGHUP, &newSigAction, NULL);
	sigaction(SIGTERM, &newSigAction, NULL);
	sigaction(SIGINT, &newSigAction, NULL);

	/* Fork*/
	pid = fork();

	if (pid < 0)
	{
		exit(EXIT_FAILURE);
	}

	if (pid > 0)
	{
		printf("Child process created: %d\n", pid);
		exit(EXIT_SUCCESS);
	}
	
	umask(027); /* file permissions 750 */

	sid = setsid();
	if (sid < 0)
	{
		exit(EXIT_FAILURE);
	}

	for (i = getdtablesize(); i >= 0; --i)
	{
		close(i);
	}

	/* Route I/O connections */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	chdir(rundir); /* change running directory */

	/* Ensure only one copy */
	pidFilehandle = open(pidfile, O_RDWR|O_CREAT, 0600);

	if (pidFilehandle == -1 )
	{
		syslog(LOG_INFO, "Could not open PID lock file %s, exiting", pidfile);
		exit(EXIT_FAILURE);
	}

	/* Try to lock file */
	if (lockf(pidFilehandle,F_TLOCK,0) == -1)
	{
		/* Couldn't get lock on lock file */
		syslog(LOG_INFO, "Could not lock PID lock file %s, exiting", pidfile);
		exit(EXIT_FAILURE);
	}

	/* Get and format PID */
	sprintf(str,"%d\n",getpid());

	/* write pid to lockfile */
	write(pidFilehandle, str, strlen(str));
}

int cfile(void)
{
	config_t cfg;
	config_setting_t *setting;
	config_init(&cfg);

	int chdir(const char *path);

	chdir ("/etc");

	if(!config_read_file(&cfg, DAEMON_NAME".cfg"))
	{
		syslog(LOG_INFO, "Config error > /etc/%s - %s\n", config_error_file(&cfg),config_error_text(&cfg));
		config_destroy(&cfg);
		daemonShutdown();
		exit(EXIT_FAILURE);
	}

	if (!config_lookup_string(&cfg, "vzserver", &vzserver))
	{
		syslog(LOG_INFO, "Missing 'VzServer' setting in configuration file.");
		config_destroy(&cfg);
		daemonShutdown();
		exit(EXIT_FAILURE);
	}
	else
	syslog(LOG_INFO, "VzServer:%s", vzserver);

	if (!config_lookup_int(&cfg, "vzport", &vzport))
	{
		syslog(LOG_INFO, "Missing 'VzPort' setting in configuration file.");
		config_destroy(&cfg);
		daemonShutdown();
		exit(EXIT_FAILURE);
	}
	else
	syslog(LOG_INFO, "VzPort:%d", vzport);


	if (!config_lookup_string(&cfg, "vzpath", &vzpath))
	{
		syslog(LOG_INFO, "Missing 'VzPath' setting in configuration file.");
		config_destroy(&cfg);
		daemonShutdown();
		exit(EXIT_FAILURE);
	}
	else
	syslog(LOG_INFO, "VzPath:%s", vzpath);

	for (i=0; i<inputs; i++)
	{
		char gpio[BUF_LEN];
		sprintf ( gpio, "GPIO%01d", i );
		if ( config_lookup_string( &cfg, gpio, &vzuuid[i]) == CONFIG_TRUE )
		syslog ( LOG_INFO, "%s = %s", gpio, vzuuid[i] );
	}
	
	return ( EXIT_SUCCESS );
}

int http_post(vzuuid)
{
        char format[] = "http://%s:%d/%s/data/%s.json";
        char url[sizeof format+128];

        sprintf ( url, format, vzserver, vzport, vzpath, vzuuid );

		CURL *curl;
		CURLcode res;

		curl_global_init(CURL_GLOBAL_ALL);

		curl = curl_easy_init();

		if(curl) 
		{
			FILE* devnull = NULL;
			devnull = fopen("/dev/null", "w+");

			curl_easy_setopt(curl, CURLOPT_USERAGENT, DAEMON_NAME " " DAEMON_VERSION ); 
			curl_easy_setopt(curl, CURLOPT_URL, url);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");

			curl_easy_setopt(curl, CURLOPT_WRITEDATA, devnull);

			res = curl_easy_perform(curl);

				if(res != CURLE_OK)
				syslog(LOG_INFO, "http_post() %s", curl_easy_strerror(res)); 

			curl_easy_cleanup(curl);

			fclose(devnull);
		}

		curl_global_cleanup();

		return ( EXIT_SUCCESS );
}

int main(void)
{

	// Dont talk, just kiss!
	fclose(stdout);
	fclose(stderr);

	// Start Logging
	setlogmask(LOG_UPTO(LOG_INFO));
	openlog(DAEMON_NAME, LOG_CONS | LOG_PERROR, LOG_USER);

	// Im awaken!
	syslog(LOG_INFO, "S0/Impulse to Volkszaehler RaspberryPI deamon %s", DAEMON_VERSION);

	// Check and process the config file (/etci/s0vz.cfg) */
	cfile();

	// Deamonize
	daemonize("/tmp/", "/tmp/s0vz.pid");

		// S0 starts here!
		char buffer[BUF_LEN];
		struct pollfd fds[inputs];
				
			for (i=0; i<inputs; i++) 
			{
				char buffer[BUF_LEN];
				snprintf ( buffer, BUF_LEN, "/sys/class/gpio/gpio%d/value", gpio_pin_id[i] );
				if((fds[i].fd = open( buffer, O_RDONLY )) ==-1)
				{
					syslog(LOG_INFO,"Error:%s (%m)", buffer);
				}
			}
		
			for (i=0; i<inputs; i++) 
			{
				fds[i].events = POLLPRI;
			}
				
			while(1) 
			{
				int ret = poll(fds, inputs, -1 );
						
					if(ret<0) 
					{
						syslog(LOG_INFO,"Error: poll(fds, inputs, -1 )");
					}
				
				for (i=0; i<inputs; i++) 
				{
					if (fds[i].revents & POLLPRI)
					{
						int in = read(fds[i].fd, buffer, BUF_LEN);
						http_post(vzuuid[i]);
						
					}
				}
			}
}
