# Markdown workbench

The Markdown workbench is **one window for two jobs**: writing prompts at your own pace, and reading or tidying Markdown files — several at once, in tabs. **Nothing is ever sent from this window** — pressing Enter only adds a new line, and when the text is ready you copy it and paste it into the agent yourself.

Find it in the sidebar as the row **Markdown**. It replaces the prompt-writing page of earlier versions, and it also takes over the Markdown preview window: one window, two shapes.

## Opening the window

- **From the sidebar.** Click **Markdown**. The start page that opens has one job: getting the workbench window on screen. Its **Open the workbench** button opens the window — or, if it is already open, brings it to the front.
- **From a file.** In the file tree, **right-click a `.md` or `.markdown` file** and choose **Preview Markdown**. The window opens showing that document as a new tab. If the window is already open, the file becomes a new tab in it instead of stacking up a second window.
- **From a double-click.** Once the app is associated with Markdown files (see [Settings](settings.md)), double-clicking a `.md` file in your file manager opens it here. If the app is already running, the file opens as a new tab and the window comes to the front; otherwise the app starts and shows it. **What starts is the full application** — the whole window with the sidebar — not a lightweight viewer, so give it the usual start-up moment.
- **Recent documents.** The start page lists the documents you opened most recently (up to six, file name plus the folder it sits in). Click one to open it as a tab.

**Why is there no Preview Markdown entry?** Two possibilities: the file is not a Markdown file by extension, or this build of the app cannot preview at all — the preview needs the version that embeds a browser engine. When the capability is missing the entry never appears, so there is nothing to discover and fail at.

## The two shapes

The window opens in one of two shapes, depending on how you got there:

- **The full shape** — opened from the sidebar. The workspace file tree is open on the left, the full ribbon band is on top, and your draft fills the middle. The left side also carries the **Templates** and **Favorites** panels under the file tree.
- **The compact shape** — opened from a Markdown file. The file tree is collapsed to a thin edge, the ribbon is folded down to its row of tab names, and the document fills the window. It is the reading shape; the outline panel appears on the right when you turn the outline on.

While the window is open, each shape remembers its own settings — collapse something in the compact shape and the full shape still comes back the way you left it. The full shape also remembers its ribbon preferences (folded or not, style row, last tab, theme following) between sessions. Nothing is locked: the **Full Tools** switch on the ribbon's **View** tab flips between the two shapes, and you can fold or unfold anything at any time.

## Tabs: several documents at once

The strip above the middle area holds one tab per open document, with a small dot on any tab that has **unsaved changes**. Click a tab to switch to it; click its × to close it — **closing a tab closes the view, never the file**: the document stays on disk, and opening it again is one click (or one double-click in the file tree).

- Up to eight documents can be open at once. Opening a ninth tells you to close a tab first — nothing is closed behind your back.
- Opening the same file twice does not open a second tab; it activates the one already there, edits included.
- Each tab remembers its own **Preview / Split / Source** setting; switching tabs switches you to that tab's view.

## The ribbon

The ribbon is the band of buttons across the top of the window. Its tabs are:

- **Start** — the everyday actions: **Copy Prompt** (this is how a prompt leaves the window — a second **Copy** sits in the small bar on the title row), the clipboard actions **Paste**, **Cut** and **Copy Text**, **Undo** and **Redo**, and the formatting buttons **Bold**, **Code** and **Bullets**. While a document tab is active, this tab also has **Find** and **Save**.
- **Prompt tools** — appears only while you are on the prompt (it disappears when you switch to a document): **Insert Template** opens the template library, **Save as Template** and **Save Favorite** store what you wrote. See the next two sections.
- **View** — what you see and how: the **Preview / Split / Source** switch and the outline toggle, **Full Tools** (flip between the two shapes), **Collapse** (fold the whole band away), **Style** (one, two or three button rows), and **Follow Theme** (whether the band's colours track the app's theme).

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

The draft has a rendered view too, like any document — use the **Preview / Split / Source** buttons above it. **Split** is the natural way to write a long prompt: the text on the left, the rendered result on the right.

## Templates: starting from a skeleton

The **Templates** panel in the left side of the window (labelled **Templates**, under the file tree) is a shelf of ready-made prompt skeletons — bug report, feature request, test plan, code review, and more. Ten are built in and always available; templates you save yourself appear in the same list.

- **Search** narrows the list as you type — the search matches titles, descriptions, tags and the template text itself.
- **Click a template** to insert it into your draft at the cursor. Inserting never replaces what you wrote: the skeleton is added at the cursor position, and if text was selected, it becomes the value of the template's `{{selection}}` placeholder.
- **Placeholders.** A template's text contains fields like `{{symptom}}` or `{{symptom:What visibly goes wrong}}`. If the template has any, a small form opens: one field per placeholder, pre-filled with the default when there is one. `Tab` moves between the fields, `Enter` inserts, `Esc` cancels. **A field you leave empty is a skip** — the placeholder stays in your text as `{{symptom}}`, so you can find it later instead of the prompt quietly shipping half-filled. The same placeholder appearing twice is filled once, everywhere.
- Two placeholders are filled for you and never asked: the current **workspace** path and today's **date**.

**Saving your own template:** with the prompt written the way you like it, use **Save as Template** (the **Prompt tools** tab of the ribbon, or the toolbar). You get a small form — the title (pre-filled from the first line of your draft), a category, a one-line description — and the template lands in your library immediately. Templates you save are ordinary files in the app's data folder, one file per template: you can edit them with any text editor, back them up, or drop them in a colleague's folder. A shipped template can be overridden by giving your file the same name.

## Favorites: finished prompts

Templates are skeletons with holes to fill; **favorites** are complete prompts you want to use again exactly as they are. The **Favorites** panel sits under the Templates panel (collapsed by default — click it open).

- **Save a favorite** with **Save Favorite** (the **Prompt tools** tab, or the toolbar): the whole draft becomes a favorite, its title taken from the first line. A toast confirms it.
- **Click a favorite** to insert its full text into the draft at the cursor.
- Hovering a favorite row reveals **copy** (straight to the clipboard, nothing inserted) and **remove**.
- Favorites live in the app's own settings storage, up to fifty; saving the same text again just refreshes it rather than taking a second slot.

**Which one is which?** If you find yourself deleting the placeholders every time, it is a favorite pretending to be a template. If you fill in the blanks every time, it is a template. The two panels keep them apart so the click does the right thing without asking.

## The three views

The buttons on the strip above the middle switch what it shows — for the draft or for the active document tab:

- **Preview** — the rendered result only.
- **Split** — the plain text on the left, the rendering on the right.
- **Source** — the plain text, with nothing rendered.

**Split** and **Source** are greyed out when the file cannot be written; only **Preview** stays usable, and the status line at the bottom says why. On builds without the preview capability the buttons are absent and the text stays in the editor.

## Reading a document

The strip above the middle carries the reading controls: the **Preview / Split / Source** buttons, the **outline** button, the **search** button, the **text size** and **width** pickers, and the **More actions** menu.

- **Outline.** The panel on the right lists the headings of whatever is open — the draft's headings included; click one to jump there. A document with no headings shows **No headings**.
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
- Closing the window with unsaved edits asks: **Save and close**, **Discard changes**, or **Cancel**. With more than one edited document the dialog counts them, and **Save all and close** writes every one of them — not just the tab you are looking at.
- If something else changes the file while you have unsaved edits, a dialog appears: **Reload from disk** takes the outside version and drops yours, **Keep my changes** keeps yours and stops asking about that particular change.
- `F5` re-reads the file from disk. If you have unsaved changes it asks you first, so `F5` cannot quietly throw your editing away.

**Saving preserves the file's shape.** If the file used Windows line endings, or had a byte-order mark at the start, saving keeps those. The window does not "tidy" your file into a different shape.

A file can be read-only for three reasons, and the status line names the one that applies: it is too big to edit here (previewing is still fine), it is not writable on disk, or its text is not valid UTF-8 — saving that back would permanently damage it, so editing is refused. Very large files (tens of megabytes) are not opened at all rather than opened and left struggling.

## Coming back to what you had open

Close the window and open it again later **in the same app run**, and everything is as you left it: the same tabs, the same active one, unsaved edits included. Only closing a tab or restarting the app clears anything.

**Restarting brings the tabs back.** The app remembers which documents were open, which tab was active, and each tab's view setting; reopening the workbench window after a restart restores them. A document whose file has since been moved or deleted is simply skipped — the rest still come back. The remembered shapes (full or compact, panels folded or not) come back too.

**The app does not open this window by itself at startup** — the start of the app stays fast and quiet. If you want the workbench (with its last tabs) already open when the app starts, turn on **Open the Markdown workbench on startup** in [Settings → Launchers](settings.md).

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
- A good bug report is mostly structure — start from the bug-report template and fill the fields; save your own shape as a template once you have tuned it.
- Prompts you keep re-typing belong in **Favorites**; prompts whose shape you keep re-typing belong in **Templates**.

## Troubleshooting

**A folder disappeared from the list.** Folders are capped at twenty and the least recently used one is dropped when you add another. Add it again if you still need it.

**The file tree is empty.** Check that the selected folder still exists and is not empty. The window says **The workspace folder is empty or unavailable.** in that case. Use the refresh button, or pick another folder.

**I cannot drag a file into the editor.** Make sure you are dragging a row from the file tree, not text from elsewhere. Dragging from the tree into the editor inserts a reference; dragging text within the editor moves that text.

**Copy does nothing.** If the editor is empty, the app tells you there is nothing to copy.

**The preview shows nothing.** Check the status line; it names the reason when the file cannot be read. An empty file says **The document is empty** in the preview area, and **Nothing to edit** in the editor.

**A diagram is blank or shows an error.** A diagram that fails to parse says **Diagram could not be drawn**; fix the text in **Source** and the block updates. In a document with a lot of diagrams, a placeholder says **Click to render this diagram** and stays empty until you click it — that is the window refusing to draw dozens of diagrams you were never going to look at.

**Something did not render.** If part of the document fails, a message appears and the rest of the document stays visible — the window never shows a half-blank page in silence.

**The window will not open a ninth document.** The tab strip holds eight documents at most. Close a tab you are done with — the file itself is never touched — and open the next one. Opening the same file twice does not need a second tab: it brings the existing one forward.

**A tab I reopened is empty or gone after a restart.** The remembered tabs are re-opened from their files; a file that has been moved or deleted is skipped (the rest come back), and a file that can no longer be read tells you why in the status line.

**Double-clicking a `.md` file opens something else.** The association only puts this app in the **Open with** list — the default app is chosen in Windows Settings. Right-click the file, choose **Open with**, and pick this app there (or set it as the default).

**The Prompt tools tab is missing.** It appears only while the prompt is what you are working on. Switch back with **Full Tools** on the **View** tab and it returns.

**A template inserted text with `{{placeholders}}` still in it.** That is what an empty field in the form means — the skipped field is left visible on purpose. Fill it in the draft, or re-insert the template and fill the field this time.

**My saved template is not in the list.** The list caps at thirty rows after a search; narrow the search, or check the template folder (the files are plain text — a file whose header is broken still loads, with the title falling back to the file name).

**The formula or diagram area stays empty while the text is fine.** That usually means the page could not load its resources. Reopen the file; if it keeps happening, this build's preview components are not installed correctly, and the message explains it.

Curious how it works inside? See [Markdown workbench](../development/markdown-workbench.md).
