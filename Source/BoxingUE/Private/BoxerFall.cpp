#include "BoxerFall.h"

namespace BoxerFall
{
	FVector2D ToWorld(const FVector2D& Local, const FVector2D& Pos, float YawDeg)
	{
		const float R = FMath::DegreesToRadians(YawDeg);
		const float C = FMath::Cos(R), S = FMath::Sin(R);
		// Вперёд = (C, S), вправо = (−S, C) — оси UE (X вперёд, Y вправо).
		return FVector2D(Pos.X + Local.X * C - Local.Y * S, Pos.Y + Local.X * S + Local.Y * C);
	}

	float SegDist(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double L2 = AB.SizeSquared();
		const double T = L2 > 1e-4 ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L2, 0.0, 1.0) : 0.0;
		return static_cast<float>(FVector2D::Distance(P, A + AB * T));
	}

	float Overhang(const FLayout& L, const FVector2D& Pos, float YawDeg, float Limit)
	{
		float Out = 0.f;
		for (const FVector2D& P : L.Pts)
		{
			const FVector2D W = ToWorld(P, Pos, YawDeg);
			Out = FMath::Max3(Out, static_cast<float>(FMath::Abs(W.X)) - Limit, static_cast<float>(FMath::Abs(W.Y)) - Limit);
		}
		return FMath::Max(0.f, Out);
	}

	float Blend(float T, float BlendS)
	{
		const float U = BlendS > 0.f ? FMath::Clamp(T / BlendS, 0.f, 1.f) : 1.f;
		return U * U * (3.f - 2.f * U);
	}

	FPlaceOut Solve(const FPlaceIn& In)
	{
		FPlaceOut Best;
		if (!In.Layout || !In.Layout->bValid || In.Layout->Pts.Num() == 0)
		{
			return Best;
		}
		const FLayout& L = *In.Layout;
		Best.Cost = TNumericLimits<float>::Max();
		Best.bInside = false;
		// Перебор от «как есть» в обе стороны: при равной цене выигрывает меньший доворот.
		for (int32 K = 0; K <= static_cast<int32>(MAX_TURN_DEG / 5.f) * 2; ++K)
		{
			const int32 Step = (K + 1) / 2;
			const float Turn = (K % 2 == 1 ? 1.f : -1.f) * 5.f * Step;
			const float Yaw = In.YawDeg + Turn;
			float MinX = TNumericLimits<float>::Max(), MaxX = -MinX, MinY = MinX, MaxY = -MinX;
			for (const FVector2D& P : L.Pts)
			{
				const FVector2D W = ToWorld(P, In.Pos, Yaw);
				MinX = FMath::Min(MinX, static_cast<float>(W.X));
				MaxX = FMath::Max(MaxX, static_cast<float>(W.X));
				MinY = FMath::Min(MinY, static_cast<float>(W.Y));
				MaxY = FMath::Max(MaxY, static_cast<float>(W.Y));
			}
			// Минимальный сдвиг по каждой оси, чтобы всё легло в [−Limit, Limit] (тело шире ринга — не бывает).
			auto Shift = [&In](float Lo, float Hi, bool& bOk)
			{
				if (Hi - Lo > 2.f * In.Limit)
				{
					bOk = false;
					return 0.f;
				}
				if (Hi > In.Limit) return In.Limit - Hi;
				if (Lo < -In.Limit) return -In.Limit - Lo;
				return 0.f;
			};
			bool bOk = true;
			const FVector2D Off(Shift(MinX, MaxX, bOk), Shift(MinY, MaxY, bOk));
			if (!bOk)
			{
				continue;
			}
			float Cost = FMath::Abs(Turn) * COST_PER_DEG + static_cast<float>(Off.Size()) * COST_PER_CM;
			const FVector2D Pos = In.Pos + Off;
			for (const FAvoid& A : In.Avoid)
			{
				float Deep = 0.f;
				for (const FVector2D& P : L.Pts)
				{
					Deep = FMath::Max(Deep, A.R - SegDist(ToWorld(P, Pos, Yaw), A.A, A.B));
				}
				Cost += Deep * AVOID_COST;
			}
			if (Cost < Best.Cost - 1e-4f)
			{
				Best.Cost = Cost;
				Best.TurnDeg = Turn;
				Best.Offset = Off;
				Best.bInside = true;
			}
		}
		if (!Best.bInside)
		{
			Best = FPlaceOut();
			Best.bInside = Overhang(L, In.Pos, In.YawDeg, In.Limit) <= 0.f;
		}
		return Best;
	}
}
