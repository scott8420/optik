# Optik

An OCR workbench for scanned books, for Linux (GTK4 / gtkmm-4.0), on
ABBYY FineReader's model: the page is always in view, the engine (Tesseract)
proposes the page's areas and its text, the author adjusts what is wrong and
proofs what is uncertain, and the book exports to ODT for LibreOffice Writer.

    | Pages | the page, its areas, uncertain words | the text |

- **New from Scans** -- a folder of scans becomes a book beside it
  (`<folder>.optik/`); two-page spreads are split; every page is read.
- **The page** -- areas coloured by kind (text green, heading blue, picture
  red, running head / folio purple); uncertain words amber. Click a word to
  find it in the text. Drag an area's edge to resize it (it is read again);
  **Draw Area** to add one; **Area** menu or Del to retype or remove it.
- **The text** -- line for line as printed, editable; F4 goes to the next word
  to check, Ctrl+Enter accepts it. Italic is measured from the pixels and shown
  in italic (Ctrl+I sets or clears it); words the dictionary does not know get
  a squiggle -- Ctrl+Shift+Enter adds one to the book's own words.
- **Format** -- the bar over the text: Bold, Italic, Underline, Small Caps,
  Superscript (Ctrl+B / I / U, Ctrl+Shift+K / P) on the selected words; Heading
  (Ctrl+Shift+H) for the area. The arrows turn the page outside the text.
- **Find and Replace** -- Ctrl+F: across the book, Enter / Shift+Enter to walk
  the matches, Replace All as one undoable step.
- **The pages** -- move a page (Alt+Up / Alt+Down), turn it a quarter
  (Ctrl+] / Ctrl+[), delete it, or **Re-cut the Scan…**: one page or two, the
  cut dragged to the gutter. A turned or re-cut page is read again.
- **Condition** -- before the engine (and on the screen): Deskew (the page's
  tilt measured when read, the page and its words straightened), Decolour
  (grey), Level (paper to white, ink to black). Book settings, each declinable.
- **Undo** -- every change, Ctrl+Z / Ctrl+Shift+Z, from anywhere.
- **Export** -- ODT (Text body + Heading 1, for Writer to restyle) or plain
  text. Running heads and folios left out; paragraphs broken by the page turn
  joined; line-end hyphens mended; formats as Writer's character styles. Each is
  a Book setting. (s29) Export asks first: which pages (All, This Page),
  running heads kept or not, page breaks before headings or at each page.

## Build

    ./build.sh && ./build/optik_selftest && build/optik

Dependencies are listed at the top of `build.sh`. Run `build/optik <folder>`
to open a book or make one from a folder of scans.

## The sweep (s26)

    scripts/sweep.sh            (from the workspace root; needs Xvfb)

Builds with ASan + UBSan in `optik/build-asan`, runs the selftest there, then
opens a scratch book under Xvfb and drives the window itself: every menu verb,
the canvas (clicks, edge drags, drawn areas, at two zooms, on a turned page),
the page verbs and the Re-cut window, and the races between a read in flight
and a change. The book is audited after every step; undo to the start, redo
to the end and a save / open must agree. `--plain` skips the sanitizers;
`scripts/sweep.sh <book.optik>` sweeps a copy of one of your books.

## Layout

See `docs/ARCHITECTURE.md`. `core/` and `libs/` are GTK-free; the selftest
runs them headless.

## License

MIT.
