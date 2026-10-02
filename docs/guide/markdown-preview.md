# Markdown preview

Markdown preview is a **separate window** that shows one Markdown file the way it is meant to be
read: headings, tables, highlighted code, formulas, and diagrams. You can also edit the text and
save it back to the same file.

It is not a page in the sidebar. You open it from a file, and it stays a window of its own while you
read or tidy that file.

## How to open a file

1. Go to **Agent Tools - Prompt** and pick the folder that holds the file.
2. In the file tree on the right, **right-click a `.md` or `.markdown` file**.
3. Choose **Preview Markdown**.

The window opens with the rendered document. If you right-click another file while the window is
already open, that window switches to the new file instead of stacking up a second one.

**Why is there no Preview Markdown entry?** Two possibilities: the file is not a Markdown file by
extension, or this build of the app cannot preview at all — the preview needs the version that embeds
a browser engine. When the capability is missing the entry never appears, so there is nothing to
discover and fail at.

## The three views

The leftmost buttons on the toolbar switch what you see:

- **Preview** — the rendered document only.
- **Split** — the Markdown source on the left, the rendering on the right.
- **Source** — the plain text, with nothing rendered.

**Split** and **Source** are greyed out when the file cannot be written; only **Preview** stays
usable, and the status bar at the bottom says why.

## Reading

- **Outline.** The panel on the left lists the document's headings; click one to jump there. Toggle
  it with the outline button in the toolbar (its tooltip is **Show outline**). A document with no
  headings shows **No headings**.
- **Text size.** The first drop-down picks the body text size: **Theme size** follows the current
  appearance theme, or choose a fixed size from 12 px to 24 px.
- **Width.** The second drop-down picks how wide the text column is: **Default width**, a fixed
  width, or **Full width** to use the whole window.
- **Status bar.** It shows the file's path, its size, the rendering engine's version numbers,
  **Unsaved changes** while you have edits that are not written, and the reason if the file cannot
  be edited.

## Finding text in the document

Press `Ctrl+F`, or click the search button. Type your text and use **Next** and **Previous** to step
through matches; the label tells you which match you are on (**3 of 17**). Turn on **Match case**
(Aa) for a case-sensitive search. `Esc` closes the search bar.

Two things it will not do, both because of how the window renders long files quickly:

- It counts only the part that has been laid out. On a very long document, searching immediately
  after opening it can report fewer matches than it will a moment later — re-run the search once the
  whole document is rendered.
- It cannot find text that is split by formatting. Searching for `Markdown` will not match inside
  `**Mark**down`, because the bold markers break the word into pieces.

## Editing and saving

Type in **Split** or **Source**. The window asks before discarding your work:

- `Ctrl+S` writes the file. A confirmation appears, and the **Unsaved changes** mark goes away.
- Closing the window with unsaved edits asks: **Save and close**, **Discard changes**, or **Cancel**.
- If something else changes the file while you have unsaved edits, a dialog appears: **Reload from
  disk** takes the outside version and drops yours, **Keep my changes** keeps yours and stops asking
  about that particular change.
- `F5` re-reads the file from disk. If you have unsaved changes it asks you first, so `F5` cannot
  quietly throw your editing away.

**Saving preserves the file's shape.** If the file used Windows line endings, or had a byte-order
mark at the start, saving keeps those. The window does not "tidy" your file into a different shape.

A file can be read-only for three reasons, and the status bar names the one that applies: it is too
big to edit here (previewing is still fine), it is not writable on disk, or its text is not valid
UTF-8 — saving that back would permanently damage it, so editing is refused. Very large files
(tens of megabytes) are not opened at all rather than opened and left struggling.

## Copying, opening elsewhere, exporting

All of these are under **More actions** (the icon at the right of the toolbar):

- **Copy selection** copies the selected rendered text. Select something in the preview first; if
  nothing is selected the app tells you.
- **Copy Markdown source** copies the raw Markdown.
- **Open in browser** writes a copy of the rendered page and opens it in your system browser. The
  copy lands in the app's own data folder and is named after the document, so using this again on the
  same file replaces that copy.
- **Export HTML next to the document** writes a self-contained HTML file beside the Markdown file.
- **Export HTML to a folder...** lets you pick the folder.

Either export finishes with a message naming the file, and shows you where it landed. Both wait until
the whole document has been rendered first — a document with many diagrams takes a moment before the
file appears.

Note about the two exports: the exported page refers to images **relative to the document**, so
pictures only appear if the HTML sits next to them. Exporting to a different folder is your choice,
but the pictures will not come along.

## What renders, and what does not

Formulas, diagrams, tables, task lists and code highlighting work. Specifically:

- Formulas written with `$$ … $$`, `\[ … \]` and inline forms renders as typeset formulas. A formula with a
  syntax error is shown in red rather than hidden.
- A code block whose language is `mermaid` becomes a diagram. Diagrams are drawn as you scroll near
  them, not all at once — a document with very many diagrams will ask you to click a diagram before
  drawing it.
- GitHub-style notice blocks (`> [!NOTE]` and its four relatives) are drawn as coloured callouts.
- **Embedded HTML is not rendered.** Tags written directly into the Markdown appear as text. This is
  a deliberate safety choice: this window shows files you did not write, and HTML in a document
  would otherwise be able to run code inside the app.

## Troubleshooting

**The preview window shows nothing.** Check the status bar; it names the reason when the file cannot
be read. An empty file says **The document is empty** in the preview area, and **Nothing to edit** in
the editor.

**A diagram is blank or shows an error.** A diagram that fails to parse says **Diagram could not be
drawn**; fix the text in **Source** and the block updates. In a document with a lot of diagrams, a
placeholder says **Click to render this diagram** and stays empty until you click it — that is the
window refusing to draw dozens of diagrams you were never going to look at.

**Something did not render.** If part of the document fails, a message appears and the rest of the
document stays visible — the window never shows a half-blank page in silence.

**The window will not open a second file.** It is not a bug: one window shows one file at a time.
Opening another replaces the current one (after asking about unsaved changes).

**The formula or diagram area stays empty while the text is fine.** That usually means the page could
not load its resources. Reopen the file; if it keeps happening, this build's preview components are
not installed correctly, and the message explains it.

Want to know how it works inside? See [Markdown preview](../development/markdown-preview.md).
