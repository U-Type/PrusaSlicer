# U-Type PrusaSlicer 2.9.4 modifications

This branch began at official `version_2.9.4` commit `69c8e569410efe82259e706cd09a05ea82da9359`.
All custom features are marked with `MOD:` comments, grouped into focused commits.
**The source changes still require a complete build, runtime verification and sliced-G-code regression tests before production printing.**

## Commit index

| Feature | Commit | Files / behavior |
| --- | --- | --- |
| Save As: Ctrl+Shift+S plus legacy Ctrl+Alt+S on Windows/Linux | `8c3ca145`, `b3b227ec` | `MainFrame.hpp/.cpp` |
| Hide settings Printables tab | `b3b227ec` | `MainFrame.cpp` (panel remains instantiated for account API compatibility; Prusa Connect remains) |
| Default travels, seam markers, Feature Type legend | `a5997576`, `0bf1fede` | `Settings.hpp`, `GLCanvas3D.cpp` |
| Front seam: mirrored Rear, negative Y | `6851b5a9` | `PrintConfig.hpp/.cpp`, `GCode/SeamRear.hpp/.cpp`, `GCode/SeamPlacer.cpp`; integration test |
| Force clockwise perimeters | `0f26d052` | New **per-object** Boolean, applies to CLOSED perimeter loops (includes hole loops), does not redefine winding of open paths |
| Rear/Front skirt and brim seam positions | `3ea9630a` | `GCode/ExtrusionOrder.cpp` biases closed-loop starts using their own bounding boxes; only if all objects in print share chosen side |
| Rear/Front concentric top/bottom infill | `3db536ae` | `Fill/FillBase.hpp`, `Fill/Fill.cpp`, `Fill/FillConcentric.cpp`; classic/Arachne use each loop's side |
| Midpoint of contiguous seam-painted band | `03ca8069` | `GCode/SeamChoice.hpp/.cpp`, `GCode/SeamPlacer.cpp`; includes synthetic test |

## Known limitations / required validation

1. **Build and launch** the application in a development configuration. No local C++ build has been executed when these commits were authored.
2. **Keyboard**: verify Ctrl+Shift+S, Ctrl+Alt+S, Ctrl+S, and menu-state gating on Windows. macOS original Ctrl+Shift+S remains.
3. **Settings**: verify Printables absent and Prusa Connect / printer settings / account login still work; verify settings-dedicated-window and integrated modes.
4. **Preview**: verify fresh launch, import, reslice, project switching, Feature Type legend, and travels/seams toggles. User toggles should remain operative.
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
