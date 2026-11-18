// The one translation unit that defines VMA's implementation. Exactly one is
// required: none gives undefined symbols for every vmaCreate*, two gives
// duplicate symbols. Nothing else belongs in this file.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
