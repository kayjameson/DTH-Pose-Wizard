#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/** A figure's part of the face the fit reproduces: its vertices (indices into the figure's base mesh), the facets made of
 * them (indices into Vertices; the 4th is -1 for a triangle) and their rest positions (Daz space, cm). */
struct FDTHFaceFigure
{
	FString Name;
	TArray<int32> Vertices;
	TArray<FIntVector4> Facets;
	TArray<FVector3f> Rest;
};

/**
 * The face region the Daz samplers write (face.json: the face figures with their region vertices, facets and rest
 * positions) and their face geometry files (one line per sampled state: "<state> <figure> <count> (<vertex> <dx> <dy>
 * <dz>)*", integers in the region's unit). Shared by the Expression, Pose and Animation Wizards' samplers.
 */
namespace DTHFaceRegion
{
	/** Reads a face region object ({"unit", "figures": [...]}) into Out. */
	DTHFACECORE_API void ReadFace(const TSharedPtr<FJsonObject>& Face, TArray<FDTHFaceFigure>& Out, double& OutUnit);

	/** Reads face.json. */
	DTHFACECORE_API bool LoadFace(const FString& File, TArray<FDTHFaceFigure>& Out, double& OutUnit, FString& OutError);

	/**
	 * Reads a geometry file into Out[state - First][figure][region vertex] (Daz space, cm). States outside Out are skipped;
	 * a state's figures missing from the file stay empty.
	 */
	DTHFACECORE_API bool ReadGeometry(const FString& Path, const TArray<FDTHFaceFigure>& Face, double Unit, int32 First,
		TArray<TArray<TArray<FVector3f>>>& Out, FString& OutError);
}
