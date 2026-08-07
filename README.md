# FBNeo Replay Runner

- Allows playing back replays from fightcade.com
- Allows generating per-game SCRD RAM archives in a matter of seconds

## Build

### macOS

```bash
make sdl 'BUILD_X86_ASM=' 'CPUTYPE=arm64' -j1
```

`-j1` is important, because there's a dependency race somewhere.

## Run

1. Put `sfiii3nr1.zip` in `roms` folder.
2. Run the executable:

    ```bash
    ./build/fbneosdldarm64 sfiii3nr1 \
        -replay-state /path/to/replay/state \
        -replay-inputs /path/to/replay/inputs \
        -dump-ram-path /path/to/save/archives/to \
        -headless
    ```

The runner writes `game_0.scrd`, `game_1.scrd`, and so on to the dump path. Each
archive contains zero-run-compressed RAM deltas for its game; no intermediate raw
`.ram` frame files are created.
