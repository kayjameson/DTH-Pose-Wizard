#include "DTHRigKeying.h"

#include "ControlRig.h"
#include "ControlRigObjectBinding.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/PackageName.h"
#include "Rigs/FKControlRig.h"
#include "Components/SkeletalMeshComponent.h"
#include "MovieScene.h"
#include "MovieSceneBindingProxy.h"
#include "ScopedTransaction.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Units/Execution/RigUnit_BeginExecution.h"
#include "Units/Execution/RigUnit_InverseExecution.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "DTHRigKeying"

namespace DTHRigKeying
{
	namespace
	{
		const TCHAR* BodyPrefix = TEXT("Body/");
		const TCHAR* FacePrefix = TEXT("Face/");
		const TCHAR* CurvePrefix = TEXT("CTRL_expressions_");

		TStrongObjectPtr<UControlRig> MakeRig(UClass* RigClass, USkeletalMesh* Mesh)
		{
			UControlRig* Rig = NewObject<UControlRig>(GetTransientPackage(), RigClass, NAME_None, RF_Transient);
			Rig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
			Rig->Initialize();
			if (Mesh)
			{
				Rig->SetBoneInitialTransformsFromSkeletalMesh(Mesh);
			}
			Rig->RequestConstruction();
			Rig->Evaluate_AnyThread();
			return TStrongObjectPtr<UControlRig>(Rig);
		}

		/** One evaluation as Sequencer plays it: bones from rest, then the pre, main and post forward solves. */
		void Forward(UControlRig* Rig)
		{
			Rig->GetHierarchy()->ResetPoseToInitial(ERigElementType::Bone);
			Rig->Execute(FRigUnit_PreBeginExecution::EventName);
			Rig->Execute(FRigUnit_BeginExecution::EventName);
			Rig->Execute(FRigUnit_PostBeginExecution::EventName);
		}

		double AngleDeg(const FQuat& A, const FQuat& B)
		{
			return FMath::RadiansToDegrees(A.AngularDistance(B));
		}

		const FRigControlElement* Control(const URigHierarchy* H, const FRigElementKey& Key)
		{
			return H->Find<FRigControlElement>(Key);
		}

		bool IsTransformControl(ERigControlType Type)
		{
			return Type == ERigControlType::EulerTransform || Type == ERigControlType::Transform
				|| Type == ERigControlType::TransformNoScale || Type == ERigControlType::Rotator;
		}

		/**
		 * Sets a control's value so that it also reads back and keys as that value: a transform control's rotation is read
		 * (and recorded by the section) from its preferred Euler angles, which setting the value alone leaves as they were.
		 */
		void SetValue(URigHierarchy* H, FRigControlElement* C, const FRigControlValue& Value)
		{
			H->SetControlValue(C, Value, ERigControlValueType::Current);
			if (IsTransformControl(C->Settings.ControlType))
			{
				H->SetControlPreferredEulerAngles(C, Value.GetAsTransform(C->Settings.ControlType, C->Settings.PrimaryAxis));
			}
		}

		TArray<FRigElementKey> ControlsWithPrefix(const URigHierarchy* H, const TCHAR* Prefix)
		{
			TArray<FRigElementKey> Out;
			for (const FRigElementKey& Key : H->GetAllKeys(true, ERigElementType::Control))
			{
				if (Key.Name.ToString().StartsWith(Prefix)) Out.Add(Key);
			}
			return Out;
		}

		/** Body controls that switch a limb or chain to IK: all off, so every bone has its own FK control. */
		TArray<FRigElementKey> IKSwitches(const URigHierarchy* H)
		{
			TArray<FRigElementKey> Out;
			for (const FRigElementKey& Key : ControlsWithPrefix(H, BodyPrefix))
			{
				const FRigControlElement* C = Control(H, Key);
				if (C && C->Settings.ControlType == ERigControlType::Bool && Key.Name.ToString().EndsWith(TEXT("_enable_ik"))) Out.Add(Key);
			}
			return Out;
		}

		void SwitchToFK(URigHierarchy* H)
		{
			for (const FRigElementKey& Key : IKSwitches(H)) H->SetControlValue(Key, FRigControlValue::Make<bool>(false));
		}

		/**
		 * Sets a control's global transform and keeps its children where they are. The hierarchy's own way (not affecting
		 * children) leaves some of DTH's rig elements half worked out, which trips an ensure on the next solve; moving the
		 * children along and putting each one back keeps every element consistent.
		 */
		void SetGlobalKeepChildren(URigHierarchy* H, const FRigElementKey& Key, const FTransform& Global)
		{
			TArray<TPair<FRigElementKey, FTransform>> Kept;
			for (const FRigElementKey& Child : H->GetChildren(Key)) Kept.Emplace(Child, H->GetGlobalTransform(Child));
			H->SetGlobalTransform(Key, Global, false, true);
			for (const TPair<FRigElementKey, FTransform>& K : Kept) H->SetGlobalTransform(K.Key, K.Value, false, true);
		}

		/** Bones the fit matches: the character's skeleton without IK and weapon helper bones (the pose leaves those alone). */
		TArray<FName> FitBones(const USkeletalMesh& Mesh, const URigHierarchy* H, TMap<FName, int32>& OutDepth)
		{
			const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
			TArray<FName> Out;
			for (int32 I = 0; I < Ref.GetNum(); ++I)
			{
				const FName Name = Ref.GetBoneName(I);
				const FString S = Name.ToString();
				int32 Depth = 0;
				for (int32 P = Ref.GetParentIndex(I); P != INDEX_NONE; P = Ref.GetParentIndex(P)) ++Depth;
				OutDepth.Add(Name, Depth);
				if (!S.StartsWith(TEXT("ik_")) && !S.StartsWith(TEXT("weapon_")) && H->Contains(FRigElementKey(Name, ERigElementType::Bone))) Out.Add(Name);
			}
			return Out;
		}

		TMap<FName, FTransform> Globals(const URigHierarchy* H, const TArray<FName>& Bones)
		{
			TMap<FName, FTransform> Out;
			for (const FName& B : Bones) Out.Add(B, H->GetGlobalTransform(FRigElementKey(B, ERigElementType::Bone)));
			return Out;
		}

		/** Which control drives which bone, measured: turning a control moves the bone highest in the skeleton first. */
		struct FBodyMap
		{
			/** Bone and its control, parents before children. */
			TArray<TPair<FName, FName>> BoneControls;
		};

		FBodyMap MeasureBody(UControlRig* Rig, const TArray<FName>& Bones, const TMap<FName, int32>& Depth)
		{
			URigHierarchy* H = Rig->GetHierarchy();
			// Restoring a control from its global transform does not undo a turn on chained controls (fingers), so every
			// control goes back to its value from before each test.
			const TArray<FRigElementKey> AllControls = H->GetAllKeys(false, ERigElementType::Control);
			TArray<FRigControlValue> Snapshot;
			for (const FRigElementKey& Key : AllControls) Snapshot.Add(H->GetControlValue(Key));
			auto Restore = [&]()
			{
				for (int32 I = 0; I < AllControls.Num(); ++I)
				{
					if (FRigControlElement* C = H->Find<FRigControlElement>(AllControls[I])) SetValue(H, C, Snapshot[I]);
				}
			};
			Forward(Rig);
			const TMap<FName, FTransform> Base = Globals(H, Bones);
			const FTransform Turn(FRotator(7.0, 11.0, 5.0));
			const double TurnDeg = FMath::RadiansToDegrees(Turn.GetRotation().GetAngle());
			// Per bone: the control that moves it rigidly by the full turn, preferring the one that moves fewest bones.
			TMap<FName, TPair<FName, double>> Best;
			for (const FRigElementKey& Key : ControlsWithPrefix(H, BodyPrefix))
			{
				const FRigControlElement* C = Control(H, Key);
				if (!C || !C->Settings.IsAnimatable() || !IsTransformControl(C->Settings.ControlType)) continue;
				const FTransform G = H->GetGlobalTransform(Key);
				SetGlobalKeepChildren(H, Key, Turn * G);
				Forward(Rig);
				FName Top;
				int32 TopDepth = MAX_int32;
				int32 Moved = 0;
				double TopDeg = 0.0;
				for (const FName& B : Bones)
				{
					const double D = AngleDeg(H->GetGlobalTransform(FRigElementKey(B, ERigElementType::Bone)).GetRotation(), Base[B].GetRotation());
					if (D < 0.5) continue;
					++Moved;
					// No control of DTH's rig drives a twist bone (they move with their limb); the layer carries their twist.
					if (B.ToString().Contains(TEXT("twist"))) continue;
					if (Depth[B] < TopDepth || (Depth[B] == TopDepth && D > TopDeg)) { TopDepth = Depth[B]; Top = B; TopDeg = D; }
				}
				Restore();
				if (Top.IsNone()) continue;
				const double Score = FMath::Abs(TopDeg - TurnDeg) * 100.0 + Moved;
				TPair<FName, double>* Have = Best.Find(Top);
				if (!Have || Score < Have->Value) Best.Add(Top, TPair<FName, double>(Key.Name, Score));
			}
			Restore();
			Forward(Rig);
			FBodyMap Out;
			for (const TPair<FName, TPair<FName, double>>& E : Best) Out.BoneControls.Emplace(E.Key, E.Value.Key);
			FString Listing;
			for (const TPair<FName, TPair<FName, double>>& E : Best) Listing += FString::Printf(TEXT(" %s=%s"), *E.Key.ToString(), *E.Value.Key.ToString());
			UE_LOG(LogTemp, Display, TEXT("DTHRigKeying: %s body map, %d bones:%s"), *Rig->GetClass()->GetName(), Best.Num(), *Listing);
			Out.BoneControls.Sort([&Depth](const TPair<FName, FName>& A, const TPair<FName, FName>& B) { return Depth[A.Key] < Depth[B.Key]; });
			return Out;
		}

		/** The face board, measured: each CTRL_expressions_* curve, the control axis that writes it, its sign and gain. */
		struct FSlot
		{
			FName Control;
			int32 Axis = 0;
			float Sign = 1.0f;
			float Gain = 1.0f;
		};

		struct FFaceMap
		{
			TMap<FName, FSlot> Curves;
			TArray<FRigElementKey> Controls;
		};

		void ReadCurves(const URigHierarchy* H, TMap<FName, float>& Out)
		{
			Out.Reset();
			for (const FRigElementKey& Key : H->GetAllKeys(false, ERigElementType::Curve))
			{
				if (Key.Name.ToString().StartsWith(CurvePrefix)) Out.Add(Key.Name, H->GetCurveValue(Key));
			}
		}

		FRigControlValue Axes(ERigControlType Type, float X, float Y)
		{
			return Type == ERigControlType::Float ? FRigControlValue::Make<float>(X) : FRigControlValue::Make<FVector3f>(FVector3f(X, Y, 0.f));
		}

		float AxisOf(const FRigControlValue& V, ERigControlType Type, int32 Axis)
		{
			if (Type == ERigControlType::Float) return V.Get<float>();
			const FVector3f P = V.Get<FVector3f>();
			return Axis == 0 ? P.X : P.Y;
		}

		FFaceMap MeasureFace(UControlRig* Rig)
		{
			URigHierarchy* H = Rig->GetHierarchy();
			FFaceMap Out;
			Forward(Rig);
			TMap<FName, float> Base, Now;
			ReadCurves(H, Base);
			for (const FRigElementKey& Key : ControlsWithPrefix(H, FacePrefix))
			{
				const FRigControlElement* C = Control(H, Key);
				if (!C || !C->Settings.IsAnimatable()) continue;
				const ERigControlType Type = C->Settings.ControlType;
				if (Type != ERigControlType::Float && Type != ERigControlType::Vector2D) continue;
				const FRigControlValue Rest = H->GetControlValue(Key, ERigControlValueType::Initial);
				bool bDrives = false;
				for (int32 Axis = 0; Axis < (Type == ERigControlType::Float ? 1 : 2); ++Axis)
				{
					for (const float Side : {1.0f, -1.0f})
					{
						H->SetControlValue(Key, Axes(Type, Axis == 0 ? Side : 0.f, Axis == 1 ? Side : 0.f));
						const float Applied = AxisOf(H->GetControlValue(Key), Type, Axis);
						Forward(Rig);
						ReadCurves(H, Now);
						if (FMath::Abs(Applied) < 1e-4f) continue;
						for (const TPair<FName, float>& V : Now)
						{
							const float Delta = V.Value - Base.FindRef(V.Key);
							if (FMath::Abs(Delta) > 1e-4f && !Out.Curves.Contains(V.Key))
							{
								Out.Curves.Add(V.Key, {Key.Name, Axis, Side, FMath::Abs(Delta / Applied)});
								bDrives = true;
							}
						}
					}
				}
				H->SetControlValue(Key, Rest);
				if (bDrives) Out.Controls.Add(Key);
			}
			Forward(Rig);
			return Out;
		}

		TMap<FString, FFaceMap>& FaceCache() { static TMap<FString, FFaceMap> Cache; return Cache; }
		TMap<FString, FBodyMap>& BodyCache() { static TMap<FString, FBodyMap> Cache; return Cache; }

		const FFaceMap& FaceMapFor(UClass* RigClass, UControlRig* Rig)
		{
			const FString Id = RigClass->GetPathName();
			if (const FFaceMap* Found = FaceCache().Find(Id)) return *Found;
			return FaceCache().Add(Id, MeasureFace(Rig));
		}

		FFrameNumber TickOf(const UMovieScene* MovieScene, int32 DisplayFrame)
		{
			return FFrameRate::TransformTime(FFrameTime(DisplayFrame), MovieScene->GetDisplayRate(), MovieScene->GetTickResolution()).FloorToFrame();
		}

		void RefreshSequencer()
		{
			ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
		}
	}

	TArray<FBinding> Bindings(ULevelSequence* Sequence)
	{
		TArray<FBinding> Out;
		UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
		if (!MovieScene) return Out;
		for (const FMovieSceneBinding& B : static_cast<const UMovieScene*>(MovieScene)->GetBindings())
		{
			FBinding Entry;
			Entry.Guid = B.GetObjectGuid();
			if (const FMovieScenePossessable* P = MovieScene->FindPossessable(Entry.Guid)) Entry.Name = P->GetName();
			else if (const FMovieSceneSpawnable* S = MovieScene->FindSpawnable(Entry.Guid)) Entry.Name = S->GetName();
			for (UMovieSceneTrack* T : MovieScene->FindTracks(UMovieSceneControlRigParameterTrack::StaticClass(), Entry.Guid))
			{
				const UMovieSceneControlRigParameterTrack* CR = Cast<UMovieSceneControlRigParameterTrack>(T);
				if (CR && CR->GetControlRig()) { Entry.RigClass = CR->GetControlRig()->GetClass(); break; }
			}
			Out.Add(Entry);
		}
		return Out;
	}

	UClass* DefaultRig(const USkeletalMesh* Mesh)
	{
		UObject* Rig = Mesh ? Mesh->GetDefaultAnimatingRig().LoadSynchronous() : nullptr;
		if (UClass* Class = Cast<UClass>(Rig))
		{
			return Class->IsChildOf(UControlRig::StaticClass()) ? Class : nullptr;
		}
		return Rig ? LoadClass<UControlRig>(nullptr, *(Rig->GetPathName() + TEXT("_C")), nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
	}

	bool Resolve(ULevelSequence* Sequence, const FString& BindingName, const USkeletalMesh* Character, FTarget& Out, FString& OutError)
	{
		Out = FTarget();
		UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
		if (!MovieScene)
		{
			OutError = TEXT("Pick a level sequence to key into.");
			return false;
		}
		const FBinding* Found = nullptr;
		const TArray<FBinding> All = Bindings(Sequence);
		for (const FBinding& B : All)
		{
			if (B.Name == BindingName) { Found = &B; break; }
		}
		if (!Found)
		{
			OutError = FString::Printf(TEXT("%s has no binding named \"%s\"."), *Sequence->GetName(), *BindingName);
			return false;
		}
		Out.Sequence = Sequence;
		Out.Binding = Found->Guid;
		UClass* Wanted = DefaultRig(Character);
		UMovieSceneControlRigParameterTrack* Any = nullptr;
		for (UMovieSceneTrack* T : MovieScene->FindTracks(UMovieSceneControlRigParameterTrack::StaticClass(), Found->Guid))
		{
			UMovieSceneControlRigParameterTrack* CR = Cast<UMovieSceneControlRigParameterTrack>(T);
			if (!CR || !CR->GetControlRig()) continue;
			if (!Any) Any = CR;
			if (Wanted && CR->GetControlRig()->GetClass() == Wanted) { Any = CR; break; }
		}
		if (!Any)
		{
			if (!Wanted)
			{
				OutError = FString::Printf(TEXT("\"%s\" has no control rig track, and the character has no default animating rig to add."), *BindingName);
				return false;
			}
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			Any = Cast<UMovieSceneControlRigParameterTrack>(UControlRigSequencerEditorLibrary::FindOrCreateControlRigTrack(World, Sequence, Wanted,
				FMovieSceneBindingProxy(Found->Guid, Sequence)));
			if (!Any || !Any->GetControlRig())
			{
				OutError = FString::Printf(TEXT("Could not add %s to \"%s\"."), *Wanted->GetName(), *BindingName);
				return false;
			}
		}
		Out.Track = Any;
		Out.RigClass = Any->GetControlRig()->GetClass();
		for (UMovieSceneTrack* T : MovieScene->FindTracks(UMovieSceneControlRigParameterTrack::StaticClass(), Found->Guid))
		{
			UMovieSceneControlRigParameterTrack* CR = Cast<UMovieSceneControlRigParameterTrack>(T);
			if (CR && CR->GetControlRig() && CR->GetControlRig()->IsA<UFKControlRig>() && CR->GetControlRig()->IsAdditive()) { Out.Layer = CR; break; }
		}
		if (!Out.Layer)
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			Out.Layer = Cast<UMovieSceneControlRigParameterTrack>(UControlRigSequencerEditorLibrary::FindOrCreateControlRigTrack(World, Sequence,
				UFKControlRig::StaticClass(), FMovieSceneBindingProxy(Found->Guid, Sequence), true));
		}
		if (!Out.Layer || !Out.Layer->GetControlRig())
		{
			OutError = FString::Printf(TEXT("Could not add the layered FK Control Rig track to \"%s\"."), *BindingName);
			return false;
		}
		// A layer added while the sequence is not open in Sequencer is bound to nothing, so its FK rig has no controls yet.
		// Build them from the character, as Sequencer does when it binds the rig, and give the section its channels.
		UControlRig* LayerRig = Out.Layer->GetControlRig();
		if (LayerRig->GetHierarchy()->GetAllKeys(false, ERigElementType::Control).IsEmpty())
		{
			if (!Character)
			{
				OutError = TEXT("The FK layer needs the character to build its controls.");
				return false;
			}
			USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			Component->SetSkeletalMeshAsset(const_cast<USkeletalMesh*>(Character));
			if (!LayerRig->GetObjectBinding().IsValid()) LayerRig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
			LayerRig->GetObjectBinding()->BindToObject(Component);
			LayerRig->Initialize();
			LayerRig->Evaluate_AnyThread();
			for (UMovieSceneSection* S : Out.Layer->GetAllSections())
			{
				if (UMovieSceneControlRigParameterSection* CR = Cast<UMovieSceneControlRigParameterSection>(S)) CR->RecreateWithThisControlRig(LayerRig, true);
			}
			LayerRig->GetObjectBinding()->UnbindFromObject();
		}
		return true;
	}

	bool Key(const FTarget& Target, UMovieSceneControlRigParameterTrack* Track, int32 DisplayFrame, const TMap<FName, FRigControlValue>& Values,
		const TMap<FName, FTransform>& LocalTransforms, bool bStepped, FString& OutError)
	{
		UControlRig* Rig = Track ? Track->GetControlRig() : nullptr;
		UMovieScene* MovieScene = Target.Sequence ? Target.Sequence->GetMovieScene() : nullptr;
		UMovieSceneControlRigParameterSection* Section = Track ? Cast<UMovieSceneControlRigParameterSection>(Track->GetSectionToKey()) : nullptr;
		if (!Section && Track && !Track->GetAllSections().IsEmpty())
		{
			Section = Cast<UMovieSceneControlRigParameterSection>(Track->GetAllSections()[0]);
		}
		if (!Rig || !MovieScene || !Section)
		{
			OutError = TEXT("The control rig track has no section to key.");
			return false;
		}
		const FFrameNumber Tick = TickOf(MovieScene, DisplayFrame);
		const FScopedTransaction Transaction(LOCTEXT("KeyRig", "Key onto Control Rig"));
		Target.Sequence->Modify();
		Section->Modify();
		Section->ExpandToFrame(Tick);

		URigHierarchy* H = Rig->GetHierarchy();
		TArray<FName> Keyed;
		for (const TPair<FName, FRigControlValue>& V : Values)
		{
			FRigControlElement* C = H->Find<FRigControlElement>(FRigElementKey(V.Key, ERigElementType::Control));
			if (!C) continue;
			SetValue(H, C, V.Value);
			Keyed.Add(V.Key);
		}
		for (const TPair<FName, FTransform>& T : LocalTransforms)
		{
			const FRigElementKey Key(T.Key, ERigElementType::Control);
			if (!H->Contains(Key)) continue;
			H->SetLocalTransform(Key, T.Value, false, false);
			if (FRigControlElement* C = H->Find<FRigControlElement>(Key))
			{
				H->SetControlPreferredEulerAngles(C, H->GetControlValue(C, ERigControlValueType::Current).GetAsTransform(C->Settings.ControlType, C->Settings.PrimaryAxis));
			}
			Keyed.Add(T.Key);
		}
		if (Keyed.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s has none of the controls to key."), *Rig->GetClass()->GetName());
			return false;
		}
		const TArray<FName> Selection = Rig->CurrentControlSelection();
		Rig->ClearControlSelection();
		for (const FName& Name : Keyed) Rig->SelectControl(Name, true);
		Section->RecordControlRigKey(Tick, false, bStepped ? EMovieSceneKeyInterpolation::Constant : EMovieSceneKeyInterpolation::Auto, true);
		Rig->ClearControlSelection();
		for (const FName& Name : Selection) Rig->SelectControl(Name, true);

		Target.Sequence->MarkPackageDirty();
		RefreshSequencer();
		return true;
	}

	bool FitBody(UClass* RigClass, USkeletalMesh* Character, const TMap<FName, FTransform>& LocalPose, FBodyFit& Out, FString& OutError)
	{
		Out = FBodyFit();
		if (!RigClass || !Character)
		{
			OutError = TEXT("No rig or no character.");
			return false;
		}
		TStrongObjectPtr<UControlRig> Rig = MakeRig(RigClass, Character);
		URigHierarchy* H = Rig->GetHierarchy();
		SwitchToFK(H);
		TMap<FName, int32> Depth;
		const TArray<FName> Bones = FitBones(*Character, H, Depth);

		const FString Id = RigClass->GetPathName() + TEXT("|") + Character->GetPathName();
		if (!BodyCache().Contains(Id)) BodyCache().Add(Id, MeasureBody(Rig.Get(), Bones, Depth));
		const FBodyMap& Map = BodyCache()[Id];
		if (Map.BoneControls.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s has no body controls (\"%s\") that drive the skeleton."), *RigClass->GetName(), BodyPrefix);
			return false;
		}

		// The pose's global transforms, through the rig's own bone hierarchy.
		H->ResetPoseToInitial(ERigElementType::Bone);
		const FReferenceSkeleton& Ref = Character->GetRefSkeleton();
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			const FRigElementKey Key(Ref.GetBoneName(I), ERigElementType::Bone);
			if (const FTransform* T = LocalPose.Find(Ref.GetBoneName(I)); T && H->Contains(Key)) H->SetLocalTransform(Key, *T, false, true);
		}
		const TMap<FName, FTransform> Target = Globals(H, Bones);
		// A start close to the answer for whatever the backwards solve does carry; the fit below does the rest.
		if (Rig->SupportsEvent(FRigUnit_InverseExecution::EventName)) Rig->Execute(FRigUnit_InverseExecution::EventName);
		SwitchToFK(H);

		// Each control moves its bone rigidly, and setting a control's global transform without its children keeps every
		// other control where it is, so one pass lands within float precision; a few more absorb any coupling.
		for (int32 Pass = 0; Pass < 12; ++Pass)
		{
			Forward(Rig.Get());
			double Worst = 0.0;
			for (const TPair<FName, FName>& BC : Map.BoneControls)
			{
				const FTransform Now = H->GetGlobalTransform(FRigElementKey(BC.Key, ERigElementType::Bone));
				Worst = FMath::Max(Worst, AngleDeg(Now.GetRotation(), Target[BC.Key].GetRotation()));
				Worst = FMath::Max(Worst, (Now.GetTranslation() - Target[BC.Key].GetTranslation()).Size());
			}
			if (Worst < 1e-4) break;
			for (const TPair<FName, FName>& BC : Map.BoneControls)
			{
				const FRigElementKey CK(BC.Value, ERigElementType::Control);
				const FTransform Now = H->GetGlobalTransform(FRigElementKey(BC.Key, ERigElementType::Bone));
				const FTransform C = H->GetGlobalTransform(CK);
				SetGlobalKeepChildren(H, CK, C * Now.Inverse() * Target[BC.Key]);
			}
		}
		Forward(Rig.Get());

		for (const FName& B : Bones)
		{
			// Every bone is the pose's, twist bones included: DTH's post-process twist step (Twist 2 Bones By Ratio) blends each
			// twist bone's own twist with its limb's, so the twist the pose brings in shows. Face bones too (a Daz pose can turn
			// the eyes or open the jaw): an expression keyed on the same frame adds its face to the pose's, as the two previews
			// put together would show.
			const FTransform Now = H->GetGlobalTransform(FRigElementKey(B, ERigElementType::Bone));
			const double Deg = AngleDeg(Now.GetRotation(), Target[B].GetRotation());
			const double Cm = (Now.GetTranslation() - Target[B].GetTranslation()).Size();
			if (Deg > Out.WorstDeg) { Out.WorstDeg = Deg; Out.WorstBone = B; }
			Out.WorstCm = FMath::Max(Out.WorstCm, Cm);
			if (Deg > 0.05 || Cm > 0.05) Out.Unreached.Add(FString::Printf(TEXT("%s %.2f deg %.2f cm"), *B.ToString(), Deg, Cm));
			// The layer applies Delta * (the rig's local) per bone, in hierarchy order, so the local delta is exact.
			const FTransform* Wanted = LocalPose.Find(B);
			const FTransform RigLocal = H->GetLocalTransform(FRigElementKey(B, ERigElementType::Bone));
			FTransform Delta = Wanted ? *Wanted * RigLocal.Inverse() : FTransform::Identity;
			Delta.NormalizeRotation();
			Out.Layer.Add(UFKControlRig::GetControlName(B, ERigElementType::Bone), Delta);
		}
		// Every body control, not just the fitted ones: the backwards solve's start sets the rest, and a key must fully
		// set the body (a control left unkeyed would hold whatever an earlier key or its default says).
		for (const FRigElementKey& Key : ControlsWithPrefix(H, BodyPrefix))
		{
			const FRigControlElement* C = Control(H, Key);
			if (C && C->Settings.IsAnimatable()) Out.Values.Add(Key.Name, H->GetControlValue(Key));
		}
		for (const FRigElementKey& Key : IKSwitches(H)) Out.Values.Add(Key.Name, FRigControlValue::Make<bool>(false));
		return true;
	}

	TSet<FName> BoardCurves(UClass* RigClass)
	{
		TSet<FName> Out;
		if (!RigClass) return Out;
		TStrongObjectPtr<UControlRig> Rig = MakeRig(RigClass, nullptr);
		for (const TPair<FName, FSlot>& C : FaceMapFor(RigClass, Rig.Get()).Curves) Out.Add(C.Key);
		return Out;
	}

	TArray<FBoardSlot> BoardMap(UClass* RigClass)
	{
		TArray<FBoardSlot> Out;
		if (!RigClass) return Out;
		TStrongObjectPtr<UControlRig> Rig = MakeRig(RigClass, nullptr);
		for (const TPair<FName, FSlot>& C : FaceMapFor(RigClass, Rig.Get()).Curves)
		{
			Out.Add({C.Key, C.Value.Control, C.Value.Axis, C.Value.Sign, C.Value.Gain});
		}
		return Out;
	}

	bool FaceBoard(UClass* RigClass, const TMap<FName, float>& Curves, TMap<FName, FRigControlValue>& OutValues,
		TMap<FName, FRigControlValue>& OutLayer, int32& OutCorrected, FString& OutError)
	{
		OutValues.Reset();
		OutLayer.Reset();
		OutCorrected = 0;
		if (!RigClass)
		{
			OutError = TEXT("No rig.");
			return false;
		}
		TStrongObjectPtr<UControlRig> Rig = MakeRig(RigClass, nullptr);
		URigHierarchy* H = Rig->GetHierarchy();
		const FFaceMap& Map = FaceMapFor(RigClass, Rig.Get());
		if (Map.Controls.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s has no face board (\"%s\" controls writing %s curves)."), *RigClass->GetName(), FacePrefix, CurvePrefix);
			return false;
		}

		// Per control axis: the positive and negative side's wanted value (curves on one side are averaged; a board
		// control drives each curve alone and linearly, gain 1 on DTH's board, measured).
		struct FWant { double Pos = 0.0; int32 NPos = 0; double Neg = 0.0; int32 NNeg = 0; };
		TMap<TPair<FName, int32>, FWant> Want;
		for (const TPair<FName, float>& C : Curves)
		{
			const FSlot* Slot = Map.Curves.Find(C.Key);
			if (!Slot) continue;
			FWant& W = Want.FindOrAdd({Slot->Control, Slot->Axis});
			const double V = C.Value / Slot->Gain;
			if (Slot->Sign > 0) { W.Pos += V; ++W.NPos; } else { W.Neg += V; ++W.NNeg; }
		}
		for (const FRigElementKey& Key : Map.Controls)
		{
			const ERigControlType Type = Control(H, Key)->Settings.ControlType;
			const FRigControlValue Rest = H->GetControlValue(Key, ERigControlValueType::Initial);
			float Axis[2] = {AxisOf(Rest, Type, 0), Type == ERigControlType::Float ? 0.f : AxisOf(Rest, Type, 1)};
			for (int32 A = 0; A < 2; ++A)
			{
				if (const FWant* W = Want.Find({Key.Name, A}))
				{
					const double Pos = W->NPos ? W->Pos / W->NPos : 0.0;
					const double Neg = W->NNeg ? W->Neg / W->NNeg : 0.0;
					Axis[A] = static_cast<float>(Pos >= Neg ? Pos : -Neg);
				}
			}
			H->SetControlValue(Key, Axes(Type, Axis[0], Axis[1]));
			OutValues.Add(Key.Name, H->GetControlValue(Key));
		}
		// What the board actually writes (limits clamp, a slider shows one of its two curves, some curves have no control);
		// the layer adds the difference for every expression curve, so the face is exactly the mix.
		Forward(Rig.Get());
		TMap<FName, float> Got;
		ReadCurves(H, Got);
		TSet<FName> All;
		for (const TPair<FName, float>& G : Got) All.Add(G.Key);
		for (const TPair<FName, float>& C : Curves) All.Add(C.Key);
		for (const FName& Curve : All)
		{
			const float Delta = Curves.FindRef(Curve) - Got.FindRef(Curve);
			if (FMath::Abs(Delta) > 1e-4f) ++OutCorrected;
			OutLayer.Add(UFKControlRig::GetControlName(Curve, ERigElementType::Curve), FRigControlValue::Make<float>(FMath::Abs(Delta) > 1e-6f ? Delta : 0.f));
		}
		return true;
	}

}

#undef LOCTEXT_NAMESPACE
