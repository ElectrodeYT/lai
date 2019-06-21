/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#include <lai/core.h>
#include "aml_opcodes.h"
#include "ns_impl.h"
#include "libc.h"
#include "exec_impl.h"
#include "eval.h"

uint32_t bswap32(uint32_t);
uint16_t bswap16(uint16_t);
uint8_t char_to_hex(char);

int lai_is_name(char character) {
    if ((character >= '0' && character <= '9')
        || (character >= 'A' && character <= 'Z')
        || character == '_'
        || character == ROOT_CHAR
        || character == PARENT_CHAR
        || character == MULTI_PREFIX
        || character == DUAL_PREFIX)
        return 1;

    else
        return 0;
}

size_t lai_eval_integer(uint8_t *object, uint64_t *integer) {
    uint8_t *byte = (uint8_t*)(object + 1);
    uint16_t *word = (uint16_t*)(object + 1);
    uint32_t *dword = (uint32_t*)(object + 1);
    uint64_t *qword = (uint64_t*)(object + 1);

    switch (object[0]) {
        case ZERO_OP:
            integer[0] = 0;
            return 1;
        case ONE_OP:
            integer[0] = 1;
            return 1;
        case ONES_OP:
            integer[0] = 0xFFFFFFFFFFFFFFFF;
            return 1;
        case BYTEPREFIX:
            integer[0] = (uint64_t)byte[0];
            return 2;
        case WORDPREFIX:
            integer[0] = (uint64_t)word[0];
            return 3;
        case DWORDPREFIX:
            integer[0] = (uint64_t)dword[0];
            return 5;
        case QWORDPREFIX:
            integer[0] = qword[0];
            return 9;
        default:
            return 0;
    }
}

size_t lai_parse_pkgsize(uint8_t *data, size_t *destination) {
    destination[0] = 0;

    uint8_t bytecount = (data[0] >> 6) & 3;
    if (!bytecount)
        destination[0] = (size_t)(data[0] & 0x3F);
    else if (bytecount == 1){
        destination[0] = (size_t)(data[0] & 0x0F);
        destination[0] |= (size_t)(data[1] << 4);
    } else if (bytecount == 2) {
        destination[0] = (size_t)(data[0] & 0x0F);
        destination[0] |= (size_t)(data[1] << 4);
        destination[0] |= (size_t)(data[2] << 12);
    } else if (bytecount == 3) {
        destination[0] = (size_t)(data[0] & 0x0F);
        destination[0] |= (size_t)(data[1] << 4);
        destination[0] |= (size_t)(data[2] << 12);
        destination[0] |= (size_t)(data[3] << 20);
    }

    return (size_t)(bytecount + 1);
}

int lai_eval_package(lai_object_t *package, size_t index, lai_object_t *destination) {
    if (package->type != LAI_PACKAGE) {
        lai_warn("attempt to evaluate non-package object.");
        return 1;
    } else if (index >= lai_exec_pkg_size(package)) {
        lai_warn("attempt to evaluate index %d of package of size %d",
                index, lai_exec_pkg_size(package));
        return 1;
    }

    lai_exec_pkg_load(destination, package, index);
    return 0;
}

int lai_eval(lai_object_t *destination, char *path) {
    lai_nsnode_t *handle;
    char *path_copy = laihost_malloc(lai_strlen(path) + 1);
    lai_strcpy(path_copy, path);
    handle = lai_exec_resolve(path_copy);
    laihost_free(path_copy);
    if (!handle)
        return 1;

    while (handle->type == LAI_NAMESPACE_ALIAS) {
        handle = lai_resolve(handle->alias);
        if (!handle)
            return 1;
    }

    if (handle->type == LAI_NAMESPACE_NAME) {
        lai_copy_object(destination, &handle->object);
        return 0;
    } else if (handle->type == LAI_NAMESPACE_METHOD) {
        lai_state_t state;
        lai_init_state(&state);
        int ret;
        if ((ret = lai_exec_method(handle, &state)))
            return ret;
        lai_move_object(destination, &state.retvalue);
        lai_finalize_state(&state);
        return 0;
    }

    return 1;
}

uint16_t bswap16(uint16_t word) {
    return (uint16_t)((word >> 8) & 0xFF) | ((word << 8) & 0xFF00);
}

uint32_t bswap32(uint32_t dword) {
    return (uint32_t)((dword>>24) & 0xFF)
        | ((dword<<8) & 0xFF0000)
        | ((dword>>8)&0xFF00)
        | ((dword<<24)&0xFF000000);
}

uint8_t char_to_hex(char character) {
    if (character <= '9')
        return character - '0';
    else if(character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    else if(character >= 'a' && character <= 'f')
        return character - 'a' + 10;

    return 0;
}

void lai_eisaid(lai_object_t *object, char *id) {
    size_t n = lai_strlen(id);
    if (lai_strlen(id) != 7) {
        if(lai_create_string(object, n))
            lai_panic("could not allocate memory for string");
        memcpy(lai_exec_string_access(object), id, n);
        return;
    }

    // convert a string in the format "UUUXXXX" to an integer
    // "U" is an ASCII character, and "X" is an ASCII hex digit
    object->type = LAI_INTEGER;

    uint32_t out = 0;
    out |= ((id[0] - 0x40) << 26);
    out |= ((id[1] - 0x40) << 21);
    out |= ((id[2] - 0x40) << 16);
    out |= char_to_hex(id[3]) << 12;
    out |= char_to_hex(id[4]) << 8;
    out |= char_to_hex(id[5]) << 4;
    out |= char_to_hex(id[6]);

    out = bswap32(out);
    object->integer = (uint64_t)out & 0xFFFFFFFF;
}
