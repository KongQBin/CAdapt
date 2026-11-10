#include <type_traits>
#include "ElfPtrs.h"
#include "kmod/ElfSymInfos.h"

static_assert(!std::is_copy_constructible<ElfPtrs<Elf64Traits>>::value, "ELF mappings must not be copied");
static_assert(!std::is_copy_assignable<ElfPtrs<Elf64Traits>>::value, "ELF mappings must not be assigned");
static_assert(!std::is_copy_constructible<ElfPtrs<Elf32Traits>>::value, "ELF mappings must not be copied");
static_assert(!std::is_copy_assignable<ElfPtrs<Elf32Traits>>::value, "ELF mappings must not be assigned");
static_assert(!std::is_copy_constructible<ElfSymInfos<Elf64Traits>>::value, "Module mapping owners must not be copied");
static_assert(!std::is_copy_assignable<ElfSymInfos<Elf64Traits>>::value, "Module mapping owners must not be assigned");
static_assert(!std::is_copy_constructible<ElfSymInfos<Elf32Traits>>::value, "Module mapping owners must not be copied");
static_assert(!std::is_copy_assignable<ElfSymInfos<Elf32Traits>>::value, "Module mapping owners must not be assigned");

int main() { return 0; }
