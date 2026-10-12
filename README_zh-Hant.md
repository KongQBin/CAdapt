<p align="center">
  <a href="./README_en.md">English</a> &middot;
  <a href="./README.md">简体中文</a> &middot;
  <a href="./README_zh-Hant.md">繁體中文</a> &middot;
  <a href="./README_ja.md">日本語</a>
</p>

[![CI](https://github.com/KongQBin/CAdapt/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/KongQBin/CAdapt/actions/workflows/ci.yml)

# GLIBC & KMOD 符號版本適配工具（CAdapt）

CAdapt 是面向 Linux 的 ELF 中繼資料修補工具，可調整已編譯程式、動態函式庫與核心模組的符號版本要求。適用於已確認 ABI 相容，但版本中繼資料阻止執行或載入的情境。

## 功能

- **GLIBC**：讀取指定 `libc.so.6` 的符號版本，適配單一 ELF 或目錄；原版本可用時優先保留，保留符號隱藏標記。
- **KMOD**：修改 `.ko` 的 `vermagic` 與傳統 `__versions` 格式的符號 CRC。

## 建置與檢查

需要 Linux、支援 GNU++11 的 C++ 編譯器與 CMake 3.5+。測試另需 Python 3.7+ 與 `cc`。

```bash
cmake -S . -B build -DCADAPT_STRICT=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

語言標準固定為 **GNU++11**。`CADAPT_STRICT=ON` 將編譯警告與語言擴充視為錯誤。僅建置工具可加 `-DBUILD_TESTING=OFF`，不需 Python。較舊 CMake 可進入建置目錄執行 `cmake ..` 與 `ctest`。在舊系統執行時，請使用對應的 GLIBC/sysroot 建置。

## 使用

```bash
./build/cadapt -h

# GLIBC
./build/cadapt -c /path/to/libc.so.6 -t /path/to/target_elf_or_directory

# KMOD
./build/cadapt -m ./my_driver.ko -s ./Module.symvers \
  -v "4.18.0-193.el8.x86_64 SMP mod_unload"
```

### 參數與行為

- 提供 `-s/--symvers` 時，CRC 僅取自該檔案；缺少符號時不會默默從目前核心補齊。
- 省略 `-s` 或 `-v` 時，依需求從目前系統模組取得 CRC 或 `vermagic`。使用系統 CRC 時，明確指定的 `vermagic` 必須與其來源一致；適配其他核心請提供對應的 `Module.symvers` 與 `vermagic`。
- KMOD 完整驗證後才寫入；驗證失敗時目標保持不變。同步寫入失敗會回傳失敗，但不保證復原已寫入的內容。
- 目錄掃描略過符號連結。正常說明回傳 `0`，參數或適配失敗回傳非零退出碼。

## 審查、修復與驗證

**2026-10-12，專案藉助 OpenAI Codex 完成一輪程式碼審查、問題修復與迴歸驗證。** 修復記錄可於[提交歷史](https://github.com/KongQBin/CAdapt/commits/main/)查閱，驗證基準為 [`4af3cbc`](https://github.com/KongQBin/CAdapt/commit/4af3cbc)。

- 本機使用 **GCC 9.5 與 GCC 15.2** 完成 GNU++11 嚴格編譯，八項 CTest 檢查全部通過。
- 檢查涵蓋命令列、GLIBC 版本選擇與隱藏位元、KMOD CRC／來源衝突、異常 ELF、掃描與同步失敗、日誌及資源所有權。
- 迴歸腳本位於 [tests/](./tests/)，CI 設定位於 [ci.yml](./.github/workflows/ci.yml)；遠端執行狀態見頁面上方徽章。

上述說明記錄 AI 輔助審查與實際測試結果，不代表 OpenAI 官方認證，也不保證任意檔案或 ABI 的相容性。

## 支援範圍與風險

- 目前支援帶節標頭表的 **64 位元小端序 ELF**；不支援 32 位元、大端序或擴充節區編號。
- 擴充 KMOD 節區 `__version_ext_names`／`__version_ext_crcs` 會明確拒絕修改。
- 工具直接修改原檔，請先備份。僅修改中繼資料，無法檢查或修復 ABI 差異；程式仍可能當機，不相容核心模組可能引發 Kernel Panic/Oops。
- 錯誤日誌寫入執行檔目錄下的 `cadaptErrlog.txt`；日誌無法寫入時會輸出至 stderr。
