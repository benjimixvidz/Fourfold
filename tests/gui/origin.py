"""Tetrad's top-left corner in a window screenshot: the first amber pixel of its name band is module (4, 4)."""
import subprocess, sys
path = sys.argv[1]
p = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height", "-of", "csv=p=0", path], capture_output=True, text=True, check=True)
w, h = map(int, p.stdout.strip().split(","))
raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", path, "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], capture_output=True, check=True).stdout
def amber(x, y):
    i = 3 * (y * w + x)
    return abs(raw[i] - 255) <= 6 and abs(raw[i + 1] - 176) <= 8 and abs(raw[i + 2] - 59) <= 10
for y in range(40, h):
    xs = [x for x in range(0, w // 2) if all(amber(x + k, y) for k in range(0, 400, 20))]
    if xs:
        print(xs[0] - 4 - 1, y - 4 - 1)   # rounded corner: one pixel in
        sys.exit(0)
sys.exit(1)
