# Third-Party Notices

Railsim Plus(仮称)は GNU Lesser General Public License v2.1 のもとで配布する(`LICENSE` を参照)。
このファイルは、配布物に含まれる、または配布物が使う第三者の成果物と、そのライセンスの一覧。

## RailSim II のソースコード(移植元)

- 出典: https://github.com/aizentranza/railsim2(Kinoshita Naoki 氏、LGPL-2.1)
- 振り子傾斜・車輪回転・車体姿勢・カーブの分割・架線柱の配置アルゴリズムなど、一部の処理をこのソースから移植している。
  移植した箇所は、ソースコメントに移植元のクラス名・関数名を書いている(`移植` で検索できる)。同じ規則・順序に合わせただけの箇所は、コメントに `RailSim II と同じ` のように書いている。
- 本プロジェクトは RailSim II の非公式の互換ソフトであり、RailSim II およびその作者とは関係がない。
- RailSim II -k-build(作者による非公式の拡張版)の拡張形式にも互換がある。ソースやバイナリは取り込んでいない。

## ライブラリ

| 名前 | 用途 | ライセンス | 配布形態 |
|---|---|---|---|
| OpenAL Soft | 音声再生 | LGPL-2.0 | DLL(動的リンク) |
| assimp | .x の読込 | BSD-3-Clause | 静的リンク |
| RmlUi | UI | MIT | 静的リンク |
| Dear ImGui | 開発用 UI | MIT | 静的リンク |
| FreeType | フォント描画 | FreeType License(FTL)を選択 | 静的リンク |
| DirectXTK12 / DirectXTex / DirectXMesh | DX12 補助・画像・メッシュ | MIT | 静的リンク |
| nlohmann/json | JSON | MIT | ヘッダのみ |
| spdlog | ログ | MIT | 静的リンク |
| CLI11 | コマンドライン | BSD-3-Clause | ヘッダのみ |
| magic_enum | 列挙の文字列化 | MIT | ヘッダのみ |
| miniaudio | 音声デコード | MIT-0 / Public Domain | 静的リンク |
| WinPixEventRuntime | GPU 計測(PIX) | MIT | DLL |
| Catch2 | テスト(配布物に含めない) | BSL-1.0 | - |

各ライセンスの全文は、`licenses/` フォルダに1ファイルずつ置いている(ビルド時に取得したソースの `LICENSE` と、同梱フォントの `OFL.txt` のコピー)。配布物にもこのフォルダを同梱する。
FreeType は FTL の全文、OpenAL Soft は本体の `COPYING` と同梱 pffft の `LICENSE` を置いている。依存のバージョンを上げたときは、全文が変わっていないか確認して差し替える。

## 素材

| 素材 | 出典 | ライセンス |
|---|---|---|
| `assets/fonts/NotoSansJP-VariableFont_wght.ttf` | Google Fonts(Noto Sans JP) | SIL OFL 1.1(`assets/fonts/OFL.txt`) |
| `assets/sky/Landscape.png` | Poly Haven「lilienstein」を加工 | CC0 |
| `assets/sounds/MouseUp.wav` | 作者自作 | 本プロジェクトと同じ(LGPL-2.1-only) |
| `assets/ui/icons/` のうち Lucide 由来のアイコン(元データは `tools/ui/svg/`。`mode_train`・`mode_signal`・`radio_dot`・`toggle_knob`・`loading_train` は自作) | Lucide(lucide-icons/lucide)を白1色に加工 | ISC。一部は Feather 由来で MIT(`licenses/Lucide.txt`) |
| `assets/ui/icons/loading_train.png` | 本プロジェクトでSVGを作図しInkscapeで書き出し(元データは `tools/ui/svg/loading_train.svg`) | 本プロジェクトと同じ(LGPL-2.1-only) |
