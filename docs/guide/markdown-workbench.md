# Markdown workbench

The Markdown workbench is **one place for two jobs**: writing prompts at your own pace, and reading or tidying Markdown files — several at once, in tabs. **Nothing is ever sent from this place** — pressing Enter only adds a new line (or continues the list you are in), and when the text is ready you copy it and paste it into the agent yourself.

Find it in the sidebar under the **Agent Tools** label, as the row **Prompt Writing**. It is an advanced tool of the Agent Tools family that lives inside the app's main window — not a separate window: it fills the work area between the sidebar and the status bar.

## Opening the workbench

- **From the sidebar.** Click **Prompt Writing** (under **Agent Tools**). The workbench fills the work area; the first visit takes a moment to set itself up, after that switching back is instant.
- **From a file.** In the file tree, **double-click a `.md` or `.markdown` file**, or **right-click it** and choose **Preview Markdown**. The workbench opens with that document as a new tab.
- **From a double-click.** Once the app is associated with Markdown files (see [Settings](settings.md)), double-clicking a `.md` file in your file manager opens it here. If the app is already running, the file opens as a new tab and the app comes to the front; otherwise the app starts and shows it. **What starts is the full application** — the whole window with the sidebar — not a lightweight viewer, so give it the usual start-up moment.

**Why is Preview Markdown greyed out?** Two possibilities: the file is not a Markdown file by extension, or this build of the app cannot preview at all — the preview needs the version that embeds a browser engine. The entry stays in the menu either way, disabled rather than hidden, so an unusable action never leaves a blank gap where it used to be.

## The two shapes

The workbench shows one of two shapes, depending on what is active:

- **The full shape** — while you write a prompt. The workspace file tree is open on the left, the full ribbon band is on top, and your draft fills the middle.
- **The compact shape** — while you read a document. The file tree is collapsed to a thin edge, the ribbon is folded down to its row of tab names, and the document fills the work area. It is the reading shape; a floating outline panel appears over the right edge of the rendered page when you turn the outline on.
- **The left panel's width is yours** — drag the divider between the file tree and the middle area. Dragging it far to the left collapses the tree to its thin edge; the collapse button in the **Workspace** title row does the same thing; the small arrow button on that edge brings it back at its last width.

Each shape remembers its own settings while the workbench is alive — collapse something in the compact shape and the full shape still comes back the way you left it. The full shape also remembers its ribbon preferences (folded or not, style, last tab, theme following) between sessions. Nothing is locked: clicking a draft tab or a document tab flips between the two shapes, and you can fold or unfold anything at any time. The templates and favorites you used to find in the left panel live on the ribbon's **Prompt** tab now, and the checks and history that used to fill the right side open as small floating windows.

The workbench stays alive while you switch to other pages and back — your text, the undo history, the scroll position and the split proportion are exactly as you left them. It is created on its first visit and lives until the app closes.

## Tabs: several documents at once

The strip above the middle area holds one tab per open document **and one per draft**, with a small dot on any tab that has **unsaved changes**. Click a tab to switch to it; click its × to close it — **closing a tab closes the view, never the content**: the document stays on disk and the draft stays in the app's storage, and opening either again is one click.

- Up to eight documents can be open at once. Opening a ninth tells you to close a tab first — nothing is closed behind your back.
- Opening the same file twice does not open a second tab; it activates the one already there, edits included.
- Each tab remembers its own **Preview / Split / Source** setting; switching tabs switches you to that tab's view.
- Draft tabs mix in with document tabs; see [Drafts: several at once](#drafts-several-at-once).

## The ribbon

The ribbon is the band of buttons across the top of the workbench. Its tabs are:

- **Start** — the everyday actions: the clipboard actions **Paste**, **Cut**, **Undo** and **Redo**, the formatting buttons **Bold**, **Italic**, **Underline**, **Strikethrough**, **Link**, **Heading**, **Bullets**, **Numbered**, **Task List**, **Quote** and **Divider** (see [Formatting tools](#formatting-tools)), the insert buttons **Inline Code**, **Code Block** and **Table**, and **Copy Prompt** — this is how a prompt leaves the workbench. The find field also lives here (see [Finding](#finding)).
- **Prompt** — the library and the review tools: the **templates** and **favorites** galleries (click an entry to insert it), **Save as Template** and **Save Favorite** to store what you wrote, **Check Prompt** to review the draft, and **History** to browse its versions (see [Prompt checks](#prompt-checks) and [Version history](#version-history)). The floating windows it opens never block your writing — leave them open as long as you like.
- **View** — what you see and how: the **Preview / Split / Source** switch, the **Font Size** and **Width** pickers, the outline toggle, **Collapse** (fold the whole band away), **Style** (compact or loose), and **Follow Theme** (whether the band's colours track the app's theme).

- **Folding.** The ribbon can be folded down to its row of tab names to give the document more room — click a tab name and its buttons drop down as a floating panel until you click elsewhere; double-click a tab to toggle folding. The compact shape starts folded.
- **Colours.** The ribbon follows the application's appearance theme automatically; **Follow Theme** can turn that off, and the band then keeps its own colours.
- **Style.** The **Style** picker on the View tab switches the band between **compact** and **loose**. In the loose style the Start tab's clipboard and formatting buttons carry their names under their icons; in the compact style they keep to their icons alone to leave more room for the document.
- Some custom builds show a simple toolbar instead of the ribbon. The actions are the same.

## Writing a prompt

1. Pick a folder from the workspace menu at the top of the file tree, or click the **folder button** (tooltip **Choose a workspace folder**) beside it to add one.
2. Write your prompt in the middle. Pressing Enter never sends anything — on a plain line it inserts a new line, and on a list, numbered, quote or task item it continues the marker on the next line (an empty item leaves the list instead). The line at the bottom keeps count of your characters and lines, and estimates the prompt's token size (an estimate — see [The token estimate](#the-token-estimate)).
3. Bring in files from the tree: **drag a file row into the editor**, or **right-click a file row** and choose **Insert path at cursor**. Either way a reference to that file, written as `` `./relative/path` ``, lands at the cursor. Double-clicking a file row does something else now: it **opens** the file (a Markdown file becomes a new document tab; anything else goes to the app your system associates with it).
4. Click **Copy Prompt** on the **Start** tab when you are done. The prompt goes to the clipboard with a confirmation message; paste it into the agent.

**Your drafts are saved automatically.** Close the app, reopen it, and your texts are still there.

The usual text shortcuts work in the editor: `Ctrl+Z` to undo, `Ctrl+Y` to redo, `Ctrl+X` / `Ctrl+C` / `Ctrl+V` to cut, copy and paste (see the [User guide](index.md)). While the editor has focus, `Ctrl+B` is bold, `Ctrl+I` italic, `Ctrl+K` link and `Ctrl+E` inline code — everywhere else `Ctrl+B` keeps its global meaning (collapse the sidebar). Right-clicking inside the editor opens a menu with the same formatting buttons and clipboard actions. **Tab** behaves like in a code editor: with several lines selected it indents every selected line (press it again to indent further, `Shift+Tab` to outdent, `Ctrl+Z` to step back); with nothing or one line selected it inserts one indent unit — spaces by default (2 per level, the portable way to nest Markdown lists), configurable in [Settings → Markdown](settings.md#markdown). **Typing a Markdown mark pairs it**: `*`, a backtick, `~`, `[` or `(` also inserts the closing mark and puts the cursor in between; typing the mark again skips over the closer. All of these typing behaviors have switches in [Settings → Markdown](settings.md#markdown). **Hold Ctrl and scroll the mouse wheel** over the text to make the editor's letters bigger or smaller — a reading zoom that resets when the app restarts.

The draft has a rendered view too, like any document — use the **Preview / Split / Source** buttons above it. **Split** is the natural way to write a long prompt: the text on the left, the rendered result on the right.

## Drafts: several at once

You are not limited to one prompt-in-progress. The **+ button at the right end of the tab strip** starts a fresh draft in a new tab; the previous one stays in its own tab, text and all. Switch drafts by clicking their tabs — each draft keeps its own undo history, so undo never reaches into a draft you were writing before.

- **Rename** a draft from its tab's right-click menu; the title shows in the tab.
- **Delete** (also in the right-click menu) asks first and cannot be undone: the draft's text, its saved versions and its crash-recovery copy all go together.
- **Closing a draft's tab is not deleting it**: the draft lives in the app's own storage and comes back when you open it again.

## Version history

While you write, the app quietly keeps versions of each draft — when you copy it or switch to another draft. The **History** button on the ribbon's **Prompt** tab opens a small floating window that lists the current draft's versions, newest first, with the reason each was taken; **Save version** in the same window marks one by hand. **Click a version to roll back to it.** A confirmation dialog explains that your current text is saved as a version first, so rolling back never loses anything. The window never blocks your writing — leave it open or close it at any time.

If the app closes unexpectedly, the next start asks once whether to **Restore** the draft text from that session (it becomes a new draft) or **Discard** it.

## Templates: starting from a skeleton

The **templates gallery** on the ribbon's **Prompt** tab is a shelf of ready-made prompt skeletons — bug report, feature request, test plan, code review, and more. Ten are built in and always available; templates you save yourself appear in the same shelf, grouped by their category (the arrow buttons on the gallery's right edge page through the groups, and the bottom button opens the full shelf as a bigger grid).

- **Click a template** to insert it into your draft at the cursor. Inserting never replaces what you wrote: the skeleton is added at the cursor position, and if text was selected, it becomes the value of the template's `{{selection}}` placeholder.
- **Placeholders.** A template's text contains fields like `{{symptom}}` or `{{symptom:What visibly goes wrong}}`. If the template has any, a small form opens: one field per placeholder, pre-filled with the default when there is one. `Tab` moves between the fields, `Enter` inserts, `Esc` cancels. **A field you leave empty is a skip** — the placeholder stays in your text as `{{symptom}}`, so you can find it later instead of the prompt quietly shipping half-filled. The same placeholder appearing twice is filled once, everywhere.
- Two placeholders are filled for you and never asked: the current **workspace** path and today's **date**.

**Saving your own template:** with the prompt written the way you like it, use **Save as Template** (the **Prompt** tab of the ribbon, or the toolbar). You get a small form — the title (pre-filled from the first line of your draft), a category, a one-line description — and the template lands in your library immediately. Templates you save are ordinary files in the app's data folder, one file per template: you can edit them with any text editor, back them up, or drop them in a colleague's folder. A shipped template can be overridden by giving your file the same name.

## Favorites: finished prompts

Templates are skeletons with holes to fill; **favorites** are complete prompts you want to use again exactly as they are. The **favorites gallery** sits beside the templates gallery on the **Prompt** tab.

- **Save a favorite** with **Save Favorite** (the **Prompt** tab, or the toolbar): the whole draft becomes a favorite, its title taken from the first line. A toast confirms it.
- **Click a favorite** to insert its full text into the draft at the cursor.
- Favorites live in the app's own settings storage, up to fifty; saving the same text again just refreshes it rather than taking a second slot.

**Which one is which?** If you find yourself deleting the placeholders every time, it is a favorite pretending to be a template. If you fill in the blanks every time, it is a template. The two galleries keep them apart so the click does the right thing without asking.

## Formatting tools

The **Start** tab of the ribbon carries the formatting buttons: **Bold**, **Italic**, **Underline**, **Strikethrough**, **Inline Code**, **Link**, **Heading** (a menu with levels 1, 2, 3 and a remove option), **Bullets**, **Numbered**, **Task List**, **Quote**, **Divider**, **Code Block** (a menu of common languages) and **Table**.

- Everything is **one undo step**: press `Ctrl+Z` right after any of them and the whole change reverts.
- Toggles are honest: applying **Numbered** to lines that are all numbered removes the numbers again; headings change level instead of stacking `###` onto `##`. **Bold**, **Italic**, **Underline**, **Strikethrough** and **Inline Code** toggle too — selecting text that is already bold and pressing **Bold** (or `Ctrl+B`) removes the markers instead of doubling them.
- **Inline Code wraps, Code Block fences.** Inline Code wraps the selection in backticks. Code Block fences whole lines — the selected lines (or the line the cursor is on) become a code block with the language you picked on its first line; with the cursor on an empty line it drops an empty block and puts the cursor inside, ready to type.
- In a document tab, the same buttons work in the source editor when the file is editable — the formatting is not prompt-only.

- **Headings can be re-levelled.** The **Heading** menu's **Promote heading** and **Demote heading** entries move the headings under the cursor one level up or down (## becomes #; promoting a # removes the heading entirely).

Besides `Ctrl+F` (find — see [Finding](#finding)), the rest of the keyboard shortcuts are below, and they work while the caret is in the text.

## Editing shortcuts

These work in the prompt editor and in a document's source view. The ones that change text need an editable document; the navigation ones work on read-only files too:

- `Ctrl+B` / `Ctrl+I` / `Ctrl+K` / `Ctrl+E` — bold, italic, link, inline code (the buttons on the **Start** tab, see [Formatting tools](#formatting-tools)). They belong to the editor: outside it, `Ctrl+B` keeps its global meaning (collapse the sidebar).
- `Alt+↑` / `Alt+↓` — move the line (or the selected lines) up / down.
- `Shift+Alt+↓` — copy the line below itself.
- `Ctrl+Shift+K` — delete the whole line.
- `Ctrl+L` — select the whole line; `Ctrl+D` — select the word at the caret.
- `Ctrl+G` — go to a line number (a small dialog asks; 1 is the first line).
- The right-click menu carries the same line operations (move, duplicate, delete, select line) and the three case conversions (**UPPERCASE**, **lowercase**, **Title Case**) — they act on the selection, or on the word at the caret.
- The status line at the bottom always shows where the caret is (`Ln 3, Col 14`).

## Finding

A find field sits at the end of the **Start** tab, and `Ctrl+F` jumps to it from anywhere. Type what you are looking for and press `Enter`: the first match is found and selected. Press `Enter` again to jump to the next match (a toast tells you when the search has wrapped back to the start).

- Every match in the text gets a soft background highlight while you search, and the field shows `3/12` — which match you are on, out of how many.
- **Replacing:** the field under the find field is the replacement. Type into both, then press `Enter` in the replace field (or click **Replace**) to swap the current match for your text and jump to the next; **Replace All** swaps every match in one undo step and tells you how many it replaced. `Ctrl+H` jumps to the replace field from anywhere.
- In a document tab the find field searches the rendered page, but replace always writes the source text — switch the document to **Split** or **Source** to see what a replacement changed.
- The **×** in the field clears it — the highlight and the `3/12` count go with it — and hands focus back to the editor; `Esc` does the same while the caret is in the field.
- The search is case-insensitive (and whole-word off) by default; the case setting you may have set in an older version still applies.

## Prompt checks

The **Check Prompt** button on the **Prompt** tab opens a small floating window that reviews the draft and returns a list of suggestions — never a score. Opening the window runs the check automatically.

- Every finding is one line with a coloured dot: grey for a hint, yellow for a caution, red for a warning.
- **Click a finding** and the editor jumps to the place it is about, selecting the relevant text (findings about the whole prompt, like a missing acceptance line, have nowhere to jump).
- **Tired of one kind of advice?** The × on a finding's row turns that rule off, everywhere, permanently — the list re-runs and that class of suggestion disappears. (There is no settings screen for turning rules back on yet: the list lives in the app's `tools.json` file, in the `checks` section.)
- What it looks for: vague wording, a prompt that only asks questions, no acceptance criteria, Windows-style paths that should be `./like/this`, no tech stack or files named, a context budget risk, no structure in a long prompt, `{{placeholders}}` left unfilled, trailing whitespace, and smart quotes that break command lines.

## The token estimate

The status line at the bottom shows `~1,240 tokens (estimated)` for the draft (and the same kind of number for an open document). **This is an estimate** — a rough measure of how much context the agent will read, not a promise that matches any model's exact tokenizer. Treat it as a magnitude: a few hundred is small, tens of thousands is a lot.

- The colour follows the size: neutral when small, warning-coloured at a quarter of the context window, danger-coloured above three fifths.
- The window size the estimate is measured against is 32000 tokens by default. It can be changed in the app's `tools.json` file, in the `editor` section (the `contextWindowTokens` value) — there is no settings screen for it yet.
- Hovering the number in the draft explains what it is measured against.

**Clearing the whole draft** (the **Clear** button on the **Start** tab, prompt shape only) always asks first, and `Ctrl+Z` brings the text back even after you confirm.

## The three views

The **Preview / Split / Source** buttons switch what the middle shows — for the draft or for the active document tab. They live in two places, and the two always agree:

- On the ribbon's **View** tab (on the plain toolbar, at its right end).
- At the **right end of the status line** at the bottom of the workbench — **Source**, **Split**, **Preview**, then a divider, then the **Outline** switch — the copy within reach while you write.

Exactly one of the three is always on; clicking the button that is already on keeps it on.

- **Preview** — the rendered result only.
- **Split** — the plain text on the left, the rendering on the right.
- **Source** — the plain text, with nothing rendered.

**Split** and **Source** are greyed out when the file cannot be written; only **Preview** stays usable, and the status line at the bottom says why. On builds without the preview capability the buttons are absent and the text stays in the editor.

## Reading a document

The strip above the middle answers one question — *which document am I on*: the tabs, plus the **new draft** button (+) and the **More actions** menu at the strip's right end. The reading options that are not about *where* but about *how* sit with the three views on the **View** tab:

- **Outline.** A floating panel over the rendered page's right edge lists the headings of whatever is open — the draft's headings included; click one to jump there. A document with no headings shows **No headings**. It takes no space from the prompt tools on the right, and it is only present while a rendered view is. The panel is yours to shape: the button at the right of its title row collapses it down to the title row (click it again to bring the list back), the grip in its bottom-right corner drags its height, and dragging its left edge makes it wider or narrower — the panel stays anchored to the right edge of the rendered page either way. These choices live in the app's memory: they survive switching pages, and restarting the application starts the panel full-height again.
- **Text size.** The **Font Size** menu on the **View** tab picks the body text size: **Theme size** follows the current appearance theme, or choose a fixed size from 12 px to 24 px.
- **Width.** The **Width** menu on the **View** tab picks how wide the text column is: **Default width**, a fixed width, or **Full width** to use the whole work area.
- **Status line.** It shows the file's path, its size, the rendering engine's version numbers, a token estimate for the file's text, where the caret is while the source view is on screen (`Ln 3, Col 14`), **Unsaved changes** while you have edits that are not written, and the reason if the file cannot be edited.

## Finding text in the document

Press `Ctrl+F` — the find field on the **Start** tab takes the caret. Type your text and press `Enter` to jump through the matches on the rendered page.

Two things it will not do, both because of how the workbench renders long files quickly:

- It counts only the part that has been laid out. On a very long document, searching immediately after opening it can report fewer matches than it will a moment later — re-run the search once the whole document is rendered.
- It cannot find text that is split by formatting. Searching for `Markdown` will not match inside `**Mark**down`, because the bold markers break the word into pieces.

## Editing and saving

Type in **Split** or **Source**. The app asks before discarding your work:

- `Ctrl+S` writes the file. A confirmation appears, and the **Unsaved changes** mark goes away.
- Exiting the app with unsaved edits asks: **Save and exit**, **Discard changes**, or **Cancel**. With more than one edited document the dialog counts them, and **Save all and exit** writes every one of them — not just the tab you are looking at.
- If something else changes the file while you have unsaved edits, a dialog appears: **Reload from disk** takes the outside version and drops yours, **Keep my changes** keeps yours and stops asking about that particular change.
- `F5` re-reads the file from disk. If you have unsaved changes it asks you first, so `F5` cannot quietly throw your editing away.

**Saving preserves the file's shape.** If the file used Windows line endings, or had a byte-order mark at the start, saving keeps those. The app does not "tidy" your file into a different shape.

A file can be read-only for three reasons, and the status line names the one that applies: it is too big to edit here (previewing is still fine), it is not writable on disk, or its text is not valid UTF-8 — saving that back would permanently damage it, so editing is refused. Very large files (tens of megabytes) are not opened at all rather than opened and left struggling.

## Coming back to what you had open

Switching to another page and back keeps everything as you left it: the same tabs, the same active one, unsaved edits included. Only closing a tab or restarting the app clears anything.

**Restarting brings the tabs back.** The app remembers which documents and drafts were open, which tab was active, and each tab's view setting; the first visit to the workbench after a restart restores them. A document whose file has since been moved or deleted — or a draft that was deleted — is simply skipped; the rest still come back. The remembered shapes (full or compact, panels folded or not) come back too.

**The app does not open the workbench by itself at startup** — the start of the app stays fast and quiet. If you want the workbench (with its last tabs) already open when the app starts, turn on **Open the Markdown workbench on startup** in [Settings → Launchers](settings.md).

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
- A code block whose language is `mermaid` becomes a diagram. Diagrams are drawn as you scroll near them, not all at once — a document with very many diagrams will ask you to click a diagram before drawing it. On the build that runs on the older engine (the fallback platform), diagrams are beyond what that engine can execute: each diagram shows its source with a note instead of a drawing, while formulas, tables and code highlighting are unaffected.
- GitHub-style notice blocks (`> [!NOTE]` and its four relatives) are drawn as coloured callouts.
- **Embedded HTML is not rendered.** Tags written directly into the Markdown appear as text. This is a deliberate safety choice: this workbench shows files you did not write, and HTML in a document would otherwise be able to run code inside the app.

## Using the file tree

- **Click a folder** to expand or collapse it. Folders with contents show an arrow.
- **Double-click a file** to open it: a Markdown file opens as a new document tab in the workbench, any other file opens with whatever app your system associates with it.
- **Drag a file row** into the draft to insert its reference at the drop point.
- **Right-click a file row** for **Preview Markdown**, **Insert path at cursor**, **Copy relative path** or **Copy absolute path**. **Insert path at cursor** puts the file's `` `./relative/path` `` reference into the draft where your cursor is. **Preview Markdown** is greyed out when the file is not a Markdown file by extension or when this build cannot preview — it stays visible, disabled rather than hidden.
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
- Long prompt? Run **Check Prompt** before copying — the findings usually name the one thing missing.
- The find field remembers nothing between searches — type and go.

## Troubleshooting

**A folder disappeared from the list.** Folders are capped at twenty and the least recently used one is dropped when you add another. Add it again if you still need it.

**The file tree is empty.** Check that the selected folder still exists and is not empty. The workbench says **The workspace folder is empty or unavailable.** in that case. Use the refresh button, or pick another folder.

**I cannot drag a file into the editor.** Make sure you are dragging a row from the file tree, not text from elsewhere. Dragging from the tree into the editor inserts a reference; dragging text within the editor moves that text.

**Copy does nothing.** If the editor is empty, the app tells you there is nothing to copy.

**The preview shows nothing.** Check the status line; it names the reason when the file cannot be read. An empty file says **The document is empty** in the preview area, and **Nothing to edit** in the editor.

**A diagram is blank or shows an error.** A diagram that fails to parse says **Diagram could not be drawn**; fix the text in **Source** and the block updates. In a document with a lot of diagrams, a placeholder says **Click to render this diagram** and stays empty until you click it — that is the workbench refusing to draw dozens of diagrams you were never going to look at.

**Something did not render.** If part of the document fails, a message appears and the rest of the document stays visible — a half-blank page is never shown in silence.

**A ninth document will not open.** The tab strip holds eight documents at most. Close a tab you are done with — the file itself is never touched — and open the next one. Opening the same file twice does not need a second tab: it brings the existing one forward.

**A tab I reopened is empty or gone after a restart.** The remembered tabs are re-opened from their files; a file that has been moved or deleted is skipped (the rest come back), and a file that can no longer be read tells you why in the status line.

**Double-clicking a `.md` file opens something else.** The association only puts this app in the **Open with** list — the default app is chosen in Windows Settings. Right-click the file, choose **Open with**, and pick this app there (or set it as the default).

**The Prompt tab's galleries are empty.** The templates need nothing to show — the built-ins are always there. If the grid looks empty, page the groups with the gallery's arrow buttons or open the full shelf with the bottom button; templates you saved appear under their own category group.

**A template inserted text with `{{placeholders}}` still in it.** That is what an empty field in the form means — the skipped field is left visible on purpose. Fill it in the draft, or re-insert the template and fill the field this time.

**My saved template is not in the list.** The list caps at thirty rows after a search; narrow the search, or check the template folder (the files are plain text — a file whose header is broken still loads, with the title falling back to the file name).

**The formula or diagram area stays empty while the text is fine.** That usually means the page could not load its resources. Reopen the file; if it keeps happening, this build's preview components are not installed correctly, and the message explains it.

**I turned a check off and want it back.** The off-switch list lives in the app's `tools.json` file (the `checks` section, under `disabled`): remove the rule's name from that list and the advice returns. There is no settings screen for it yet.

**`Ctrl+F` found nothing but the text is right there.** The field searches the draft's source text in the prompt, and the rendered page in a document tab — the routing is automatic, but the rendered page is what the renderer produced (collapsed details, generated diagrams' alt text and so on can differ from the source).

**The token estimate looks wrong.** It is an estimate — see [The token estimate](#the-token-estimate). If the magnitude is consistently off for how you work, the window size it is measured against can be changed in `tools.json` (`editor` → `contextWindowTokens`).

Curious how it works inside? See [Markdown workbench](../development/markdown-workbench.md).
