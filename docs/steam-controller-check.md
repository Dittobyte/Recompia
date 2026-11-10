# Steam Controller (2026): 15-minute hardware check

For anyone who owns the new Steam Controller. No skills needed: press, look, write down what happened. You need the **Testing build 0.10.2-beta.2 or newer** (Settings > About > Update channel: Testing), your own game files, and a USB cable or the Puck. Use a test save, not your main one.

## Before you start
1. Plug the controller in (cable or Puck) **before** you press Play. Close Steam if you can (we test with Steam Input off first).
2. Launcher > Settings > Controls > **Button icons** = *Steam Controller*.
3. Press **Play**, reach the title screen, then quit the game once. Do not worry about anything else yet.

## The checks (write the answer next to each line)
| # | Do this | Write down |
|---|---|---|
| 1 | Open `pkp.log` (next to `pkp.exe`) and find the lines that start `gamepad`. | Copy the line with `added:` (it says `kind=`) and the line with `caps:`. If there is no `caps:` line, say so. |
| 2 | In the game: Settings > Controller. Pick **Controller Button**, press each of the **four back grips** one at a time (the screen shows which one it saw), or set **Presses** to A for one grip and test it in the game. | Which physical grip is *left upper, left lower, right upper, right lower*, and what the screen called them (L4, L5, R4, R5). |
| 3 | On the title screen look at the prompts at the bottom (the little round buttons with icons). Press the **small left button** next to the Steam button, then the **small right button**. | Which one made the game react like "Start/Menu" (opens menus) and which like "Back/View". Which icon the prompt showed for each (two squares or three lines). **This one matters most.** |
| 4 | Bind **Left trackpad click** and **Right trackpad click** (Controller Button list) to A and B, then click each trackpad in the game. | Worked / did not work for each. |
| 5 | Bind **Steam button** and the **"..." button** the same way and press them. Do this once with Steam closed and once with Steam open. | Worked / did not, for each case. |
| 6 | Walk around and use the camera for a minute. | Do the sticks, triggers and buttons feel normal? Does vibration work (any scene with a rumble)? Any stuck buttons or double presses? |
| 7 | **Wireless (Puck) only:** repeat check 1 and write the `power=` and `percent=` values on the `caps:` line. | The two values, and whether the controller was charging. |

## Linux / Steam Deck / Bazzite only
Repeat checks 1 to 4 twice: once with the game started from the launcher with **Steam Input off** for that shortcut, once added to Steam (Settings > Game > Add to Steam) with **Steam Input on**. Write down, for each: the `kind=` value, whether the controller is seen at all, and whether grips and trackpad clicks react.

## What to send back
Launcher > Settings > **Testing** > *Export and report*. It makes one zip of your logs with your name and folders removed and opens the bug form: drag the zip in, pick **Testing** as the channel, and paste your table. Do not attach game files or saves.

## What we are trying to learn
Whether the controller is seen as a Steam Controller (`kind=Steam`), which grip is which, whether the small-button icons are the right way round (SDL names them the opposite of an Xbox pad), and what the `caps:` line says about trackpads, battery and lights. Nothing here changes your settings or your save.
