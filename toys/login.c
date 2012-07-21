/* vi: set sw=4 ts=4:
 *
 * login.c - Start a session on the system.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * Not in SUSv4.
 * No support for PAM/securetty/selinux/login script/issue/utmp
 * Relies on libcrypt for hash calculation.

USE_LOGIN(NEWTOY(login, ">1fph:", TOYFLAG_BIN))

config LOGIN
	bool "login"
	default y
	help
	  usage: login [-p] [-h host] [[-f] username]

	  Establish a new session with the system.
	  -p    Preserve environment
	  -h    The name of the remote host for this login
	  -f    Do not perform authentication
*/

#include "toys.h"

#define LOGIN_TIMEOUT 60
#define LOGIN_FAIL_TIMEOUT 3
#define USER_NAME_MAX_SIZE 32
#define HOSTNAME_SIZE 32

DEFINE_GLOBALS(
	char * hostname;
)
#define TT this.login

static void login_timeout_handler(int sig __attribute__((unused)))
{
	printf("\nLogin timed out after %d seconds.\n", LOGIN_TIMEOUT);
	exit(0);
}

static const char *forbid[] = {
	"BASH_ENV",
	"ENV",
	"HOME",
	"IFS",
	"LD_LIBRARY_PATH",
	"LD_PRELOAD",
	"LD_TRACE_LOADED_OBJECTS",
	"LD_BIND_NOW",
	"LD_AOUT_LIBRARY_PATH",
	"LD_AOUT_PRELOAD",
	"LD_NOWARN",
	"LD_KEEPDIR",
	"SHELL",
	NULL
};

// Unset dangerous environment variables.
void sanitize_env()
{
	const char **p = forbid;
	do {
		unsetenv(*p);
		p++;
	} while (*p);
}

int verify_password(char * pwd)
{
	char * pass;

	if (read_password(toybuf, sizeof(toybuf), "Password: "))
		return 1;
	if (!pwd)
		return 1;
	if (pwd[0] == '!' || pwd[0] == '*')
		return 1;

	pass = crypt(toybuf, pwd);
	if (pass != NULL && strcmp(pass, pwd)==0)
		return 0;

	return 1;
}

void read_user(char * buff, int size)
{
	char hostname[HOSTNAME_SIZE+1];
	int i = 0;
	hostname[HOSTNAME_SIZE] = 0;
	if(!gethostname(hostname, HOSTNAME_SIZE))
		fputs(hostname, stdout);

	fputs(" login: ", stdout);
	fflush(stdout);

	do {
		buff[0] = getchar();
		if (buff[0] == EOF)
			exit(EXIT_FAILURE);
	} while (isblank(buff[0]));

	if (buff[0] != '\n')
		if(!fgets(&buff[1], HOSTNAME_SIZE-1, stdin))
			_exit(1);

	while(i<HOSTNAME_SIZE-1 && isgraph(buff[i]))
	{
		i++;
	}
	buff[i] = 0;
}

void handle_nologin(void)
{
	int fd = open("/etc/nologin", O_RDONLY);
	int size;
	if (fd == -1)
		return;

	size = readall(fd, toybuf,sizeof(toybuf)-1);
	toybuf[size] = 0;
	if (!size)
		puts("System closed for routine maintenance\n");
	else
		puts(toybuf);

	close(fd);
	fflush(stdout);
	exit(EXIT_FAILURE);
}

void handle_motd(void)
{
	int fd = open("/etc/motd", O_RDONLY);
	int size;
	if (fd == -1)
		return;

	size = readall(fd, toybuf,sizeof(toybuf)-1);
	toybuf[size] = 0;
	puts(toybuf);

	close(fd);
	fflush(stdout);
}

int change_identity(const struct passwd *pwd)
{
	if (initgroups(pwd->pw_name,pwd->pw_gid))
		return 1;
	if (setgid(pwd->pw_uid))
		return 1;
	if (setuid(pwd->pw_uid))
		return 1;

	return 0;
}

void spawn_shell(const char *shell)
{
	const char * exec_name = strrchr(shell,'/');
	if (exec_name)
		exec_name++;
	else
		exec_name = shell;

	snprintf(toybuf,sizeof(toybuf)-1, "-%s", shell);
	execl(shell, toybuf, NULL);
	error_exit("Failed to spawn shell");
}

void setup_environment(const struct passwd *pwd, int clear_env)
{
	if (chdir(pwd->pw_dir))
		printf("can't chdir to home directory: %s\n", pwd->pw_dir);

	if (clear_env)
	{
		const char * term = getenv("TERM");
		clearenv();
		if (term) setenv("TERM", term, 1);
	}

	setenv("USER",	pwd->pw_name,  1);
	setenv("LOGNAME", pwd->pw_name,  1);
	setenv("HOME",	pwd->pw_dir,   1);
	setenv("SHELL",   pwd->pw_shell, 1);
}

void login_main(void)
{
	int f_flag = (toys.optflags & 4) >> 2;
	int p_flag = (toys.optflags & 2) >> 1;
	int h_flag = toys.optflags & 1;
	char username[USER_NAME_MAX_SIZE+1];
	struct passwd * pwd = NULL;
	struct spwd * spwd = NULL;
	char *pass = NULL;
	int auth_fail_cnt = 0;

	if (f_flag && toys.optc != 1)
		error_exit("-f requires username");

	if (geteuid() != 0 )
		error_exit("Cannot possibly work without effective root");

	if (!isatty(0) || !isatty(1) || !isatty(2))
		error_exit("Not connected to a tty");

	openlog("login", LOG_PID | LOG_CONS, LOG_AUTH);
	signal(SIGALRM, login_timeout_handler);
	alarm(LOGIN_TIMEOUT);
	sanitize_env();

	while (1) {
		tcflush(0, TCIFLUSH);

		username[USER_NAME_MAX_SIZE] = 0;
		if (toys.optargs[0])
			strncpy(username, toys.optargs[0], USER_NAME_MAX_SIZE);
		else {
			read_user(username, USER_NAME_MAX_SIZE+1);
			if (username[0] == 0)
				continue;
		}

		pwd = getpwnam(username);
		if (!pwd)
			goto query_pass; // Non-existing user

		if (pwd->pw_passwd[0] == '!' || pwd->pw_passwd[0] == '*')
			goto query_pass;  // Locked account

		if (f_flag)
			break; // Pre-authenticated

		if (pwd->pw_passwd[0] == '\0')
			break; // Password-less account

		pass = pwd->pw_passwd;
		if (pwd->pw_passwd[0] == 'x') {
			spwd = getspnam (username);
			if (spwd)
				pass = spwd->sp_pwdp;
		}

	query_pass:
		if (!verify_password(pass))
			break;

		f_flag = 0;
		syslog(LOG_WARNING, "invalid password for '%s' on %s %s %s", username,
			ttyname(0),
			(h_flag)?"from":"",
			(h_flag)?TT.hostname:"");

		sleep(LOGIN_FAIL_TIMEOUT);
		puts("Login incorrect");

		if (++auth_fail_cnt == 3)
		{
			error_exit("Maximum number of tries exceeded (%d)\n", auth_fail_cnt);
		}

		username[0] = 0;
		pwd = NULL;
		spwd = NULL;
	}

	alarm(0);

	if (pwd->pw_uid)
		handle_nologin();

	if (change_identity(pwd))
		error_exit("Failed to change identity");

	setup_environment(pwd, !p_flag);

	handle_motd();

	syslog(LOG_INFO, "%s logged in on %s %s %s", pwd->pw_name,
		ttyname(0),
		(h_flag)?"from":"",
		(h_flag)?TT.hostname:"");

	spawn_shell(pwd->pw_shell);
}
