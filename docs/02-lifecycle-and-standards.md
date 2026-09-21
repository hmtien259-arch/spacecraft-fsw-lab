# Session 4 - Lifecycle, Standards, and Traceability Notes

## Criticality category for the HK program

Treating `labs/01-foundation/hk_telemetry.c` as if it were flight code (it is a training
exercise, but the exercise is to reason about this honestly): it reports state and drives a
SAFE-mode transition, but it does not itself command an actuator or a burn. Under
ECSS-E-ST-40C that puts it closer to **category C** - a failure degrades the mission (a
missed or wrong SAFE-mode call delays a real safety response) but does not, by itself, cause
loss of the vehicle or loss of life, since a real FDIR chain would still have an independent
path to detect a stuck OBC. It is not category D, because the mode output does feed a safety-
relevant decision downstream, and it is not category A/B, because it has no direct actuation
authority. If this logic were promoted into the real HK service that actually gates SAFE-mode
entry fleet-wide, it would need to move to category B and pick up independent verification (ISVV).

## Why traceability matters when you cannot patch in orbit

Once software is running on hardware you cannot walk up to, the traceability matrix is the
only way to know, before launch, that every requirement was actually tested and that no test
is testing something nobody asked for - because the one moment you can still cheaply fix a
gap is before it leaves the ground.

## Requirements review

`HK-REQ-006` (battery percentage stays within 0-100) was added specifically because the
original five say nothing about value *validity*, only presence and mode effect - see
`labs/01-foundation/requirements.md` for the full rationale, and `traceability.md` for its
test mapping.
