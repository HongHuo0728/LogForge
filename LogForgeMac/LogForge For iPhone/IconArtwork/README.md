# LogForge iOS glass icon

The built-in Image Gen tool was used in **edit** mode with the Windows icon `D:/LogForge/resources/LogForge.png` as the edit target. The Windows asset was not modified. The generated full-bleed square has an opaque navy outer background; the glass transparency is inside the ribbons, not an alpha cutout around the app icon.

Saved deliverables:

- `LogForge-Glass-Generated.png`: original generated artwork, 1254 × 1254.
- `../LogForge For iPhone/Assets.xcassets/AppIcon.appiconset/LogForge-Glass-1024.png`: RGB 1024 × 1024 asset preparation, without changing the design.
- `../LogForge For iPhone/LogForge.icon`: editable native package with three ribbon SVGs and a white log-curve SVG. Its vector silhouettes are hand-authored layers; they are not an automatic raster-to-vector conversion of the generated image. Glass, specular and translucency are described by native layer parameters.

The active Xcode app icon name is `LogForge`. The raster `AppIcon` catalog is retained as an alternate asset. The `.icon` package and its appearances have not been opened or compiled in Icon Composer on this Windows machine. Do not claim a rendered PNG verifies native dynamic reflections. See the Apple-platform checks in `../../docs/NATIVE_UPGRADE.md`.

Exact final tool prompt:

> Use case: style-transfer. Edit target: attached LogForge Windows icon. Create the iOS Liquid Glass version of exactly this recognizable brand mark: three rising curved cyan-to-blue-to-violet ribbons from bottom left to right, with the slim white logarithmic curve above them. Preserve their positions, curvature, count and overall proportions. Change material only to highly transparent optically clear sculpted glass with delicate edge highlights, realistic refraction and reflective sheen. Cool deep navy background, sophisticated quiet Apple-native visual feel. App icon artwork 1024 square edge-to-edge, full-bleed square background with NO rounded outer corners and NO transparent outer margin because the operating system supplies the icon mask. The glass ribbons should reveal the backdrop while retaining color and readability at small sizes. No text, no letters, no Apple logo, no extra objects, no mockup, no drop shadow outside the artwork. Save generated image for project integration.
