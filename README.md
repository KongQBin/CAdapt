# CAdapt

项目参考至ReferenceData/ModifiersSolym.c文件，该文件中有我在查看的过程中添加的一些注释，感兴趣的也可以进行参考。
本项目与其的差异在于：可以根据参数一指定的libc库自动的获取其所提供的符号版本，并自动与参数二所指定的Elf文件或指定的目录中的所有Elf文件进行符号版本适配，而参考文件版本是固定的，并不实用。

注意：
  被降级的ELF文件中使用的符号在低版本'libc.so.6'中存在,方可成功，如果失败了，可以查看该程序所在目录中的错误日志，日志中会提示是在哪个文件中，包含的哪个符号导致的适配失败，进而可以将提示出的符号替换为低版本libc中存在的，再进行适配。
  该项目制作仓促，并没有精心设计数据结构，且对于极个别特殊文件，在适配过程中可能会有小概率触发段错误，虽然已经进行了容错，造成文件损坏的概率不大，但还是希望使用前进行备份，避免造成损失。

  The project reference is to the ReferenceData/ModifiersSolym.c file. This file contains some comments I added during the review process. Those who are interested can also refer to it.
The difference between this project and the above file is that it can automatically obtain the symbol version provided by the libc library specified by parameter 1, and automatically compare it with the Elf file specified by parameter 2 or all Elf files in the specified directory version adaptation, while the reference file version is fixed, which is not practical.

Notice:
   The symbols used in the downgraded ELF file must exist in the lower version 'libc.so.6' to succeed. If it fails, you can check the error log in the directory where the program is located. The log will prompt which file it is in. , which symbol contained caused the adaptation to fail, and then you can replace the prompted symbols with those that exist in the lower version of libc, and then adapt.
   The project was produced in a hurry, and the data structure was not carefully designed. Moreover, for very few special files, there may be a small probability of triggering a segfault during the adaptation process. Although fault tolerance has been implemented, the probability of file damage is small, but we still hope Make a backup before use to avoid losses.
