# Skills

The **Skills** page collects the "skill" instruction files scattered around your
computer - the `SKILL.md` files that agents and editors read - into one
searchable list, with each one's folder shown so you can find it again.

## What it is

A skill is a folder containing a `SKILL.md` file that describes something an AI
assistant can do. Different tools keep their skills in different places. This
page scans the usual places and lists what it finds, without changing or moving
anything.

## When to use it

- To see which skills are installed on your computer.
- To find out where a particular skill lives.
- To copy a skill's folder path so you can paste it somewhere else.
- To add your own folder of skills to the list.

## How it works

The page scans the folders listed under **Settings → Skills**, collects every
`SKILL.md` it finds, and shows one card per skill. Scanning happens in the
background; the page stays usable while it runs, and a short line at the bottom
reports how many skills were found and how long it took.

## Searching, filtering and sorting

- **Search box** - matches the skill's name or description.
- **Filter buttons** - **All**, **Agents**, **Claude**, **Codex**, **Plugin**,
  **Project**, **Custom**. Each filters by where the skill came from. Click a
  button to filter by that source; click it again to turn the filter off.
- **Sort menu** - **Sort: name**, **Sort: modified** or **Sort: source**.
- **Rescan** - runs the scan again right away.

## Reading a card

Each card shows the skill's name, a small badge for its source, a short
description, and its folder path, plus a copy button.

Hover a card for a moment and a details panel opens with the full description,
the source, the exact location of the `SKILL.md` file, when it was last
modified, its size, and any extra information the file declares.

## Actions

- **Click a card** to copy its folder path. A message confirms it.
- **Right-click a card** for:
  - **Copy path** - the skill's folder.
  - **Copy SKILL.md path** - the file itself.
  - **Copy name** - the skill's name.
  - **Open containing folder** - opens the folder in your file manager.
  - **Reveal SKILL.md** - opens the file manager with the file selected.
- With a card selected by keyboard, **Enter** copies the path and
  **Ctrl+Enter** opens the folder.

## Choosing where to look

Open **Settings → Skills** to control the scan.

- Each existing source has a **switch** to turn it on or off, and an × to remove
  it.
- The text box at the bottom adds a folder of your own: type or paste a path and
  click **Add**, or press Enter.
- **Rescan** runs a fresh scan.

In a path you can use:

- `~` for your home folder, for example `~/.agents/skills`.
- `%NAME%` for an environment variable, for example `%USERPROFILE%/my-skills`.
- `*` as a wildcard for a part of the path.

!!! warning "Adding your own source replaces the built-in list"
    As long as you have not added a source of your own, the page uses the
    built-in list of folders. **The moment you add your first custom source, that
    list becomes the entire list** - the built-in folders are no longer scanned
    unless you add them back yourself. If you want to keep one of the built-in
    folders, add it again by hand before or after removing the defaults.

### The built-in folders

With no custom sources, the page looks in these places:

| Label | Folder |
|---|---|
| Agents | `~/.agents/skills` |
| Claude | `~/.claude/skills` |
| Codex | `~/.codex/skills` |
| ZCode plugins | the editor-plugin cache |
| Project (.agents) | `.agents/skills` in the current project |
| Project (.claude) | `.claude/skills` in the current project |

### Why plugin skills show up once

Editors keep several versions of the same plugin side by side. When the same
plugin appears more than once, only the newest version is listed, so the list
does not fill up with duplicates.

## Tips

- Use the search box rather than scrolling - there can be a lot of skills.
- Clicking a card is the fastest way to get a path for pasting into an agent or
  a settings file.
- If a skill you know exists is missing, check that its folder is one of the
  scanned sources, and press **Rescan**.

## Troubleshooting

**Nothing is found at all.** Open **Settings → Skills** and check that at least
one source is switched on, and that its path is correct. If you added a custom
source, remember that it replaced the built-in list.

**Odd files appear in the results.** The scan looks for `SKILL.md` files
wherever they are, including inside other tools' caches. Turn off the source you
do not care about.

**Scanning is slow.** Very large or deeply nested folders take longer to walk.
Remove sources you do not need, or point a source at a narrower folder. (A depth
limit exists as an advanced settings entry, but changing it is rarely necessary.)

Curious how it works inside? See [Skill browser](../development/skill-browser.md).
