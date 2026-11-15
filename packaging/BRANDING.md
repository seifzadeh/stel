# Stel identity

The blue three-ribbon swoosh and uppercase italic STEL wordmark follow the
silver desk-phone reference used by the application. Keep the three tapered
curves together and preserve their transparent background.

| Colour | Value |
| --- | --- |
| Deep blue | #00448B |
| Blue | #087BD6 |
| Highlight | #73D3FF |

- Master mark: stel.svg (vector, transparent rounded corners).
- Outlined wordmark: stel-wordmark.svg; it has no font dependency.
- Transparent raster wordmark: stel-wordmark.png.
- Raster application icons: icons/stel-{16,20,24,32,40,48,64,128,256,512,1024}.png.
- Windows icon: stel.ico, nine PNG frames from 16 to 256 px (Windows 7+).

The wordmark is outlined from DejaVu Sans Bold Oblique. The font file is not embedded.
All application artwork is available under the project's GPL-3.0-or-later terms.

To regenerate in the development environment:

```sh
source scripts/env.sh
cmake --build build --target stel_brand_assets
QT_QPA_PLATFORM=offscreen build/stel_brand_assets packaging
```

The regeneration tool uses Qt's SVG image plugin. End-user rendering uses embedded
PNG resources, so displaying the logo does not require an SVG plugin or external
logo files. The Windows resource script attaches the ICO to both application and
portable launcher EXEs. Linux desktop assets install in the hicolor icon theme.

The main-window chassis asset and its preparation notes are in `assets/desk/`.
