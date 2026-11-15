# Desk-phone skin

`chassis.png` is the silver housing, handset, empty keycaps and blue city wallpaper.
It follows the reference supplied by Mehrdad Seifzadeh (`stel.png`, 1448 × 1086).
The reference was edited with the built-in imagegen tool; the preparation prompt
is in `chassis.prompt.txt`. Artwork is GPL-3.0-or-later, like the rest of Stel.

All display text, call data, navigation, key legends and fax controls are drawn
or hosted by native Qt widgets. The image contains no account state, phone
number, document, clock, button label or registration indicator.

`src/deskphone.cpp` uses reference coordinates and a uniform scale with centred
letterboxing. `src/faxwidget.cpp` hosts the same conversion/transmission logic in
the lower fax dock. The SVG blue swoosh, application icons and outlined STEL
wordmark are maintained separately in `packaging/`.
