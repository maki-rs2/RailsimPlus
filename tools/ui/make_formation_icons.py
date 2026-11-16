"""編成カードの車両ボタン用アイコン(flip.png・trash.png)と、編成情報カードの運転席視点ボタン用のcabin.pngを作る。

白1色+アルファの32x32(@2x。RCSSの@spritesheetで resolution: 2x とし、@1xの16x16として扱う)。
色はRCSSの image-color で掛けるので、素材に色は焼き込まない。
使い方: python tools/ui/make_formation_icons.py  (リポジトリのルートで実行。PillowがあればOK)
"""
from pathlib import Path

from PIL import Image, ImageDraw

SIZE = 32          # 書き出し寸法(@2x)
SUPERSAMPLE = 8    # 縁をなめらかにするため大きく描いて縮める
STROKE = 3.0       # 線の太さ(書き出し寸法で)
OUT_DIR = Path(__file__).resolve().parents[2] / "assets" / "ui" / "icons"
WHITE = (255, 255, 255, 255)


def new_canvas():
    big = SIZE * SUPERSAMPLE
    image = Image.new("RGBA", (big, big), (255, 255, 255, 0))
    return image, ImageDraw.Draw(image)


def finish(image, name):
    image.resize((SIZE, SIZE), Image.LANCZOS).save(OUT_DIR / name)


def scaled(value):
    return value * SUPERSAMPLE


def line(draw, points, width=STROKE):
    pts = [(scaled(x), scaled(y)) for x, y in points]
    draw.line(pts, fill=WHITE, width=round(scaled(width)), joint="curve")
    radius = scaled(width) / 2
    for x, y in (pts[0], pts[-1]):
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=WHITE)


def make_flip():
    """Uターンの矢印: 左から立ち上がり、上で右へ回り込み、右で下向きの矢じりになる。"""
    image, draw = new_canvas()
    left, right, top, bottom = 9.0, 23.0, 7.0, 27.0
    radius = (right - left) / 2
    # 左の縦線(下から上)、上の半円、右の縦線(下向き)
    line(draw, [(left, bottom), (left, top + radius)])
    box = (scaled(left), scaled(top), scaled(right), scaled(top + 2 * radius))
    draw.arc(box, 180, 360, fill=WHITE, width=round(scaled(STROKE)))
    line(draw, [(right, top + radius), (right, bottom - 6.0)])
    # 矢じり(右の線の下端)
    tip = (right, bottom + 1.0)
    head = [(scaled(right - 5.5), scaled(bottom - 6.0)), (scaled(right + 5.5), scaled(bottom - 6.0)),
            (scaled(tip[0]), scaled(tip[1]))]
    draw.polygon(head, fill=WHITE)
    finish(image, "flip.png")


def make_trash():
    """ゴミ箱: 持ち手つきのふた、先細りの本体、本体の中の縦線2本。"""
    image, draw = new_canvas()
    line(draw, [(6.0, 9.0), (26.0, 9.0)])                       # ふた
    line(draw, [(12.5, 9.0), (12.5, 5.5), (19.5, 5.5), (19.5, 9.0)])  # 持ち手
    line(draw, [(8.5, 12.0), (9.5, 26.5), (22.5, 26.5), (23.5, 12.0)])  # 本体
    line(draw, [(13.0, 15.0), (13.5, 23.0)], width=2.2)
    line(draw, [(19.0, 15.0), (18.5, 23.0)], width=2.2)
    finish(image, "trash.png")


def make_cabin():
    """目: 運転席から見る視点のアイコン。アーモンド形の輪郭と、中の瞳。"""
    image, draw = new_canvas()
    left, right, mid_y, rise = 4.0, 28.0, 16.0, 9.0
    steps = 24
    upper = []
    for i in range(steps + 1):
        t = i / steps
        x = left + (right - left) * t
        upper.append((x, mid_y - rise * 4.0 * t * (1.0 - t)))
    lower = [(x, 2.0 * mid_y - y) for x, y in reversed(upper)]
    line(draw, upper + lower + [upper[0]])
    pupil = 3.6
    cx, cy = 16.0, mid_y
    draw.ellipse((scaled(cx - pupil), scaled(cy - pupil), scaled(cx + pupil), scaled(cy + pupil)), fill=WHITE)
    finish(image, "cabin.png")


if __name__ == "__main__":
    make_flip()
    make_trash()
    make_cabin()
