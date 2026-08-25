from pathlib import Path

from PIL import Image, ImageDraw


SIZES = (16, 32, 48, 256)
BLUE = (37, 99, 235, 255)
BLUE_DARK = (29, 78, 216, 255)
WHITE = (255, 255, 255, 255)


def draw_icon(size: int) -> Image.Image:
    scale = 4
    canvas_size = size * scale
    image = Image.new("RGBA", (canvas_size, canvas_size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    def box(values: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
        return tuple(value * scale for value in values)

    radius = max(3, round(size * 0.22))
    draw.rounded_rectangle(box((1, 1, size - 1, size - 1)), radius * scale,
                           fill=BLUE_DARK)
    draw.rounded_rectangle(box((2, 2, size - 2, size - 2)),
                           max(2, (radius - 1) * scale), fill=BLUE)

    # A white folder frame with two blue panes carries the product's group/
    # workspace idea while remaining legible at taskbar size.
    left, top, right, bottom = (round(size * value)
                                for value in (0.19, 0.27, 0.81, 0.73))
    tab_right = round(size * 0.49)
    tab_top = round(size * 0.21)
    draw.rounded_rectangle(box((left, top, right, bottom)),
                           max(2, round(size * 0.09) * scale), fill=WHITE)
    draw.rectangle(box((left, tab_top, tab_right, top + 2)), fill=WHITE)

    inset = max(2, round(size * 0.09))
    pane_left, pane_top = left + inset, top + inset
    pane_right, pane_bottom = right - inset, bottom - inset
    draw.rounded_rectangle(box((pane_left, pane_top, pane_right, pane_bottom)),
                           max(1, round(size * 0.035) * scale), fill=BLUE_DARK)
    divider = max(1, round(size * 0.045))
    mid = (pane_left + pane_right) // 2
    draw.rectangle(box((mid - divider // 2, pane_top, mid + divider // 2,
                        pane_bottom)), fill=WHITE)

    return image.resize((size, size), Image.Resampling.LANCZOS)


def main() -> None:
    output = Path(__file__).with_name("app.ico")
    images = [draw_icon(size) for size in SIZES]
    images[-1].save(output, format="ICO", sizes=[(size, size) for size in SIZES])
    with Image.open(output) as icon:
        assert icon.ico.sizes() == {(16, 16), (32, 32), (48, 48), (256, 256)}


if __name__ == "__main__":
    main()
