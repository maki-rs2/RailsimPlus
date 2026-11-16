"""全天球HDRI(Radiance .hdr、正距円筒図法)から、標準の空の遠景画像 assets/sky/Landscape.png を作る。

使い方: python make_landscape_band.py 入力.hdr 出力.png [--warm 0.06] [--exposure-median 0.22]
必要なパッケージ: numpy, opencv-python(-headless)
画像の仕様:
  4096x512、RGBA8(sRGB)。横=方位360度(左右はつながる)、縦=仰角+35度(上端)〜-10度(下端)。
  空はアルファ0、地形・木はアルファ1。上端の数行は、ぶつ切りにならないようアルファを0へ向けてフェードする。
"""
import argparse
import cv2
import numpy as np

ELEV_TOP_DEG = 35.0
ELEV_BOTTOM_DEG = -10.0
OUT_W, OUT_H = 4096, 512
SKY_LO_K, SKY_HI_K = 0.25, 0.60     # 天頂の空の明るさに対する比。これ未満=地形、これ超=空(間は滑らかに)
TOP_FADE_ROWS = 12                  # 上端のフェード行数(出力画像の行)
FRINGE_SIGMA = 3.0                  # 縁の色の補正に使うぼかし(元画像の画素)


def smooth(a):
    return a * a * (3.0 - 2.0 * a)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--warm", type=float, default=0.0,
                    help="色温度の補正。負で青寄り、正で暖色寄り(リニアのR/Bゲインを 1±warm にする)")
    ap.add_argument("--exposure-median", type=float, default=0.22,
                    help="地形の輝度の中央値(リニア)をこの値に合わせる")
    args = ap.parse_args()

    hdr = cv2.imread(args.src, cv2.IMREAD_UNCHANGED)[:, :, :3][:, :, ::-1].astype(np.float32)
    h, w = hdr.shape[:2]
    ppd = w / 360.0
    lum = 0.2126 * hdr[..., 0] + 0.7152 * hdr[..., 1] + 0.0722 * hdr[..., 2]
    zenith = float(np.median(lum[: int(10 * ppd)]))
    top = int(round((90.0 - ELEV_TOP_DEG) * ppd))
    bottom = top + int(round((ELEV_TOP_DEG - ELEV_BOTTOM_DEG) * ppd))
    band = hdr[top:bottom]
    y = lum[top:bottom]

    # 空 = 上端から「つながっている」明るい領域だけ。日向の明るい草地など、地形側の明るい部分を空と取り違えない
    bright = (y > 0.5 * (SKY_LO_K + SKY_HI_K) * zenith).astype(np.uint8)
    count, labels = cv2.connectedComponents(bright, connectivity=4)
    top_labels = np.unique(labels[0][bright[0] > 0])
    sky_region = np.isin(labels, top_labels[top_labels > 0]).astype(np.uint8)
    sky_region = cv2.dilate(sky_region, np.ones((9, 9), np.uint8))          # 縁の滑らかな部分も含める
    soft = 1.0 - smooth(np.clip((y - SKY_LO_K * zenith) / ((SKY_HI_K - SKY_LO_K) * zenith), 0, 1))
    alpha = np.where(sky_region > 0, soft, 1.0).astype(np.float32)
    horizon_row = int(round(ELEV_TOP_DEG * ppd))
    alpha[horizon_row:] = 1.0                                                  # 水平線より下は必ず地形
    # 縁: 空が混ざった色を、内側の地形の色で置き換え、アルファを1画素ぶん内側へ詰める
    solid = (alpha > 0.97).astype(np.float32)
    num = cv2.GaussianBlur(band * solid[..., None], (0, 0), FRINGE_SIGMA)
    den = cv2.GaussianBlur(solid, (0, 0), FRINGE_SIGMA)[..., None]
    fill = np.where(den > 1e-3, num / np.maximum(den, 1e-3), band)
    band = np.where((alpha < 0.97)[..., None] & (den > 1e-3), fill, band)
    alpha = cv2.erode(alpha, np.ones((5, 5), np.uint8))

    # 色温度(リニアのゲイン)→ 地形の輝度の中央値を合わせる → 弱い肩つきで0〜1へ
    gain = np.array([1.0 + args.warm, 1.0, 1.0 - args.warm], dtype=np.float32)
    band = band * gain
    yb = 0.2126 * band[..., 0] + 0.7152 * band[..., 1] + 0.0722 * band[..., 2]
    solid_mask = alpha > 0.99
    band = band * (args.exposure_median / float(np.median(yb[solid_mask])))
    band = np.clip(band, 0, None)
    band = np.clip(band / (1.0 + 0.35 * band), 0, 1)
    srgb = np.where(band <= 0.0031308, band * 12.92, 1.055 * np.power(band, 1 / 2.4) - 0.055)

    # プリマルチプライで縮小(縁に空の色が混ざらない)
    pm = np.dstack([srgb * alpha[..., None], alpha])
    pm = cv2.resize(pm, (OUT_W, OUT_H), interpolation=cv2.INTER_AREA)
    a = np.clip(pm[..., 3], 0, 1)
    rgb = np.where(a[..., None] > 1e-4, pm[..., :3] / np.maximum(a[..., None], 1e-4), 0.0)
    fade = np.ones(OUT_H, dtype=np.float32)
    fade[:TOP_FADE_ROWS] = np.linspace(0.0, 1.0, TOP_FADE_ROWS)   # 上端の行ほどアルファを0へ
    a = a * fade[:, None]
    bgra = np.dstack([rgb[..., 2], rgb[..., 1], rgb[..., 0], a])
    cv2.imwrite(args.out, (np.clip(bgra, 0, 1) * 255 + 0.5).astype(np.uint8))
    print(f"zenith={zenith:.3f} warm={args.warm} out={args.out} size={OUT_W}x{OUT_H}")


if __name__ == "__main__":
    main()
