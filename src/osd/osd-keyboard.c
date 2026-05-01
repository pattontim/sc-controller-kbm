#include "scc/utils/traceback.h"
#include "scc/utils/assert.h"
#include "scc/osd/osd_keyboard.h"
#include "scc/tools.h"
#include <gtk/gtk.h>

static void on_mnu_exit(void* _kbd, int code) {
	exit(code);
}

static void on_mnu_ready(void* _kbd) {
	gtk_widget_show_all(GTK_WIDGET(_kbd));
}


int main(int argc, char** argv) {
	OSDKeyboardOptions options;
	traceback_set_argv0(argv[0]);
	
	// Pre-parse args to catch --silent before gtk_init
	if (osd_keyboard_parse_args(&options, argc, argv) < 0)
		return 1;
	
	if (options.silent) {
		setenv("G_MESSAGES_DEBUG", "", 1);
		freopen("/dev/null", "w", stdout);
		freopen("/dev/null", "w", stderr);
	}
	
	gtk_init(&argc, &argv);
	
	OSDKeyboard* keyboard = osd_keyboard_new(&options);
	
	g_signal_connect(G_OBJECT(keyboard), "exit", (GCallback)&on_mnu_exit, NULL);
	g_signal_connect(G_OBJECT(keyboard), "ready", (GCallback)&on_mnu_ready, NULL);
	if (keyboard != NULL) {
		gtk_main();
		return 0;
	} else {
		return 1;
	}
}

