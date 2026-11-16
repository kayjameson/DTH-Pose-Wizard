#include "DTHFaceRigProbe.h"

#include "Animation/AnimCurveMetadata.h"
#include "Animation/AnimInstance.h"
#include "Animation/PoseAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "DTHFaceCoreLog.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "DTHFaceRig.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "DTHCurveDriverInstance.h"
#include "PreviewScene.h"
#include "SkeletalRenderPublic.h"

#define LOCTEXT_NAMESPACE "DTHFaceRigProbe"

namespace
{
	constexpr double Changed = 1e-3;
	const TCHAR* BoardPrefix = TEXT("CTRL_expressions_");

	/** The character in an off-screen preview world, driven by curves, read after its post-process blueprint. */
	struct FHarness
	{
		FPreviewScene Scene{FPreviewScene::ConstructionValues()};
		USkeletalMeshComponent* Component = nullptr;
		UDTHCurveDriverInstance* Driver = nullptr;
		TArray<FName> Read;
		int32 Evaluations = 0;

		bool Init(USkeletalMesh* Mesh, FString& OutError)
		{
			Component = NewObject<USkeletalMeshComponent>(GetTransientPackage());
			Component->SetSkeletalMesh(Mesh);
			Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Component->bEnableUpdateRateOptimizations = false;
			Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			Component->SetAnimInstanceClass(UDTHCurveDriverInstance::StaticClass());
			Scene.AddComponent(Component, FTransform::Identity);
			Driver = Cast<UDTHCurveDriverInstance>(Component->GetAnimInstance());
			if (!Driver || !Component->GetPostProcessInstance())
			{
				OutError = TEXT("The character has no post-process blueprint running (it needs DTH's, as the DTH import sets up).");
				return false;
			}
			return true;
		}

		void Eval(const TMap<FName, float>& In, TArray<float>& Out)
		{
			++Evaluations;
			Driver->SetCurves(In);
			Component->TickAnimation(1.0f / 30.0f, false);
			Component->RefreshBoneTransforms();
			Component->HandleExistingParallelEvaluationTask(true, true);
			const UAnimInstance* Post = Component->GetPostProcessInstance();
			Out.SetNumUninitialized(Read.Num());
			for (int32 I = 0; I < Read.Num(); ++I)
			{
				float V = 0.0f;
				Post->GetCurveValue(Read[I], V);
				Out[I] = V;
			}
		}

		/** Skinned positions (component space, cm) of these render vertices of LOD 0 for curves In, as Unreal skins them on the CPU. */
		void Geometry(const TMap<FName, float>& In, const TArray<int32>& Vertices, TArray<FVector3f>& Out)
		{
			++Evaluations;
			Driver->SetCurves(In);
			// two ticks: a node that eases toward its input would not have arrived after one
			for (int32 T = 0; T < 2; ++T)
			{
				Component->TickAnimation(1.0f / 30.0f, false);
				Component->RefreshBoneTransforms();
				Component->HandleExistingParallelEvaluationTask(true, true);
			}
			TArray<FFinalSkinVertex> Skinned;
			Component->GetCPUSkinnedVertices(Skinned, 0);
			Out.SetNumUninitialized(Vertices.Num());
			for (int32 I = 0; I < Vertices.Num(); ++I)
			{
				Out[I] = Skinned.IsValidIndex(Vertices[I]) ? Skinned[Vertices[I]].Position : FVector3f::ZeroVector;
			}
		}

		~FHarness()
		{
			if (Component)
			{
				Scene.RemoveComponent(Component);
			}
		}
	};

	void AddDependencies(FName Package, TSet<FName>& Out, int32 Depth)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FName> Deps;
		Registry.GetDependencies(Package, Deps, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);
		for (const FName& D : Deps)
		{
			if (!D.ToString().StartsWith(TEXT("/Script/")) && !Out.Contains(D))
			{
				Out.Add(D);
				if (Depth > 1) AddDependencies(D, Out, Depth - 1);
			}
		}
	}

	TSet<FName> SetupPackages(const USkeletalMesh* Character)
	{
		TSet<FName> Packages;
		const UClass* Post = Character ? Character->GetPostProcessAnimBlueprint().Get() : nullptr;
		if (Post)
		{
			const FName Package = Post->GetOutermost()->GetFName();
			Packages.Add(Package);
			AddDependencies(Package, Packages, 2);
		}
		return Packages;
	}
}

FString FDTHFaceRigProbe::RigFile(const USkeletalMesh* Character)
{
	const UClass* Post = Character ? Character->GetPostProcessAnimBlueprint().Get() : nullptr;
	const FString Name = Post ? FPaths::MakeValidFileName(Post->GetOutermost()->GetName().Replace(TEXT("/"), TEXT("_"))) : TEXT("none");
	return FPaths::ProjectSavedDir() / TEXT("DTHExpressionWizard") / (TEXT("FaceRig") + Name + TEXT(".json"));
}

TArray<FString> FDTHFaceRigProbe::Fingerprint(const USkeletalMesh* Character)
{
	TArray<FString> Out;
	for (const FName& Package : SetupPackages(Character))
	{
		FString File;
		if (FPackageName::DoesPackageExist(Package.ToString(), &File))
		{
			Out.Add(FString::Printf(TEXT("%s|%lld|%s"), *Package.ToString(), IFileManager::Get().FileSize(*File),
				*IFileManager::Get().GetTimeStamp(*File).ToString()));
		}
	}
	Out.Sort();
	return Out;
}

bool FDTHFaceRigProbe::GetOrMeasure(USkeletalMesh* Character, FDTHFaceRig& OutRig, bool& bOutMeasured, FString& OutError)
{
	bOutMeasured = false;
	const FString File = RigFile(Character);
	FString LoadError;
	if (OutRig.Load(File, LoadError) && OutRig.Fingerprint == Fingerprint(Character))
	{
		return true;
	}
	if (!Measure(Character, OutRig, OutError))
	{
		return false;
	}
	bOutMeasured = true;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
	if (!OutRig.Save(File))
	{
		UE_LOG(LogDTHFace, Warning, TEXT("Could not save the face rig measurement to %s."), *File);
	}
	return true;
}

bool FDTHFaceRigProbe::Measure(USkeletalMesh* Character, FDTHFaceRig& OutRig, FString& OutError)
{
	OutRig = FDTHFaceRig();
	if (!Character || !Character->GetPostProcessAnimBlueprint())
	{
		OutError = TEXT("Pick a DTH character (one with DTH's post-process blueprint) so its face setup can be measured.");
		return false;
	}

	// Face-board curves and every curve the setup can output: the curves registered on the character.
	TArray<FName> Registered;
	if (const UAnimCurveMetaData* Meta = Character->GetAssetUserData<UAnimCurveMetaData>())
	{
		Meta->GetCurveMetaDataNames(Registered);
	}
	Registered.Sort(FNameLexicalLess());
	TArray<FName> Curves;
	for (const FName& N : Registered)
	{
		if (N.ToString().StartsWith(BoardPrefix)) Curves.Add(N);
	}
	if (Curves.IsEmpty())
	{
		OutError = TEXT("The character has no face-board curves (CTRL_expressions_*) registered; import it with DTH's curves.");
		return false;
	}

	const double StartTime = FPlatformTime::Seconds();
	FHarness H;
	if (!H.Init(Character, OutError))
	{
		return false;
	}
	H.Read = Registered;
	TMap<FName, int32> ReadIndex;
	for (int32 I = 0; I < Registered.Num(); ++I) ReadIndex.Add(Registered[I], I);

	// Progress in evaluations: each step gets its share of the bar up front (the pair search's share is its batches,
	// the check's an estimate), and runs its own bar within that share.
	const int32 NumCurves = Curves.Num();
	constexpr int32 BatchSize = 24;
	float SearchWork = 0.0f;
	for (int32 A = 0; A < NumCurves; ++A) SearchWork += 2.0f * FMath::DivideAndRoundUp(NumCurves - A - 1, BatchSize);
	const float SinglesWork = 2.0f * NumCurves, PosesWork = 250.0f, CheckWork = 150.0f;
	FScopedSlowTask Task(SinglesWork + PosesWork + SearchWork + CheckWork, LOCTEXT("Measuring", "Measuring DTH's face rig on the character..."));
	Task.MakeDialog(true);
	auto Progress = [&](const TCHAR* Step)
	{
		UE_LOG(LogDTHFace, Log, TEXT("Face rig measurement: %s (%d evaluations, %.1f s)."), Step, H.Evaluations, FPlatformTime::Seconds() - StartTime);
		GLog->Flush();
	};
	auto Cancelled = [&Task, &OutError]()
	{
		if (!Task.ShouldCancel()) return false;
		OutError = TEXT("Measurement cancelled.");
		return true;
	};

	TArray<float> Base, Out;
	TMap<FName, float> In;
	H.Eval(In, Base);
	H.Eval(In, Base);  // settle the post-process blueprint and its rig
	auto Delta = [&](const TArray<float>& Values, TArray<float>& D)
	{
		D.SetNumUninitialized(Values.Num());
		for (int32 I = 0; I < Values.Num(); ++I) D[I] = Values[I] - Base[I];
	};

	// Every curve alone, at full and half strength.
	TArray<TArray<float>> Full, Half;
	Full.SetNum(NumCurves);
	Half.SetNum(NumCurves);
	Task.EnterProgressFrame(SinglesWork, LOCTEXT("Singles", "Each face-board curve on its own..."));
	FScopedSlowTask SinglesTask(static_cast<float>(NumCurves));
	for (int32 C = 0; C < NumCurves; ++C)
	{
		SinglesTask.EnterProgressFrame();
		if (Cancelled()) return false;
		In = {{Curves[C], 1.0f}}; H.Eval(In, Out); Delta(Out, Full[C]);
		In = {{Curves[C], 0.5f}}; H.Eval(In, Out); Delta(Out, Half[C]);
	}

	Progress(TEXT("single curves done"));

	// Does the setup remember earlier frames? Play a curve, something else, and the curve again: the two must match.
	{
		TArray<float> First, Second;
		In = {{Curves[0], 1.0f}}; H.Eval(In, First);
		In = {{Curves[NumCurves / 2], 1.0f}, {Curves[NumCurves - 1], 1.0f}}; H.Eval(In, Out);
		In = {{Curves[0], 1.0f}}; H.Eval(In, Second);
		float Diff = 0.0f;
		FName Where;
		for (int32 I = 0; I < First.Num(); ++I)
		{
			if (FMath::Abs(First[I] - Second[I]) > Diff) { Diff = FMath::Abs(First[I] - Second[I]); Where = Registered[I]; }
		}
		UE_LOG(LogDTHFace, Log, TEXT("Face rig measurement: repeat check differs by %.5f (%s)."), Diff, *Where.ToString());
	}

	// DTH's face pose asset: among the pose assets the setup uses, the one whose poses the face-board curves drive most.
	const UPoseAsset* Best = nullptr;
	int32 BestScore = 0;
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	for (const FName& Package : SetupPackages(Character))
	{
		TArray<FAssetData> Assets;
		Registry.GetAssetsByPackageName(Package, Assets);
		for (const FAssetData& A : Assets)
		{
			if (!A.IsInstanceOf(UPoseAsset::StaticClass())) continue;
			const UPoseAsset* Pose = Cast<UPoseAsset>(A.GetAsset());
			int32 Score = 0;
			for (const FName& P : Pose ? Pose->GetPoseFNames() : TArray<FName>())
			{
				const int32* R = ReadIndex.Find(P);
				for (int32 C = 0; R && C < NumCurves; ++C)
				{
					if (FMath::Abs(Full[C][*R]) > Changed) { ++Score; break; }
				}
			}
			if (Score > BestScore) { Best = Pose; BestScore = Score; }
		}
	}
	if (!Best || Best->GetNumPoses() < 2)
	{
		OutError = TEXT("Could not find the face pose asset the character's post-process blueprint drives.");
		return false;
	}
	OutRig.Curves = Curves;
	OutRig.Poses = TArray<FName>(Best->GetPoseFNames().GetData() + 1, Best->GetNumPoses() - 1);
	const int32 NumPoses = OutRig.Poses.Num();

	// How each pose shows in the output: its own weight curve, or, for poses the rig writes straight to their morph,
	// the curve that answers when the pose's weight is played directly (and the rig leaves it alone).
	TArray<int32> PoseRead;
	PoseRead.Init(INDEX_NONE, NumPoses);
	TArray<int32> MorphRead;
	MorphRead.Init(INDEX_NONE, NumPoses);
	Task.EnterProgressFrame(PosesWork, LOCTEXT("Poses", "Reading the face poses..."));
	FScopedSlowTask PosesTask(static_cast<float>(NumPoses));
	for (int32 P = 0; P < NumPoses; ++P)
	{
		PosesTask.EnterProgressFrame();
		if (Cancelled()) return false;
		const int32* R = ReadIndex.Find(OutRig.Poses[P]);
		if (!R) continue;
		PoseRead[P] = *R;
		In = {{OutRig.Poses[P], 1.0f}};
		H.Eval(In, Out);
		float Strongest = 0.0f;
		for (int32 I = 0; I < Out.Num(); ++I)
		{
			const float D = FMath::Abs(Out[I] - Base[I]);
			if (I != *R && D > 0.5f && D > Strongest) { Strongest = D; MorphRead[P] = I; }
		}
	}
	auto PoseDelta = [&](const TArray<float>& D, int32 P) -> float
	{
		if (PoseRead[P] == INDEX_NONE) return 0.0f;
		const float W = D[PoseRead[P]];
		return (FMath::Abs(W) > Changed || MorphRead[P] == INDEX_NONE) ? W : D[MorphRead[P]];
	};

	// Linear links and exponents.
	OutRig.Exponent.Init(1, NumCurves);
	TArray<TArray<float>> Single;
	Single.SetNum(NumCurves);
	TArray<bool> bActive;
	bActive.Init(false, NumCurves);
	for (int32 C = 0; C < NumCurves; ++C)
	{
		Single[C].SetNumZeroed(NumPoses);
		float Largest = 0.0f, Ratio = 1.0f;
		for (int32 P = 0; P < NumPoses; ++P)
		{
			const float W = PoseDelta(Full[C], P);
			if (FMath::Abs(W) <= Changed) continue;
			Single[C][P] = W;
			OutRig.Linear.Add({C, P, W});
			bActive[C] = true;
			if (FMath::Abs(W) > Largest) { Largest = FMath::Abs(W); Ratio = PoseDelta(Half[C], P) / W; }
		}
		if (bActive[C])
		{
			if (FMath::Abs(Ratio - 0.25f) < 0.05f) OutRig.Exponent[C] = 2;
			else if (FMath::Abs(Ratio - 0.5f) > 0.05f)
			{
				OutRig.Notes.Add(FString::Printf(TEXT("%s: at half strength gives %.2f of its full effect (not modelled)."), *Curves[C].ToString(), Ratio));
			}
		}
	}

	// Pairs: what two curves do together beyond the sum of each alone. Few pairs act together, so curve A is first tried
	// with a whole batch of other curves (A with the batch, against the batch alone and A alone); only a batch that
	// shows something is split in halves, down to single pairs, which are then measured exactly.
	auto PoseDeltas = [&](const TMap<FName, float>& Curves01, TArray<float>& OutPoses)
	{
		H.Eval(Curves01, Out);
		Delta(Out, Out);
		OutPoses.SetNumUninitialized(NumPoses);
		for (int32 P = 0; P < NumPoses; ++P) OutPoses[P] = PoseDelta(Out, P);
	};
	auto WithAll = [&](const TArray<int32>& Set, int32 Extra)
	{
		TMap<FName, float> M;
		for (const int32 C : Set) M.Add(Curves[C], 1.0f);
		if (Extra != INDEX_NONE) M.Add(Curves[Extra], 1.0f);
		return M;
	};
	TArray<float> Together, Alone;
	TFunction<void(int32, const TArray<int32>&)> Search = [&](int32 A, const TArray<int32>& Set)
	{
		PoseDeltas(WithAll(Set, A), Together);
		if (Set.Num() == 1)
		{
			for (int32 P = 0; P < NumPoses; ++P)
			{
				const float Extra = Together[P] - Single[A][P] - Single[Set[0]][P];
				if (FMath::Abs(Extra) > Changed) OutRig.Pairs.Add({A, Set[0], P, Extra});
			}
			return;
		}
		PoseDeltas(WithAll(Set, INDEX_NONE), Alone);
		bool bSomething = false;
		for (int32 P = 0; P < NumPoses && !bSomething; ++P)
		{
			bSomething = FMath::Abs(Together[P] - Alone[P] - Single[A][P]) > Changed;
		}
		if (bSomething)
		{
			const int32 Half = Set.Num() / 2;
			Search(A, TArray<int32>(Set.GetData(), Half));
			Search(A, TArray<int32>(Set.GetData() + Half, Set.Num() - Half));
		}
	};
	Task.EnterProgressFrame(SearchWork, LOCTEXT("Pairs", "Curves that act together..."));
	FScopedSlowTask SearchTask(SearchWork);
	for (int32 A = 0; A < NumCurves; ++A)
	{
		SearchTask.EnterProgressFrame(2.0f * FMath::DivideAndRoundUp(NumCurves - A - 1, BatchSize));
		if (Cancelled()) return false;
		TArray<int32> Partners;
		for (int32 B = A + 1; B < NumCurves; ++B)
		{
			if (bActive[A] || bActive[B]) Partners.Add(B);
		}
		for (int32 First = 0; First < Partners.Num(); First += BatchSize)
		{
			Search(A, TArray<int32>(Partners.GetData() + First, FMath::Min(BatchSize, Partners.Num() - First)));
		}
	}

	{
		TMap<int32, int32> PerPose;
		for (const FDTHFaceRig::FPair& Pair : OutRig.Pairs) ++PerPose.FindOrAdd(Pair.Pose);
		PerPose.ValueSort(TGreater<int32>());
		FString Top;
		int32 Shown = 0;
		for (const TPair<int32, int32>& E : PerPose)
		{
			if (Shown++ == 8) break;
			Top += FString::Printf(TEXT(" %s=%d"), *OutRig.Poses[E.Key].ToString(), E.Value);
		}
		UE_LOG(LogDTHFace, Log, TEXT("Face rig measurement: pair search found %d terms on %d poses; most:%s"), OutRig.Pairs.Num(), PerPose.Num(), *Top);
		Progress(TEXT("pair search done"));
	}

	// Check the pair terms at half strength.
	float Worst = 0.0f;
	TArray<double> Values, Weights;
	Task.EnterProgressFrame(CheckWork, LOCTEXT("Check", "Checking the curves that act together..."));
	FScopedSlowTask CheckTask(static_cast<float>(OutRig.Pairs.Num()));
	for (int32 I = 0; I < OutRig.Pairs.Num(); ++I)
	{
		CheckTask.EnterProgressFrame();
		if (Cancelled()) return false;
		const FDTHFaceRig::FPair Pair = OutRig.Pairs[I];
		for (const TPair<float, float>& S : {TPair<float, float>(0.5f, 1.0f), TPair<float, float>(1.0f, 0.5f)})
		{
			In = {{Curves[Pair.A], S.Key}, {Curves[Pair.B], S.Value}};
			H.Eval(In, Out);
			Delta(Out, Out);
			Values.Init(0.0, NumCurves);
			Values[Pair.A] = S.Key;
			Values[Pair.B] = S.Value;
			OutRig.Evaluate(Values, Weights);
			Worst = FMath::Max(Worst, static_cast<float>(FMath::Abs(PoseDelta(Out, Pair.Pose) - Weights[Pair.Pose])));
		}
	}
	if (Worst > 0.02f)
	{
		OutRig.Notes.Add(FString::Printf(TEXT("Curve pairs at half strength differ from the model by up to %.3f."), Worst));
	}

	OutRig.Character = Character->GetPathName();
	OutRig.PostProcessBlueprint = Character->GetPostProcessAnimBlueprint()->GetPathName();
	OutRig.PoseAsset = Best->GetPathName();
	OutRig.Fingerprint = Fingerprint(Character);
	OutRig.UpdateUndriven();
	UE_LOG(LogDTHFace, Log, TEXT("Measured the face rig on %s: %d curves, %d poses (%d not driven), %d links, %d pair terms; %d evaluations in %.0f s."),
		*Character->GetName(), NumCurves, NumPoses, OutRig.Undriven.Num(), OutRig.Linear.Num(), OutRig.Pairs.Num(), H.Evaluations,
		FPlatformTime::Seconds() - StartTime);
	return true;
}

bool FDTHFaceRigProbe::DebugEvaluate(USkeletalMesh* Character, const TMap<FName, float>& Curves, const TArray<FName>& Read, TArray<float>& OutValues, FString& OutError)
{
	static TUniquePtr<FHarness> Harness;
	static TWeakObjectPtr<USkeletalMesh> HarnessMesh;
	if (!Harness || HarnessMesh.Get() != Character)
	{
		Harness = MakeUnique<FHarness>();
		HarnessMesh = Character;
		if (!Character || !Harness->Init(Character, OutError))
		{
			Harness.Reset();
			return false;
		}
	}
	Harness->Read = Read;
	Harness->Eval(Curves, OutValues);
	return true;
}

namespace
{
	TUniquePtr<FHarness>& GeometryHarness() { static TUniquePtr<FHarness> H; return H; }
	TWeakObjectPtr<USkeletalMesh>& GeometryHarnessMesh() { static TWeakObjectPtr<USkeletalMesh> M; return M; }
}

bool FDTHFaceRigProbe::EvaluateGeometry(USkeletalMesh* Character, TConstArrayView<TMap<FName, float>> CurveSets, const TArray<int32>& Vertices,
	TArray<FVector3f>& OutPositions, FString& OutError)
{
	TUniquePtr<FHarness>& Harness = GeometryHarness();
	if (!Harness || GeometryHarnessMesh().Get() != Character)
	{
		Harness = MakeUnique<FHarness>();
		GeometryHarnessMesh() = Character;
		if (!Character || !Harness->Init(Character, OutError))
		{
			Harness.Reset();
			return false;
		}
	}
	OutPositions.Reset(CurveSets.Num() * Vertices.Num());
	TArray<FVector3f> One;
	for (const TMap<FName, float>& Set : CurveSets)
	{
		Harness->Geometry(Set, Vertices, One);
		OutPositions.Append(One);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
