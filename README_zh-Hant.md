<p align="center">
  <a href="./README_en.md">English</a>
  &middot;
  <a href="./README.md">简/</a>
  <a href="./README_zh-Hant.md">繁體中文</a>
  &middot;
  <a href="./README_ja.md">日本語</a>
</p>

# GLIBC 符號版本適配工具
本專案是一個 ELF 工具，用於修改 ELF 檔案或指定目錄中的所有 ELF 檔案，使其適應不同版本的 GLIBC (`libc.so.6`)。
專案思路參考自 `ReferenceData/ModifiersSolym.c` 檔案（該檔案中包含了一些註釋，可供參考）。

**主要差異：**
與參考檔案（使用固定的版本號）不同，本專案功能更強大：
	1. 可以自動從使用者指定的 `libc.so.6` (參數1) 中解析其支援的所有符號版本。
	2. 自動修補目標 ELF 檔案或目錄 (參數2) 所需的符號版本依賴，使其與參數1的 `libc` 相容。

**使用方法 (Usage):**
```bash
 #./[程式名] [宿主libc.so.6的路徑] [要適配的ELF檔案或目錄路徑]
 ./your_program /path/to/your/libc.so.6 /path/to/target_elf_or_directory
```
# 警告：關於 ABI 相容性
- 本專案 只修改 ELF 檔案中的符號版本元資料（.gnu.version 和 .gnu.version_r 節區）。
- 本專案 不會，也 無法 檢查或修復由於 GLIBC 版本變更引起的 ABI (應用程式二進位介面) 不相容問題。
- 如果一個函數（例如 memcpy 或 fopen）在舊版本和新版本 libc 間的行為、參數、或其內部使用的資料結構（struct）發生了變化，那麼即使符號版本被成功「降級」，程式在執行時也極有可能因 ABI 不匹配而崩潰（如段錯誤）或產生不可預期的錯誤資料。
- 此工具假設您已自行確認新舊 GLIBC 版本間的 ABI 是完全相容的。請僅在您確切知道自己在做什麼的情況下使用。

**其他注意事項與風險**

- 符號依賴: 適配成功的前提是：目標 ELF 所依賴的所有符號必須在您提供的（通常是低版本的）libc.so.6 中 實體存在。
- 錯誤日誌: 如果適配失敗，請檢查程式目錄下的錯誤日誌 (例如 Errlog.txt)。日誌會指明哪個檔案的哪個符號導致了失敗。
- 穩定性風險: 對於結構特殊的 ELF 檔案，適配過程有小機率觸發段錯誤。
- 資料備份: 雖然程式對檔案操作進行了容錯處理（使用 mmap 和 msync），但鑒於此工具的底層修改特性，強烈建議 在使用前備份所有目標 ELF 檔案，以防萬一。

