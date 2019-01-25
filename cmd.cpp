#include <string.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>

#include "mpvhandler.h"
#include "util.h"
#include "cmd.h"
#include "chat.h"

void cmd_pp(char *args, mpvhandler *mpvh);
void cmd_status(char *args, mpvhandler *mpvh);
void cmd_seek(char *args, mpvhandler *mpvh);
void cmd_seekplus(char *args, mpvhandler *mpvh);
void cmd_seekminus(char *args, mpvhandler *mpvh);

struct {
	const char *name;
	void (*fn)(char *, mpvhandler *);
} cmdtab[] = {
	{ "pp", cmd_pp },
	{ "STATUS", cmd_status },
	{ "SEEK", cmd_seek },
	{ "SEEK+", cmd_seekplus },
	{ "SEEK-", cmd_seekminus },
};

void cmd_pp(char *args, mpvhandler *mpvh)
{
	UNUSED(args);

	mpvh_pp(mpvh);
	mpvinfo i = mpvh_getinfo(mpvh);
	statusstr status = statestr(i.state);
	sendmsg(status.str);
}

void cmd_status(char *args, mpvhandler *mpvh)
{
	UNUSED(args);

	mpvinfo i = mpvh_getinfo(mpvh);
	statusstr status = statestr(i.state);
	sendmsg(status.str);
}

void cmd_seek(char *args, mpvhandler *mpvh)
{
	double time = parsetime(args, strlen(args));
	mpvh_seek(mpvh, time);
}

void cmd_seekplus(char *args, mpvhandler *mpvh)
{
	double time = parsetime(args, strlen(args));
	mpvh_seekrel(mpvh, time);
}

void cmd_seekminus(char *args, mpvhandler *mpvh)
{
	double time = parsetime(args, strlen(args));
	time *= -1;
	mpvh_seekrel(mpvh, time);
}

void handlecmd(char *text, mpvhandler *mpvh)
{
	static size_t ncmd = sizeof cmdtab / sizeof cmdtab[0];

	while (isspace(*text))
		text++;

	size_t cmdlen = 0;
	while (text[cmdlen] && !isspace(text[cmdlen]))
		cmdlen++;

	char *args = text + cmdlen;
	while (*args != '\0' && isspace(*args))
		args++;

	for (size_t i = 0; i < ncmd; i++) {
		if (strstr(text, cmdtab[i].name) != text)
			continue;
		if (cmdlen != strlen(cmdtab[i].name))
			continue;
		cmdtab[i].fn(args, mpvh);
		break;
	}
}