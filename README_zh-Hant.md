<p align="center">
  <a href="./README_en.md">EN</a>
  &middot;
  <a href="./README.md">简/</a>
  <a href="./README_zh-Hant.md">繁</a>
  &middot;
  <a href="./README_ja.md">日</a>
</p>

# GLIBC & KMOD 符號版本適配工具 (CAdapt)

本專案是一個底層的 ELF/核心模組 (Kernel Module) 修補工具，主要用於解決特定場景下已編譯的執行檔、動態函式庫或驅動程式由於系統版本不匹配而無法執行或載入的問題。

本專案支援兩大核心功能：
1. **GLIBC 適配**：解析指定的 `libc.so.6` 所支援的符號，並自動修補目標 ELF 檔案（或目錄）的符號版本依賴，使其與提供的 libc 相容。
2. **KMOD 適配**：修改 Linux 核心模組 (`.ko`) 的 `vermagic` 和核心符號 CRC 校驗值，繞過核心載入檢查，使其能夠強制載入到版本不完全匹配的核心中。

**使用方法 (Usage):**
```bash
# 【GLIBC 適配模式】
# ./[程式名] -c [宿主libc.so.6的路徑] -t [要適配的ELF檔案或目錄路徑]
./cadapt -c /path/to/your/libc.so.6 -t /path/to/target_elf_or_directory

# 【KMOD (核心模組) 適配模式】
# ./[程式名] -m [要修改的.ko檔案] -s [可選: Module.symvers路徑] -v [可選: 目標vermagic字串]
./cadapt -m ./my_driver.ko -s ./Module.symvers -v "4.18.0-193.el8.x86_64 SMP mod_unload"
# 註：若不提供 -s 或 -v，程式將嘗試自動從目前執行的系統中提取相關 CRC 和魔術字串資訊。
```

# ⚠️ 警告：關於 ABI 相容性與核心崩潰風險
- 僅修改元資料：本專案 只修改 ELF 或 KO 檔案中的符號版本元資料（如 .gnu.version 節區或符號 CRC 校驗碼），不會且無法檢查或修復由於底層版本變更引起的 ABI (應用程式二進位介面) 不相容問題。

- GLIBC 崩潰風險：如果函式（例如 memcpy 或 fopen）在新舊 libc 間的行為、參數或其內部使用的資料結構發生了實質性變化，程式在執行時極有可能因 ABI 不匹配而觸發段錯誤 (Segmentation Fault) 或產生不可預期的髒資料。

- KMOD 崩潰風險 (Kernel Panic)：強制修改 .ko 的 CRC 和 vermagic 繞過了核心的安全檢查機制。如果核心匯出符號（如核心結構體佈局）發生了改變，強行載入修改後的模組將直接導致核心崩潰 (Kernel Panic / Oops)。

- 此工具假設您已自行確認新舊環境間的 ABI 是完全相容的。請僅在您確切知道自己在做什麼的情況下使用，並在操作前務必備份原檔案。
