#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <string.h>
#include <ctype.h>
#include "smf.h"
#include "config.h"

#ifdef WITH_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

smf_track_t *selected_track = NULL;
smf_event_t *selected_event = NULL;
smf_t *smf = NULL;
char *last_file_name = NULL;

static void
usage(void)
{
	fprintf(stderr, "usage: smfsh [file]\n");

	exit(EX_USAGE);
}

static void
log_handler(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer notused)
{
	fprintf(stderr, "%s: %s\n", log_domain, message);
}

static int cmd_track(char *arg);

static int
cmd_load(char *file_name)
{
	if (file_name == NULL) {
		if (last_file_name == NULL) {
			g_critical("Please specify file name.");
			return -1;
		}

		file_name = last_file_name;
	}

	if (smf != NULL)
		smf_delete(smf);

	selected_track = NULL;
	selected_event = NULL;

	last_file_name = strdup(file_name);
	smf = smf_load(file_name);
	if (smf == NULL) {
		g_critical("Couln't load '%s'.", file_name);

		smf = smf_new();
		if (smf == NULL) {
			g_critical("Cannot initialize smf_t.");
			return -1;
		}

		return -2;
	}

	g_message("File '%s' loaded.", file_name);

	cmd_track("1");

	return 0;
}

static int
cmd_save(char *file_name)
{
	int ret;

	if (file_name == NULL) {
		if (last_file_name == NULL) {
			g_critical("Please specify file name.");
			return -1;
		}

		file_name = last_file_name;
	}

	if (file_name == NULL) {
		g_critical("Please specify file name.");
		return -1;
	}

	last_file_name = strdup(file_name);
	ret = smf_save(smf, file_name);
	if (ret) {
		g_critical("Couln't save '%s'", file_name);
		return -1;
	}

	g_message("File '%s' saved.", file_name);

	return 0;
}

static int
cmd_ppqn(char *new_ppqn)
{
	int tmp;
	char *end;

	if (new_ppqn == NULL) {
		g_message("Pulses Per Quarter Note (aka Division) is %d.", smf->ppqn);
	} else {
		tmp = strtol(new_ppqn, &end, 10);
		if (end - new_ppqn != strlen(new_ppqn)) {
			g_critical("Invalid PPQN, garbage characters after the number.");
			return -1;
		}

		if (tmp <= 0) {
			g_critical("Invalid PPQN, valid values are greater than zero.");
			return -2;
		}

		if (smf_set_ppqn(smf, tmp)) {
			g_message("smf_set_ppqn failed.");
			return -3;
		}

		g_message("Pulses Per Quarter Note changed to %d.", smf->ppqn);
	}
	
	return 0;
}

static int
cmd_format(char *new_format)
{
	int tmp;
	char *end;

	if (new_format == NULL) {
		g_message("Format is %d.", smf->format);
	} else {
		tmp = strtol(new_format, &end, 10);
		if (end - new_format != strlen(new_format)) {
			g_critical("Invalid format value, garbage characters after the number.");
			return -1;
		}

		if (tmp < 0 || tmp > 2) {
			g_critical("Invalid format value, valid values are in range 0 - 2, inclusive.");
			return -2;
		}

		if (smf_set_format(smf, tmp)) {
			g_critical("smf_set_format failed.");
			return -3;
		}

		g_message("Forma changed to %d.", smf->format);
	}
	
	return 0;
}


static int
cmd_tracks(char *notused)
{
	if (smf->number_of_tracks > 0)
		g_message("There are %d tracks, numbered from 1 to %d.", smf->number_of_tracks, smf->number_of_tracks);
	else
		g_message("There are no tracks.");

	return 0;
}

static int
cmd_track(char *arg)
{
	int num;

	if (arg == NULL) {
		if (selected_track == NULL)
			g_message("No track currently selected.");
		else
			g_message("Currently selected is track number %d, containing %d events.",
				selected_track->track_number, selected_track->number_of_events);
	} else {
		if (smf->number_of_tracks == 0) {
			g_message("There are no tracks.");
			return -1;
		}

		num = atoi(arg);
		if (num < 1 || num > smf->number_of_tracks) {
			g_critical("Invalid track number specified; valid choices are 1 - %d.", smf->number_of_tracks);
			return -2;
		}

		selected_track = smf_get_track_by_number(smf, num);
		if (selected_track == NULL) {
			g_critical("smf_get_track_by_number() failed, track not selected.");
			return -3;
		}

		selected_event = NULL;

		g_message("Track number %d selected; it contains %d events.",
				selected_track->track_number, selected_track->number_of_events);
	}

	return 0;
}

static int
cmd_trackadd(char *notused)
{
	selected_track = smf_track_new();
	if (selected_track == NULL) {
		g_critical("smf_track_new() failed, track not created.");
		return -1;
	}

	smf_add_track(smf, selected_track);

	selected_event = NULL;

	g_message("Created new track; track number %d selected.", selected_track->track_number);

	return 0;
}

static int
cmd_trackrm(char *notused)
{
	if (selected_track == NULL) {
		g_critical("No track selected - please use 'track [number]' command first.");
		return -1;
	}

	selected_event = NULL;
	smf_track_delete(selected_track);
	selected_track = NULL;

	return 0;
}

#define BUFFER_SIZE 1024

static int
show_event(smf_event_t *event)
{
	int off = 0, i;
	char *decoded, *type;

	if (smf_event_is_metadata(event))
		type = "Metadata";
	else
		type = "Event";
	
	decoded = smf_event_decode(event);

	if (decoded == NULL) {
		decoded = malloc(BUFFER_SIZE);
		if (decoded == NULL) {
			g_critical("show_event: malloc failed.");
			return -1;
		}

		off += snprintf(decoded + off, BUFFER_SIZE - off, "Unknown event:");

		for (i = 0; i < event->midi_buffer_length && i < 5; i++)
			off += snprintf(decoded + off, BUFFER_SIZE - off, " 0x%x", event->midi_buffer[i]);
	}

	g_message("%d: %s: %s, %f seconds, %d pulses, %d delta pulses", event->event_number, type, decoded,
		event->time_seconds, event->time_pulses, event->delta_time_pulses);

	free(decoded);

	return 0;
}

static int
cmd_events(char *notused)
{
	smf_event_t *event;

	if (selected_track == NULL) {
		g_critical("No track selected - please use 'track [number]' command first.");
		return -1;
	}

	g_message("List of events in track %d follows:", selected_track->track_number);

	smf_rewind(smf);

	while ((event = smf_track_get_next_event(selected_track)) != NULL) {
		show_event(event);
	}

	smf_rewind(smf);

	return 0;
}

static int
cmd_event(char *arg)
{
	int num;

	if (arg == NULL) {
		if (selected_event == NULL) {
			g_message("No event currently selected.");
		} else {
			g_message("Currently selected is event %d, track %d.", selected_event->event_number, selected_track->track_number);
			show_event(selected_event);
		}
	} else {
		num = atoi(arg);
		if (num < 1 || num > selected_track->number_of_events) {
			g_critical("Invalid event number specified; valid choices are 1 - %d.", selected_track->number_of_events);
			return -1;
		}

		selected_event = smf_track_get_event_by_number(selected_track, num);
		if (selected_event == NULL) {
			g_critical("smf_get_event_by_number() failed, event not selected.");
			return -2;
		}

		g_message("Event number %d selected.", selected_event->event_number);
		show_event(selected_event);
	}

	return 0;
}

static int
decode_hex(char *str, unsigned char **buffer, int *length)
{
	int i, value, midi_buffer_length;
	char buf[3];
	unsigned char *midi_buffer = NULL;
	char *end = NULL;

	if ((strlen(str) % 2) != 0) {
		g_critical("Hex value should have even number of characters, you know.");
		goto error;
	}

	midi_buffer_length = strlen(str) / 2;
	midi_buffer = malloc(midi_buffer_length);
	if (midi_buffer == NULL) {
		g_critical("malloc() failed.");
		goto error;
	}

	for (i = 0; i < midi_buffer_length; i++) {
		buf[0] = str[i * 2];
		buf[1] = str[i * 2 + 1];
		buf[2] = '\0';
		value = strtoll(buf, &end, 16);

		if (end - buf != 2) {
			g_critical("Garbage characters detected after hex.");
			goto error;
		}

		midi_buffer[i] = value;
	}

	*buffer = midi_buffer;
	*length = midi_buffer_length;

	return 0;

error:
	if (midi_buffer != NULL)
		free(midi_buffer);

	return -1;
}

static void
eventadd_usage(void)
{
	g_message("Usage: eventadd time-in-seconds midi-in-hex.  For example, 'eventadd 1 903C7F'");
        g_message("will add Note On event, one second from the start of song, channel 1, note C4, velocity 127.");
}

static int
cmd_eventadd(char *str)
{
	int midi_buffer_length;
	double seconds;
	unsigned char *midi_buffer;
	char *time, *endtime;

	if (selected_track == NULL) {
		g_critical("Please select a track first.");
		return -1;
	}

	if (str == NULL) {
		eventadd_usage();
		return -2;
	}

	/* Extract the time. */
	time = strsep(&str, " ");
	seconds = strtod(time, &endtime);
	if (endtime - time != strlen(time)) {
		g_critical("Time is supposed to be a number, without trailing characters.");
		return -3;
	}

	/* Called with one parameter? */
	if (str == NULL) {
		eventadd_usage();
		return -4;
	}

	if (decode_hex(str, &midi_buffer, &midi_buffer_length)) {
		eventadd_usage();
		return -5;
	}

	selected_event = smf_event_new();
	if (selected_event == NULL) {
		g_critical("smf_event_new() failed, event not created.");
		return -6;
	}

	selected_event->midi_buffer = midi_buffer;
	selected_event->midi_buffer_length = midi_buffer_length;

	if (smf_event_is_valid(selected_event) == 0) {
		g_critical("Event is invalid from the MIDI specification point of view, not created.");
		smf_event_delete(selected_event);
		selected_event = NULL;
		return -7;
	}

	smf_track_add_event_seconds(selected_track, selected_event, seconds);

	g_message("Event created.");

	return 0;
}

static int
cmd_eventaddeot(char *notused)
{
	if (selected_track == NULL) {
		g_critical("Please select a track first.");
		return -1;
	}

	if (smf_track_add_eot(selected_track)) {
		g_critical("smf_track_add_eot() failed.");
		return -2;
	}

	g_message("Event created.");

	return 0;
}

static int
cmd_eventrm(char *notused)
{
	if (selected_event == NULL) {
		g_critical("No event selected - please use 'event [number]' command first.");
		return -1;
	}

	smf_event_delete(selected_event);
	selected_event = NULL;

	g_message("Event removed.");

	return 0;
}

static int
cmd_tempo(char *notused)
{
	int i;
	smf_tempo_t *tempo;

	for (i = 0;; i++) {
		tempo = smf_get_tempo_by_number(smf, i);
		if (tempo == NULL)
			break;

		g_message("Tempo #%d: Starts at %d pulses, %f seconds, setting %d microseconds per quarter note.",
			i, tempo->time_pulses, tempo->time_seconds, tempo->microseconds_per_quarter_note);
		g_message("Time signature: %d/%d, %d clocks per click, %d 32nd notes per quarter note.",
			tempo->numerator, tempo->denominator, tempo->clocks_per_click, tempo->notes_per_note);
	}

	return 0;
}

static int
cmd_exit(char *notused)
{
	g_debug("Good bye.");
	exit(0);
}

static int cmd_help(char *notused);

struct command_struct {
	char *name;
	int (*function)(char *command);
	char *help;
} commands[] = {{"help", cmd_help, "show this help."},
		{"load", cmd_load, "load named file."},
		{"save", cmd_save, "save to named file."},
		{"ppqn", cmd_ppqn, "show ppqn (aka division), or set ppqn if used with parameter."},
		{"format", cmd_format, "show format, or set format if used with parameter."},
		{"tracks", cmd_tracks, "show number of tracks."},
		{"track", cmd_track, "show number of currently selected track, or select a track."},
		{"trackadd", cmd_trackadd, "add a track and select it."},
		{"trackrm", cmd_trackrm, "remove currently selected track."},
		{"events", cmd_events, "show events in the currently selected track."},
		{"event", cmd_event, "show number of currently selected event, or select an event."},
		{"eventadd", cmd_eventadd, "add an event and select it."},
		{"add", cmd_eventadd, NULL},
		{"eventaddeot", cmd_eventaddeot, "add an End Of Track event."},
		{"eot", cmd_eventaddeot, NULL},
		{"eventrm", cmd_eventrm, "remove currently selected event."},
		{"tempo", cmd_tempo, "show tempo map."},
		{"exit", cmd_exit, "exit to shell."},
		{"quit", cmd_exit, NULL},
		{"bye", cmd_exit, NULL},
		{NULL, NULL, NULL}};

static int
cmd_help(char *notused)
{
	struct command_struct *tmp;

	g_message("Available commands:");

	for (tmp = commands; tmp->name != NULL; tmp++) {
		/* Skip commands with no help string. */
		if (tmp->help == NULL)
			continue;
		g_message("%s: %s", tmp->name, tmp->help);
	}

	return 0;
}

/**
 * Removes (in place) all whitespace characters before the first
 * non-whitespace and all trailing whitespace characters.  Replaces
 * more than one consecutive whitespace characters with one.
 */
static void
strip_unneeded_whitespace(char *str, int len)
{
	char *src, *dest;
	int skip_white = 1;

	for (src = str, dest = str; src < dest + len; src++) {
		if (*src == '\n' || *src == '\0') {
			*dest = '\0';
			break;
		}

		if (isspace(*src)) {
			if (skip_white)
				continue;

			skip_white = 1;
		} else {
			skip_white = 0;
		}

		*dest = *src;
		dest++;
	}

	/* Remove trailing whitespace. */
	len = strlen(dest);
	if (isspace(dest[len - 1]))
		dest[len - 1] = '\0';
}

static char *
read_command(void)
{
	char *buf;
	int len;

#ifdef WITH_READLINE
	buf = readline("smfsh> ");
#else
	buf = malloc(1024);
	if (buf == NULL) {
		g_critical("Malloc failed.");
		return NULL;
	}

	fprintf(stdout, "smfsh> ");
	fflush(stdout);

	buf = fgets(buf, 1024, stdin);
#endif

	if (buf == NULL) {
		fprintf(stdout, "exit\n");
		return "exit";
	}

	strip_unneeded_whitespace(buf, 1024);

	len = strlen(buf);

	if (len == 0)
		return read_command();

#ifdef WITH_READLINE
	add_history(buf);
#endif

	return buf;
}

static int
execute_command(char *line)
{
	char *command, *args;
	struct command_struct *tmp;

	args = line;
	command = strsep(&args, " ");

	for (tmp = commands; tmp->name != NULL; tmp++) {
		if (strcmp(tmp->name, command) == 0)
			return (tmp->function)(args);
	}

	g_warning("No such command: '%s'.  Type 'help' to see available commands.", command);

	return -1;
}

static void
read_and_execute_command(void)
{
	int ret;
	char *command;

	command = read_command();

	ret = execute_command(command);
	if (ret) {
		g_warning("Command finished with error.");
	}

	free(command);
}

#ifdef WITH_READLINE

static char *
smfsh_command_generator(const char *text, int state)
{
	static struct command_struct *command = commands;
	char *tmp;

	if (state == 0)
		command = commands;

	while (command->name != NULL) {
		tmp = command->name;
		command++;

		if (strncmp(tmp, text, strlen(text)) == 0)
			return strdup(tmp);
	}

	return NULL;
}

static char **
smfsh_completion(const char *text, int start, int end)
{
	int i;

	/* Return NULL if "text" is not the first word in the input line. */
	if (start != 0) {
		for (i = 0; i < start; i++) {
			if (!isspace(rl_line_buffer[i]))
				return NULL;
		}
	}

	return rl_completion_matches(text, smfsh_command_generator);
}

#endif

int main(int argc, char *argv[])
{
	if (argc > 2) {
		usage();
	}

	g_log_set_default_handler(log_handler, NULL);

	smf = smf_new();
	if (smf == NULL) {
		g_critical("Cannot initialize smf_t.");
		return -1;
	}

	if (argc == 2) {
		last_file_name = argv[1];
		cmd_load(last_file_name);
	} else {
		cmd_trackadd(NULL);
	}

#ifdef WITH_READLINE
	rl_readline_name = "smfsh";
	rl_attempted_completion_function = smfsh_completion;
#endif

	for (;;)
		read_and_execute_command();

	return 0;
}

