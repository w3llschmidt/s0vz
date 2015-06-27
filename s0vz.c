/**************************************************************************

S0/Impulse to Volkszaehler 'RaspberryPI deamon'.

https://github.com/w3llschmidt/s0vz.git

Henrik Wellschmidt  <w3llschmidt@gmail.com>

**************************************************************************/

#define DAEMON_NAME "s0vz"
#define DAEMON_VERSION "1.4"
#define DAEMON_BUILD "4"

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
#include <poll.h>			/* wait for events on file descriptors */

#include <sys/ioctl.h>		/* */

#define BUF_LEN 64

void daemonShutdown();
void signal_handler(int sig);
void daemonize(char *rundir, char *pidfile);

int pidFilehandle, vzport, len, running_handles, rc, aggmin;

const char *vzserver, *vzpath, *vzuuid[64];

char gpio_pin_id[] = { 17, 18, 27, 22, 23, 24 }, url[128];

int inputs = sizeof(gpio_pin_id)/sizeof(gpio_pin_id[0]);
int counter[sizeof(gpio_pin_id)/sizeof(gpio_pin_id[0])];
unsigned long long start_ts;

struct timeval tv;

CURL *easyhandle[sizeof(gpio_pin_id)/sizeof(gpio_pin_id[0])];
CURLM *multihandle;
CURLMcode multihandle_res;

static char errorBuffer[CURL_ERROR_SIZE+1];

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
		remove("/tmp/s0vz.pid");
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
	
	umask(027);

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
		syslog(LOG_ERR, "Could not open PID lock file %s, exiting", pidfile);
		exit(EXIT_FAILURE);
	}

	if (lockf(pidFilehandle,F_TLOCK,0) == -1)
	{
		syslog(LOG_ERR, "Could not lock PID lock file %s, exiting", pidfile);
		exit(EXIT_FAILURE);
	}

	sprintf(str,"%d\n",getpid());

	write(pidFilehandle, str, strlen(str));
}

void cfile() {

	config_t cfg;
	int i;

	//config_setting_t *setting;

	config_init(&cfg);

	int chdir(const char *path);

	chdir ("/etc");

	if(!config_read_file(&cfg, DAEMON_NAME".cfg"))
	{
		syslog(LOG_ERR, "Config error > /etc/%s - %s\n", config_error_file(&cfg),config_error_text(&cfg));
		config_destroy(&cfg);
		daemonShutdown();
		exit(EXIT_FAILURE);
	}

	if (!config_lookup_string(&cfg, "vzserver", &vzserver))
	{
		syslog(LOG_ERR, "Missing 'VzServer' setting in configuration file.");
		config_destroy(&cfg);
		daemonShutdown();
		exit(EXIT_FAILURE);
	}
	else
		syslog(LOG_INFO, "VzServer:%s", vzserver);

	if (!config_lookup_int(&cfg, "vzport", &vzport))
	{
		syslog(LOG_ERR, "Missing 'VzPort' setting in configuration file.");
		config_destroy(&cfg);
		daemonShutdown();
		exit(EXIT_FAILURE);
	}
	else
		syslog(LOG_INFO, "VzPort:%d", vzport);


	if (!config_lookup_string(&cfg, "vzpath", &vzpath))
	{
		syslog(LOG_ERR, "Missing 'VzPath' setting in configuration file.");
		config_destroy(&cfg);
		daemonShutdown();
		exit(EXIT_FAILURE);
	}
	else
		syslog(LOG_INFO, "VzPath:%s", vzpath);

	if (!config_lookup_int(&cfg, "aggmin", &aggmin))
	{
		aggmin = 0;
		syslog(LOG_INFO, "Aggretation not active!", aggmin);
	}
	else
		syslog(LOG_INFO, "Aggretation:%d minutes", aggmin);
		
	for (i=0; i<inputs; i++)
	{
		char gpio[6];
		sprintf ( gpio, "GPIO%01d", i );
		if ( config_lookup_string( &cfg, gpio, &vzuuid[i]) == CONFIG_TRUE )
			syslog ( LOG_INFO, "%s = %s", gpio, vzuuid[i] );
	}

}

unsigned long long unixtime() {

	gettimeofday(&tv, NULL);
	unsigned long long ms_timestamp = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;

return ms_timestamp;
}

void update_curl_handle(const char *vzuuid, int countval, int index) {

	curl_multi_remove_handle(multihandle, easyhandle[index]);
	
	sprintf(url, "http://%s:%d/%s/data/%s.json?ts=%llu&value=%d", vzserver, vzport, vzpath, vzuuid, unixtime(), countval);
	
	curl_easy_setopt(easyhandle[index], CURLOPT_URL, url);
	
	curl_multi_add_handle(multihandle, easyhandle[index]);
			
}

int main(void) {
	int i;

	freopen( "/dev/null", "r", stdin);
	freopen( "/dev/null", "w", stdout);
	freopen( "/dev/null", "w", stderr);

	FILE* devnull = NULL;		
	devnull = fopen("/dev/null", "w+");
		
	setlogmask(LOG_UPTO(LOG_INFO));
	openlog(DAEMON_NAME, LOG_CONS | LOG_PERROR, LOG_USER);

	syslog ( LOG_INFO, "S0/Impulse to Volkszaehler RaspberryPI deamon %s.%s", DAEMON_VERSION, DAEMON_BUILD );
	
	cfile();
	
	char pid_file[16];
	sprintf ( pid_file, "/tmp/%s.pid", DAEMON_NAME );
	daemonize( "/tmp/", pid_file );
	
	char buffer[BUF_LEN];
	struct pollfd fds[inputs];
	
	curl_global_init(CURL_GLOBAL_ALL);
	multihandle = curl_multi_init();
		
	for (i=0; i<inputs; i++) {
	
		snprintf ( buffer, BUF_LEN, "/sys/class/gpio/gpio%d/value", gpio_pin_id[i] );

		if((fds[i].fd = open(buffer, O_RDONLY|O_NONBLOCK)) == 0) {
		
			syslog(LOG_INFO,"Error:%s (%m)", buffer);
			exit(1);
			
		}
	
		fds[i].events = POLLPRI;
		fds[i].revents = 0;	
			
		easyhandle[i] = curl_easy_init();
		
		curl_easy_setopt(easyhandle[i], CURLOPT_URL, url);
		curl_easy_setopt(easyhandle[i], CURLOPT_POSTFIELDS, "");
		curl_easy_setopt(easyhandle[i], CURLOPT_USERAGENT, DAEMON_NAME " " DAEMON_VERSION );
		curl_easy_setopt(easyhandle[i], CURLOPT_WRITEDATA, devnull);
		curl_easy_setopt(easyhandle[i], CURLOPT_ERRORBUFFER, errorBuffer);
		
		curl_multi_add_handle(multihandle, easyhandle[i]);
		
		//Initialize event counter
		counter[i] = 0;			
	}
	
	//Initialize time of next upload
	start_ts = unixtime() + aggmin * 60 * 1000;
				
	for ( ;; ) {
	
		// At least 5 minutes over?	
		//
		if ((start_ts) < unixtime()) {
			int value_count = 0;
			start_ts = unixtime() + aggmin * 60 * 1000;
			
			syslog ( LOG_DEBUG, "Aggregation interval reached");
	
			//Update curls URLs if value > 0
			//
			for (i=0; i<inputs; i++) {
				if (counter[i] > 0) {
					syslog ( LOG_DEBUG, "Update CURL URL for GPIO: %d", i);
					update_curl_handle(vzuuid[i], counter[i], i);
					counter[i] = 0;
					value_count++;
				}
			}
	
			//Trigger CURL posting to vz middleware	
			//
			if (value_count > 0) {	
				syslog ( LOG_DEBUG, "Sending channel updates to VZ: %d", value_count);
				if((multihandle_res = curl_multi_perform(multihandle, &running_handles)) != CURLM_OK) {
					syslog(LOG_ERR, "HTTP_POST(): %s", curl_multi_strerror(multihandle_res) );
				}
			}
		}
		
		int ret = poll(fds, inputs, 1000);
				
		if(ret>0) {

			syslog ( LOG_DEBUG, "Impulse received");
	
			for (i=0; i<inputs; i++) {
				if (fds[i].revents & POLLPRI) {
					len = read(fds[i].fd, buffer, BUF_LEN);
					
					syslog ( LOG_DEBUG, "Update GPIO: %d", i);
					
					//Increase event counter
					//	
					counter[i]++;
				}
			}
		}
	}

	curl_global_cleanup();
	
	return 0;
}

