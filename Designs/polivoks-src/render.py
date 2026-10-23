import sys, pathlib
from playwright.sync_api import sync_playwright
d = pathlib.Path(sys.argv[1]).resolve()
with sync_playwright() as p:
    b = p.chromium.launch()
    for name in sys.argv[2:]:
        pg = b.new_page(device_scale_factor=2, viewport={"width":1200,"height":660})
        msgs=[]; pg.on('console', lambda m: msgs.append(m.text)); pg.on('pageerror', lambda e: msgs.append(str(e)))
        pg.goto((d / f"{name}.html").as_uri()); pg.wait_for_timeout(400)
        w, h = pg.evaluate("[document.body.scrollWidth, document.body.offsetHeight]")
        pg.set_viewport_size({"width": w, "height": h})
        pg.screenshot(path=str(d / f"{name}.clean.png"), clip={"x": 0, "y": 0, "width": w, "height": h})
        print(name, w, h, msgs)
    b.close()
