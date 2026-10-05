# Markdown workbench

The Markdown workbench is **one window for two jobs**: writing prompts at your own pace, and reading or tidying a Markdown file. **Nothing is ever sent from this window** — pressing Enter only adds a new line, and when the text is ready you copy it and paste it into the agent yourself.

Find it in the sidebar as the row **Markdown**. It replaces the prompt-writing page of earlier versions, and it also takes over the Markdown preview window: one window, two shapes.

## Opening the window

- **From the sidebar.** Click **Markdown**. The start page that opens has one job: getting the workbench window on screen. Its **Open the workbench** button opens the window — or, if it is already open, brings it to the front.
- **From a file.** In the file tree, **right-click a `.md` or `.markdown` file** and choose **Preview Markdown**. The window opens showing that document. If the window is already open, it switches to the new file instead of stacking up a second one.

**Why is there no Preview Markdown entry?** Two possibilities: the file is not a Markdown file by extension, or this build of the app cannot preview at all — the preview needs the version that embeds a browser engine. When the capability is missing the entry never appears, so there is nothing to discover and fail at.

## The two shapes

The window opens in one of two shapes, depending on how you got there:

- **The full shape** — opened from the sidebar. The workspace file tree is open on the left, the full ribbon band is on top, and your draft fills the middle.
- **The compact shape** — opened from a Markdown file. The file tree is collapsed to a thin edge, the ribbon is folded down to its row of tab names, and the document fills the window. It is the reading shape; the outline panel appears on the right when you turn the outline on.

While the window is open, each shape remembers its own settings — collapse something in the compact shape and the full shape still comes back the way you left it. The full shape also remembers its ribbon preferences (folded or not, style row, last tab, theme following) between sessions. Nothing is locked: the **Full Tools** switch on the ribbon's **View** tab flips between the two shapes, and you can fold or unfold anything at any time.

## The ribbon

The ribbon is the band of buttons across the top of the window. Its tabs are:

- **Start** — the everyday actions: **Copy Prompt** (this is how a prompt leaves the window — a second **Copy** sits in the small bar on the title row), the clipboard actions **Paste**, **Cut** and **Copy Text**, **Undo** and **Redo**, and the formatting buttons **Bold**, **Code** and **Bullets**. While a document is open, this tab also has **Find** and **Save**.
- **View** — what you see and how: the **Preview / Split / Source** switch and the outline toggle (both while a document is open), **Full Tools** (flip between the two shapes), **Collapse** (fold the whole band away), **Style** (one, two or three button rows), and **Follow Theme** (whether the band's colours track the app's theme).

- **Folding.** The ribbon can be folded down to its row of tab names to give the document more room — click the row to unfold it again. The compact shape starts folded.
- **Colours.** The ribbon follows the application's appearance theme automatically; **Follow Theme** can turn that off, and the band then keeps its own colours.
- Some custom builds show a simple toolbar instead of the ribbon. The actions are the same.

## Writing a prompt

1. Pick a folder from the workspace menu at the top of the file tree, or click the **folder button** (tooltip **Choose a workspace folder**) beside it to add one.
2. Write your prompt in the middle. Pressing Enter inserts a new line — it does not send anything. The line at the bottom of the window keeps count of your characters and lines.
3. Bring in files from the tree: **drag a file row into the editor**, or **double-click a file row**. This inserts a reference to that file, written as `` `./relative/path` ``, at the cursor.
4. Click **Copy Prompt** on the **Start** tab when you are done. The prompt goes to the clipboard with a confirmation message; paste it into the agent.

**Your draft is saved automatically.** Close the app, reopen it, and your text is still there.

The usual text shortcuts work in the editor: `Ctrl+Z` to undo, `Ctrl+Y` to redo, `Ctrl+X` / `Ctrl+C` / `Ctrl+V` to cut, copy and paste (see the [User guide](index.md)). Right-clicking inside the editor opens a menu with the same formatting buttons and clipboard actions.

Your draft has no rendered view — it lives in the editor alone. The rendered views belong to open documents:

## The three views

While a document is open, the buttons on the strip above it switch what the middle shows:

- **Preview** — the rendered document only.
- **Split** — the plain text on the left, the rendering on the right.
- **Source** — the plain text, with nothing rendered.

**Split** and **Source** are greyed out when the file cannot be written; only **Preview** stays usable, and the status line at the bottom says why.

## Reading a document

The strip above the document carries the reading controls: the **Preview / Split / Source** buttons, the **outline** button, the **search** button, the **text size** and **width** pickers, and the **More actions** menu.

- **Outline.** The panel on the right lists the document's headings; click one to jump there. A document with no headings shows **No headings**.
- **Text size.** Pick the body text size: **Theme size** follows the current appearance theme, or choose a fixed size from 12 px to 24 px.
- **Width.** Pick how wide the text column is: **Default width**, a fixed width, or **Full width** to use the whole window.
- **Status line.** It shows the file's path, its size, the rendering engine's version numbers, **Unsaved changes** while you have edits that are not written, and the reason if the file cannot be edited.

## Finding text in the document

Press `Ctrl+F`, or click the search button on the strip. Type your text and use **Next** and **Previous** to step through matches; the label tells you which match you are on (**3 of 17**). Turn on **Match case** (Aa) for a case-sensitive search. `Esc` closes the search bar.

Two things it will not do, both because of how the window renders long files quickly:

- It counts only the part that has been laid out. On a very long document, searching immediately after opening it can report fewer matches than it will a moment later — re-run the search once the whole document is rendered.
- It cannot find text that is split by formatting. Searching for `Markdown` will not match inside `**Mark**down`, because the bold markers break the word into pieces.

## Editing and saving

Type in **Split** or **Source**. The window asks before discarding your work:

- `Ctrl+S` writes the file. A confirmation appears, and the **Unsaved changes** mark goes away.
- Closing the window with unsaved edits asks: **Save and close**, **Discard changes**, or **Cancel**.
- If something else changes the file while you have unsaved edits, a dialog appears: **Reload from disk** takes the outside version and drops yours, **Keep my changes** keeps yours and stops asking about that particular change.
- `F5` re-reads the file from disk. If you have unsaved changes it asks you first, so `F5` cannot quietly throw your editing away.

**Saving preserves the file's shape.** If the file used Windows line endings, or had a byte-order mark at the start, saving keeps those. The window does not "tidy" your file into a different shape.

A file can be read-only for three reasons, and the status line names the one that applies: it is too big to edit here (previewing is still fine), it is not writable on disk, or its text is not valid UTF-8 — saving that back would permanently damage it, so editing is refused. Very large files (tens of megabytes) are not opened at all rather than opened and left struggling.

## Copying, opening elsewhere, exporting

All of these are under **More actions**, at the right end of the strip above the document:

- **Copy selection** copies the selected rendered text. Select something in the preview first; if nothing is selected the app tells you.
- **Copy Markdown source** copies the raw Markdown.
- **Open in browser** writes a copy of the rendered page and opens it in your system browser. The copy lands in the app's own data folder and is named after the document, so using this again on the same file replaces that copy.
- **Export HTML next to the document** writes a self-contained HTML file beside the Markdown file.
- **Export HTML to a folder...** lets you pick the folder.

Either export finishes with a message naming the file, and shows you where it landed. Both wait until the whole document has been rendered first — a document with many diagrams takes a moment before the file appears.

Note about the two exports: the exported page refers to images **relative to the document**, so pictures only appear if the HTML sits next to them. Exporting to a different folder is your choice, but the pictures will not come along.

## What renders, and what does not

Formulas, diagrams, tables, task lists and code highlighting work. Specifically:

- Formulas written with `$$ … $$`, `\[ … \]` and inline forms render as typeset formulas. A formula with a syntax error is shown in red rather than hidden.
- A code block whose language is `mermaid` becomes a diagram. Diagrams are drawn as you scroll near them, not all at once — a document with very many diagrams will ask you to click a diagram before drawing it.
- GitHub-style notice blocks (`> [!NOTE]` and its four relatives) are drawn as coloured callouts.
- **Embedded HTML is not rendered.** Tags written directly into the Markdown appear as text. This is a deliberate safety choice: this window shows files you did not write, and HTML in a document would otherwise be able to run code inside the app.

## Using the file tree

- **Click a folder** to expand or collapse it. Folders with contents show an arrow.
- **Double-click a file** to insert a reference to it into the draft.
- **Drag a file row** into the draft to insert its reference at the drop point.
- **Right-click a file row** for **Copy relative path** or **Copy absolute path**. On a Markdown file the menu also offers **Preview Markdown**. The entry only appears when this build can preview and the file is a Markdown file by extension.
- The tree refreshes automatically when files change; the refresh button next to the workspace menu forces a refresh.

## Managing workspace folders

- The **folder button** next to the workspace menu (tooltip **Choose a workspace folder**) opens a folder picker. Choose any folder on your computer.
- The workspace menu lists your folders, most recently used first. Pick one to switch to it.
- Each entry in that menu has an × to remove it from the list.
- The app remembers up to twenty folders. Adding a twenty-first drops the least recently used one.

## Tips

- Add the folder you are actually working in and keep a couple of others handy — switching is one click.
- Dragging a file into the middle of a sentence is often faster than writing the path.
- Compose a reusable instruction here once, copy it, and paste it into several agents.

## Troubleshooting

**A folder disappeared from the list.** Folders are capped at twenty and the least recently used one is dropped when you add another. Add it again if you still need it.

**The file tree is empty.** Check that the selected folder still exists and is not empty. The window says **The workspace folder is empty or unavailable.** in that case. Use the refresh button, or pick another folder.

**I cannot drag a file into the editor.** Make sure you are dragging a row from the file tree, not text from elsewhere. Dragging from the tree into the editor inserts a reference; dragging text within the editor moves that text.

**Copy does nothing.** If the editor is empty, the app tells you there is nothing to copy.

**The preview shows nothing.** Check the status line; it names the reason when the file cannot be read. An empty file says **The document is empty** in the preview area, and **Nothing to edit** in the editor.

**A diagram is blank or shows an error.** A diagram that fails to parse says **Diagram could not be drawn**; fix the text in **Source** and the block updates. In a document with a lot of diagrams, a placeholder says **Click to render this diagram** and stays empty until you click it — that is the window refusing to draw dozens of diagrams you were never going to look at.

**Something did not render.** If part of the document fails, a message appears and the rest of the document stays visible — the window never shows a half-blank page in silence.

**The window will not open a second file.** It is not a bug: one window shows one file at a time. Opening another replaces the current one (after asking about unsaved changes).

**The formula or diagram area stays empty while the text is fine.** That usually means the page could not load its resources. Reopen the file; if it keeps happening, this build's preview components are not installed correctly, and the message explains it.

Curious how it works inside? See [Markdown workbench](../development/markdown-workbench.md).
