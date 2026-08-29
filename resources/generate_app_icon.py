from pathlib import Path

from PIL import Image


SIZES = (16, 32, 48, 256)


def main() -> None:
    source_path = Path(__file__).with_name("app-icon-source.png")
    output_path = Path(__file__).with_name("app.ico")

    with Image.open(source_path) as source:
        source = source.convert("RGBA")
        bounds = source.getchannel("A").getbbox()
        assert bounds is not None
        source = source.crop(bounds)

        side = max(source.size)
        padding = round(side * 0.05)
        canvas = Image.new("RGBA", (side + 2 * padding,) * 2, (0, 0, 0, 0))
        canvas.alpha_composite(
            source, ((canvas.width - source.width) // 2,
                     (canvas.height - source.height) // 2))
        canvas.save(output_path, format="ICO",
                    sizes=[(size, size) for size in SIZES])

    with Image.open(output_path) as icon:
        assert icon.ico.sizes() == {(16, 16), (32, 32), (48, 48), (256, 256)}


if __name__ == "__main__":
    main()
