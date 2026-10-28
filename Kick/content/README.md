# Bundled demo projects

Playable projects that the simulator copies into your projects folder on its first start
(see `installBundledContent()` in `Kick/apps/kickem/simulator/kick_Simulator.cpp`). The project
files contain `@PROJECT_DIR@` instead of their own folder; the simulator fills in the real path.

| Project | Samples | Source | License |
|---|---|---|---|
| Kickray Groove | 8 electronic drums, synthesized by the simulator (`KICKEM_SELFTEST=makedemo`) | AwesomeKick | CC0-1.0 |
| Rusty Kit | 16 acoustic one-shots made with `scripts/build-cc0-kit.py` from *Big Rusty Drums* by Karoryfer Samples | [sfzinstruments/karoryfer.big-rusty-drums](https://github.com/sfzinstruments/karoryfer.big-rusty-drums) | CC0-1.0 |

Rebuild them with `KICKEM_SELFTEST=makedemo KICKEM_MAKEDEMO_OUT=<folder> KICKEM_MAKEDEMO_KIT=<Rusty Kit samples>`,
then replace the output folder in the `.kickPrj` files with `@PROJECT_DIR@`.
