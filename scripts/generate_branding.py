#!/usr/bin/env python3
"""Generate the project's original geometric N/book mark (Pillow, development only).

No external image, font, icon library or model is used. Outputs keep the existing
resource names so the application needs no source or deployment changes.
SPDX-License-Identifier: MIT
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "QtExamSystem" / "resources"


def main():
    size = 1024
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((32, 32, 992, 992), radius=216, fill="#0f766e")
    # Two pages and a rising diagonal form an abstract N; all points are original.
    draw.polygon([(232, 264), (360, 264), (360, 760), (232, 760)], fill="white")
    draw.polygon([(664, 264), (792, 264), (792, 760), (664, 760)], fill="white")
    draw.polygon([(328, 264), (464, 264), (696, 760), (560, 760)], fill="white")
    draw.rounded_rectangle((220, 810, 804, 854), radius=22, fill="#99f6e4")
    icon = image.resize((256, 256), Image.Resampling.LANCZOS)
    DEST.mkdir(parents=True, exist_ok=True)
    icon.save(DEST / "app_icon.png")
    icon.save(DEST / "app_icon.ico", sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    image.resize((512, 512), Image.Resampling.LANCZOS).save(DEST / "logo_full.png")
    print("Generated original geometric branding (no external source assets).")


if __name__ == "__main__":
    main()
