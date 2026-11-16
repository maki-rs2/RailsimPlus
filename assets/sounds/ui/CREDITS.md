# UI効果音の出典

`assets/sounds/ui/` の音声ファイルの出どころ。Freesound の音は CC0 1.0、Pixabay の音は Pixabay Content License で、どちらも表記の義務はないが、元をたどれるように残す。Pixabay の音は、音声ファイル単体での再配布・販売が禁止されている。
元の音から、頭と尻の無音(-70dB 以下)を削り、モノラル 48kHz にして、長さは 0.34〜0.45 秒を上限に切っている。上限で切った音は、終わりを指数的にフェードアウトさせる。音量は、聞こえ方の大きさ(ITU-R BS.1770 のK特性で重み付けした、50ms窓の最大)でそろえ、頻繁に鳴る音ほど小さくしている。click・toggle・tab_switch は -32、shutter は -26、それ以外は -28(LUFS相当)。focus.wav は、元の音のうち最初のビープ(0.90〜1.07秒)だけを 0.15 秒で切り(終わり 10ms をフェードアウト)、録音の雑音を除くために 3〜6kHz だけを残している。

| ファイル | 元の題名 | 作者 | 出典 | ライセンス |
|---|---|---|---|---|
| click.wav | Logitech Mouse Click | UnderlinedDesigns | https://freesound.org/people/UnderlinedDesigns/sounds/383239/ | CC0 1.0 |
| toggle.wav | Light Switch 2 | Crinkem | https://freesound.org/people/Crinkem/sounds/496329/ | CC0 1.0 |
| tab_switch.wav | 25.wav | adcbicycle | https://freesound.org/people/adcbicycle/sounds/14078/ | CC0 1.0 |
| confirm.wav | Simple or Cute UI / UX / Interface Confirm sound | Feraly_ | https://freesound.org/people/Feraly_/sounds/836450/ | CC0 1.0 |
| node_place.wav | Snapping-Closed an Airpods Case | redpanda69 | https://freesound.org/people/redpanda69/sounds/520954/ | CC0 1.0 |
| delete.wav | Woosh (stick swung in the air) | Dalesome | https://freesound.org/people/Dalesome/sounds/352719/ | CC0 1.0 |
| train_place.wav | Box with Metal Latches Hit 2.wav | suspensiondigital | https://freesound.org/people/suspensiondigital/sounds/389705/ | CC0 1.0 |
| warning.wav | Good Phone Notification Sound | qubodup | https://freesound.org/people/qubodup/sounds/782969/ | CC0 1.0 |
| error.wav | Error.wav | 本プロジェクトの作者 | 作者のRailSim II用スキンの音 | 作者の自作 |
| shutter.wav | Camera Shutter | freesound_community | https://pixabay.com/sound-effects/camera-shutter-6305/ | Pixabay Content License |
| focus.wav | Shutter Sound | fishhuntfloridaoutdoors | https://pixabay.com/sound-effects/shutter-sound-192352/ | Pixabay Content License |

列車を外す音(train_remove)は delete.wav を使う。
