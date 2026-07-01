#!/usr/bin/env python3
"""
Battery icon preview - mirrors the exact logic from battery_status.c
"""

import argparse


def draw_battery(width: int, height: int, level: int, horizontal: bool, usb_present: bool = False) -> list[list[str]]:
    """Draw battery icon using the same logic as battery_status.c"""
    # Initialize canvas with black pixels
    canvas = [['.' for _ in range(width)] for _ in range(height)]

    def draw_rect_outline(x: int, y: int, w: int, h: int):
        """Draw rectangle outline (1px border)"""
        for i in range(w):
            if 0 <= y < height and 0 <= x + i < width:
                canvas[y][x + i] = '#'
            if 0 <= y + h - 1 < height and 0 <= x + i < width:
                canvas[y + h - 1][x + i] = '#'
        for j in range(h):
            if 0 <= y + j < height and 0 <= x < width:
                canvas[y + j][x] = '#'
            if 0 <= y + j < height and 0 <= x + w - 1 < width:
                canvas[y + j][x + w - 1] = '#'

    def draw_rect_fill(x: int, y: int, w: int, h: int):
        """Draw filled rectangle"""
        for j in range(h):
            for i in range(w):
                if 0 <= y + j < height and 0 <= x + i < width:
                    canvas[y + j][x + i] = '#'

    if horizontal:
        # Horizontal battery: terminal on right side
        # Terminal nub: 2px wide, 1/3 height centered
        term_height = height // 3
        term_y = (height - term_height) // 2
        draw_rect_fill(width - 2, term_y, 2, term_height)

        # Body outline: 1px walls
        body_width = width - 2
        draw_rect_outline(0, 0, body_width, height)

        # Fill area inside body (1px inset from walls)
        fill_area_width = body_width - 2
        fill_area_height = height - 2

        # Calculate fill based on level (fills from right, showing empty space on left)
        fill_width = (fill_area_width * (100 - level)) // 100
        if usb_present:
            fill_width = fill_area_width

        if fill_width > 0:
            draw_rect_fill(1 + (fill_area_width - fill_width), 1, fill_width, fill_area_height)
    else:
        # Vertical battery: terminal on top
        # Terminal nub: 1/3 width centered, 2px tall
        term_width = width // 3
        term_x = (width - term_width) // 2
        draw_rect_fill(term_x, 0, term_width, 2)

        # Body outline: 1px walls
        body_height = height - 2
        draw_rect_outline(0, 2, width, body_height)

        # Fill area inside body (1px inset from walls)
        fill_area_width = width - 2
        fill_area_height = body_height - 2

        # Calculate fill based on level (fills from top, showing empty space at bottom)
        fill_height = (fill_area_height * (100 - level)) // 100
        if usb_present:
            fill_height = fill_area_height

        if fill_height > 0:
            draw_rect_fill(1, 3, fill_area_width, fill_height)

    return canvas


def render_ascii(canvas: list[list[str]], scale: int = 1) -> str:
    """Render canvas as ASCII art"""
    lines = []
    for row in canvas:
        line = ''.join(c * scale for c in row)
        for _ in range(scale):
            lines.append(line)
    return '\n'.join(lines)


def render_png(canvas: list[list[str]], filename: str, scale: int = 10):
    """Render canvas as PNG image"""
    try:
        from PIL import Image
    except ImportError:
        print("PIL not installed. Run: pip install Pillow")
        return

    height = len(canvas)
    width = len(canvas[0])

    img = Image.new('1', (width * scale, height * scale), 0)
    pixels = img.load()

    for y, row in enumerate(canvas):
        for x, pixel in enumerate(row):
            if pixel == '#':
                for sy in range(scale):
                    for sx in range(scale):
                        pixels[x * scale + sx, y * scale + sy] = 1

    img.save(filename)
    print(f"Saved to {filename}")


def main():
    parser = argparse.ArgumentParser(description='Preview ZMK battery icon')
    parser.add_argument('-W', '--width', type=int, default=10, help='Battery width (default: 10)')
    parser.add_argument('-H', '--height', type=int, default=6, help='Battery height (default: 6)')
    parser.add_argument('-l', '--level', type=int, default=50, help='Battery level 0-100 (default: 50)')
    parser.add_argument('--horizontal', action='store_true', help='Horizontal orientation')
    parser.add_argument('--usb', action='store_true', help='USB present (charging)')
    parser.add_argument('-s', '--scale', type=int, default=2, help='ASCII scale factor (default: 2)')
    parser.add_argument('-o', '--output', type=str, help='Save as PNG file')
    parser.add_argument('--png-scale', type=int, default=10, help='PNG scale factor (default: 10)')
    parser.add_argument('--all-levels', action='store_true', help='Show all battery levels')

    args = parser.parse_args()

    if args.all_levels:
        for level in [100, 75, 50, 25, 10, 0]:
            print(f"\n=== Level: {level}% ===")
            canvas = draw_battery(args.width, args.height, level, args.horizontal, args.usb)
            print(render_ascii(canvas, args.scale))
    else:
        canvas = draw_battery(args.width, args.height, args.level, args.horizontal, args.usb)
        print(f"Battery: {args.width}x{args.height}, Level: {args.level}%, {'Horizontal' if args.horizontal else 'Vertical'}")
        print()
        print(render_ascii(canvas, args.scale))

        if args.output:
            render_png(canvas, args.output, args.png_scale)


if __name__ == '__main__':
    main()
