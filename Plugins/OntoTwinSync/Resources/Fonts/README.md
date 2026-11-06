# OntoTwin overlay fonts

The OntoTwin UE overlay theme ships these font files so Editor, Standalone, and
packaged builds use the same glyph coverage on every host project.

- `Inter-Regular.ttf` and `Inter-SemiBold.ttf`: Inter 4.1, from the official
  `rsms/inter` release.
- `NotoSansCJKsc-Regular.otf` and `NotoSansCJKsc-Medium.otf`: Noto Sans CJK,
  Simplified Chinese language-specific OTFs from `notofonts/noto-cjk`.

The corresponding SIL Open Font License texts are stored beside the fonts as
`LICENSE-Inter.txt` and `LICENSE-NotoSansCJK.txt`.

Runtime code builds one composite Slate font: Inter is the default Latin and
numeric typeface, while Noto Sans CJK SC is the fallback for Chinese glyphs.
