# U-Type PrusaSlicer 2.9.4 modifications

This branch began at official `version_2.9.4` commit `69c8e569410efe82259e706cd09a05ea82da9359`.
All custom features are marked with `MOD:` comments, grouped into focused commits.
**The source changes still require a complete build, runtime verification and sliced-G-code regression tests before production printing.**

## Commit index

| Feature | Commit | Files / behavior |
| --- | --- | --- |
| Save As: Ctrl+Shift+S plus legacy Ctrl+Alt+S on Windows/Linux | `8c3ca145`, `b3b227ec` | `MainFrame.hpp/.cpp` |
| Hide settings Printables tab | `b3b227ec` | `MainFrame.cpp` (panel remains instantiated for account API compatibility; Prusa Connect remains) |
| Saved preview visualization states (replaces forced-on Travel/Seam defaults) | `25bf2244` | `GCodeViewer.cpp/.hpp`, `Settings.hpp`, `GUI_Preview.cpp` |
| Front seam: mirrored Rear, negative Y | `6851b5a9` | `PrintConfig.hpp/.cpp`, `GCode/SeamRear.hpp/.cpp`, `GCode/SeamPlacer.cpp`; integration test |
| Force clockwise perimeters | `0f26d052` | New **per-object** Boolean, applies to CLOSED perimeter loops (includes hole loops), does not redefine winding of open paths |
| Rear/Front skirt and brim seam positions | `3ea9630a` | `GCode/ExtrusionOrder.cpp` biases closed-loop starts using their own bounding boxes; only if all objects in print share chosen side |
| Rear/Front concentric top/bottom infill | `3db536ae` | `Fill/FillBase.hpp`, `Fill/Fill.cpp`, `Fill/FillConcentric.cpp`; classic/Arachne use each loop's side |
| Midpoint of contiguous seam-painted band | `03ca8069` | `GCode/SeamChoice.hpp/.cpp`, `GCode/SeamPlacer.cpp`; includes synthetic test |

## Known limitations / required validation

1. **Build and launch** the application in a development configuration. No local C++ build has been executed when these commits were authored.
2. **Keyboard**: verify Ctrl+Shift+S, Ctrl+Alt+S, Ctrl+S, and menu-state gating on Windows. macOS original Ctrl+Shift+S remains.
3. **Settings**: verify Printables absent and Prusa Connect / printer settings / account login still work; verify settings-dedicated-window and integrated modes.
4. **Preview**: verify that individual Feature Type roles, Travel, Wipe, Retract, Deretract, Seam and other icon toggles, plus legend-window visibility, each retain their own state after app restart, reslicing and project switching. Verify both the unsliced preview and final G-code preview. Unset settings must retain native defaults.
5. **Front/Rear geometry**: slice a cylinder and a concave shape with both choices; compare first/last endpoints against positive/negative Y.
6. **Skirt/brim**: slice with 0, small, and large skirt distances and multiple skirts, with single object and multiple objects (same/different seam preferences). Closed brim loops are biased to the chosen side. **Open brim extrusions are left unchanged** because they have no rotatable seam and should not be disrupted by this update.
7. **Concentric top/bottom**: compare classic and Arachne for several separated islands and shapes with holes, changing `seam_position` and `prefer_clockwise_movements`, ensuring path endpoints remain near correct Y side and no new open-path connections.
8. **Painted seam**: paint narrow and wide vertical enforcer stripes on circular and concave models. Ensure the seam stays centered across heights, check blocker precedence, scarf seams, multiple separate stripes, and `staggered_inner_seams` interactions. The midpoint approach uses **classified painted perimeter samples**; exact brush polygon centroids and projection-offset behavior need verification.
9. **Clockwise setting**: check outer contours, hole walls, Arachne/classic, reversed winding and inside-out order by inspecting exported G-code. Open/irregular paths and wipe/bridge segments have no universally defined clockwise winding.
10. **Compare baseline** against vanilla 2.9.4 output with each modification disabled (where applicable). Test with hardware only after inspecting actual G-code.

## Future-version migration

Keep pristine version tags and avoid merging the latest `master` into this maintenance branch.
To port to a future official release, create a new branch **from the new version tag** and replay commits by feature:

```bash
git remote add upstream https://github.com/prusa3d/PrusaSlicer.git
git fetch upstream --tags
git switch -c release-NEXT-mod version_NEXT
git log --oneline version_2.9.4..release-2.9.4-mod
git cherry-pick <feature-commit>
```

Cherry-pick in the order listed; earlier Save As commits modify the same file, and the Front enum must precede the adhesion/infill changes.
Conflicts are expected because upstream C++ APIs and G-code generation may change. Rebuild and run regression tests after each cherry-pick. Avoid treating cherry-picks as automatically feature-compatible.

## Original option

The upstream printer-level setting **Prefer clockwise movements** exists separately and remains supported. The new **Force clockwise perimeters** is an object-level override for closed perimeter loops, default OFF.


### Fourth settings tab: Modifications

The former Printables position now hosts a dedicated Modifications page, following Print, Filament and Printer Settings (fourth settings tab, fifth tab including Plater). The sole custom slicing checkbox, `force_clockwise_perimeters`, has been moved from Print Settings; the same print-preset option remains available to object-level overrides. The page also includes a standard seam position selector (Random, Nearest, Aligned, Rear, Front) synchronized with the Print Settings option, a Print Settings navigation button for saving the preset, and descriptions of always-on custom functionality. The new panel is deliberately not a second `TabPrint`: it modifies the existing print-preset config through `Tab::load_config` to preserve dirty state and propagation. Not yet compiled or verified on Windows.


### Feature Type legend visibility persistence

Every individual Feature Type extrusion-role visibility state and every preview icon visualization switch now persists between sessions through PrusaSlicer's AppConfig. This includes Travel, Wipe, Retract, Deretract, Seams, Tool changes, Color changes, Print pauses, Custom G-codes, Center of gravity, Shells, and Tool marker, plus the legend window itself. Unset preferences retain the native defaults rather than forcing Travel/Seams on. Both pre-G-code and sliced G-code previews use the remembered states. The separate Modifications tab text describes the behavior. Changes have not yet been compiled or tested on Windows.


### Concentric solid infill: accurate Rear/Front seams and forced clockwise direction

- The Modifications tab now uses an actual bulleted list for the modifications, and the tooltips cover Concentric solid infill.
- For **top and bottom solid surfaces using the Concentric pattern**, each closed infill ring's seam is projected to the closest point on the edge facing the chosen Rear (+Y) or Front (-Y) side. This can place the seam in the middle of a long straight segment, rather than at one of its corner vertices. The existing loop-clipping gap is retained.
- The **Force clockwise perimeters** option now applies to the same top/bottom Concentric solid rings (including rings around holes) for Classic and Arachne generation. Direction is established before cutting/opening the seam; paths remain oriented during their subsequent ordered infill execution.
- Other infill patterns, sparse/internally generated infill, and open extrusion lines retain the existing behavior. Geometry, obstacle avoidance, extrusion order and separate disconnected islands can still require some travel between perimeters and infill; this adjustment aligns seam positions rather than merging separate paths.
- Added a focused regression test on square top/bottom surfaces with and without holes, both front/rear sides and printer-level direction preferences. **Windows build and runtime tests are still required**, particularly for Arachne variable-width loops.


### Verified G-code regression: layer 131 / Z32.75 / FillEnsuring

The user's exported `test2.gcode` (not stored in the repository) at Z32.75 has `seam_position = front`, `force_clockwise_perimeters = 1`, `perimeter_generator = arachne`, and Concentric top/bottom fill patterns. Its closed perimeter starts near X=0,Y=-39.8, but the `;TYPE:Solid infill` path begins near X=-38.832,Y=-0.217, implying about 55.5mm of travel from the preceding perimeter. The path loops around radii ~38.8 and ~38.2 in opposite directions, merging two concentric-like gap-fill rings into a single open path. `group_fills()` remaps internal solids to `ipEnsuring`, whose `make_fill_polylines()` used `ex_bb.min` as its ring seam and later joined adjacent rings. Earlier fixes only targeted `ipConcentric`; they never reached `ipEnsuring`.

The new constrained code propagates Front/Rear and clockwise parameters to `ipEnsuring` only when top or bottom fill is Concentric, selects seam points from global Y in its rotated coordinate frame, protects near-closed thick rings from rejoining/accidental reversal, and leaves ordinary open ensuring paths unmodified. It retains variable extrusion-width interpolation and the existing seam-gap clip. **Needs a Windows rebuild and comparison against the exported G-code**, particularly around Z32.75. When the user reslices, expect each ring to start at negative Y for Front, positive Y for Rear, and print clockwise if forced; separate gaps/travels may still be present.
