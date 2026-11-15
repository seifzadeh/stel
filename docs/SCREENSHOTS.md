# Application screenshots

The website and both READMEs use PNGs captured directly from Stel's production
Qt widgets. The current set is under `website/assets/screenshots/v0.9.0/`.
`capture.json` records the application/SDK/Qt versions, capture date, source
revision, image dimensions and SHA-256 hashes.

## Refresh on Linux

Use a configured development build with `STEL_BUILD_TESTS=ON` and the normal
runtime library paths from [BUILDING.md](BUILDING.md), then run:

```sh
bash scripts/capture-screenshots.sh build build/screenshots
```

The helper creates a private application directory and disposable Secret Service,
registers two demo accounts on a loopback PBX, makes voice/video calls, exchanges
Persian/English SIP messages and sends a two-page T.38 fax. Audio and video use
the built-in dummy devices. Settings, credentials and history belong to this
temporary session. A normal run takes about a minute.

Review the images, copy them and `capture.json` into the versioned website asset
directory, then update the README, hero/Open Graph image and gallery paths. Keep
the gallery's English/Persian captions and width/height attributes in sync with
the images. Versioned paths let browsers load the refreshed screenshots.

Build and check the website with `python3 scripts/build-site.py` before publishing.
The capture executable is an optional development target and is not included in
user packages.
