# Appearance

**Appearance** is where you change the application's color scheme and font. Two
color schemes ship built in - one dark, one light - and both are meant to be
readable.

## What it is

The **Settings → Appearance** section has three controls: **Theme**, the
**Follow system color scheme** switch, and **Font**. All of them take effect
immediately; you do not need to restart the app.

## When to use it

- To switch between the dark and light schemes.
- To let the app follow your system's light/dark setting.
- To use a different font for the whole application.
- To install your own custom color scheme.

## Changing the theme

Open **Settings → Appearance** and use the **Theme** menu. It lists every
available theme. The two built-in ones appear as **Dark** and **Light**; hover
the menu to see their full names (**Catppuccin Mocha (Dark)** and
**Catppuccin Latte (Light)**).

Pick one and the whole interface changes at once - sidebar, cards, web tabs,
dialogs, everywhere.

Both built-in schemes are designed to be readable. If any text or control is
hard to see in one of them, that is worth reporting.

## Following the system color scheme

Below the **Theme** menu there is a switch: **Follow system color scheme**. Turn
it on and the app uses the built-in dark or light scheme to match your
operating system's light/dark setting - and switches right away if the system
setting changes while the app is open.

While the switch is on, the **Theme** menu is dimmed: your explicit pick is kept
but not used. Turn the switch off to go back to the theme you picked.

On some builds the switch itself is dimmed. There the app cannot detect the
system setting and keeps the theme you picked.

## Changing the font

Use the **Font** menu just below. The first entry, **Theme default**, means "use
the font the theme or your system provides". Below it is the list of fonts
installed on your computer; pick one to use it everywhere in the app.

The change is applied immediately.

## Making your own theme

You can create a color scheme by hand. This is for people who like to tinker -
the built-in ones are fine for everyday use.

1. In your data folder, find the `themes` subfolder. (Open it from **Settings →
   Advanced → Open data folder**.) If `themes` does not exist, create it.
2. Copy one of the built-in theme files into it. The built-in files are named
   `mocha-dark.json` (dark) and `latte-light.json` (light).
3. Rename the copy so its name matches the `id` you want, for example
   `my-theme.json`, and edit the contents.
4. Save the file. The app notices the change and reloads it right away.
5. Open **Settings → Appearance** and pick your theme from the **Theme** menu.

Rules to keep in mind:

- **The file name must match the `id` inside the file.** A file named
  `my-theme.json` must contain `"id": "my-theme"`. A mismatch means the file is
  ignored.
- **Colors are written as six-digit hexadecimal values**, for example
  `#89B4FA`.
- **Anything you leave out falls back to the built-in theme** with the same
  `variant` (dark or light).
- **Unrecognized entries are ignored**, so a small typo will not break the app.

A minimal theme file looks like this. The field names are part of the file
format, so keep them in English:

```json
{
  "id": "my-theme",
  "name": "My Theme",
  "variant": "dark",
  "colors": {
    "windowBg": "#1e1e2e",
    "sidebarBg": "#181825",
    "accent": "#89b4fa",
    "textPrimary": "#cdd6f4"
  }
}
```

`variant` is either `dark` or `light`; it decides which built-in theme supplies
the colors you did not set. The other keys name the colors of the interface. A
good starting point is to copy a built-in file and change only the colors you
care about.

If you give a custom theme the same `id` as a built-in one, yours takes
precedence.

## Per-agent accent colors

The color that highlights an agent's card is set on the agent itself, not here -
see [Agent launcher](agent-launcher.md). If you leave it empty, a color is
assigned automatically from the current theme's palette.

## Troubleshooting

**My theme does not appear in the list.** Check that the file is in the `themes`
subfolder of your data folder, that its name ends in `.json`, and that the name
matches the `id` inside the file.

**I picked a theme but nothing changed.** Check that you picked the right entry.
A theme with no colors set falls back entirely to a built-in theme, so it can
look identical. Add at least one color that differs. Also check the **Follow
system color scheme** switch: while it is on, your pick is kept but not used.

**Some part of the interface is hard to read.** Switch to the other built-in
theme to compare, and report it - both built-in schemes are supposed to be
readable.

**The font did not change.** Choose **Theme default** and then your font again,
and make sure the font is actually installed on your system.

Curious how it works inside? See [Theme engine](../development/theme-engine.md).
