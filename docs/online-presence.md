# Online presence: what it does and what it does not do

**Share that I am online** is an optional launcher setting. It lets other players see that you are playing Recompia right now,
and how many Pokémon have a home in your save. It is **off** unless you switch it on yourself, and you can switch it off at any time.

This page explains exactly what happens when it is on, and what never happens. The launcher code that does it is open source:
[`launcher/src-tauri/src/online.rs`](../launcher/src-tauri/src/online.rs) sends it, and
[`launcher/src-tauri/src/home_count.rs`](../launcher/src-tauri/src/home_count.rs) counts the Pokémon.

## Where the switch is

The same switch is in three places; changing it in one changes it in all of them:

- the **Online** chip on the launcher's Home page;
- the **Online** group at the top of the launcher's **Settings > Game**;
- the **Online** tab in the game's own Settings screen.

The first time you press Play, the launcher asks you once whether to switch it on. The answer is **no** unless you pick yes.

## What it sends, and when

Only while the game is running **and** the switch is on, the launcher sends a small message to the Recompia online server
(`api.recompia.com`): once when the game starts, then once every 30 seconds, then once when the game closes. Each message holds:

| What | Example | Where it comes from |
|---|---|---|
| Your online name | `Misty`, or nothing (shown as "anonymous") | the **Online name** field in Settings > Game. Empty by default |
| The Recompia version | `0.10.4-beta.2` | the launcher |
| Windows or Linux | `windows` | the launcher |
| How many Pokémon have a home in your save | `64` | one number, counted on your PC from your save |
| A random online ID | a 64-character code | made on your PC the first time you switch it on (see below) |

That is the whole list. Like any program that connects to a website, the connection itself also says which program it is
(`Recompia-Launcher` and its version) and comes from your IP address (see "How long it is kept").

## What it never does

- **It never sends your save.** The launcher reads your save on your PC, counts one number from it, and sends only that number.
  The save is never uploaded or changed.
- **It never sends who you are.** It does not send your launcher profile nickname, your user or account ID, your Nintendo
  account, your PC's name, your Windows or Linux user name, your files or folders, or your e-mail address.
- **It never sends where you are or what you do.** No position in the game, no area, no chat, no screenshots, no keys you press,
  no other programs on your PC. No hardware details beyond "Windows" or "Linux".
- **It does not track you.** There are no cookies, no advertising, no analytics and no tracking across websites or apps. Nothing is
  sold or given to anyone.
- **The server cannot control your PC.** The launcher only reads two things in the server's answer: a session number and how often to
  send the next message. Nothing is downloaded, installed or run from it.
- **It does not change the game.** Gameplay, Link Play and your saves work the same whether it is on or off. It costs one tiny
  message every 30 seconds.
- **It does not talk to Nintendo.** It only talks to the Recompia online server.
- **When it is off, nothing is sent at all,** and no online ID is made.

## Your online ID

So that the server can tell your sessions apart, the launcher makes a random code once, on your PC, and keeps it in the launcher's own
settings file (`launcher.json`). The ID that is sent is a one-way hash of that code and your profile: it cannot be turned back into your
user ID or anything else about you. **Reset my online ID** (Settings > Game) throws the code away; the next time you play you get a
new, unrelated ID.

## What other people can see

Anything you share is **public** while you are online: anyone can see the list of players online now (online name or "anonymous",
version, Windows or Linux, when they were last seen) and a leaderboard of Pokémon at home (online name or "anonymous", the number).
If you do not want your name to appear, leave **Online name** empty.

## How long it is kept

- You leave the online list when the game closes, or within a few seconds when you switch the setting off. If the game crashes,
  you show as offline after 90 seconds.
- A play session is deleted 24 hours after you go offline.
- The history of your home count is kept for statistics. After 7 days it is reduced to one value per hour.
- The server sees your IP address, as every website does, to stop one PC from flooding it. It is not stored. The connection goes
  through Cloudflare, which handles it the same way it does for the recompia.com website.

## How to stop it, and how to have your data removed

- Switch it off in any of the three places. You leave the online list within a few seconds, and nothing more is sent.
- To have the rows of your online name removed, ask in the Recompia Discord (`discord.recompia.com`) with your online name.
- Pressing **Reset my online ID** starts a new history that is not linked to the old one.

The website's privacy section (recompia.com, "Online presence (optional)") says the same.
