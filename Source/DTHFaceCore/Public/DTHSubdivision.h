#pragma once

#include "CoreMinimal.h"

/**
 * One level of Catmull-Clark subdivision as a fixed linear operator, so any number of shapes on the same mesh subdivide
 * by one sparse product. The DTH export subdivides the Daz mesh once; this gives the same points from Daz's base mesh,
 * so a Daz face can be laid onto the exported character point for point. Boundaries are smooth along their edges (Daz's
 * "edges only" boundary interpolation).
 */
class DTHFACECORE_API FDTHSubdivision
{
public:
	/** Facets index 0..NumVertices-1; a facet's 4th index is negative for a triangle. */
	FDTHSubdivision(int32 NumVertices, TConstArrayView<FIntVector4> Facets);

	int32 NumPoints() const { return Starts.Num() - 1; }

	/** Out[point] = sum of weight * In[vertex] over the point's row. */
	void Apply(TConstArrayView<FVector3f> In, TArray<FVector3f>& Out) const;

private:
	TArray<int32> Starts;
	TArray<int32> Columns;
	TArray<float> Weights;
};
