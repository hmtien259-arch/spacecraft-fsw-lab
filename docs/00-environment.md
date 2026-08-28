# Session 2 - Development Environment Notes

## Toolchain verification

Captured from the environment this repository was scaffolded and built in.

| Tool | Version | Status |
|---|---|---|
| gcc | 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) | OK |
| g++ | 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) | OK |
| make | GNU Make 4.3 | OK |
| cmake | 3.28.3 | OK |
| git | 2.43.0 | OK |
| gh (GitHub CLI) | - | **Not installed / not authenticated in this environment** |
| qemu-system-arm | - | Not installed yet (only needed once the RTOS labs reach hardware emulation beyond the POSIX port) |
| gdb | 15.1 | OK |
| openocd | - | Not installed yet (optional until hardware bring-up) |
| arm-none-eabi-gcc | - | Not installed yet (optional until hardware bring-up) |
| node | v22.22.2 | OK (Claude CLI dependency satisfied) |
| claude (Claude CLI) | 2.1.274 | OK, running against this repo |

Per Session 2's own guidance, `arm-none-eabi-gcc` and `openocd` are optional until the RTOS
labs need real hardware targets; the FreeRTOS work in `labs/02-rtos` uses the POSIX simulation
port instead, so it does not block on the cross toolchain.

## Git identity

```
git config user.name "Ho Manh Tien"
git config user.email "hmtien259@gmail.com"
git config init.defaultBranch main
git config pull.rebase true
```

## VPS access, hardening, and project hosting - action needed

The following Session 2 deliverables require credentials and infrastructure that only the
student has access to (VPS host/IP, SSH login, and a domain/subdomain). They were **not**
performed in this workspace and need to be completed separately:

- [ ] Generate an SSH key (`ssh-keygen -t ed25519 -C "hmtien-refonte" -f ~/.ssh/refonte_vps`)
      and copy it to the VPS shared over the program's Slack channel.
- [ ] Add the `refonte-vps` host alias to `~/.ssh/config`.
- [ ] Disable password authentication, run as a non-root sudo user, enable `ufw` (SSH, HTTP,
      HTTPS only).
- [ ] Install nginx + certbot, point an A record at the VPS, and publish a placeholder page
      over HTTPS.
- [ ] Run `claude` for the first time on the actual workstation/VPS to complete the login flow
      (this Claude CLI session is running in a separate cloud workspace and cannot perform the
      interactive OAuth login on the student's own machine).
- [ ] Authenticate `gh` (`gh auth login`) or add a GitHub Personal Access Token so this
      repository can be pushed to GitHub as `spacecraft-fsw-lab`.

## Program repository

Bootstrapped with the layout from Session 2 section 4: `docs/`, `labs/01-foundation` through
`labs/05-fdir`, `capstone/{src,tests,report}`, `tools/scripts/`.

## Project URL

Pending: to be filled in once the VPS + domain steps above are completed by the student.
