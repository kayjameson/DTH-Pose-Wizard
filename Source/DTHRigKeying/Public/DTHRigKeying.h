#pragma once

#include "CoreMinimal.h"
#include "Rigs/RigHierarchyDefines.h"

class ULevelSequence;
class UMovieSceneControlRigParameterTrack;
class USkeletalMesh;
class USkeleton;

/**
 * Keys DTH poses and expressions onto a character's control rig in a level sequence, so both wizards can key the same
 * frames without touching each other's channels: the Pose Wizard keys the body controls ("Body/" on DTH's modular
 * rig), the Expression Wizard the face board ("Face/").
 *
 * What the character's rig cannot show exactly goes on a layered (additive) FK Control Rig track on the same binding,
 * which Sequencer applies after the rig: the bones no body control of DTH's rig drives (spine_05, the metatarsals, the
 * pec clavicles, the twist bones, the eyes and anything else the pose turns) and the face curves the board cannot write
 * (no control for them, or two curves at once on one slider). Every key sets the whole layer for its owner (bones for
 * poses, curves for expressions), so the result matches the wizard's preview exactly.
 *
 * Nothing here uses the rig's Backwards Solve: on DTH's modular rig it bakes with the IK legs and spine on, drops the
 * head's own turn and leaves the pelvis off (measured 2026-10-28, Milly: up to 41 deg).
 * Instead every value is found with the Forwards Solve, which is what Sequencer plays.
 */
namespace DTHRigKeying
{
	/** A binding in a level sequence that a control rig track can go on. */
	struct FBinding
	{
		FGuid Guid;
		FString Name;
		/** The rig class of the binding's control rig track, or null if it has none. */
		UClass* RigClass = nullptr;
	};

	DTHRIGKEYING_API TArray<FBinding> Bindings(ULevelSequence* Sequence);

	/** The rig to add when a binding has no control rig track yet: the character's default animating rig. */
	DTHRIGKEYING_API UClass* DefaultRig(const USkeletalMesh* Mesh);

	/** The binding (by name) and its control rig track, which is added with one section if the binding has none. */
	struct FTarget
	{
		ULevelSequence* Sequence = nullptr;
		FGuid Binding;
		UMovieSceneControlRigParameterTrack* Track = nullptr;
		UClass* RigClass = nullptr;
		/** The layered FK Control Rig track on the same binding (added if missing). */
		UMovieSceneControlRigParameterTrack* Layer = nullptr;
	};
	DTHRIGKEYING_API bool Resolve(ULevelSequence* Sequence, const FString& BindingName, const USkeletalMesh* Character, FTarget& Out, FString& OutError);

	/**
	 * Keys Values (control name to value) and LocalTransforms (control name to local transform) at DisplayFrame on Track's
	 * section. Only these controls get keys; every other channel keeps its keys. bStepped holds each key until the next
	 * one (one pose or expression per frame).
	 */
	DTHRIGKEYING_API bool Key(const FTarget& Target, UMovieSceneControlRigParameterTrack* Track, int32 DisplayFrame, const TMap<FName, FRigControlValue>& Values,
		const TMap<FName, FTransform>& LocalTransforms, bool bStepped, FString& OutError);

	/** The body controls that reproduce a pose, and how close they get. */
	struct FBodyFit
	{
		TMap<FName, FRigControlValue> Values;
		/** Largest remaining error over the bones the rig drives. */
		double WorstDeg = 0.0;
		double WorstCm = 0.0;
		FName WorstBone;
		/** Skeleton bones the rig leaves off the pose by more than 0.05 deg / 0.05 cm (the layer corrects them), with the error. */
		TArray<FString> Unreached;
		/** The layer's bone controls (FK control name to local delta): exact for every pose bone, identity where the rig is. */
		TMap<FName, FTransform> Layer;
	};

	/**
	 * Fits RigClass's body controls (FK, IK switches off) to LocalPose (mesh-space local bone transforms, as the wizard
	 * previews them) on Character, by running the Forwards Solve and correcting each control until its bone matches.
	 * Twist bones are the pose's too (DTH's post-process blends each one's own twist with its limb's, so it shows), and so
	 * are face bones (a pose can turn the eyes or open the jaw); an expression on the same frame adds its face on top.
	 */
	DTHRIGKEYING_API bool FitBody(UClass* RigClass, USkeletalMesh* Character, const TMap<FName, FTransform>& LocalPose, FBodyFit& Out, FString& OutError);

	/**
	 * The face-board control values that produce Curves (CTRL_expressions_* name to value) on RigClass, and the layer's
	 * curve controls (FK control name to delta) that make up the rest: every expression curve gets one (0 where the board
	 * is exact), so a key fully sets the face. OutCorrected counts the curves the layer had to change.
	 */
	DTHRIGKEYING_API bool FaceBoard(UClass* RigClass, const TMap<FName, float>& Curves, TMap<FName, FRigControlValue>& OutValues,
		TMap<FName, FRigControlValue>& OutLayer, int32& OutCorrected, FString& OutError);

	/** Every CTRL_expressions_* curve RigClass's face board writes. */
	DTHRIGKEYING_API TSet<FName> BoardCurves(UClass* RigClass);

	/** Where a face-board curve comes from: its control, the control's axis (0 = x, 1 = y), the side and the gain. */
	struct FBoardSlot
	{
		FName Curve;
		FName Control;
		int32 Axis = 0;
		float Sign = 1.0f;
		float Gain = 1.0f;
	};

	/** RigClass's face board, one slot per curve it writes (measured on the rig, as FaceBoard uses it). */
	DTHRIGKEYING_API TArray<FBoardSlot> BoardMap(UClass* RigClass);

}
