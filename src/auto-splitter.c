#include <linux/limits.h>
#include <pthread.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <lauxlib.h>
#include <luajit.h>
#include <lualib.h>

#include "auto-splitter.h"
#include "memory.h"
#include "process.h"
#include "settings.h"

char auto_splitter_file[PATH_MAX];
int refresh_rate = 60;
int maps_cache_cycles = 0; // 0=off, 1=current cycle, +1=multiple cycles
int maps_cache_cycles_value = 0; // same as `maps_cache_cycles` but this one represents the current value rather than the reference from the script
atomic_bool timer_started = false;
atomic_bool auto_splitter_enabled = true;
atomic_bool call_start = false;
atomic_bool call_on_start = false;
atomic_bool call_split = false;
atomic_bool call_on_split = false;
atomic_bool toggle_loading = false;
atomic_bool call_reset = false;
atomic_bool call_on_reset = false;
bool prev_is_loading;

static const char* disabled_functions[] = {
    "collectgarbage",
    "dofile",
    "getmetatable",
    "setmetatable",
    "getfenv",
    "setfenv",
    "load",
    "loadfile",
    "loadstring",
    "rawequal",
    "rawget",
    "rawset",
    "module",
    "require",
    "newproxy",
};

extern game_process process;

// I have no idea how this works
// https://stackoverflow.com/a/2336245
static void mkdir_p(const char* dir, __mode_t permissions)
{
    char tmp[256] = { 0 };
    char* p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, permissions);
            *p = '/';
        }
    mkdir(tmp, permissions);
}

void check_directories()
{
    char libresplit_directory[PATH_MAX] = { 0 };
    get_libresplit_folder_path(libresplit_directory);

    char auto_splitters_directory[PATH_MAX];
    char themes_directory[PATH_MAX];
    char splits_directory[PATH_MAX];

    strcpy(auto_splitters_directory, libresplit_directory);
    strcat(auto_splitters_directory, "/auto-splitters");

    strcpy(themes_directory, libresplit_directory);
    strcat(themes_directory, "/themes");

    strcpy(splits_directory, libresplit_directory);
    strcat(splits_directory, "/splits");

    // Make the libresplit directory if it doesn't exist
    mkdir_p(libresplit_directory, 0755);

    // Make the autosplitters directory if it doesn't exist
    if (mkdir(auto_splitters_directory, 0755) == -1) {
        // Directory already exists or there was an error
    }

    // Make the themes directory if it doesn't exist
    if (mkdir(themes_directory, 0755) == -1) {
        // Directory already exists or there was an error
    }

    // Make the splits directory if it doesn't exist
    if (mkdir(splits_directory, 0755) == -1) {
        // Directory already exists or there was an error
    }
}

static const luaL_Reg lj_lib_load[] = {
    { "", luaopen_base },
    { LUA_STRLIBNAME, luaopen_string },
    { LUA_MATHLIBNAME, luaopen_math },
    { LUA_BITLIBNAME, luaopen_bit },
    { LUA_JITLIBNAME, luaopen_jit },
    { NULL, NULL }
};

LUALIB_API void luaL_openlibs(lua_State* L)
{
    const luaL_Reg* lib;
    for (lib = lj_lib_load; lib->func; lib++) {
        lua_pushcfunction(L, lib->func);
        lua_pushstring(L, lib->name);
        lua_call(L, 1, 0);
    }
}

void disable_functions(lua_State* L, const char** functions)
{
    for (int i = 0; functions[i] != NULL; i++) {
        lua_pushnil(L);
        lua_setglobal(L, functions[i]);
    }
}

/*
    Generic function to call lua functions
    Signatures are something like `disb>s`
    1. d = double
    2. i = int
    3. s = string
    4. b = boolean
    5. > = return separator

    Example: `call_va("functionName", "dd>d", x, y, &z);`
*/
bool call_va(lua_State* L, const char* func, const char* sig, ...)
{
    va_list vl;
    int narg, nres; /* number of arguments and results */

    va_start(vl, sig);
    lua_getglobal(L, func); /* get function */

    /* push arguments */
    narg = 0;
    while (*sig) { /* push arguments */
        switch (*sig++) {
            case 'd': /* double argument */
                lua_pushnumber(L, va_arg(vl, double));
                break;

            case 'i': /* int argument */
                lua_pushnumber(L, va_arg(vl, int));
                break;

            case 's': /* string argument */
                lua_pushstring(L, va_arg(vl, char*));
                break;

            case 'b':
                lua_pushboolean(L, va_arg(vl, int));
                break;

            case '>':
                break;

            default:
                printf("invalid option (%c)\n", *(sig - 1));
                return false;
        }
        if (*(sig - 1) == '>')
            break;
        narg++;
        luaL_checkstack(L, 1, "too many arguments");
    }

    /* do the call */
    nres = strlen(sig); /* number of expected results */
    if (lua_pcall(L, narg, nres, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        printf("error running function '%s': %s\n", func, err);
        return false;
    }

    /* retrieve results */
    nres = -nres; /* stack index of first result */
    /* check if there's a return value */
    if (!lua_isnil(L, nres)) {
        while (*sig) { /* get results */
            switch (*sig++) {
                case 'd': /* double result */
                    if (!lua_isnumber(L, nres)) {
                        printf("function '%s' wrong result type, expected double\n", func);
                        return false;
                    }
                    *va_arg(vl, double*) = lua_tonumber(L, nres);
                    break;

                case 'i': /* int result */
                    if (!lua_isnumber(L, nres)) {
                        printf("function '%s' wrong result type, expected int\n", func);
                        return false;
                    }
                    *va_arg(vl, int*) = (int)lua_tonumber(L, nres);
                    break;

                case 's': /* string result */
                    if (!lua_isstring(L, nres)) {
                        printf("function '%s' wrong result type, expected string\n", func);
                        return false;
                    }
                    *va_arg(vl, const char**) = lua_tostring(L, nres);
                    break;

                case 'b':
                    if (!lua_isboolean(L, nres)) {
                        printf("function '%s' wrong result type, expected boolean\n", func);
                        return false;
                    }
                    *va_arg(vl, bool*) = lua_toboolean(L, nres);
                    break;

                default:
                    printf("invalid option (%c)\n", *(sig - 1));
                    return false;
            }
            nres++;
        }
    } else {
        va_end(vl);
        return false;
    }
    va_end(vl);
    return true;
}

int get_process_names(lua_State* L, char process_names[100][256], int* num_process_names)
{
    lua_getglobal(L, "State");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_istable(L, -1)) {
                const char* process_name = lua_tostring(L, -2);

                strncpy(process_names[*num_process_names], process_name, 255);
                process_names[*num_process_names][255] = '\0';
                (*num_process_names)++;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return *num_process_names;
}

void startup(lua_State* L)
{
    call_va(L, "Startup", "");

    lua_getglobal(L, "RefreshRate");
    if (lua_isnumber(L, -1)) {
        refresh_rate = lua_tointeger(L, -1);
    }
    lua_pop(L, 1); // Remove 'refreshRate' from the stack

    lua_getglobal(L, "MapsCacheCycles");
    if (lua_isnumber(L, -1)) {
        maps_cache_cycles = lua_tointeger(L, -1);
        maps_cache_cycles_value = maps_cache_cycles;
    }
    lua_pop(L, 1); // Remove 'mapsCacheCycles' from the stack
}

bool update(lua_State* L)
{
    bool ret;
    if (call_va(L, "Update", ">b", &ret)) {
        lua_pop(L, 1);
        return ret;
    }
    lua_pop(L, 1);
    return true;
}

bool start(lua_State* L)
{
    bool ret;
    if (call_va(L, "Start", ">b", &ret) && ret) {
        atomic_store(&call_start, true);
        printf("Start: true\n");
        lua_pop(L, 1);
        return true;
    }
    lua_pop(L, 1); // Remove the return value from the stack
    return false;
}

bool split(lua_State* L)
{
    bool ret;
    if (call_va(L, "Split", ">b", &ret) && ret) {
        atomic_store(&call_split, true);
        printf("Split: true\n");
        lua_pop(L, 1);
        return true;
    }
    lua_pop(L, 1); // Remove the return value from the stack
    return false;
}

void is_loading(lua_State* L)
{
    bool ret;
    if (call_va(L, "IsLoading", ">b", &ret)) {
        if (ret != prev_is_loading) {
            atomic_store(&toggle_loading, true);
            printf("IsLoading: %s\n", ret ? "true" : "false");
            prev_is_loading = !prev_is_loading;
        }
    }
    lua_pop(L, 1); // Remove the return value from the stack
}

bool reset(lua_State* L)
{
    bool ret;
    if (call_va(L, "Reset", ">b", &ret) && ret) {
        atomic_store(&call_reset, true);
        printf("Reset: true\n");
        lua_pop(L, 1);
        return true;
    }
    lua_pop(L, 1);
    return false;
}

const char* version(lua_State* L)
{
    lua_getglobal(L, "Version");
    if (lua_isstring(L, -1)) {
        const char* version = lua_tostring(L, -1);
        lua_pop(L, 1);
        printf("Version: %s\n", version);
        return version;
    }
    lua_pop(L, 1);
    return NULL;
}

bool lua_function_exists(lua_State* L, const char* func_name)
{
    lua_getglobal(L, func_name);
    bool exists = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return exists;
}

void run_auto_splitter_cycle(
    lua_State* L,
    bool memory_map_exists,
    bool start_exists,
    bool on_start_exists,
    bool split_exists,
    bool on_split_exists,
    bool is_loading_exists,
    bool reset_exists,
    bool on_reset_exists,
    bool update_exists)
{
    if ((update_exists && !update(L)) || !memory_map_exists) {
        return;
    }

    read_address(L);

    if (is_loading_exists) {
        is_loading(L);
    }

    if (!atomic_load(&timer_started)) {
        if (start_exists) {
            start(L);
        }
    } else if (reset_exists) {
        reset(L);
    } else if (split_exists) {
        split(L);
    }

    if (on_start_exists && atomic_load(&call_on_start)) {
        call_va(L, "OnStart", "");
        atomic_store(&call_on_start, false);
    }

    if (on_split_exists && atomic_load(&call_on_split)) {
        call_va(L, "OnSplit", "");
        atomic_store(&call_on_split, false);
    }

    if (on_reset_exists && atomic_load(&call_on_reset)) {
        call_va(L, "OnReset", "");
        atomic_store(&call_on_reset, false);
    }

    // Clear the memory maps cache if needed
    maps_cache_cycles_value--;
    if (maps_cache_cycles_value < 1) {
        p_maps_cache_size = 0; // We dont need to "empty" the list as the elements after index 0 are considered invalid
        maps_cache_cycles_value = maps_cache_cycles;
        // printf("Cleared maps cache\n");
    }
}

void run_auto_splitter()
{
    lua_State* L = luaL_newstate();

    // Load the Lua file
    if (luaL_loadfile(L, auto_splitter_file) != LUA_OK) {
        fprintf(stderr, "Lua syntax error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        atomic_store(&auto_splitter_enabled, false);
        return;
    }

    // Execute the Lua file
    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        fprintf(stderr, "Lua runtime error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        atomic_store(&auto_splitter_enabled, false);
        return;
    }

    luaL_openlibs(L);
    disable_functions(L, disabled_functions);
    lua_pushcfunction(L, getPid);
    lua_setglobal(L, "getPID");
    lua_newtable(L);
    lua_setglobal(L, "vars");

    char current_file[PATH_MAX];
    strcpy(current_file, auto_splitter_file);

    bool start_exists = lua_function_exists(L, "Start");
    bool on_start_exists = lua_function_exists(L, "OnStart");
    bool split_exists = lua_function_exists(L, "Split");
    bool on_split_exists = lua_function_exists(L, "OnSplit");
    bool is_loading_exists = lua_function_exists(L, "IsLoading");
    bool startup_exists = lua_function_exists(L, "Startup");
    bool reset_exists = lua_function_exists(L, "Reset");
    bool on_reset_exists = lua_function_exists(L, "OnReset");
    bool update_exists = lua_function_exists(L, "Update");
    bool init_exists = lua_function_exists(L, "Init");
    bool memory_map_exists = false;
    atomic_store(&call_on_start, false);
    atomic_store(&call_on_split, false);
    atomic_store(&call_on_reset, false);

    if (startup_exists) {
        startup(L);
    }

    printf("Refresh rate: %d\n", refresh_rate);
    int rate = 1000000 / refresh_rate;

    char process_names[100][256];
    int num_process_names = 0;
    const char* version_str;
    if (get_process_names(L, process_names, &num_process_names)) {
        if (find_process_id(process_names, num_process_names)) {
            if (init_exists) {
                call_va(L, "Init", "");
            }
            lua_newtable(L);
            lua_setglobal(L, "old");
            lua_newtable(L);
            lua_setglobal(L, "current");
            version_str = version(L);
            memory_map_exists = store_memory_tables(L, version_str);
        }
    }

    while (1) {
        struct timespec clock_start;
        clock_gettime(CLOCK_MONOTONIC, &clock_start);

        if (!process_exists() && find_process_id(process_names, num_process_names) && init_exists) {
            call_va(L, "Init", "");
        } else if (!atomic_load(&auto_splitter_enabled) || strcmp(current_file, auto_splitter_file) != 0) {
            break;
        }

        run_auto_splitter_cycle(
            L,
            memory_map_exists,
            start_exists,
            on_start_exists,
            split_exists,
            on_split_exists,
            is_loading_exists,
            reset_exists,
            on_reset_exists,
            update_exists);

        struct timespec clock_end;
        clock_gettime(CLOCK_MONOTONIC, &clock_end);
        long long duration = (clock_end.tv_sec - clock_start.tv_sec) * 1000000 + (clock_end.tv_nsec - clock_start.tv_nsec) / 1000;
        // printf("duration: %llu\n", duration);
        if (duration < rate) {
            usleep(rate - duration);
        }
    }

    lua_close(L);
}
