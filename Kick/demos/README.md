# Bundled demo projects

`Demo 01`–`Demo 05` are the firmware's own demos; the public release has no samples for them, so the
simulator hides them. `AwesomeKick Groove` and `Rusty Kit` are playable: they appear on the start screen,
and the simulator opens (and on its first start installs) your own copy in the projects folder
(`ui::userCopyOfDemo()` in `Kick/modules/kick_ui/recent_project/kick_RecentProject.cpp`). Their project
files contain `@PROJECT_DIR@` instead of their own folder; the copy gets the real path.

| Project | Samples | Source | License |
|---|---|---|---|
| AwesomeKick Groove | 8 electronic drums, synthesized by the simulator (`KICKEM_SELFTEST=makedemo`) | AwesomeKick | CC0-1.0 |
| Rusty Kit | 16 acoustic one-shots made with `scripts/build-cc0-kit.py` from *Big Rusty Drums* by Karoryfer Samples | [sfzinstruments/karoryfer.big-rusty-drums](https://github.com/sfzinstruments/karoryfer.big-rusty-drums) | CC0-1.0 |

Rebuild them with `KICKEM_SELFTEST=makedemo KICKEM_MAKEDEMO_OUT=<folder> KICKEM_MAKEDEMO_KIT=<Rusty Kit samples>`,
then replace the output folder in the `.kickPrj` files with `@PROJECT_DIR@`.
