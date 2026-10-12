<p align="center">
  <a href="./README_en.md">English</a> &middot;
  <a href="./README.md">简体中文</a> &middot;
  <a href="./README_zh-Hant.md">繁體中文</a> &middot;
  <a href="./README_ja.md">日本語</a>
</p>

[![CI](https://github.com/KongQBin/CAdapt/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/KongQBin/CAdapt/actions/workflows/ci.yml)

# GLIBC & KMOD シンボルバージョン適応ツール（CAdapt）

CAdapt は Linux 向けの ELF メタデータ修正ツールです。コンパイル済みのプログラム、共有ライブラリ、カーネルモジュールのシンボルバージョン要件を調整します。ABI の互換性を確認済みで、バージョン情報が実行やロードを妨げている場合を対象としています。

## 機能

- **GLIBC**：指定した `libc.so.6` のシンボルバージョンを読み込み、単一の ELF またはディレクトリを処理します。元のバージョンが利用可能なら維持し、シンボルの hidden フラグも保持します。
- **KMOD**：`.ko` の `vermagic` と、従来の `__versions` 形式のシンボル CRC を変更します。

## ビルドとテスト

Linux、GNU++11 対応の C++ コンパイラ、CMake 3.5+ が必要です。テストには Python 3.7+ と `cc` も必要です。

```bash
cmake -S . -B build -DCADAPT_STRICT=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

言語標準は **GNU++11** に固定しています。`CADAPT_STRICT=ON` はコンパイル警告と非標準の言語拡張をエラーとして扱います。ツールだけをビルドする場合は `-DBUILD_TESTING=OFF` を追加すると Python は不要です。古い CMake ではビルドディレクトリ内で `cmake ..` と `ctest` を実行できます。古いシステム向けには対応する GLIBC/sysroot を使ってビルドしてください。

## 使い方

```bash
./build/cadapt -h

# GLIBC
./build/cadapt -c /path/to/libc.so.6 -t /path/to/target_elf_or_directory

# KMOD
./build/cadapt -m ./my_driver.ko -s ./Module.symvers \
  -v "4.18.0-193.el8.x86_64 SMP mod_unload"
```

### オプションと動作

- `-s/--symvers` を指定した場合、CRC はそのファイルからのみ取得します。不足するシンボルを現在のカーネルから自動補完しません。
- `-s` または `-v` を省略した場合、必要に応じて現在のシステムモジュールから CRC または `vermagic` を取得します。システムの CRC を使う場合、明示した `vermagic` はその取得元と一致する必要があります。別のカーネル向けには対応する `Module.symvers` と `vermagic` を指定してください。
- KMOD は全項目の検証後に書き込みます。検証失敗時は対象ファイルを変更しません。同期書き込みに失敗すると失敗を返しますが、書き込み済みデータのロールバックは保証しません。
- ディレクトリ走査ではシンボリックリンクをスキップします。通常のヘルプは `0`、引数エラーや適応失敗はゼロ以外の終了コードを返します。

## レビュー、修正、検証

**2026-10-12、OpenAI Codex の支援によりコードレビュー、不具合修正、回帰検証を実施しました。** 変更内容は[コミット履歴](https://github.com/KongQBin/CAdapt/commits/main/)で確認でき、検証時の基準コミットは [`4af3cbc`](https://github.com/KongQBin/CAdapt/commit/4af3cbc) です。

- ローカルで **GCC 9.5 と GCC 15.2** による GNU++11 の厳格なビルドと、8 件の CTest チェックがすべて成功しました。
- コマンドライン、GLIBC のバージョン選択と hidden ビット、KMOD の CRC／取得元の競合、不正な ELF、走査／同期失敗、ログ、リソース所有権を検証します。
- 回帰スクリプトは [tests/](./tests/)、CI 設定は [ci.yml](./.github/workflows/ci.yml) にあります。リモート実行の状態は上部のバッジで確認できます。

この説明は AI 支援によるレビューと実際のテスト結果を記録したもので、OpenAI の公式認証や、任意のファイル・ABI の互換性を保証するものではありません。

## 対応範囲とリスク

- セクションヘッダーテーブルを持つ **64 ビット・リトルエンディアン ELF** に対応します。ELF32、ビッグエンディアン、拡張セクション番号は未対応です。
- 拡張 KMOD セクション `__version_ext_names`／`__version_ext_crcs` は明示的に変更を拒否します。
- ファイルを直接変更するため、事前にバックアップしてください。メタデータの変更で ABI の差異を検査・修復することはできません。アプリケーションのクラッシュや、不適合モジュールによる Kernel Panic/Oops が発生する可能性があります。
- エラーは実行ファイルと同じディレクトリの `cadaptErrlog.txt` に記録します。ログに書き込めない場合は stderr に出力します。
