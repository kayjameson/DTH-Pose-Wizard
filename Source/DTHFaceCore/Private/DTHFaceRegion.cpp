#include "DTHFaceRegion.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** Next whitespace-separated integer of a geometry line; false at its end. */
	bool NextInt(const TCHAR*& P, int64& Out)
	{
		while (*P == ' ' || *P == '\t' || *P == '\r') ++P;
		if (!*P || *P == '\n') return false;
		bool bNegative = false;
		if (*P == '-') { bNegative = true; ++P; }
		int64 V = 0;
		while (*P >= '0' && *P <= '9') V = V * 10 + (*P++ - '0');
		Out = bNegative ? -V : V;
		return true;
	}

}

namespace DTHFaceRegion
{
	/**
	 * Reads a geometry file ("<expression> <figure> <count> (<vertex> <dx> <dy> <dz>)*" per line) into Out[expression][figure],
	 * the expression index taken from First on.
	 */
	bool ReadGeometry(const FString& Path, const TArray<FDTHFaceFigure>& Face, double Unit, int32 First, TArray<TArray<TArray<FVector3f>>>& Out, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("Cannot read the face geometry %s."), *Path);
			return false;
		}
		const TCHAR* P = *Text;
		while (*P)
		{
			int64 Index = -1, FigureIndex = -1, Count = 0;
			if (NextInt(P, Index) && NextInt(P, FigureIndex) && NextInt(P, Count)
				&& Out.IsValidIndex(Index - First) && Face.IsValidIndex(FigureIndex))
			{
				TArray<TArray<FVector3f>>& PerFigure = Out[Index - First];
				PerFigure.SetNum(Face.Num());
				TArray<FVector3f>& D = PerFigure[FigureIndex];
				D.Init(FVector3f::ZeroVector, Face[FigureIndex].Vertices.Num());
				for (int64 K = 0; K < Count; ++K)
				{
					int64 Vi, X, Y, Z;
					if (!NextInt(P, Vi) || !NextInt(P, X) || !NextInt(P, Y) || !NextInt(P, Z)) break;
					if (D.IsValidIndex(Vi)) D[Vi] = FVector3f(X * Unit, Y * Unit, Z * Unit);
				}
			}
			while (*P && *P != '\n') ++P;
			if (*P == '\n') ++P;
		}
		return true;
	}

	/** The face region: its figures, their vertices, facets and rest positions. */
	void ReadFace(const TSharedPtr<FJsonObject>& Face, TArray<FDTHFaceFigure>& Out, double& OutUnit)
	{
		OutUnit = Face->GetNumberField(TEXT("unit"));
		for (const TSharedPtr<FJsonValue>& V : Face->GetArrayField(TEXT("figures")))
		{
			const TSharedPtr<FJsonObject>& F = V->AsObject();
			FDTHFaceFigure& Figure = Out.AddDefaulted_GetRef();
			Figure.Name = F->GetStringField(TEXT("name"));
			for (const TSharedPtr<FJsonValue>& I : F->GetArrayField(TEXT("vertices"))) Figure.Vertices.Add(static_cast<int32>(I->AsNumber()));
			for (const TSharedPtr<FJsonValue>& Q : F->GetArrayField(TEXT("facets")))
			{
				const TArray<TSharedPtr<FJsonValue>>& A = Q->AsArray();
				Figure.Facets.Add(FIntVector4(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber(), A[3]->AsNumber()));
			}
			const TArray<TSharedPtr<FJsonValue>>& R = F->GetArrayField(TEXT("rest"));
			for (int32 I = 0; I + 2 < R.Num(); I += 3) Figure.Rest.Add(FVector3f(R[I]->AsNumber(), R[I + 1]->AsNumber(), R[I + 2]->AsNumber()));
		}
	}

	bool LoadFace(const FString& File, TArray<FDTHFaceFigure>& Out, double& OutUnit, FString& OutError)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("Cannot read the face region %s."), *File);
			return false;
		}
		Out.Reset();
		ReadFace(Root, Out, OutUnit);
		return true;
	}
}
