Mixtormat Breakup finish bundle
================================

Run from the Mixtormat plugin repository root:

    python apply_breakup_finish.py

This finishes the remaining Chipping -> Breakup work after the five files already changed:
- GPU Breakup field/apply passes
- editor selection/add/name/badge integration
- Breakup inspector
- compositor automation test replacement
- enum redirect
- removal of old Chipping/reduction shaders

The GitHub connector is read-only, so this bundle applies the changes locally in one command.
The script creates .breakup.bak backups before editing existing files.

After applying:
1. build UE 5.8 Development Win64 / SM6
2. verify FMixtormatBreakupFieldCS and FMixtormatBreakupApplyCS compile
3. run Mixtormat compositor tests
