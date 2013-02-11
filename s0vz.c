/**************************************************************************

S0/Impulse to Volkszaehler 'RaspberryPI deamon'.

sudo gcc -o /usr/sbin/s0vz s0vz.c -lconfig -lcurl

https://github.com/w3llschmidt/s0vz.git
https://github.com/volkszaehler/volkszaehler.org.git

Henrik Wellschmidt  <w3llschmidt@gmail.com>

**************************************************************************/

#define DAEMON_NAME "s0vz"
#define DAEMON_VERSION "1.1"
#define DAEMON_BUILD "0064"

/**************************************************************************

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

#include <stdio.h>              /* standard library functions for file input and output */
#include <stdlib.h>             /* standard library for the C programming language, */
#include <string.h>             /* functions implementing operations on strings  */
#include <unistd.h>             /* provides access to the POSIX operating system API */
#include <sys/stat.h>           /* declares the stat() functions; umask */
#include <fcntl.h>              /* file descriptors */
#include <syslog.h>             /* send messages to the system logger */
#include <errno.h>              /* macros to report error conditions through error codes */
#include <signal.h>             /* signal processing */
#include <stddef.h>             /* defines the macros NULL and offsetof as well as the types ptrdiff_t, wchar_t, and size_t */

#include <libconfig.h>          /* reading, manipulating, and writing structured configuration files */
#include <curl/curl.h>          /* multiprotocol file transfer library */
#include <poll.h>		/* wait for events on file descriptors */

#include <sys/ioctl.h>		/* */

#define BUF_LEN 32

void daemonShutdown();
void signal_handler(int sig);
void daemonize(char *rundir, char *pidfile);

int pidFilehandle, vzport, i;

const char *vzserver, *vzpath, *vzuuid[64];

char gpio_pin_id[] = { 17, 18, 27, 22, 23, 24 }, url[254];
int inputs = sizeof(gpio_pin_id)/sizeof(gpio_pin_id[0]);

struct timeval tv;

void signal_handler(int sig) {

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

void daemonShutdown() {
		close(pidFilehandle);
		remove("/tmp/1wirevz.pid");
}

void daemonize(char *rundir, char *pidfile) {
	int pid, sid, i;
	char str[10];
	struct sigaction newSigAction;
	sigset_t newSigSet;

	if (getppid() == 1)
	{
		return;
	}

	sigemptyset(&newSigSet);
	sigaddset(&newSigSet, SIGCHLD);
	sigaddset(&newSigSet, SIGTSTP);
	sigaddset(&newSigSet, SIGTTOU);
	sigaddset(&newSigSet, SIGTTIN);
	sigprocmask(SIG_BLOCK, &newSigSet, NULL);

	newSigAction.sa_handler = signal_handler;
	sigemptyset(&newSigAction.sa_mask);
	newSigAction.sa_flags = 0;

	sigaction(SIGHUP, &newSigAction, NULL);
	sigaction(SIGTERM, &newSigAction, NULL);
	sigaction(SIGINT, &newSigAction, NULL);

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
	
	//umask(027);

	sid = setsid();
	if (sid < 0)
	{
		exit(EXIT_FAILURE);
	}

	for (i = getdtablesize(); i >= 0; --i)
	{
		close(i);
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	chdir(rundir);

	pidFilehandle = open(pidfile, O_RDWR|O_CREAT, 0600);

	if (pidFilehandle == -1 )
	{
		syslog(LOG_INFO, "Could not open PID lock file %s, exiting", pidfile);
		exit(EXIT_FAILURE);
	}

	if (lockf(pidFilehandle,F_TLOCK,0) == -1)
	{
		syslog(LOG_INFO, "Could not lock PID lock file %s, exiting", pidfile);
		exit(EXIT_FAILURE);
	}

	sprintf(str,"%d\n",getpid());

	write(pidFilehandle, str, strlen(str));
}

void cfile() {

	config_t cfg;

	//config_setting_t *setting;

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
		char gpio[6];
		sprintf ( gpio, "GPIO%01d", i );
		if ( config_lookup_string( &cfg, gpio, &vzuuid[i]) == CONFIG_TRUE )
		syslog ( LOG_INFO, "%s = %s", gpio, vzuuid[i] );
	}

}

void http_post(const char *vzuuid) {

	gettimeofday(&tv,NULL);
	unsigned long long ms_timestamp = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;

	sprintf(url, "http://%s:%d/%s/data/%s.json?ts=%llu", vzserver, vzport, vzpath, vzuuid, ms_timestamp);
		
	CURL *curl;
	CURLcode curl_res;
	
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
	
			if( (curl_res = curl_easy_perform(curl)) != CURLE_OK) {
			syslog(LOG_INFO, "HTTP_POST(): %s", curl_easy_strerror(curl_res) );
			}
	
		curl_easy_cleanup(curl);
		fclose ( devnull );
		
	}

curl_global_cleanup();
}

int main() {

	freopen( "/dev/null", "r", stdin);
	freopen( "/dev/null", "w", stdout);
	freopen( "/dev/null", "w", stderr);

	setlogmask(LOG_UPTO(LOG_INFO));
	openlog(DAEMON_NAME, LOG_CONS | LOG_PERROR, LOG_USER);

	syslog ( LOG_INFO, "S0/Impulse to Volkszaehler RaspberryPI deamon %s (%s)", DAEMON_VERSION, DAEMON_BUILD );

	cfile();

	char pid_file[16];
	sprintf ( pid_file, "/tmp/%s.pid", DAEMON_NAME );
	daemonize( "/tmp/", pid_file );
		
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
						read(fds[i].fd, buffer, BUF_LEN);
						http_post(vzuuid[i]);
					}
				}
			}
}
