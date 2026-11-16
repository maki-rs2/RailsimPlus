# Railsim Plus

RailSim II のプラグインやレイアウト(`.rs2`)を、そのまま読み込んで遊べる鉄道シミュレーターです。描画は DirectX 12 で作り直していて、今も開発を続けています。

> **非公式のソフトです。** RailSim II およびその作者とは関係がありません。

## できること

- RailSim II のプラグインと `.x` モデルを読み込んで表示できます。
- `.rs2` のレイアウトを開いて、列車を走らせられます。
- 線路を敷いたり直したりできます(元に戻す・やり直しもできます)。信号やダイヤの設定、運転台からの眺めにも対応しています。
- 空や光の当たり方、夜の月明かりなどを、実物に近い見た目で描きます。

## ダウンロード

**[最新版をダウンロード(RailsimPlus-win64.zip)](https://github.com/maki-rs2/RailsimPlus/releases/latest/download/RailsimPlus-win64.zip)**

リンクを押すと、すぐに ZIP のダウンロードが始まります。GitHub のアカウントは要りません。版ごとの変更点は [Releases](https://github.com/maki-rs2/RailsimPlus/releases) のページで見られます。

## 使い方

1. ダウンロードした ZIP を右クリックして「すべて展開」を選び、好きなフォルダに展開します。設定や保存したレイアウトは exe と同じフォルダに保存されるので、`Program Files` のような書き込めない場所は避けてください。
2. 展開したフォルダにある `RailsimPlus.exe` をダブルクリックして起動します。
3. 右上のメニューから「ゲーム設定」を開いて、「ルートを管理…」→「追加…」と進み、RailSim II のフォルダを選びます。`Rail` や `Train` などのフォルダが入っているフォルダです。いくつでも登録できます。
4. 登録したフォルダのプラグインが、画面のブラウザに並びます。RailSim II のレイアウト(`.rs2`)は、メニューの「ロード」→「RailSim2 のレイアウト」から開けます。一覧には、登録したフォルダの `Layout` フォルダにあるものが出ます。

RailSim II 本体・プラグイン・`.rs2` は付いていません。お手持ちのものを使ってください。

## 動作環境

- Windows 10/11(64bit)
- DirectX 12 に対応した GPU

## 困ったとき

- **「Windows によって PC が保護されました」と出て起動できない**: このソフトには署名を付けていないので、初めて起動したときにこの画面が出ます。「詳細情報」を押すと「実行」ボタンが出るので、それを押すと起動できます。
- **うまく起動しない・すぐ終わってしまう**: ZIP を展開せずに、中の exe を直接開いていないか確かめてください。展開しないままだと必要なファイルが見つからず、正しく動きません。
- **プラグインが一覧に出ない**: 登録したフォルダが、`Rail` や `Train` などのフォルダが入っているフォルダになっているか確かめてください。
- **不具合を見つけたとき**: [Issues](https://github.com/maki-rs2/RailsimPlus/issues) から報告してもらえると助かります。何が起きたか、そのときの手順、使っている Windows と GPU を書いてもらえると調べやすくなります。

## 開発者向け

ビルドには Visual Studio 2022(C++23)、CMake 3.28 以上、Windows SDK が要ります。

### ビルド

依存ライブラリは、CMake の FetchContent が自動で取ってきます(初回はネットワークにつながっている必要があります)。

```powershell
cmake -S . -B build
cmake --build build --config Release --target RailsimPlus
```

### 起動

```powershell
.\build\Release\RailsimPlus.exe --help
```

起動オプションの一覧が出ます。

### 配布用 ZIP を作る

展開してそのまま動く、ポータブル版の ZIP を作ります。Release でビルドしたあとに実行してください。`build/` の下に `RailsimPlus-win64.zip` ができます。ダウンロードの直リンクが版ごとに変わらないように、ファイル名には版を入れていません。

```powershell
cmake --build build --config Release --target RailsimPlus
cpack --config build/CPackConfig.cmake -C Release
```

ZIP には、exe、実行に要る DLL(VC++ ランタイムを含みます)、`assets/`、ライセンス一式が入ります。設定(`settings.dat`)、保存したレイアウト、スクリーンショットは exe と同じフォルダに保存されます。ZIP に入れるものは `cmake/ReleasePackage.cmake` で決めています。

## ライセンス

GNU Lesser General Public License v2.1(のみ)です。全文は [LICENSE](LICENSE) にあります。
一部の処理は、RailSim II のソースコード(LGPL-2.1)から移植しています。
使っているライブラリとそのライセンスは、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) と [licenses/](licenses/) にまとめてあります。
