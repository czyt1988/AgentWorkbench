# Agent Tools

**Agent Tools** is a prompt-writing scratchpad. You compose a prompt at your own
pace in a text editor, with a file tree beside it, and copy the finished text
into the agent when you are ready. **Nothing is ever sent from this page.**

## What it is

A two-pane workbench: the editor on the left, a browser for one of your project
folders on the right. Pressing Enter only adds a new line - there is no send
button to hit by accident.

## When to use it

- When a prompt is long enough that typing it directly into an agent's input box
  risks sending it half-finished.
- When you want to refer to files in your project but typing their paths by hand
  is tedious.
- When you want to keep one prompt around while you work.

## The two panes

- **Left: the editor.** This is where you write. It highlights Markdown
  formatting as you type. A long prompt scrolls — use the scroll bar on its
  right edge or the mouse wheel — and the caret is kept in view while you type.
- **Right: the file tree.** It shows the files in the workspace folder you
  selected. A draggable divider separates the two - drag it to give either side
  more room.

Above the panes is a toolbar with the workspace selector, **Add Folder...**, a
refresh button for the tree, and the **Copy** button at the right.

## Writing a prompt

1. Pick a folder from the **Workspace** menu, or click **Add Folder...** to add
   one.
2. Write your prompt in the editor. Pressing Enter inserts a new line - it does
   not send anything.
3. Bring in files from the tree: **drag a file row into the editor**, or
   **double-click a file row**. This inserts a reference to that file, written as
   `` `./relative/path` ``, at the cursor.
4. Click **Copy** when you are done. The prompt goes to the clipboard with a
   confirmation message; paste it into the agent.

**Your draft is saved automatically.** Close the app, reopen it, and your text is
still there.

## Formatting and editing

Right-click inside the editor for a menu with:

- A row of quick-format buttons: **Bold**, **Code** (inline code) and
  **Bulleted list**.
- **Undo** and **Redo**.
- **Cut**, **Copy** and **Paste**.

The usual keyboard shortcuts work too (see the [User guide](index.md)).

## Using the file tree

- **Click a folder** to expand or collapse it. Folders with contents show an
  arrow.
- **Double-click a file** to insert a reference to it into the editor.
- **Drag a file row** into the editor to insert its reference at the drop point.
- **Right-click a file row** for **Copy relative path** or
  **Copy absolute path**.
- The tree refreshes automatically when files change; the refresh button in the
  toolbar forces a refresh.

## Managing workspace folders

- **Add Folder...** opens a folder picker. Choose any folder on your computer.
- The **Workspace** menu lists your folders, most recently used first. Pick one
  to switch to it.
- Each entry in that menu has an × to remove it from the list.
- The app remembers up to twenty folders. Adding a twenty-first drops the least
  recently used one.

## Tips

- Add the folder you are actually working in and keep a couple of others handy -
  switching is one click.
- Dragging a file into the middle of a sentence is often faster than writing the
  path.
- Compose a reusable instruction here once, copy it, and paste it into several
  agents.

## Troubleshooting

**A folder disappeared from the list.** Folders are capped at twenty and the
least recently used one is dropped when you add another. Add it again if you
still need it.

**The file tree is empty.** Check that the selected folder still exists and is
not empty. The page says **The workspace folder is empty or unavailable.** in
that case. Use the refresh button, or pick another folder.

**I cannot drag a file into the editor.** Make sure you are dragging a row from
the file tree, not text from elsewhere. Dragging from the tree into the editor
inserts a reference; dragging text within the editor moves that text.

**Copy does nothing.** If the editor is empty, the app tells you there is nothing
to copy.

Curious how it works inside? See [Agent tools](../development/agent-tools.md).
