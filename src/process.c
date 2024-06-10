#include <linux/limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <luajit.h>
#include <lualib.h>

#include "auto-splitter.h"
#include "process.h"

struct game_process process;
#define MAPS_CACHE_MAX_SIZE 32
ProcessMap p_maps_cache[MAPS_CACHE_MAX_SIZE];
uint32_t p_maps_cache_size = 0;

void execute_command(const char* command, char* output)
{
    char buffer[4096];
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "Error executing command: %s\n", command);
        exit(1);
    }

    while (fgets(buffer, 128, pipe) != NULL) {
        strcat(output, buffer);
    }

    pclose(pipe);
}

/*
Gets the base address of a module
if `module` equals to a nullptr, the main process is used, else it will search for the base addr of the specified module
*/
uintptr_t find_base_address(const char* module)
{
    const char* module_to_grep = module == 0 ? process.name : module;

    for (int32_t i = 0; i < p_maps_cache_size; i++) {
        const char* name = p_maps_cache[i].name;
        if (strstr(name, module_to_grep) == NULL) {
            return p_maps_cache[i].start;
        }
    }

    char path[22]; // 22 is the maximum length the path can be (strlen("/proc/4294967296/maps"))

    snprintf(path, sizeof(path), "/proc/%d/maps", process.pid);

    FILE* f = fopen(path, "r");

    if (f) {
        char current_line[PATH_MAX + 100];
        while (fgets(current_line, sizeof(current_line), f) != NULL) {
            if (strstr(current_line, module_to_grep) == NULL)
                continue;
            fclose(f);
            uintptr_t addr_start = strtoull(current_line, NULL, 16);
            if (maps_cache_cycles_value != 0 && p_maps_cache_size < MAPS_CACHE_MAX_SIZE) {
                ProcessMap map;
                if (parseMapsLine(current_line, &map)) {
                    p_maps_cache[p_maps_cache_size] = map;
                    p_maps_cache_size++;
                }
            }
            return addr_start;
        }
        fclose(f);
    }
    printf("Couldn't find base address\n");
    return 0;
}

int stock_process_id(const char* pid_command, const char* process_name)
{
    char pid_output[PATH_MAX + 100];
    pid_output[0] = '\0';

    execute_command(pid_command, pid_output);
    process.pid = strtoul(pid_output, NULL, 10);
    if (process.pid) {
        process.name = process_name;
        return 1;
    }
    return 0;
}

int find_process_id(char process_names[100][256], int num_process_names)
{
    int found = 0;

    while (atomic_load(&auto_splitter_enabled)) {
        printf("\r\033[KSearching for processes: ");
        for (int i = 0; i < num_process_names; i++) {
            printf("%s", process_names[i]);
            if (i < num_process_names - 1) {
                printf(", ");
            }
        }
        fflush(stdout);

        for (int i = 0; i < num_process_names; i++) {
            char command[256];
            snprintf(command, sizeof(command), "pgrep \"%.*s\"", (int)strnlen(process_names[i], 15), process_names[i]);

            if (stock_process_id(command, process_names[i])) {
                found = 1;
                break;
            }
        }

        if (found) {
            printf("\r\033[KProcess: %s\n", process.name);
            printf("PID: %u\n", process.pid);
            process.base_address = find_base_address(NULL);
            process.dll_address = process.base_address;
            return 1;
        } else {
            usleep(100000); // Sleep for 100ms before retrying
        }
    }
    return 0;
}

int getPid(lua_State* L)
{
    lua_pushinteger(L, process.pid);
    return 1;
}

int process_exists()
{
    int result = kill(process.pid, 0);
    return result == 0;
}

bool parseMapsLine(char* line, ProcessMap* map)
{
    size_t end;
    char mode[8];
    unsigned long offset;
    unsigned int major_id, minor_id, node_id;

    // Thank you kernel source code
    int sscanf_res = sscanf(line, "%lx-%lx %7s %lx %u:%u %u %s", &map->start,
        &end, mode, &offset, &major_id,
        &minor_id, &node_id, map->name);
    if (!sscanf_res)
        return false;

    return true;
}
