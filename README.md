# GLIBC 符号版本适配工具
本项目是一个ELF工具，用于修改ELF文件或指定目录中的所有ELF文件，使其适应不同版本的 GLIBC (`libc.so.6`)。
项目思路参考自 `ReferenceData/ModifiersSolym.c` 文件（该文件中包含了一些注释，可供参考）。

**主要差异：**
与参考文件（使用固定的版本号）不同，本项目功能更强大：
	1. 可以自动从用户指定的 `libc.so.6` (参数1) 中解析其支持的所有符号版本。
	2. 自动修补目标ELF文件或目录 (参数2) 所需的符号版本依赖，使其与参数1的 `libc` 兼容。

**使用方法 (Usage):**
```bash
 #./[程序名] [宿主libc.so.6的路径] [要适配的ELF文件或目录路径]
 ./your_program /path/to/your/libc.so.6 /path/to/target_elf_or_directory
```
#⚠️ 警告：关于ABI兼容性
- 本项目 只修改 ELF 文件中的符号版本元数据（.gnu.version 和 .gnu.version_r 节区）。
- 本项目 不会，也无法 检查或修复由于 GLIBC 版本变更引起的 ABI (应用程序二进制接口) 不兼容 问题。
- 如果一个函数（例如 memcpy 或 fopen）在旧版本和新版本 libc 间的行为、参数、或其内部使用的数据结构（struct）发生了变化，那么即使符号版本被成功“降级”，程序在运行时也极有可能因ABI不匹配而崩溃（如段错误）或产生不可预期的错误数据。
- 此工具假定您已自行确认新旧 GLIBC 版本间的 ABI 是完全兼容的。请仅在您确切知道自己在做什么的情况下使用。

**其他注意事项与风险**

- 符号依赖: 适配成功的前提是：目标ELF所依赖的所有符号必须在您提供的（通常是低版本的）libc.so.6 中 物理存在。
- 错误日志: 如果适配失败，请检查程序目录下的错误日志 (例如 Errlog.txt)。日志会指明哪个文件的哪个符号导致了失败。
- 稳定性风险: 对于结构特殊的ELF文件，适配过程有小概率触发段错误。
- 数据备份: 虽然程序对文件操作进行了容错处理（使用 mmap 和 msync），但鉴于此工具的底层修改特性，强烈建议在使用前备份所有目标ELF文件，以防万一。

#GLIBC Symbol Version Adapter
This project is an ELF utility designed to modify an ELF file, or all ELF files within a directory, to adapt them to a different version of GLIBC (libc.so.6).
The project concept is based on ReferenceData/ModifiersSolym.c (which contains comments that may be useful for reference).

**Key Difference:**
Unlike the reference file (which uses fixed, hard-coded versions), this project is more powerful:
1. It automatically parses all supported symbol versions from a user-provided libc.so.6 (argument 1).
2. It automatically patches the symbol version dependencies in the target ELF file or directory (argument 2) to be compatible with the provided libc.

**Usage:**
```bash
#./[program_name] [path/to/host/libc.so.6] [path/to/target_elf_or_directory]
./your_program /path/to/your/libc.so.6 /path/to/target_elf_or_directory
```

#⚠️ WARNING: ABI COMPATIBILITY

- It does not, and cannot, check for or fix ABI (Application Binary Interface) incompatibilities caused by the GLIBC change.
- This tool only modifies the symbol version metadata within the ELF file (the .gnu.version and .gnu.version_r sections).
- If a function (e.g., memcpy or fopen) has different behavior, parameters, or internal data structures (struct) between the old and new libc versions, the program will very likely crash (e.g., Segmentation Fault) or produce corrupt data at runtime, even if the symbol version was "downgraded" successfully.
- This tool assumes YOU have independently verified that the ABIs between the old and new GLIBC versions are fully compatible. Use only if you know exactly what you are doing.

**Other Disclaimers and Risks**

- Symbol Dependency: Successful adaptation requires that all symbols used by the target ELF must physically exist in the provided (usually older) libc.so.6.
- Error Log: If adaptation fails, check the error log (e.g., Errlog.txt) in the program's directory. The log will specify which file and which symbol caused the failure.
- Stability Risk: There is a small chance of a segmentation fault when processing certain complex or unusual ELF files.
- BACKUP YOUR FILES: Although the tool has fault tolerance (using mmap and msync), given the low-level nature of this operation, it is strongly recommended to back up all target ELF files before use to prevent data loss.

#GLIBC シンボルバージョン適応ツール
このプロジェクトは、ELFファイル、または指定されたディレクトリ内のすべてのELFファイルを変更し、異なるバージョンの GLIBC (libc.so.6) に適応させるためのELFユーティリティです。
プロジェクトのコンセプトは ReferenceData/ModifiersSolym.c に基づいています（参考となるコメントがファイル内に含まれています）。

**主な違い:**
参照ファイル（固定バージョンを使用）とは異なり、このプロジェクトはより強力です：
1. ユーザーが指定した libc.so.6 (引数1) から、サポートされているすべてのシンボルバージョンを自動的に解析します。
2. ターゲットELFファイルまたはディレクトリ (引数2) が要求するシンボルバージョンの依存関係を自動的に修正（パッチ）し、提供された libc と互换性を持たせます。

**使用方法:**

```bash
# ./[プログラム名] [ホストlibc.so.6のパス] [適応させたいELFファイルまたはディレクトリのパス]
./your_program /path/to/your/libc.so.6 /path/to/target_elf_or_directory
```

#⚠️ 警告：ABI互換性について

- 本ツールは、ELFファイル内のシンボルバージョン・メタデータ（.gnu.version および .gnu.version_r セクション）を 変更するだけ です。
- GLIBCのバージョン変更によって引き起こされる ABI (アプリケーション・バイナリ・インターフェース) の非互換性 については、一切チェックも修正も 行いません（行うこともできません）。
- もし関数（例：memcpy や fopen）の動作、引数、またはその内部で使用されるデータ構造（struct）が新旧の libc バージョン間で異なる場合、たとえシンボルバージョンの「ダウングレード」に成功したとしても、プログラムは実行時にABIの不一致によりクラッシュ（セグメンテーション違反など）したり、不正なデータを生成したりする可能性が非常に高いです。
- 本ツールは、使用者が新旧GLIBCバージョン間のABIが完全に互換であることを独自に確認済みであることを前提としています。ご自身が何をしているかを正確に理解している場合にのみ使用してください。

**その他の注意事項とリスク**

- シンボル依存: 適応が成功するための前提条件は、ターゲットELFが使用するすべてのシンボルが、指定された（通常は古いバージョンの）libc.so.6 に 物理的に存在する ことです。
- エラーログ: 適応に失敗した場合、プログラムディレクトリ内のエラーログ (例: Errlog.txt) を確認してください。どのファイルのどのシンボルが原因で失敗したかが記録されています。
- 安定性リスク: 特殊な構造を持つELFファイルの処理中、稀にセグメンテーション違反が発生する可能性があります。
- ファイルのバックアップ: 本ツールはファイル操作に関するフォールトトレランス（mmap と msync を使用）を備えていますが、操作の低レベルな性質を考慮し、使用前に必ずすべてのターゲットELFファイルをバックアップする ことを強く推奨します。