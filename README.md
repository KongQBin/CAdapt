# ELF_GLIBC_VersionDegradation

项目参考至ReferenceData/ModifiersSolym.c文件  
reference data is ReferenceData/ModifiersSolym.c

可以对已编译的Elf文件进行GLIBC版本降级  
You can degrade the GLIBC version of Elf files that have already been compiled

目前不保证不会出现段错误  
There is no guarantee that segment errors will not occur

使用前请做好备份工作  
Make a good backup before use

被降级的ELF文件中使用的符号在低版本'libc.so.6'中存在,方可成功  
The symbols used in the target ELF files existed in earlier versions of libc.so.just can success.
