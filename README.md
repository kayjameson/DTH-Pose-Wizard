# DTH Pose Wizard

Daz Studio body poses on DazToHue (DTH) Genesis 9 characters in Unreal Engine 5.8, matching Daz. The wizard takes your
own Daz pose presets: full-body, hands and partial poses. Daz evaluates each one once, and the wizard recreates it on
any DTH Genesis 9 character as a plain bone-track animation sequence on the UE5 Mannequin skeleton. The sequences work
with Sequencer, the IK Retargeter and layered face animation (DTH Expression Wizard sequences, MetaHuman performances).

- **One library, every character.** Poses are sampled once on base Genesis 9. The mapping from Genesis 9 bones to DTH's
  skeleton is fixed by DTH's skeleton conversion and ships with the plugin. No per-character step in Daz.
- **DTH does the rest.** Each character's own DTH post-process blueprint adds the joint correctives and solves the twist
  bones, exactly as for any animation. The plugin changes nothing on the character.
- **Measured, not guessed.** See the Phase 0 results in the project docs. Over DTH's own ROM, every joint-corrective
  pose is recreated within 0.035° of DTH's ground truth. The Phase 0 test presets (seated, a 90° forearm twist, a hand pose) match Daz on the character within 0.034° per bone.

## Install

Copy the `DTHPoseWizard` folder into your project's `Plugins` folder and restart the editor. Open it with
**Tools > DazToHue > DTH Pose Wizard**.

## 1. Sample your poses in Daz Studio (once)

On the **Settings** page, use **Copy Path** to get the sampler script (`Resources/Daz/DTH_SamplePoses.dsa`). Open it in
Daz Studio (File > Open). It:
- refuses to run if the open scene has unsaved changes;
- loads a fresh base Genesis 9;
- records DTH's ROM and every Genesis 9 pose preset in your content library (any `Poses` folder under `People/`,
  hands included; `Expressions` folders are left to the Expression Wizard);
- writes `DTH_PoseSamples.json` to `Documents/DAZ 3D/Studio4/DTH Pose Wizard`.

Nothing is saved in Daz. Presets for other figure generations are listed and skipped.

From a script server, pass arguments as the script's argument object:

| Argument | Meaning |
|---|---|
| `step` | `all` (default), `prepare`, `sample`, `rom`, `finish` |
| `out` | output folder |
| `folders` | extra content folders to sample, e.g. `["My Stuff/Poses"]` |
| `files` | sample only these presets |
| `first`, `count` | which presets a `sample` step takes |
| `skip` | presets not to load (each one stopped Daz in an earlier run) |
| `romLast` | last ROM frame |
| `geometry` | also write the base mesh per state |

## 2. Build the library

Set **Daz Samples File** (and, if you like, **Pose Library**) on the Settings page. Then on **Build Library** press
**Build Library**. The report lists:
- the poses;
- which poses are partial (hands, feet: they are laid over a base pose);
- presets that did not load, with the reason;
- for each pose, any shapes a DTH character does not take from a body pose: face shapes (key those with the Expression
  Wizard) and add-on shape morphs DTH's ROM does not carry.

Rebuilding from the same samples gives the same library.

## 3. Poses

Pick your **Character**, any DTH Genesis 9 skeletal mesh with DTH's post-process blueprint, as the DTH import sets it
up.
- **Preview:** click a pose to preview it on the character through its own post-process blueprint. Switch views with
  Body, Side, 3/4, Face and Hands.
- **Partial poses** go over the rest pose. To put them over a full-body pose instead, select it and press **Use as Base**.
- **Create Sequence** writes the previewed pose as a one-frame sequence for the character.
- **Create Pose Sheet** writes every pose on the sheet (double-click or **+** to add) as one sequence, one pose per
  frame.

Sequences go to the **Sequence Folder**.

A sequence is written for the character it is meant for: write one per character. The library and the bone map are
shared, but Unreal's retargeting doesn't carry a sequence written for one character exactly onto another with
different proportions.

## Sequencer and expressions

Add the sequence to the character's Animation track. A face sequence on a second section (or a MetaHuman face
performance) layers over it: pose sequences carry bone tracks only, no curves.

You can also key an expression straight into a pose sequence: pick the pose sequence in the DTH Expression Wizard's
Key bar and key the expression on the same frame. The pose sets the body, the expression sets the face, and neither
touches the other (`DTH.Pose.P1.PoseWithExpression`: body within 0.00001°, face identical to the expression alone).

## Python

`unreal.DTHPoseScripting`. Every function returns text lines; failures start with `ERROR:`.

| Function | Does |
|---|---|
| `open_wizard(page)` | opens the tab on `Settings`, `Build` or `Poses` |
| `build_library()` | builds the library from the settings; returns the report |
| `list_poses(filter)` | `label \| pack \| partial/full \| N missing` |
| `select_character(mesh_path)` | previews a character |
| `select_pose(label, base_label)` | previews a pose (a partial one over `base_label`, `""` for the rest pose) |
| `capture_preview(file, view)` | saves the preview (`body`, `side`, `threequarter`, `face`, `hands`) as PNG |
| `write_sequence(mesh_path, labels, base_label, name)` | writes the poses as a sequence for the character |

## Tests

Automation tests under `DTH.Pose` (Session Frontend, or the automation tools):
- **`Unit`:** the retargeting maths.
- **`P0`:** the Phase 0 measurements against DTH's own ground truth.
- **`P1`:** the library, writing and playing back, and partial poses.

They read their inputs from the settings' Testing section and write only to the Test Folder.
