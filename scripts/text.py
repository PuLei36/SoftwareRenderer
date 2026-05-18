"""Create text image files

The Python Imaging Library is required
    pip install pillow
"""

import os

from PIL import Image, ImageDraw, ImageFont

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
COMMON_DIRECTORY = os.path.join(BASE_DIR, "..", "assets", "common")
UI_DIRECTORY = os.path.join(COMMON_DIRECTORY, "ui")

FONT_NAME = "arial.ttf"
FONT_SIZE = 14

PADDING_TOP = 6
PADDING_BOTTOM = 6
PADDING_LEFT = 8
PADDING_RIGHT = 8


def gen_text_image(text, dst_directory):
    font = ImageFont.truetype(FONT_NAME, FONT_SIZE)
    bbox = font.getbbox(text)
    ascent, descent = font.getmetrics()
    height = PADDING_TOP + ascent + descent + PADDING_BOTTOM
    width = PADDING_LEFT + (bbox[2] - bbox[0]) + PADDING_RIGHT

    image = Image.new("L", (width, height))
    draw = ImageDraw.Draw(image)
    draw.text((PADDING_LEFT, PADDING_TOP), text, (127,), font)

    name = text.lower() + ".tga"
    path = os.path.join(dst_directory, name)
    image.save(path, rle=True)


def main():
    common_texts = ["Diffuse", "Specular", "Roughness", "Occlusion", "Normal"]
    ui_texts = [
        "SELECT-RENDERER",
        "PBR",
        "BLINN-PHONG",
        "SELECT-MODEL",
        "BACK",
        "ASSASSIN",
        "AZURA",
        "BUSTER",
        "CENTAUR",
        "CRAB",
        "CRAFTSMAN",
        "DIESELPUNK",
        "DRONE",
        "ELFGIRL",
        "HELMET",
        "HORSE",
        "JUNKRAT",
        "KGIRL",
        "LIGHTHOUSE",
        "MCCREE",
        "NIER2B",
        "ORNITIER",
        "PHOENIX",
        "PONYCAR",
        "ROBOT",
        "SPHERE",
        "SPHERES",
        "VIVI",
        "WHIP",
        "WITCH",
    ]

    for text in common_texts:
        gen_text_image(text, COMMON_DIRECTORY)

    os.makedirs(UI_DIRECTORY, exist_ok=True)
    for text in ui_texts:
        gen_text_image(text, UI_DIRECTORY)


if __name__ == "__main__":
    main()
