# Application icon

The LogForge icon is an original spectrum-and-curve mark: cyan-to-violet bands and
a rising white transfer curve on a dark rounded square. It contains no Apple
logo, camera brand, lettering or third-party stock artwork.

The artwork was generated with the built-in image-generation tool for this
project on 2026-09-22. The retained prompt direction was:

> Create an original Windows app icon for LogForge, a professional video color
> transcoder. A minimal spectrum of cyan-to-violet curved bands and a clean white
> rising logarithmic curve on a dark rounded-square tile. Strong small-size
> silhouette, generous margins, transparent exterior, no text, no letters, no
> Apple logo, no camera brand, no mockup or surrounding background.

`resources/LogForge.png` is the retained RGBA artwork (1254 × 1254). The build
embeds `resources/LogForge.ico`, containing 16, 20, 24, 32, 40, 48, 64, 128 and
256 pixel sizes. ICO packaging uses ordinary Lanczos resampling of the retained
artwork; no generation tool or image library is needed to build or run LogForge.

Both the GUI and developer CLI contain icon resource 101. The GUI uses it for
the window caption, taskbar and brand area. Windows caches executable icons;
copying a new release to a new path avoids confusing an older cached icon with
the current embedded resource. The automated resource test inspects the actual
compiled executables, including the nine image sizes.

The project distributes this asset under its MIT license to the extent of its
rights. No exclusivity, trademark registration or clearance is asserted.
