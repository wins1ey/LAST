#ifndef __MEMORY_H__
#define __MEMORY_H__

#include <luajit.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/uio.h>

#define MAX_OFFSETS 10
#define MAX_MEMORY_TABLES 100

typedef struct {
    char* name;
    char* type;
    char* module;
    uint64_t address;
    int offsets[MAX_OFFSETS];
    int offset_count;
} MemoryTable;

ssize_t process_vm_readv(int pid, struct iovec* mem_local, int liovcnt, struct iovec* mem_remote, int riovcnt, int flags);
int store_memory_tables(lua_State* L, const char* version);
void read_address(lua_State* L);

#endif /* __MEMORY_H__ */
