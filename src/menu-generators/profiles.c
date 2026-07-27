#include "scc/utils/strbuilder.h"
#include "scc/menu_generator.h"
#include "scc/bindings.h"
#include "scc/tools.h"
#include <strings.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>

#define FILENAME_SUFFIX ".sccprofile"


const ParamDescription* get_parameters(size_t* size_return) {
	return NULL;
}


static int sort_fn(const void* p1, const void* p2) {
	return strcasecmp(*(char* const*)p1, *(char* const*)p2);
}

void generate(GeneratorContext* ctx) {
	// Prepare
	StringList all = list_new(char, 5);
	if (all == NULL)		// OOM
		return;
	const char* paths[2];
	const size_t suffix_len = strlen(FILENAME_SUFFIX);
	paths[0] = scc_get_default_profiles_path();
	paths[1] = scc_get_profiles_path();
	

	// FIXME: it is not easy to change the config as a user since the config is loaded from the flatpak dir
	// 100965 openat(AT_FDCWD, "/home/deck/.var/app/com.github.kozec.scc-daemon/config/scc/config.c.json", O_RDONLY) = 10
	// 100965 openat(AT_FDCWD, "/app/share/scc/osd_styles/Reloaded.gtkstyle.css", O_RDONLY) = 18
	// 100965 openat(AT_FDCWD, "/dev/uinput", O_WRONLY|O_NONBLOCK) = 18
	// 100965 access("/home/deck/.var/app/com.github.kozec.scc-daemon/config/scc/profiles/.scc-osd.keyboard.sccprofile", F_OK) = -1 ENOENT (No such file or directory)
	// 100965 access("/app/share/scc/default_profiles/.scc-osd.keyboard.sccprofile", F_OK) = 0
	// 100965 access("/home/deck/.var/app/com.github.kozec.scc-daemon/config/scc/profiles/.scc-osd.keyboard.sccprofile", F_OK) = -1 ENOENT (No such file or directory)
	// 100965 access("/app/share/scc/default_profiles/.scc-osd.keyboard.sccprofile", F_OK) = 0
	// 100965 openat(AT_FDCWD, "/app/share/scc/default_profiles/.scc-osd.keyboard.sccprofile", O_RDONLY) = 38
	// 100965 openat(AT_FDCWD, "/app/share/scc/keyboard.json", O_RDONLY) = 38

	// Grab profiles
	// TODO: This should be function somewhere in tools
	for(int i=0; i<2; i++) {
		DIR *dir;
		struct dirent *ent;
		if ((dir = opendir(paths[i])) == NULL)
			continue;
		while ((ent = readdir(dir)) != NULL) {
			bool is_profile = (strstr(ent->d_name, FILENAME_SUFFIX) == ent->d_name + strlen(ent->d_name) - strlen(FILENAME_SUFFIX));
			if (is_profile) {
				if (ent->d_name[0] == '.') continue;
				char* name = scc_path_strip_extension(ent->d_name);
				if (name == NULL) break;	// OOM
				if (!list_add(all, name)) {
					free(name);
					break;
				}
			}
		}
		closedir (dir);
	}
	list_sort(all, sort_fn);
	
	// Generate menu items
	const char* last = NULL;
	FOREACH_IN(char*, name, all) {
		if ((last != NULL) && (0 == strcmp(last, name)))
			continue;
		last = name;
		Parameter* profile = profile = scc_new_string_parameter(name);
		if (profile == NULL)
			break;
		Parameter* action_params[] = { profile };
		ActionOE action = scc_action_new_from_array("profile", 1, action_params);
		RC_REL(profile);
		if ((action.error->flag & AF_ERROR) != 0) {
			RC_REL(action.error);
			break;
		}
		if (!ctx->add_action(ctx, name, NULL, action.action)) {
			RC_REL(action.action);
			break;
		}
	}
	
	// Deallocate
	FOREACH_IN(char*, name, all) {
		free(name);
	}
	list_free(all);
}

