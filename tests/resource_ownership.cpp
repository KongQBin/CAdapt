#include <type_traits>
#include "ElfPtrs.h"
#include "kmod/ElfSymInfos.h"

static_assert(!std::is_copy_constructible<ElfPtrs>::value, "ELF mappings must not be copied");
static_assert(!std::is_copy_assignable<ElfPtrs>::value, "ELF mappings must not be assigned");
static_assert(!std::is_copy_constructible<ElfSymInfos>::value, "Module mapping owners must not be copied");
static_assert(!std::is_copy_assignable<ElfSymInfos>::value, "Module mapping owners must not be assigned");

int main() { return 0; }
