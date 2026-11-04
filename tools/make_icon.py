"""Draw the port's application icon (original artwork, no game assets).

A dark-red badge with a gold rim, a checkered-flag corner and a slanted
"R&R" in the logo's flame colours. Small sizes (<= 32 px) drop the flag and
the ampersand and keep one bold "R" so the icon still reads in a taskbar.

Usage: python tools/make_icon.py <fonts_dir> <out_dir>
Writes rnrr.ico (16-256 px) and rnrr-256.png.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont


def gradient(size, top, bottom):
    g = Image.new("RGB", (1, size[1]))
    for y in range(size[1]):
        t = y / max(1, size[1] - 1)
        g.putpixel((0, y), tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    return g.resize(size)


def flame_text(canvas, text, font, center, shear, outline):
    """Text filled with a yellow-orange-red gradient, a dark outline, slanted."""
    w, h = canvas.size
    mask = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mask)
    bbox = d.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    pos = (center[0] - tw / 2 - bbox[0], center[1] - th / 2 - bbox[1])
    d.text(pos, text, font=font, fill=255)
    # slant like the logo's script: shear around the text's vertical centre
    mask = mask.transform((w, h), Image.AFFINE, (1, shear, -shear * center[1], 0, 1, 0), Image.BICUBIC)
    top = center[1] - th / 2
    grad = Image.new("RGB", (w, h))
    band = gradient((w, max(1, int(th))), (255, 236, 110), (220, 40, 8))
    grad.paste((255, 236, 110), (0, 0, w, max(0, int(top))))
    grad.paste(band, (0, max(0, int(top))))
    grad.paste((220, 40, 8), (0, int(top + th), w, h))
    edge = mask.filter(ImageFilter.MaxFilter(outline * 2 + 1)) if outline else mask
    canvas.paste((40, 4, 0), (0, 0), edge)
    canvas.paste(grad, (0, 0), mask)


def badge(size, big):
    s = size * 4  # supersample, then downscale
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    r = int(s * 0.20)
    body = Image.new("L", (s, s), 0)
    ImageDraw.Draw(body).rounded_rectangle((0, 0, s - 1, s - 1), r, fill=255)
    bg = gradient((s, s), (120, 14, 10), (34, 4, 4)).convert("RGBA")
    # soft highlight in the upper half
    glow = Image.new("L", (s, s), 0)
    ImageDraw.Draw(glow).ellipse((-s * 0.3, -s * 0.75, s * 1.3, s * 0.55), fill=70)
    bg.paste((255, 120, 60), (0, 0), glow.filter(ImageFilter.GaussianBlur(s * 0.08)))
    img.paste(bg, (0, 0), body)
    d = ImageDraw.Draw(img)
    if big:
        # checkered flag in the top-right corner, clipped to the badge
        flag = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        fd = ImageDraw.Draw(flag)
        cell = s // 12
        for i in range(4):
            for j in range(4):
                col = (245, 245, 245, 235) if (i + j) % 2 == 0 else (20, 20, 20, 235)
                x0 = s - cell * (i + 1) - int(s * 0.055)
                y0 = int(s * 0.055) + cell * j
                fd.rectangle((x0, y0, x0 + cell, y0 + cell), fill=col)
        tri = Image.new("L", (s, s), 0)
        ImageDraw.Draw(tri).polygon([(s * 0.62, 0), (s, 0), (s, s * 0.38)], fill=255)
        img.paste(flag, (0, 0), Image.composite(flag.getchannel("A"), Image.new("L", (s, s), 0), tri))
        # gold edge along the flag's diagonal, so the corner reads as a deliberate inlay
        edge = Image.new("L", (s, s), 0)
        ImageDraw.Draw(edge).line([(s * 0.62, 0), (s, s * 0.38)], fill=255, width=max(2, int(s * 0.02)))
        img.paste((255, 196, 64), (0, 0), Image.composite(edge, Image.new("L", (s, s), 0), body))
    rim = max(2, int(s * (0.035 if big else 0.06)))
    d.rounded_rectangle((rim // 2, rim // 2, s - 1 - rim // 2, s - 1 - rim // 2), r - rim // 2,
                        outline=(255, 196, 64), width=rim)
    return img, s


def make(size, fonts):
    big = size > 32
    img, s = badge(size, big)
    bold = os.path.join(fonts, "LatoLatin-Bold.ttf")
    if big:
        font = ImageFont.truetype(bold, int(s * 0.44))
        flame_text(img, "R&R", font, (s * 0.47, s * 0.60), 0.22, max(2, int(s * 0.018)))
    else:
        font = ImageFont.truetype(bold, int(s * 0.78))
        flame_text(img, "R", font, (s * 0.50, s * 0.53), 0.18, max(2, int(s * 0.035)))
    return img.resize((size, size), Image.LANCZOS)


def main():
    fonts, out = sys.argv[1:3]
    os.makedirs(out, exist_ok=True)
    sizes = [256, 128, 64, 48, 32, 24, 16]
    imgs = [make(sz, fonts) for sz in sizes]
    imgs[0].save(os.path.join(out, "rnrr-256.png"))
    imgs[0].save(os.path.join(out, "rnrr.ico"), sizes=[(sz, sz) for sz in sizes],
                 append_images=imgs[1:])
    print("icon written to", out)


if __name__ == "__main__":
    main()
