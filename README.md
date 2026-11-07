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

It is made for libraries of hundreds of thousands of presets:
- It reads only the start of each preset file to tell what it is.
- It writes the samples 100 presets to a file (`poses/`). `DTH_PoseSamples.json` only lists those files.
- It shows its progress and can be cancelled.
- A run that stops, because you cancelled or Daz closed, carries on where it left off the next time you run the
  script. You can also start over.
- A preset that was loading when Daz went down is tried once more. If Daz goes down on it again, it is listed as not
  loaded, with the reason.

From a script server, pass arguments as the script's argument object:

| Argument | Meaning |
|---|---|
| `step` | `all` (default), `prepare`, `sample`, `rom`, `finish` |
| `out` | output folder |
| `folders` | extra content folders to sample, e.g. `["My Stuff/Poses"]` |
| `fresh` | `true`: start over instead of carrying on an unfinished run |
| `files` | sample only these presets |
| `chunk` | which file of 100 presets a `sample` step takes (default: the next one not sampled) |
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

Rebuilding from the same samples gives the same library. The build reads the samples one file at a time and can be
cancelled; a cancelled build leaves the library as it was. The library asset keeps each pose's bones in blocks that
are only read when the pose is used, so a large library opens quickly. A library built by version 0.2.0 or earlier
has to be built again.

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

## Key onto a Control Rig

Both wizards key the same control rig track on the same frames without touching each other's channels: the Pose
Wizard keys the body controls (`Body/` on DTH's modular rig), the Expression Wizard the face board (`Face/`). So one
pose can hold for four frames with a different expression on each, or the other way round.

Pick the **Level Sequence**, the **character** (its binding) and the **Frame** in the "Key onto a Control Rig" bar. The
choice is shared by both wizards, and the frame moves on by itself after each key. **Stepped** (on by default) holds
each key until the next one; turn it off to blend between keys. If the character has no control rig track yet, its
default animating rig (`MCR_<character>`) is added.

What you see in the wizard is what the track plays, exactly:
- The values are found with the rig's Forwards Solve, never its Backwards Solve (DTH's backwards solve, which
  Sequencer's own **Bake to Control Rig** uses, bakes with the IK legs and spine on and drops the head's own turn and
  the pelvis; see the bake-fixed copy of the rig for a bake that works).
- What the rig cannot show goes on a **layered FK Control Rig** track that the wizards add to the same binding, which
  Sequencer applies after the rig: bones no body control of DTH's rig drives (spine_05, the metatarsals, the pec
  clavicles, the twist bones) and face curves the board cannot write (six lip curves have no control, and
  some expressions need both ends of one slider at once, such as nose wrinkle with nostril depress). Poses own the
  layer's bones, expressions its curves. The twist bones matter: DTH's post-process blends each one's own twist with
  its limb's, so the pose's twist shows on the character.
- The eyes are left to the face board: Key Pose keys the layer's eye bones with no change, so the board's eye and aim
  controls set the gaze, and a pose keyed over an older key clears an eye offset that key left.
- Measured on Milly (2026-10-28): every body bone within 0.02 deg of the Pose Wizard's preview, every one of the 154
  expression curves within 0.005 of the Expression Wizard's mix, on every frame, with poses and expressions keyed
  together and alone; and on the character as shown (after DTH's post-process) the twist bones of a keyed pose the
  same as the pose played as an animation (they were up to 69 deg off before the layer carried them).
- The wizards key the bake-fixed copy of the rig the same way; it has controls of its own for spine_05, the
  metatarsals, the pec clavicles and the twist bones.

**Key Pose** keys the previewed pose at the frame; **Key Sheet** keys the pose sheet from the frame on, one pose per
frame. The report lists the bones the rig itself leaves off, which the layer corrects.

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
| `key_onto_control_rig(sequence_path, binding_name, mesh_path, labels, base_label, frame, stepped)` | keys the poses onto the character's control rig track, one per frame from `frame` on |

## Tests

Automation tests under `DTH.Pose` (Session Frontend, or the automation tools):
- **`Unit`:** the retargeting maths.
- **`P0`:** the Phase 0 measurements against DTH's own ground truth.
- **`P1`:** the library, writing and playing back, and partial poses. `P1.LibraryScale` builds a 100,000-preset
  library from the settings' samples and measures the build, the asset and reading poses back from it.

They read their inputs from the settings' Testing section and write only to the Test Folder.
