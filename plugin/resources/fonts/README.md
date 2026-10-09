# Fonts embedded in the MidPluck editor

The editor uses the three typefaces of the JAMRACK web app (`css/style.css`), embedded
in the plugin binary by `juce_add_binary_data` (target `midpluck_ui_assets` in
`plugin/CMakeLists.txt`):

| Folder | Family | Files | Used for |
|---|---|---|---|
| `unbounded/` | Unbounded | Medium (500), ExtraBold (800) | brand, big note name |
| `barlowcondensed/` | Barlow Condensed | Regular, Medium, SemiBold | labels, section titles, buttons |
| `splinesansmono/` | Spline Sans Mono | Regular (400), Medium (500) | values, LCD, readouts |

All three are licensed under the **SIL Open Font License 1.1**; the full licence of
each family is the `OFL.txt` file next to its fonts and must ship with them: the build
copies the three into the plugin bundle as
`MidPluck.vst3/Contents/Resources/OFL-{Unbounded,BarlowCondensed,SplineSansMono}.txt`
(POST_BUILD step in `plugin/CMakeLists.txt`, before the copy into the VST3 folder).
None of the three declares a Reserved Font Name.

- Unbounded: Copyright 2022 The Unbounded Project Authors
  (https://github.com/googlefonts/unbounded)
- Barlow Condensed: Copyright 2017 The Barlow Project Authors
  (https://github.com/jpt/barlow)
- Spline Sans Mono: Copyright 2022 The Spline Sans Mono Project Authors
  (https://github.com/SorkinType/SplineSansMono)

## Where the files come from

Downloaded on 9 October 2026 from the official Google Fonts repository,
`https://github.com/google/fonts` (branch `main`): `ofl/unbounded/Unbounded[wght].ttf`,
`ofl/splinesansmono/SplineSansMono[wght].ttf`,
`ofl/barlowcondensed/BarlowCondensed-{Regular,Medium,SemiBold}.ttf` and each family's
`OFL.txt`.

google/fonts ships Unbounded and Spline Sans Mono only as variable fonts, and JUCE 8
cannot select a variation axis (a variable font always renders at its default
instance). The static weights above were therefore cut with fontTools 4.55.3
(`fontTools.varLib.instancer`, the tool Google Fonts uses for its own static
instances), and every file was then subset (`fontTools.subset`) to Latin-1 plus a few
symbols (general punctuation, arrows, minus, beta, music signs where the font has
them) to keep the plugin small: about 270 kB for the seven files instead of 1.3 MB.
These are Modified Versions in the sense of the OFL; they keep the original names,
which the licence allows since no Reserved Font Name is declared.
