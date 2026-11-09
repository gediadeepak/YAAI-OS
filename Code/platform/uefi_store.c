#include "uefi_store.h"
#include "platform.h"
#include "../storage.h"
#include "../uefi.h"
#include <stddef.h>
typedef struct {
    TableHeader header;
    UnusedService get_time, set_time, get_wakeup, set_wakeup, set_virtual_map, convert_pointer;
    uint64_t (*get_variable)(const uint16_t *, const Guid *, uint32_t *, uint64_t *, void *);
    UnusedService next_variable;
    uint64_t (*set_variable)(const uint16_t *, const Guid *, uint32_t, uint64_t, const void *);
} RuntimeVariables;
_Static_assert(offsetof(RuntimeVariables,get_variable) == 72 && offsetof(RuntimeVariables,set_variable) == 88,"UEFI runtime variable ABI");
static const Guid owner = {0x63832bf1,0x1bf2,0x48ae,{0x98,0x2b,0x59,0x41,0x41,0x49,0x01,0x01}};
static int variable_name(const char *key, uint16_t *wide)
{
    unsigned n = 0;
    for (; key[n] && n < 33; ++n) wide[n] = (uint8_t)key[n];
    wide[n] = 0; return n && !key[n];
}
static StoreResult read_value(void *context, const char *key, void *bytes, unsigned *size)
{
    RuntimeVariables *runtime = context;
    uint16_t name[34];
    if (!variable_name(key,name)) return STORE_LIMIT;
    uint64_t length = *size; uint32_t attributes = 0;
    uint64_t status = runtime->get_variable(name,&owner,&attributes,&length,bytes);
    if (status == 0x800000000000000eULL) return STORE_NOT_FOUND;
    if (status == 0x8000000000000005ULL) return STORE_CORRUPT;
    if (status) return STORE_ERROR;
    if (attributes != 7 || length > *size) return STORE_CORRUPT;
    *size = (unsigned)length; return STORE_OK;
}
static StoreResult write_value(void *context, const char *key, const void *bytes, unsigned size)
{
    RuntimeVariables *runtime = context;
    uint16_t name[34];
    if (!variable_name(key,name)) return STORE_LIMIT;
    return runtime->set_variable(name,&owner,7,size,bytes) ? STORE_ERROR : STORE_OK;
}
void uefi_store_initialize(void *table)
{
    RuntimeVariables *runtime = table;
    StoreBackend backend = {0};
    if (runtime && runtime->header.signature == 0x56524553544e5552ULL && runtime->header.header_size >= sizeof(*runtime) &&
        runtime->get_variable && runtime->set_variable) backend = (StoreBackend){runtime,read_value,write_value};
    storage_attach(backend);
}
void platform_storage_initialize(const BootInfo *boot) { uefi_store_initialize(boot->runtime_services); }
