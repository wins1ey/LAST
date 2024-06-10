#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

#include <luajit.h>

#include "memory.h"
#include "process.h"

MemoryTable memory_tables[MAX_MEMORY_TABLES];
int memory_table_count = 0;
extern game_process process;

#define READ_MEMORY_FUNCTION(value_type)                                                         \
    value_type read_memory_##value_type(uint64_t mem_address, int32_t* err)                      \
    {                                                                                            \
        value_type value;                                                                        \
                                                                                                 \
        struct iovec mem_local;                                                                  \
        struct iovec mem_remote;                                                                 \
                                                                                                 \
        mem_local.iov_base = &value;                                                             \
        mem_local.iov_len = sizeof(value);                                                       \
        mem_remote.iov_len = sizeof(value);                                                      \
        mem_remote.iov_base = (void*)(uintptr_t)mem_address;                                     \
                                                                                                 \
        ssize_t mem_n_read = process_vm_readv(process.pid, &mem_local, 1, &mem_remote, 1, 0);    \
        if (mem_n_read == -1) {                                                                  \
            *err = (int32_t)errno;                                                               \
        } else if (mem_n_read != (ssize_t)mem_remote.iov_len) {                                  \
            printf("Error reading process memory: short read of %ld bytes\n", (long)mem_n_read); \
            exit(1);                                                                             \
        }                                                                                        \
                                                                                                 \
        return value;                                                                            \
    }

READ_MEMORY_FUNCTION(int8_t)
READ_MEMORY_FUNCTION(uint8_t)
READ_MEMORY_FUNCTION(int16_t)
READ_MEMORY_FUNCTION(uint16_t)
READ_MEMORY_FUNCTION(int32_t)
READ_MEMORY_FUNCTION(uint32_t)
READ_MEMORY_FUNCTION(int64_t)
READ_MEMORY_FUNCTION(uint64_t)
READ_MEMORY_FUNCTION(float)
READ_MEMORY_FUNCTION(double)
READ_MEMORY_FUNCTION(bool)

char* read_memory_string(uint64_t mem_address, int buffer_size, int32_t* err)
{
    char* buffer = (char*)malloc(buffer_size);
    if (buffer == NULL) {
        // Handle memory allocation failure
        return NULL;
    }

    struct iovec mem_local;
    struct iovec mem_remote;

    mem_local.iov_base = buffer;
    mem_local.iov_len = buffer_size;
    mem_remote.iov_len = buffer_size;
    mem_remote.iov_base = (void*)(uintptr_t)mem_address;

    ssize_t mem_n_read = process_vm_readv(process.pid, &mem_local, 1, &mem_remote, 1, 0);
    if (mem_n_read == -1) {
        buffer[0] = '\0';
        *err = (int32_t)errno;
    } else if (mem_n_read != (ssize_t)mem_remote.iov_len) {
        printf("Error reading process memory: short read of %ld bytes\n", (long)mem_n_read);
        exit(1);
    }

    return buffer;
}

/*
    Prints the according error to stdout
    True if the error was printed
    False if the error is unknown
*/
bool handle_memory_error(uint32_t err, const char* table_name)
{
    if (err == 0)
        return false;
    printf("%s\t", table_name);
    switch (err) {
        case EFAULT:
            printf("EFAULT: Invalid memory space/address\n");
            break;
        case EINVAL:
            printf("EINVAL: An error occurred while reading memory\n");
            break;
        case ENOMEM:
            printf("ENOMEM: Please get more memory\n");
            break;
        case EPERM:
            printf("EPERM: Permission denied\n");
            break;
        case ESRCH:
            printf("ESRCH: No process with specified PID exists\n");
            break;
    }
    return true;
}

void read_address(lua_State* L)
{
    for (int i = 0; i < memory_table_count; i++) {
        uint64_t address = 0;
        int error = 0;
        const MemoryTable* table = &memory_tables[i];

        const char* table_name = table->name;
        const char* value_type = table->type;

        if (table->module) {
            if (strcmp(process.name, table->module) != 0) {
                process.dll_address = find_base_address(table->module);
            }
            address = process.dll_address + table->address;
        } else {
            address = process.base_address + table->address;
        }

        for (int i = 0; i < table->offset_count; i++) {
            if (address <= UINT32_MAX) {
                address = read_memory_uint32_t((uint64_t)address, &error);
                if (error)
                    break;
            } else {
                address = read_memory_uint64_t(address, &error);
                if (error)
                    break;
            }
            address += table->offsets[i];
        }

        // Set the old value to the current value
        lua_getglobal(L, "current");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, table_name);
            if (!lua_isnil(L, -1)) {
                lua_getglobal(L, "old");
                lua_pushvalue(L, -2);
                lua_setfield(L, -2, table_name);
                lua_pop(L, 1); // Old table
            }
            lua_pop(L, 1); // current value or nil
        }

        // Read the new value
        if (strcmp(value_type, "sbyte") == 0) {
            int8_t value = read_memory_int8_t(address, &error);
            lua_pushinteger(L, (int)value);
        } else if (strcmp(value_type, "byte") == 0) {
            uint8_t value = read_memory_uint8_t(address, &error);
            lua_pushinteger(L, (int)value);
        } else if (strcmp(value_type, "short") == 0) {
            short value = read_memory_int16_t(address, &error);
            lua_pushinteger(L, (int)value);
        } else if (strcmp(value_type, "ushort") == 0) {
            unsigned short value = read_memory_uint16_t(address, &error);
            lua_pushinteger(L, (int)value);
        } else if (strcmp(value_type, "int") == 0) {
            int value = read_memory_int32_t(address, &error);
            lua_pushinteger(L, value);
        } else if (strcmp(value_type, "uint") == 0) {
            unsigned int value = read_memory_uint32_t(address, &error);
            lua_pushinteger(L, (int)value);
        } else if (strcmp(value_type, "long") == 0) {
            long value = read_memory_int64_t(address, &error);
            lua_pushinteger(L, (int)value);
        } else if (strcmp(value_type, "ulong") == 0) {
            unsigned long value = read_memory_uint64_t(address, &error);
            lua_pushinteger(L, (int)value);
        } else if (strcmp(value_type, "float") == 0) {
            float value = read_memory_float(address, &error);
            lua_pushnumber(L, (double)value);
        } else if (strcmp(value_type, "double") == 0) {
            double value = read_memory_double(address, &error);
            lua_pushnumber(L, value);
        } else if (strcmp(value_type, "bool") == 0) {
            bool value = read_memory_bool(address, &error);
            lua_pushboolean(L, value ? 1 : 0);
        } else if (strstr(value_type, "string") != NULL) {
            int buffer_size = atoi(value_type + 6);
            if (buffer_size < 2) {
                printf("Invalid string size, please read documentation");
                exit(1);
            }
            char* value = read_memory_string(address, buffer_size, &error);
            lua_pushstring(L, value != NULL ? value : "");
            free(value);
        } else {
            printf("Invalid value type: %s\n", value_type);
            exit(1);
        }

        if (error) {
            handle_memory_error(error, table_name);
            continue;
        }

        lua_setfield(L, -2, table_name);
        lua_pop(L, 1); // current table
    }
}

int store_memory_tables(lua_State* L, const char* version)
{
    memory_table_count = 0;

    lua_getglobal(L, "State");
    lua_getfield(L, -1, process.name);

    if (version != NULL) {
        lua_getfield(L, -1, version);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 3); // Pop the non-tables
            printf("Could not find version table %s for process %s\n", version, process.name);
            return 0;
        }
    }

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        int has_nested_table = 0;
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_istable(L, -1)) {
                    has_nested_table = 1;
                    lua_pop(L, 2); // Nested table and its key
                    break;
                }
                lua_pop(L, 1);
            }
        }

        if (!has_nested_table) {
            const char* table_name;
            if (lua_isstring(L, -2)) {
                table_name = lua_tostring(L, -2);
            } else {
                lua_pop(L, 1);
                continue;
            }
            MemoryTable* table = &memory_tables[memory_table_count++];
            table->name = strdup(table_name);
            table->type = NULL;
            table->module = NULL;
            table->address = 0;
            table->offset_count = 0;

            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_isnumber(L, -2)) {
                    int key = lua_tointeger(L, -2);
                    if (key == 1) { // Type
                        table->type = strdup(lua_tostring(L, -1));
                    } else if (key == 2) { // Module or Address
                        if (lua_isnumber(L, -1)) { // Address
                            table->address = lua_tointeger(L, -1);
                        } else if (lua_isstring(L, -1)) { // Module
                            table->module = strdup(lua_tostring(L, -1));
                        }
                    } else if (key == 3 && table->module != NULL) { // Address when module exists
                        if (lua_isnumber(L, -1)) {
                            table->address = lua_tointeger(L, -1);
                        }
                    } else if ((key > 2 && key <= MAX_OFFSETS + 2 && table->module == NULL) || (key > 3 && key <= MAX_OFFSETS + 3 && table->module != NULL)) { // Offsets
                        if (lua_isnumber(L, -1)) {
                            table->offsets[table->offset_count++] = lua_tointeger(L, -1);
                        }
                    }
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    if (version != NULL) {
        lua_pop(L, 3); // version, process, and state tables
    } else {
        lua_pop(L, 2); // process and state tables
    }

    for (int i = 0; i < memory_table_count; i++) {
        MemoryTable* table = &memory_tables[i];
        printf("%s ", table->name);
        printf("%s ", table->type);
        if (table->module) {
            printf("%s ", table->module);
        }
        printf("0x%lX ", table->address);
        if (table->offset_count) {
            for (int i = 0; i < table->offset_count; i++) {
                printf("0x%X ", table->offsets[i]);
            }
        }
        printf("\n");
    }
    return memory_table_count;
}
