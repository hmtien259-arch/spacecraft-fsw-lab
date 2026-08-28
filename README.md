# Spacecraft Software Engineer - Lab Workspace

Refonte International Training and Internship Program - Spacecraft Software Engineer track.
Program work by **Ho Manh Tien**.

This repository is the lab workspace and, later, the capstone home for the program. It
follows the structure introduced in Session 2:

```
spacecraft-fsw-lab/
|-- docs/                 # session notes and ECSS / NASA standards references
|-- labs/
|   |-- 01-foundation/    # Sessions 3-4: HK telemetry, requirements, tests, traceability
|   |-- 02-rtos/          # Sessions 5-6: FreeRTOS HK pipeline, IPC, shared resource protection
|   |-- 03-tmtc/          # Session 7+: CCSDS Space Packet / PUS TM packets
|   |-- 04-obdh/          # OBDH module (upcoming)
|   `-- 05-fdir/          # FDIR module (upcoming)
|-- capstone/             # capstone flight software stack
`-- tools/scripts/        # helper scripts (dependency fetch, build helpers)
```

## Branch policy and commit style

| Practice | Convention |
|---|---|
| Default branch | `main` stays working and reviewable at all times. |
| Feature branches | One branch per lab or task, e.g. `lab/02-rtos-scheduler`. |
| Pull requests | One PR per lab, opened for review. |
| Commit messages | Conventional Commits: `feat:`, `fix:`, `docs:`, `test:`, `chore:`. |

## Building the labs

Each lab under `labs/` is a small set of plain C files buildable with `gcc`. See each lab's
own README for its exact build command. The RTOS labs (`labs/02-rtos/`) additionally depend
on the FreeRTOS kernel, fetched on demand by `tools/scripts/fetch_freertos.sh` (not vendored
into this repo — see that lab's README).

