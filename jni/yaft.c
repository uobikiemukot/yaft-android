/* See LICENSE for licence details. */
#include "yaft.h"
#include "glyph.h"
#include "conf.h"
#include "color.h"
#include "keycode.h"
#include "util.h"
#include "wcwidth.h"
#include "android.h"
#include "terminal.h"
#include "function.h"
#include "parse.h"

volatile sig_atomic_t loop_flag = true;

void sig_handler(int signo)
{
	if (signo == SIGCHLD)
		loop_flag = false;
}

void sig_set()
{
    struct sigaction sigact;

    memset(&sigact, 0, sizeof(struct sigaction));
    sigact.sa_handler = sig_handler;
    sigact.sa_flags   = SA_RESTART;
    esigaction(SIGCHLD, &sigact, NULL);
}

void sig_reset()
{
 	/* no error handling */
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(struct sigaction));
	sigact.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sigact, NULL);
}

void fork_and_exec(int *master, int lines, int cols)
{
	pid_t pid;
	struct winsize ws = {.ws_row = lines, .ws_col = cols,
		.ws_xpixel = 0, .ws_ypixel = 0};

	pid = eforkpty(master, NULL, NULL, &ws);

	if (pid == 0) { /* child */
		esetenv("TERM", term_name, 1);
		eexecvp(shell_cmd, (const char *[]){shell_cmd, NULL});
	}
}

int keycode2keysym(int keycode, int keystate)
{
	int keysym = keycode2keysym_table[keycode];

	if (DEBUG)
		LOGE("keysym:0x%.2X keystate:%d\n", keysym, keystate);

	if (keysym != 0) { 
		/* matched keycode2keysym table */
		if (keystate & SHIFT_MASK)
			return islower(keysym) ? toupper(keysym): keycode_shift[keysym];
		else if (keystate & CTRL_MASK)
			return (islower(keysym) || (keysym == '@') || ('[' <= keysym && keysym <= '_')) ?
				keycode_ctrl[keysym]: keysym;
		else
			return keysym;
	}
	return 0;
}

static int32_t app_handle_input(struct android_app *app, AInputEvent* event) {
	struct app_state *state = (struct app_state *) app->userData;
	int action, keycode, keysym;

	if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_KEY)
		return 0;

	action  = AKeyEvent_getAction(event); 
	keycode = AKeyEvent_getKeyCode(event);

	if(action == AKEY_EVENT_ACTION_MULTIPLE)
		return 0;

	if(action == AKEY_EVENT_ACTION_DOWN) {
		if(keycode == AKEYCODE_SHIFT_RIGHT || keycode == AKEYCODE_SHIFT_LEFT)
			state->keystate |= SHIFT_MASK;
		else if(keycode == AKEYCODE_CTRL_RIGHT || keycode == AKEYCODE_CTRL_LEFT)
			state->keystate |= CTRL_MASK ;
		else if(keycode == AKEYCODE_ALT_RIGHT || keycode == AKEYCODE_ALT_LEFT)
			state->keystate |= ALT_MASK;
		else {
			if ((keysym = keycode2keysym(keycode, state->keystate)) != 0) {
				if (state->keystate & ALT_MASK)
					ewrite(state->term->fd, "\033", 1);
				ewrite(state->term->fd, &keysym, 1);
			}
			else
				return 0;
		}
	}
	else if(action == AKEY_EVENT_ACTION_UP) {
		if(keycode == AKEYCODE_SHIFT_RIGHT || keycode == AKEYCODE_SHIFT_LEFT)
			state->keystate &= ~SHIFT_MASK;
		else if(keycode == AKEYCODE_CTRL_RIGHT || keycode == AKEYCODE_CTRL_LEFT)
			state->keystate &= ~CTRL_MASK ;
		else if(keycode == AKEYCODE_ALT_RIGHT || keycode == AKEYCODE_ALT_LEFT)
			state->keystate &= ~ALT_MASK;
    }
	return 1;
}

void app_init(struct app_state *state)
{
	sig_set();
	fb_init(state->fb);
	term_init(state->term, state->fb->width, state->fb->height);
	fork_and_exec(&state->term->fd, state->term->lines, state->term->cols);
	state->focused = true;
	state->initialized = true;
}

void app_die(struct app_state *state)
{
	if (state->initialized == false)
		return;

	term_die(state->term);
	fb_die(state->fb);
	sig_reset();
	state->focused = false;
	state->initialized = false;
}

static void app_handle_cmd(struct android_app *app, int32_t cmd) {
	struct app_state *state = (struct app_state *) app->userData;
	static char *app_cmd[] = {
		[APP_CMD_INPUT_CHANGED]        = "APP_CMD_INPUT_CHANGED",
		[APP_CMD_INIT_WINDOW]          = "APP_CMD_INIT_WINDOW",
		[APP_CMD_TERM_WINDOW]          = "APP_CMD_TERM_WINDOW",
		[APP_CMD_WINDOW_RESIZED]       = "APP_CMD_WINDOW_RESIZED",
		[APP_CMD_WINDOW_REDRAW_NEEDED] = "APP_CMD_WINDOW_REDRAW_NEEDED",
		[APP_CMD_CONTENT_RECT_CHANGED] = "APP_CMD_CONTENT_RECT_CHANGED",
		[APP_CMD_GAINED_FOCUS]         = "APP_CMD_GAINED_FOCUS",
		[APP_CMD_LOST_FOCUS]           = "APP_CMD_LOST_FOCUS",
		[APP_CMD_CONFIG_CHANGED]       = "APP_CMD_CONFIG_CHANGED",
		[APP_CMD_LOW_MEMORY]           = "APP_CMD_LOW_MEMORY",
		[APP_CMD_START]                = "APP_CMD_START",
		[APP_CMD_RESUME]               = "APP_CMD_RESUME",
		[APP_CMD_SAVE_STATE]           = "APP_CMD_SAVE_STATE",
		[APP_CMD_PAUSE]                = "APP_CMD_PAUSE",
		[APP_CMD_STOP]                 = "APP_CMD_STOP",
		[APP_CMD_DESTROY]              = "APP_CMD_DESTROY",
	};

	if (DEBUG)
		LOGE("%s\n", app_cmd[cmd]);

	switch (cmd) {
	case APP_CMD_INIT_WINDOW:
		if (app->window != NULL)
			app_init(state);
		break;
	case APP_CMD_TERM_WINDOW:
		app_die(state);
		break;
	case APP_CMD_GAINED_FOCUS:
		state->focused = true;
		break;
	case APP_CMD_LOST_FOCUS:
		state->focused = false;
		break;
	default:
		break;
	}
}

void android_main(struct android_app *app)
{
	uint8_t buf[BUFSIZE];
	char log[BUFSIZE + 1];
	ssize_t size;
	fd_set fds;
	struct timeval tv;
	struct framebuffer fb;
	struct terminal term;
	struct app_state state;
	int ident;
	int events;
	struct android_poll_source* source;

	/* init state */
	setlocale(LC_ALL, "");
	fb.app = app;
	state.fb   = &fb;
	state.term = &term;
	state.keystate = 0;
	state.focused  = false;
	state.initialized = false;

	/* android */
	app_dummy();
	app->userData = &state;
	app->onAppCmd = app_handle_cmd;
	app->onInputEvent = app_handle_input;

	while (loop_flag) {
		/* handle shell output */
		if (state.initialized) {
			FD_ZERO(&fds);
			FD_SET(term.fd, &fds);
			tv.tv_sec  = 0;
			tv.tv_usec = SELECT_TIMEOUT;
			eselect(term.fd + 1, &fds, NULL, NULL, &tv);

			if (FD_ISSET(term.fd, &fds)) {
				size = read(term.fd, buf, BUFSIZE);
				if (size > 0) {
					if (DEBUG) {
						snprintf(log, BUFSIZE + 1, "%s", buf);
						LOGE("%s\n", log);
					}
					parse(&term, buf, size);

					if (LAZY_DRAW && size == BUFSIZE)
						continue;
					if (state.focused)
						refresh(&fb, &term);
				}
			}
		}

		/* handle keyboard input */
		while ((ident = ALooper_pollAll(0, NULL, &events, (void **) &source)) >= 0) {
			if (source != NULL)
				source->process(app, source);

			if (app->destroyRequested) {
				app_die(&state);
				goto loop_end;
			}
		}
	}

	/* normal exit */
loop_end:
	ANativeActivity_finish(app->activity);
	exit(EXIT_SUCCESS);
}
