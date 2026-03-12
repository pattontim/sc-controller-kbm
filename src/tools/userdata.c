/**
 * SC-Controller - Userdata
 *
 * Methods for searching for stuff.
 */
#include "scc/utils/logging.h"
#include "scc/utils/traceback.h"
#include "scc/utils/strbuilder.h"
#include "scc/tools.h"
#include <dirent.h>
#include <unistd.h>
#include <errno.h>


#ifdef _WIN32
#define SEP "\\"
#else
#define SEP "/"
#endif

/**  
 * Searches for a file in multiple directories with specified extension.  
 *   
 * @param paths Array of directory paths to search in order  
 * @param path_count Number of paths in the array  
 * @param name Base name of the file to search for (without extension)  
 * @param extension File extension to append (including dot if needed)  
 * @return Newly allocated string with full path if found, NULL otherwise  
 *   
 * The caller is responsible for freeing the returned string.  
 * Searches paths in order and returns first match found.  
 */  
static char* find_stuff(const char* paths[], size_t path_count, const char* name, const char* extension) {
	if (name == NULL) return NULL;
	StrBuilder* sb = strbuilder_new();
	if (sb == NULL) return NULL;
	for (size_t i=0; i<path_count; i++) {
		strbuilder_add(sb, paths[i]);
		strbuilder_add_path(sb, name);
		strbuilder_add(sb, extension);
		
		if (!strbuilder_failed(sb)) {
			if (access(strbuilder_get_value(sb), F_OK) != -1)
				return strbuilder_consume(sb);
		}
		
		strbuilder_clear(sb);
	}

	strbuilder_free(sb);
	return NULL;
}

/**  
 * Finds a profile file by name.  
 *   
 * @param name Name of the profile to search for (without .sccprofile extension)  
 * @return Newly allocated string with full path to profile file, or NULL if not found  
 *   
 * Searches in user profiles directory first, then in system default profiles directory.  
 * The caller is responsible for freeing the returned string.  
 *   
 */  
char* scc_find_profile(const char* name) {
	const char* paths[] = {
		scc_get_profiles_path(),
		scc_get_default_profiles_path()
	};
	for (size_t i=0; i<sizeof(paths) / sizeof(char*); i++) {
		char* path = strbuilder_fmt("%s" SEP "%s.sccprofile", paths[i], name);
		if (path == NULL){
			DEBUG("Failed to allocate path string for profile '%s' in directory '%s'", name, paths[i]);
			continue;
		}

		if (access(path, F_OK) == 0){
				DEBUG("Found profile '%s' at path '%s'", name, path);
				
		} else {
				DEBUG("Profile '%s' not found at path '%s': %s", name, path, strerror(errno));
		}
		free(path);
	}
	return find_stuff(paths, sizeof(paths) / sizeof(char*), name, ".sccprofile");
}

/**  
 * Finds a menu file by name.  
 *   
 * @param name Name of the menu to search for  
 * @return Newly allocated string with full path to menu file, or NULL if not found  
 *   
 * Searches in user menus directory first, then in system default menus directory.  
 * The caller is responsible for freeing the returned string.  
 *   
 */  
char* scc_find_menu(const char* name) {
	const char* paths[] = {
		scc_get_menus_path(),
		scc_get_default_menus_path()
	};
	return find_stuff(paths, sizeof(paths) / sizeof(char*), name, "");
}
