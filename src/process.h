#ifndef __PROCESS_H__
#define __PROCESS_H__

#include <linux/limits.h>
#include <stdbool.h>
#include <stdint.h>

#include <luajit.h>
#include <lualib.h>

struct game_process {
    const char* name;
    int pid;
    uintptr_t base_address;
    uintptr_t dll_address;
};
typedef struct game_process game_process;

typedef struct ProcessMap {
    uint64_t start;
    char name[PATH_MAX];
} ProcessMap;
extern uint32_t p_maps_cache_size;

uintptr_t find_base_address(const char* module);
int process_exists();
int find_process_id(char process_names[100][256], int num_process_names);
int getPid(lua_State* L);
bool parseMapsLine(char* line, ProcessMap* map);

#endif /* __PROCESS_H__ */
