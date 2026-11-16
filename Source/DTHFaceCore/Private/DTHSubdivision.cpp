#include "DTHSubdivision.h"

namespace
{
	using FRow = TMap<int32, double>;

	void AddScaled(FRow& Out, const FRow& In, double Scale)
	{
		for (const TPair<int32, double>& E : In) Out.FindOrAdd(E.Key) += E.Value * Scale;
	}
}

FDTHSubdivision::FDTHSubdivision(int32 NumVertices, TConstArrayView<FIntVector4> Facets)
{
	TMap<TPair<int32, int32>, TArray<int32, TInlineAllocator<2>>> EdgeFaces;
	TArray<TArray<int32>> VertexFaces;
	VertexFaces.SetNum(NumVertices);
	TArray<TSet<TPair<int32, int32>>> VertexEdges;
	VertexEdges.SetNum(NumVertices);
	TArray<FRow> FacePoints;
	FacePoints.SetNum(Facets.Num());
	for (int32 F = 0; F < Facets.Num(); ++F)
	{
		const FIntVector4& Q = Facets[F];
		const int32 N = Q.W < 0 ? 3 : 4;
		for (int32 K = 0; K < N; ++K)
		{
			const int32 A = Q[K], B = Q[(K + 1) % N];
			const TPair<int32, int32> Key(FMath::Min(A, B), FMath::Max(A, B));
			EdgeFaces.FindOrAdd(Key).Add(F);
			VertexFaces[A].Add(F);
			VertexEdges[A].Add(Key);
			VertexEdges[B].Add(Key);
			FacePoints[F].Add(Q[K], 1.0 / N);
		}
	}

	TArray<FRow> Rows;
	Rows.Reserve(NumVertices + EdgeFaces.Num() + Facets.Num());
	// vertex points
	for (int32 V = 0; V < NumVertices; ++V)
	{
		FRow Row;
		TArray<int32> BoundaryNeighbours;
		for (const TPair<int32, int32>& E : VertexEdges[V])
		{
			if (EdgeFaces[E].Num() == 1) BoundaryNeighbours.Add(E.Key == V ? E.Value : E.Key);
		}
		if (VertexEdges[V].IsEmpty() || (BoundaryNeighbours.Num() > 0 && BoundaryNeighbours.Num() < 2))
		{
			Row.Add(V, 1.0);
		}
		else if (BoundaryNeighbours.Num() >= 2)
		{
			Row.Add(V, 0.75);
			Row.FindOrAdd(BoundaryNeighbours[0]) += 0.125;
			Row.FindOrAdd(BoundaryNeighbours[1]) += 0.125;
		}
		else
		{
			const double M = VertexEdges[V].Num();
			FRow FaceAverage, EdgeAverage;
			for (const int32 F : VertexFaces[V]) AddScaled(FaceAverage, FacePoints[F], 1.0 / VertexFaces[V].Num());
			for (const TPair<int32, int32>& E : VertexEdges[V])
			{
				EdgeAverage.FindOrAdd(E.Key) += 0.5 / M;
				EdgeAverage.FindOrAdd(E.Value) += 0.5 / M;
			}
			AddScaled(Row, FaceAverage, 1.0 / M);
			AddScaled(Row, EdgeAverage, 2.0 / M);
			Row.FindOrAdd(V) += (M - 3.0) / M;
		}
		Rows.Add(MoveTemp(Row));
	}
	// edge points
	for (const TPair<TPair<int32, int32>, TArray<int32, TInlineAllocator<2>>>& E : EdgeFaces)
	{
		FRow Row;
		if (E.Value.Num() == 1)
		{
			Row.Add(E.Key.Key, 0.5);
			Row.FindOrAdd(E.Key.Value) += 0.5;
		}
		else
		{
			Row.Add(E.Key.Key, 0.25);
			Row.FindOrAdd(E.Key.Value) += 0.25;
			for (const int32 F : E.Value) AddScaled(Row, FacePoints[F], 0.25);
		}
		Rows.Add(MoveTemp(Row));
	}
	// face points
	for (FRow& Row : FacePoints) Rows.Add(MoveTemp(Row));

	Starts.Reserve(Rows.Num() + 1);
	Starts.Add(0);
	for (const FRow& Row : Rows)
	{
		for (const TPair<int32, double>& E : Row)
		{
			Columns.Add(E.Key);
			Weights.Add(static_cast<float>(E.Value));
		}
		Starts.Add(Columns.Num());
	}
}

void FDTHSubdivision::Apply(TConstArrayView<FVector3f> In, TArray<FVector3f>& Out) const
{
	Out.SetNumUninitialized(NumPoints());
	for (int32 P = 0; P < NumPoints(); ++P)
	{
		FVector3f S = FVector3f::ZeroVector;
		for (int32 K = Starts[P]; K < Starts[P + 1]; ++K) S += In[Columns[K]] * Weights[K];
		Out[P] = S;
	}
}
