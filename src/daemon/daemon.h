/**
 * SC Controller - Daemon - Internal Core API
 *
 * This header defines the internal API of the SC Controller daemon, which
 * orchestrates the entire device management, input processing, and client
 * communication system. The daemon accepts input from physical controllers,
 * applies profiles and actions to map that input, and outputs to virtual
 * devices (keyboard, mouse, gamepad).
 *
 * Major subsystems managed here:
 * - Lifecycle: sccd_start(), sccd_exit(), daemon initialization
 * - Clients: Socket communication, client command dispatch
 * - Logging: Centralized log system with client subscribers
 * - Scheduling: Timer-based task scheduling (sccd_scheduler_*)
 * - Input/Controllers: Device monitoring and hotplug detection
 * - Input Drivers: Platform-specific device backends (HIDAPI, LibUSB, DInput, BSD)
 * - Mappers: Maps profiles onto physical controllers
 * - Locking/Observing: Allows clients to lock/observe action sources
 * - Special Features: CemuHook UDP motion provider, X11 integration
 *
 * This is internal to the daemon and not exposed to the public libscc API.
 *
 * Thread Safety: Daemon is single-threaded. All operations are synchronous.
 *                No synchronization primitives are needed internally.
 */
#pragma once

#include "scc/utils/strbuilder.h"
#include "scc/utils/list.h"
#include "scc/input_test.h"
#include "scc/driver.h"
#include "scc/mapper.h"
#include <sys/time.h>
#include <stdbool.h>
#include "version.h"
#ifdef _WIN32
#include "scc/input_device.h"
#endif

#define MIN_SLEEP_TIME 10 /** ms */
#define CLIENT_BUFFER_SIZE 10240
#define SCCD_OOM ((void*)-1) /* used as possible return value in some cases */

typedef struct SCCDMapper SCCDMapper;

typedef struct Client {
	int				fd;
	Mapper*			mapper;		// Mapper (controller) assigned by client
	char*			tag;		// last tag sent by client or NULL
	bool			should_be_dropped;
	char			buffer[CLIENT_BUFFER_SIZE + 1];
	char*			next;		// as in 'place in buffer I'll write next time'
} Client;

typedef struct ErrorData {
	uint16_t		id;
	char*			message;
	bool			fatal;
} ErrorData;

enum SpecialClientType {
	SCT_OSD,
	SCT_AUTOSWITCH
};

typedef LIST_TYPE(ErrorData) ErrorList;
typedef LIST_TYPE(Controller) ControllerList;

typedef void (*sccd_scheduler_cb_internal)(void* parent, void* userdata);

/** Initializes and starts the daemon main loop. Returns 0 on success */
int sccd_start();
/** Sets the daemon's process title visible in 'ps' output */
void sccd_set_proctitle(const char* name);
/** Sets the default profile to load when daemon starts */
void sccd_set_default_profile(const char* profile);
/** Terminates the daemon and shuts down all subsystems */
void sccd_exit();

extern bool sccd_no_bt;
extern bool sccd_silent;

/** Adds an error to the daemon's error list. Returns a unique error ID for later removal */
intptr_t sccd_error_add(const char* message, bool fatal);
/** Removes a previously added error from the daemon's error list */
void sccd_error_remove(intptr_t id);
/** Retrieves the list of all current daemon errors */
ErrorList sccd_get_errors();
/** Returns true on success, false if profile file cannot be found or loaded */
bool sccd_set_profile(Mapper* m, const char* filename);
/**
 * Returns filename of 'main' profile, one that's loaded on 1st controller
 * or just held in memory waiting for controller to be connected.
 * Returns NULL if no default profile has been set.
 */
const char* sccd_get_current_profile();

/** Initializes the logging system. If 'log_to_stdout' is true, also logs to console */
void sccd_logger_init(bool log_to_stdout);
/** Returns entire accumulated log as one giant string. Caller should not free the result */
const char* sccd_logger_get_log();
/**
 * Adds client to list of clients receiving log messages in real-time.
 * Returns false if memory cannot be allocated.
 */
bool sccd_logger_client_add(Client* c);
/** Removes a client from receiving log messages */
void sccd_logger_client_remove(Client* c);

/** Initializes the file descriptor polling system for input handling */
void sccd_poller_init();
/** Registers a file descriptor to be polled for input with a callback */
bool sccd_poller_add(int fd, sccd_poller_cb cb, void* userdata);
/** Unregisters a file descriptor from polling */
void sccd_poller_remove(int fd);
/** Closes the polling system and releases all resources */
void sccd_poller_close();

/** Initializes the task scheduling system (timers/delays) */
void sccd_scheduler_init();
/** Closes the scheduler and cancels all pending tasks */
void sccd_scheduler_close();
/**
 * Schedules a callback function to be called after 'timeout' milliseconds.
 * Returns a TaskID that can be used to cancel the task, or 0 on allocation failure.
 */
TaskID sccd_scheduler_schedule(uint32_t timeout, sccd_scheduler_cb_internal cb, void* parent, void* userdata);
/** Cancels a previously scheduled task. Does nothing if TaskID is invalid */
void sccd_scheduler_cancel(TaskID id);
/** Returns the user data associated with a scheduled task, or NULL if not found */
void* sccd_scheduler_get_user_data(TaskID id);
/**
 * Calculates how long the daemon can safely sleep without delaying pending tasks.
 * Sets 't' to MIN_SLEEP_TIME if no tasks are pending.
 */
void sccd_scheduler_get_sleep_time(struct timeval* t);

/** Returns false on failure */
bool sccd_socket_init();
/**
 * Sends a string to a client via socket.
 * Note: No newline is appended; caller must include one to conform to protocol.
 *
 * If 'str' is NULL, logs an error and marks the client for dropping.
 * If client is already marked for dropping, the message is discarded.
 */
void sccd_socket_send(Client* client, const char* str);
/** Like sccd_socket_send, but also deallocates the string after sending */
void sccd_socket_consume(Client* client, char* str);
/** Broadcasts a message to all connected clients */
void sccd_socket_send_to_all(const char* str);
/** Calls the callback function once for each connected client */
void sccd_clients_for_all(void (*cb)(Client* c));
/** Processes and executes a command received from a client */
void sccd_on_client_command(Client* client, char* buffer, size_t len);
/** Sends the list of available controllers to a client */
void sccd_send_controller_list(Client* client);
/** Sends the list of available profiles to a client */
void sccd_send_profile_list(Client* client);
/**
 * Schedules a client to be dropped (disconnected).
 * Safer than immediate disconnection, as code can still send messages to it
 * (they will be discarded) without worrying about use-after-free bugs.
 */
void sccd_drop_client_asap(Client* client);

/** Starts the CemuHook UDP motion provider server for gyro/motion data streaming */
bool sccd_cemuhook_socket_enable();
/** Feeds rotation data (6D: accel + gyro) to CemuHook clients */
bool sccd_cemuhook_feed(int index, float data[6]);

/** Initializes connection to X11 display server. On Windows, this is a no-op */
void sccd_x11_init();
/** Closes X11 display connection and releases resources */
void sccd_x11_close();
/**
 * Returns the open X11 display connection.
 * Returns NULL on Windows, Android, or on *nix systems if connection failed.
 */
void* sccd_x11_get_display();

/** Initializes the device monitor (controller hotplug detection) */
void sccd_device_monitor_init();
/** Platform-independent initialization for device monitor */
void sccd_device_monitor_common_init();
/** Starts the device monitor and begins detecting device hotplug events */
void sccd_device_monitor_start();
/** Forces a rescan of connected devices */
void sccd_device_monitor_rescan();
/** Closes the device monitor and stops device detection */
void sccd_device_monitor_close();
/** Platform-independent cleanup for device monitor */
void sccd_device_monitor_close_common();
/**
 * Registers a callback to be invoked when devices matching a subsystem are hotplugged.
 * Returns true on success, false on allocation failure.
 */
bool sccd_register_hotplug_cb(Subsystem sys, sccd_hotplug_cb cb, const HotplugFilter* filters, ...);

#ifdef __BSD__
/** Opens an input device on BSD systems. Returns NULL on failure */
InputDevice* sccd_input_bsd_open(const char* syspath);
/** Initializes BSD input subsystem */
void sccd_input_bsd_init();
/** Closes BSD input subsystem and releases resources */
void sccd_input_bsd_close();
#else
/** Opens an input device via LibUSB. Returns NULL on failure */
InputDevice* sccd_input_libusb_open(const char* syspath);
/** Initializes LibUSB input subsystem */
void sccd_input_libusb_init();
/** Closes LibUSB input subsystem and releases resources */
void sccd_input_libusb_close();
#endif
#ifdef USE_HIDAPI
/** Opens an input device via HIDAPI. Returns NULL on failure */
InputDevice* sccd_input_hidapi_open(const char* syspath);
/** Forces a rescan of HIDAPI devices */
void sccd_input_hidapi_rescan();
/** Closes HIDAPI input subsystem and releases resources */
void sccd_input_hidapi_close();
/** Initializes HIDAPI input subsystem */
void sccd_input_hidapi_init();
#endif
#ifdef USE_DINPUT
/** Opens an input device via DirectInput (Windows). Returns NULL on failure */
InputDevice* sccd_input_dinput_open(const InputDeviceData* idev);
/** Forces a rescan of DirectInput devices (Windows) */
void sccd_input_dinput_rescan();
/** Closes DirectInput input subsystem (Windows) and releases resources */
void sccd_input_dinput_close();
/** Initializes DirectInput input subsystem (Windows) */
void sccd_input_dinput_init();
#endif
/** Internal: Called by device monitor when a new device is detected */
void sccd_device_monitor_new_device(Daemon* d, const InputDeviceData* idata);
/** Internal: Called by device monitor when a device is removed */
void sccd_device_monitor_device_removed(Daemon* d, const char* path);
/** Tests whether a device matches a hotplug filter. Returns true if it matches */
bool sccd_device_monitor_test_filter(Daemon* d, const InputDeviceData* data, const HotplugFilter* filter);
/**
 * Returns a bitmask of enabled subsystems.
 * A subsystem is enabled if at least one hotplug callback is registered for it.
 */
uint32_t sccd_device_monitor_get_enabled_subsystems(Daemon* d);

#ifdef _WIN32
struct Win32InputDeviceData {
	InputDeviceData		idev;
	Vendor				vendor;
	Product				product;
	uint8_t				bus;
	uint8_t				dev;
	int					idx;
	void*				d8dev;
};
/** Internal: Fills a Win32-specific input device data structure */
void sccd_device_monitor_win32_fill_struct(struct Win32InputDeviceData* wdev);
#endif

/** Forces a rescan of LibUSB devices (Windows) */
void sccd_input_libusb_rescan();


/** Returns a special client (OSD daemon or autoswitch daemon), or NULL if not registered */
Client* sccd_get_special_client(enum SpecialClientType t);
/** Registers a special client, dropping any previously registered one */
void sccd_set_special_client(enum SpecialClientType t, Client* client);

/** Controls which drivers are initialized */
enum DirverInitMode {
	/** Initialize all available drivers (used by main daemon) */
	DIMODE_ALL =					1,
	/**
	 * Initialize only drivers with 'list_devices' method
	 * (used by scc-input-tester utility)
	 */
	DIMODE_LIST_DEVICES_ONLY =		2
};

/** Loads and initializes available input drivers based on the mode */
void sccd_drivers_init(Daemon* daemon, enum DirverInitMode mode);
/** Asks input drivers to enumerate available devices. Used by scc-input-tester */
void sccd_drivers_list_devices(Daemon* daemon, const controller_available_cb cb);
/** Returns a driver by name, or NULL if not found */
Driver* sccd_drivers_get_by_name(const char* driver_name);

/** Returns the list of all controllers currently connected */
ControllerList sccd_get_controller_list();
/** Returns a controller by its unique ID, or NULL if not found */
Controller* sccd_get_controller_by_id(const char* id);

/**
 * Locks specified action sources for exclusive use by a client.
 * Returns NULL on success, or the name of a source that could not be locked.
 * May return SCCD_OOM in case of out-of-memory error.
 *
 * When sources are locked on a mapper's profile, the profile is automatically
 * wrapped in a LockProfile to prevent accidental removal of the locking mechanism.
 */
const char* sccd_lock_actions(Client* c, StringList sources);
/** Unlocks all action sources previously locked by a client */
void sccd_unlock_actions(Client* c);

/**
 * Registers a client to observe (monitor) specified action sources.
 * Returns NULL on success, or the name of a source that could not be observed.
 * May return SCCD_OOM in case of out-of-memory error.
 */
const char* sccd_observe_actions(Client* c, StringList sources);

/**
 * When actions are locked on a mapper, its profile is wrapped in LockProfile.
 * This function changes the wrapped child profile without removing the wrapper.
 * Used when reloading profiles without losing the lock.
 */
void sccd_change_locked_profile(Profile* p, Profile* child);
/** Returns true if a profile is a LockProfile wrapper */
bool sccd_is_locked_profile(Profile* p);

/** Allocates a new SCCDMapper instance. Returns NULL on allocation failure */
SCCDMapper* sccd_mapper_create();
/** CastsSCCDMapper to generic Mapper interface */
Mapper* sccd_mapper_to_mapper(SCCDMapper* m);
/** Casts generic Mapper to SCCDMapper, or returns NULL if wrong type */
SCCDMapper* sccd_mapper_to_sccd_mapper(Mapper* m);
/** Returns the default mapper (used for the primary controller) */
SCCDMapper* sccd_get_default_mapper();
/** Deallocates a mapper and releases all its resources */
void sccd_mapper_deallocate(SCCDMapper* m);
/** Flushes all pending output from a mapper to virtual devices */
void sccd_mapper_flush(SCCDMapper* m);
/** Sets the profile filename for a mapper. Returns false on allocation failure */
bool sccd_mapper_set_profile_filename(SCCDMapper* m, const char* filename);
/** Returns the profile filename for a mapper, or NULL if not set */
const char* sccd_mapper_get_profile_filename(SCCDMapper* m);

/**
 * Determines which profile should be used for a controller.
 * Returns NULL if mapper for controller cannot be determined.
 * Returns the string "None" if mapper has no profile assigned.
 */
const char* get_profile_for_controller(Controller* c);

/**
 * Reads a hexadecimal or decimal number from a sysfs file.
 * Used for parsing /sys/.../idVendor, idProduct, devnum, and busnum.
 * Returns -1 on failure.
 * Base: 0 (auto-detect), 16 (hex), 10 (decimal), etc.
 */
long int read_long_from_file(const char* filename, int base);

/** For internal use only: Returns the daemon instance */
Daemon* get_daemon();
