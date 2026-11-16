# Railsim Plus

RailSim II のプラグイン(PI)・`.x`・`.rs2` と互換のある、DirectX 12 + PBR の鉄道シミュレーター(開発中)。

> **非公式のソフトです。** RailSim II およびその作者とは関係がありません。

## できること

- RailSim II のプラグイン(PI)と `.x` モデルを読み込んで表示する
- `.rs2` のレイアウトを読み込んで表示・走行する
- 線路の敷設・編集(Undo/Redo つき)、信号・ダイヤ、運転台視点
- 大気・IBL・月明かり・半透明の順序非依存合成を使った描画

## ダウンロード

**[最新版をダウンロード(RailsimPlus-win64.zip)](https://github.com/maki-rs2/RailsimPlus/releases/latest/download/RailsimPlus-win64.zip)**

リンクを押すと ZIP のダウンロードが始まる。GitHub のアカウントは要らない。各版の変更点は [Releases](https://github.com/maki-rs2/RailsimPlus/releases) にある。

## 使い方

1. ダウンロードした ZIP を右クリックして「すべて展開」を選び、好きなフォルダに展開する。`Program Files` のような書き込めない場所は避ける(設定や保存したレイアウトは exe の隣に保存される)。
2. 展開したフォルダの `RailsimPlus.exe` をダブルクリックして起動する。
3. 右上のメニューから「ゲーム設定」を開き、「ルートを管理…」→「追加…」で RailSim II のフォルダ(`Rail`・`Train` などのフォルダが入っているところ)を選ぶ。複数のフォルダを登録できる。
4. 登録したフォルダのプラグインが、画面のブラウザに並ぶ。RailSim II のレイアウト(`.rs2`)は、メニューの「ロード」→「RailSim2 のレイアウト」から開ける。登録したフォルダの `Layout` フォルダにあるものが一覧に出る。

RailSim II 本体・プラグイン・`.rs2` は付属していない。手持ちのものを使う。

## 動作環境

- Windows 10/11(64bit)
- DirectX 12 対応の GPU

## 困ったとき

- **「Windows によって PC が保護されました」と出て起動しない**: 署名をしていない exe なので、初回に出る。「詳細情報」を押すと出る「実行」で起動できる。
- **ZIP の中の exe を直接起動した**: 展開せずに起動すると、ほかのファイルが見つからずに正しく動かない。先に「すべて展開」する。
- **プラグインが一覧に出ない**: 登録したフォルダが、`Rail`・`Train` などのフォルダの1つ上かを確かめる。
- **不具合を見つけた**: [Issues](https://github.com/maki-rs2/RailsimPlus/issues) に、起こったこと・手順・使った Windows と GPU を書いて報告する。

## 開発者向け

ビルドには Visual Studio 2022(C++23)、CMake 3.28 以上、Windows SDK が要る。

### ビルド

依存ライブラリは CMake の FetchContent が自動で取得する(初回はネットワークが必要)。

```powershell
cmake -S . -B build
cmake --build build --config Release --target RailsimPlus
```

### 起動

```powershell
.\build\Release\RailsimPlus.exe --help
```

起動オプションの一覧が出る。

### 配布用 ZIP を作る

展開してそのまま動くポータブル版の ZIP を作る。Release でビルドしたあとに実行する(`build/` の下に `RailsimPlus-win64.zip` ができる。直リンクが変わらないよう、名前に版は入れない)。

```powershell
cmake --build build --config Release --target RailsimPlus
cpack --config build/CPackConfig.cmake -C Release
```

ZIP には exe・実行時の DLL(VC++ ランタイム含む)・`assets/`・ライセンス一式が入る。設定(`settings.dat`)・保存したレイアウト・スクリーンショットは exe の隣へ保存される。内容の定義は `cmake/ReleasePackage.cmake`。

## ライセンス

GNU Lesser General Public License v2.1(のみ)。全文は [LICENSE](LICENSE)。
一部の処理は RailSim II のソースコード(LGPL-2.1)から移植している。
第三者のライブラリとそのライセンスは [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) と [licenses/](licenses/) にある。
