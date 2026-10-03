#include "FootPlant.h"

// Порт web/src/ui/three/footPlant.ts (S-10, S-37). Соглашение: веб (x, z) = UE (X, −Y), курс тот же
// (вперёд веба (cos, −sin) по (x, z) = (cos, sin) по (X, Y)) — поэтому формулы с z переписаны на +Y.

namespace BoxFoot
{
	namespace
	{
		constexpr float DRAG_WINDOW = 0.14f;   // с: шаг, начатый так скоро после постановки другой ступни, — подтяг
		constexpr float MIN_STEP = 0.055f;     // м: короче — не шаг
		constexpr float SWING_KEEP = 0.6f;     // доля начальной длины переноса, короче которой слежение его не делает
		constexpr float FOLLOW_TH_STEP = 0.045f; // м: в ритме шагов «подтягивать» вторую ступню только по-крупному
		constexpr float TURN_W_MIN = 0.25f;    // рад/с
		constexpr float TURN_LEAD = 0.35f;     // с
		constexpr float TURN_MAX = 0.35f;      // рад
		constexpr float TURN_R = 0.26f;        // м — типичное плечо ступни от центра стойки
		constexpr float YAW_PIVOT_TH = 0.1f;   // рад: меньше — ступня стоит; больше — пивот на подушечке
		constexpr float YAW_PIVOT_DONE = 0.015f;
		constexpr float YAW_STEP_TH = 0.55f;   // рад: больше — ступню переставляют шагом
		constexpr float PIVOT_RATE = 9.f;
		constexpr float STRETCH_STEP = 0.985f; // нога на пределе — ступню переставляют
		constexpr float MOVING_V = 0.18f;      // м/с — корпус «в движении»
		constexpr float OVERLAP = 0.86f;       // вторая ступня может стартовать, когда первая почти поставлена
		constexpr float WALK_OVERLAP = 0.94f;
		constexpr float LEAD_MAX = 0.1f;       // м: предел упреждения точки стойки в бою
		constexpr float LIFT_OFF = 0.12f;      // доля переноса на отрыв (и столько же на постановку)
		constexpr float AHEAD_MAX = 0.6f;      // м

		float Smoothstep(float U) { return U * U * (3.f - 2.f * U); }
		float Hypot(float A, float B) { return FMath::Sqrt(A * A + B * B); }
		float Sign(float A) { return A > 0.f ? 1.f : (A < 0.f ? -1.f : 0.f); }

		float ErrOf(const FFoot& F, const FGoal& G) { return Hypot(G.X - F.X, G.Y - F.Y); }
		// Расхождение по ПОДУШЕЧКЕ (точке опоры): разворот вокруг неё — не повод шагать, это пивот.
		float BallErr(const FFoot& F, const FGoal& G, float L)
		{
			return Hypot(G.X + FMath::Cos(G.Yaw) * L - F.X - FMath::Cos(F.Yaw) * L, G.Y + FMath::Sin(G.Yaw) * L - F.Y - FMath::Sin(F.Yaw) * L);
		}

		void StartSwing(FGait& Gait, FFoot& F, const FGoal& G, bool bWalking, bool bDrag = false)
		{
			const FFootNow Now = FootNow(F);
			const float Len0 = Hypot(G.X - Now.X, G.Y - Now.Y);
			float Dur, Lift;
			SwingShape(Len0, bWalking, Dur, Lift);
			F.Sw.FX = Now.X;
			F.Sw.FY = Now.Y;
			F.Sw.FYaw = Now.Yaw;
			F.Sw.T = 0.f;
			F.Sw.Dur = Dur;
			F.Sw.Lift = bDrag ? Lift * DRAG_LIFT : Lift;
			F.Sw.bDrag = bDrag;
			F.Sw.Len0 = Len0;
			F.bSwing = true;
			F.X = G.X;
			F.Y = G.Y;
			F.Yaw = G.Yaw;
			F.Pivot = 0.f;
			++Gait.Swings;
			Gait.Drags += bDrag ? 1 : 0;
		}
	}

	float WrapAngle(float A)
	{
		return FMath::Atan2(FMath::Sin(A), FMath::Cos(A));
	}

	FV2 OverAbsorb(float X, float Y, const FAbsorb& A, float Rs)
	{
		const float OX = X > 0.f ? FMath::Max(0.f, X - A.Fwd * Rs) : FMath::Min(0.f, X + A.Back * Rs);
		const float OY = Sign(Y) * FMath::Max(0.f, FMath::Abs(Y) - A.Side * Rs);
		return FV2(OX, OY);
	}

	void SplitLunge(int32 I, float X, float Y, float Rs, FV2& OutStep, FV2& OutSlide)
	{
		const FV2 O = OverAbsorb(X, Y, I == 0 ? LEAD_ABSORB : REAR_ABSORB, Rs);
		const bool bTrailing = I == 0 ? O.X < 0.f : O.X > 0.f;
		if (!bTrailing)
		{
			OutStep = O;
			OutSlide = FV2();
			return;
		}
		const float SX = Sign(O.X) * FMath::Min(FMath::Abs(O.X), SLIDE_MAX * Rs);
		OutStep = FV2(O.X - SX, O.Y);
		OutSlide = FV2(SX, 0.f);
	}

	void HoldLunge(FLungeHold& H, const FV2& Want, bool bLock)
	{
		const float LH = Hypot(H.X, H.Y);
		const float LW = Want.Len();
		if (LW < 1e-3f && !bLock)
		{
			H.X = H.Y = H.Pk = 0.f;
			return;
		}
		const bool bFlip = LH > 1e-3f && H.X * Want.X + H.Y * Want.Y < 0.f;
		const bool bReleased = LH < 1e-3f && H.Pk > 0.f;
		if (bFlip || (bReleased ? LW > H.Pk : LW >= LH))
		{
			H.X = Want.X;
			H.Y = Want.Y;
			H.Pk = FMath::Max(bFlip ? 0.f : H.Pk, LW);
		}
		else if (!bLock && LH > 1e-3f && LW < H.Pk * LUNGE_RELEASE)
		{
			H.X = H.Y = 0.f;
		}
	}

	bool InStepRhythm(const FStepTrack& Tr)
	{
		return Tr.bOn || Tr.Since < STEP_RHYTHM_GAP;
	}

	FV2 TrackStep(FStepTrack& Tr, int32 Step, float DX, float DY, float Dt)
	{
		const int32 K = FMath::Clamp(Step, 0, 7);
		if (Dt > 0.f)
		{
			if (K != 0)
			{
				if (!Tr.bOn)
				{
					Tr.bOn = true;
					Tr.T = Dt;
					Tr.VX = Tr.VY = Tr.W = 0.f;
					Tr.Kind = K;
					Tr.Dur = Tr.Durs[K] > 0.f ? Tr.Durs[K] : (Tr.AnyDur > 0.f ? Tr.AnyDur : STEP_DUR_GUESS);
				}
				else
				{
					Tr.T += Dt;
				}
				const float VX = DX / Dt;
				const float VY = DY / Dt;
				if (Hypot(VX, VY) > 0.05f)
				{
					if (Hypot(Tr.VX, Tr.VY) > 0.05f)
					{
						const float Turn = WrapAngle(FMath::Atan2(VY, VX) - FMath::Atan2(Tr.VY, Tr.VX)) / Dt;
						Tr.W += (FMath::Clamp(Turn, -6.f, 6.f) - Tr.W) * 0.5f;
					}
					Tr.VX = VX;
					Tr.VY = VY;
				}
			}
			else if (Tr.bOn)
			{
				Tr.bOn = false;
				Tr.Since = 0.f;
				const float D = FMath::Clamp(Tr.T, 0.08f, 0.8f);
				float& Kd = Tr.Durs[Tr.Kind];
				Kd = Kd > 0.f ? Kd + (D - Kd) * 0.5f : D;
				Tr.AnyDur = Tr.AnyDur > 0.f ? Tr.AnyDur + (D - Tr.AnyDur) * 0.5f : D;
				Tr.Dur = Kd;
			}
			else
			{
				Tr.Since += Dt;
			}
		}
		if (!Tr.bOn)
		{
			return FV2();
		}
		const float Rem = FMath::Max(0.f, Tr.Dur - Tr.T);
		const float V = Hypot(Tr.VX, Tr.VY);
		float X = Tr.VX * Rem;
		float Y = Tr.VY * Rem;
		const float A = Tr.W * Rem;
		if (FMath::Abs(A) > 1e-3f && V > 1e-3f)
		{
			// по дуге: хорда поворачивающегося вектора скорости
			const float H = FMath::Atan2(Tr.VY, Tr.VX);
			const float R = V / Tr.W;
			X = R * (FMath::Sin(H + A) - FMath::Sin(H));
			Y = R * (FMath::Cos(H) - FMath::Cos(H + A));
		}
		const float L = Hypot(X, Y);
		if (L > AHEAD_MAX)
		{
			X *= AHEAD_MAX / L;
			Y *= AHEAD_MAX / L;
		}
		return FV2(X, Y);
	}

	void SwingShape(float Dist, bool bWalking, float& OutDur, float& OutLift)
	{
		if (bWalking)
		{
			OutDur = FMath::Min(0.38f, FMath::Max(0.26f, 0.22f + Dist * 0.12f));
			OutLift = FMath::Min(0.07f, 0.03f + Dist * 0.04f);
			return;
		}
		OutDur = FMath::Min(0.22f, FMath::Max(0.11f, 0.085f + Dist * 0.36f));
		OutLift = FMath::Min(0.05f, 0.016f + Dist * 0.12f);
	}

	FFootNow FootNow(const FFoot& F)
	{
		FFootNow N;
		if (!F.bSwing)
		{
			N.X = F.X;
			N.Y = F.Y;
			N.Yaw = F.Yaw;
			return N;
		}
		const FSwing& S = F.Sw;
		const float U = FMath::Min(1.f, S.Dur > 0.f ? S.T / S.Dur : 1.f);
		// Ступню сперва отрывают, потом несут и ставят: по горизонтали она идёт лишь в середине переноса.
		const float E = Smoothstep(FMath::Clamp((U - LIFT_OFF) / (1.f - 2.f * LIFT_OFF), 0.f, 1.f));
		N.X = S.FX + (F.X - S.FX) * E;
		N.Y = S.FY + (F.Y - S.FY) * E;
		N.Lift = S.Lift * FMath::Sin(PI * U);
		N.Yaw = S.FYaw + WrapAngle(F.Yaw - S.FYaw) * E;
		N.U = U;
		N.bDrag = S.bDrag;
		return N;
	}

	int32 LeaderOf(const FGoal Goals[2], float VX, float VY, const float Errs[2])
	{
		const float V = Hypot(VX, VY);
		if (V < MOVING_V)
		{
			return Errs[0] >= Errs[1] ? 0 : 1;
		}
		const float CX = (Goals[0].X + Goals[1].X) * 0.5f;
		const float CY = (Goals[0].Y + Goals[1].Y) * 0.5f;
		auto Along = [&](int32 I) { return ((Goals[I].X - CX) * VX + (Goals[I].Y - CY) * VY) / V; };
		return Along(0) >= Along(1) ? 0 : 1;
	}

	void UpdateGait(FGait& G, const FGoal Raw[2], const FGaitOpts& O)
	{
		FFoot& A = G.Feet[0];
		FFoot& B = G.Feet[1];
		const bool bFar = FMath::Max(ErrOf(A, Raw[0]), ErrOf(B, Raw[1])) > SNAP_DIST;
		const bool bSnap = !G.bReady || O.Dt > 0.5f || bFar;
		// Упреждение точек стойки по их собственной скорости.
		const float K = (bSnap || !G.bPrev) ? 0.f : 1.f - FMath::Exp(-14.f * O.Dt);
		for (int32 I = 0; I < 2; ++I)
		{
			FV2& V = G.GV[I];
			if (K > 0.f && G.bPrev && O.Dt > 0.f)
			{
				V.X += ((Raw[I].X - G.Prev[I].X) / O.Dt - V.X) * K;
				V.Y += ((Raw[I].Y - G.Prev[I].Y) / O.Dt - V.Y) * K;
			}
			else if (bSnap)
			{
				V = FV2();
			}
		}
		if (K > 0.f && G.bPrev && O.Dt > 0.f)
		{
			G.WY += (WrapAngle(Raw[0].Yaw - G.Prev[0].Yaw) / O.Dt - G.WY) * K;
		}
		else if (bSnap)
		{
			G.WY = 0.f;
		}
		if (O.Dt > 0.f || bSnap)
		{
			G.Prev[0] = Raw[0];
			G.Prev[1] = Raw[1];
			G.bPrev = true;
		}
		// Упреждение не дальше LEAD_MAX (резкий выпад иначе ставил ступню с перелётом).
		FGoal Goals[2];
		for (int32 I = 0; I < 2; ++I)
		{
			float LX = G.GV[I].X * O.Lead;
			float LY = G.GV[I].Y * O.Lead;
			const float LL = Hypot(LX, LY);
			if (!O.bWalking && LL > LEAD_MAX)
			{
				LX *= LEAD_MAX / LL;
				LY *= LEAD_MAX / LL;
			}
			const FV2 Ah = O.bAhead ? O.Ahead : FV2();
			Goals[I].X = Raw[I].X + LX + Ah.X + O.Lunge[I].X + G.Slid[I].X;
			Goals[I].Y = Raw[I].Y + LY + Ah.Y + O.Lunge[I].Y + G.Slid[I].Y;
			Goals[I].Yaw = Raw[I].Yaw;
		}
		// Разворот на месте: стойка повёрнута вперёд по ходу разворота вокруг ПОДУШЕЧКИ передней ступни.
		float TurnTh = 0.f;
		if (!bSnap && !O.bWalking && !O.bAhead && Hypot(O.VX, O.VY) < MOVING_V && FMath::Abs(G.WY) > TURN_W_MIN)
		{
			const float D = FMath::Clamp(G.WY * TURN_LEAD, -TURN_MAX, TURN_MAX);
			const float CX = Goals[0].X + FMath::Cos(Goals[0].Yaw) * O.ToeLen;
			const float CY = Goals[0].Y + FMath::Sin(Goals[0].Yaw) * O.ToeLen;
			const float C = FMath::Cos(D);
			const float S = FMath::Sin(D);
			for (FGoal& Q : Goals)
			{
				const float DX = Q.X - CX;
				const float DY = Q.Y - CY;
				Q.X = CX + DX * C - DY * S;
				Q.Y = CY + DX * S + DY * C;
				Q.Yaw += D;
			}
			TurnTh = FMath::Abs(D) * TURN_R;
		}
		if (bSnap)
		{
			// Первый кадр / телепорт: встать в стойку без шагов.
			for (int32 I = 0; I < 2; ++I)
			{
				FFoot& F = G.Feet[I];
				F.X = Goals[I].X;
				F.Y = Goals[I].Y;
				F.Yaw = Goals[I].Yaw;
				F.bSwing = false;
				F.Pivot = 0.f;
				G.Slid[I] = FV2();
			}
			G.bReady = true;
			G.Follow = -1;
			return;
		}
		if (O.Dt <= 0.f)
		{
			return;
		}
		// Скольжение стоящей ступни (подтяг волоком на выпаде и обратно): ровно на приращение скольжения.
		for (int32 I = 0; I < 2; ++I)
		{
			FV2& Sl = G.Slid[I];
			float DX = O.Slide[I].X - Sl.X;
			float DY = O.Slide[I].Y - Sl.Y;
			FFoot& F = G.Feet[I];
			if (!F.bSwing)
			{
				const float L = Hypot(DX, DY);
				const float Mx = SLIDE_V * O.Dt;
				if (L > Mx)
				{
					DX *= Mx / L;
					DY *= Mx / L;
				}
				F.X += DX;
				F.Y += DY;
			}
			Sl.X += DX;
			Sl.Y += DY;
			Goals[I].X += DX;
			Goals[I].Y += DY;
		}
		G.SinceLand += O.Dt;
		const float Fth = FMath::Max(O.FollowTh >= 0.f ? O.FollowTh : FOLLOW_TH, O.bAhead ? FOLLOW_TH_STEP : 0.f);
		// 1) Перенос: идёт к точке стойки, которая едет вместе с корпусом (к концу — меньше).
		for (int32 I = 0; I < 2; ++I)
		{
			FFoot& F = G.Feet[I];
			if (!F.bSwing)
			{
				continue;
			}
			FSwing& S = F.Sw;
			S.T += O.Dt;
			const float U = FMath::Min(1.f, S.T / S.Dur);
			// К касанию слежение гаснет: ступня у настила не «доезжает» за корпусом.
			const float Land = Smoothstep(FMath::Clamp((U - 0.6f) / (1.f - LIFT_OFF - 0.6f), 0.f, 1.f));
			const float Kf = (1.f - U * 0.6f) * (1.f - Land);
			const float PX = F.X - S.FX;
			const float PY = F.Y - S.FY;
			F.X += (Goals[I].X - F.X) * Kf;
			F.Y += (Goals[I].Y - F.Y) * Kf;
			F.Yaw += WrapAngle(Goals[I].Yaw - F.Yaw) * Kf;
			// Точка стойки вернулась к началу переноса: перенос не укорачивается до «шажка» туда-обратно.
			const float L0 = S.Len0;
			float LX = F.X - S.FX;
			float LY = F.Y - S.FY;
			float LL = Hypot(LX, LY);
			if (L0 > MIN_STEP && LL < L0 * SWING_KEEP)
			{
				if (LL < 1e-4f)
				{
					LX = PX;
					LY = PY;
					LL = Hypot(PX, PY);
				}
				if (LL > 1e-4f)
				{
					const float M = (L0 * SWING_KEEP) / LL;
					F.X = S.FX + LX * M;
					F.Y = S.FY + LY * M;
				}
			}
			if (S.T >= S.Dur)
			{
				F.bSwing = false;
				G.SinceLand = 0.f;
				const int32 Other = 1 - I;
				if (!G.Feet[Other].bSwing && BallErr(G.Feet[Other], Goals[Other], O.ToeLen) > Fth)
				{
					G.Follow = Other;
				}
			}
		}
		// Направление хода: в шаге движка — куда он ещё повезёт корпус, иначе — скорость корпуса.
		const float AL = O.bAhead ? O.Ahead.Len() : 0.f;
		const float HX = AL > 0.02f ? O.Ahead.X / AL : O.VX;
		const float HY = AL > 0.02f ? O.Ahead.Y / AL : O.VY;
		const bool bMoving = Hypot(HX, HY) >= MOVING_V;
		// 2) Пивот на подушечке (стоящая ступня, малый разворот): носок на месте, курс — к стойке.
		for (int32 I = 0; I < 2; ++I)
		{
			FFoot& F = G.Feet[I];
			if (F.bSwing)
			{
				continue;
			}
			const float DY = WrapAngle(Goals[I].Yaw - F.Yaw);
			const float Th = F.Pivot > 0.3f ? YAW_PIVOT_DONE : YAW_PIVOT_TH;
			if (FMath::Abs(DY) > Th && FMath::Abs(DY) < YAW_STEP_TH)
			{
				const float Turn = DY * (1.f - FMath::Exp(-PIVOT_RATE * O.Dt));
				const float TX = F.X + FMath::Cos(F.Yaw) * O.ToeLen;
				const float TY = F.Y + FMath::Sin(F.Yaw) * O.ToeLen;
				F.Yaw += Turn;
				F.X = TX - FMath::Cos(F.Yaw) * O.ToeLen;
				F.Y = TY - FMath::Sin(F.Yaw) * O.ToeLen;
				F.Pivot = FMath::Min(1.f, F.Pivot + O.Dt * 8.f);
			}
			else
			{
				F.Pivot = FMath::Max(0.f, F.Pivot - O.Dt * 5.f);
			}
		}
		// 3) Новый шаг.
		const float Errs[2] = {BallErr(A, Goals[0], O.ToeLen), BallErr(B, Goals[1], O.ToeLen)};
		const float Th = O.bAhead ? STEP_TH_STEP : (bMoving ? STEP_TH_MOVING : STEP_TH + TurnTh);
		auto Needs = [&](int32 I)
		{
			return (Errs[I] > MIN_STEP && (Errs[I] > Th || (G.Follow == I && Errs[I] > (TurnTh > 0.f ? Th : Fth)))) ||
				FMath::Abs(WrapAngle(Goals[I].Yaw - G.Feet[I].Yaw)) >= YAW_STEP_TH || O.Stretch[I] > STRETCH_STEP;
		};
		auto Busy = [&](int32 I) { const FFoot& F = G.Feet[I]; return F.bSwing ? F.Sw.T / F.Sw.Dur : -1.f; };
		auto Free = [&](int32 I)
		{
			if (G.Feet[I].bSwing)
			{
				return false;
			}
			const float Other = Busy(1 - I);
			return Other < 0.f || Other >= OVERLAP;
		};
		if (O.bWalking)
		{
			// Ходьба: строго попеременно, следующая — как только предыдущая почти встала.
			const int32 Next = G.Last == 0 ? 1 : (G.Last == 1 ? 0 : LeaderOf(Goals, O.VX, O.VY, Errs));
			const float PrevB = Busy(1 - Next);
			if (!G.Feet[Next].bSwing && (PrevB < 0.f || PrevB >= WALK_OVERLAP) && (Errs[Next] > FOLLOW_TH || Errs[1 - Next] > Th))
			{
				++G.Why[5];
				StartSwing(G, G.Feet[Next], Goals[Next], true);
				G.Last = Next;
				G.Follow = -1;
			}
			return;
		}
		int32 Pick = -1;
		const int32 Fol = G.Follow;
		// Нога на пределе — её ступня идёт первой, даже вне очереди «подтягивания».
		auto Over = [&](int32 I) { return O.Stretch[I] > STRETCH_STEP && Errs[I] > FOLLOW_TH; };
		const bool O0 = Over(0) && Free(0);
		const bool O1 = Over(1) && Free(1);
		if (O0 || O1)
		{
			Pick = (O0 && O1) ? (O.Stretch[0] >= O.Stretch[1] ? 0 : 1) : (O0 ? 0 : 1);
		}
		else if (Fol != -1 && Needs(Fol) && Free(Fol))
		{
			Pick = Fol;
		}
		else
		{
			const bool N0 = Needs(0) && Free(0);
			const bool N1 = Needs(1) && Free(1);
			if (N0 && N1)
			{
				Pick = LeaderOf(Goals, HX, HY, Errs);
			}
			else if (N0)
			{
				Pick = 0;
			}
			else if (N1)
			{
				Pick = 1;
			}
			// Порядок важнее порога: в движении первой идёт ступня по ходу.
			if (Pick >= 0 && bMoving && G.Follow < 0)
			{
				const int32 Lead = LeaderOf(Goals, HX, HY, Errs);
				if (Lead != Pick && Free(Lead) && Errs[Lead] > Th * 0.5f)
				{
					Pick = Lead;
				}
			}
		}
		if (Pick == -1)
		{
			return;
		}
		// Шаг-приставка: первая ступня «толкается», вторая сразу за ней подтягивается — волоком.
		const FFoot& Other = G.Feet[1 - Pick];
		const bool bDrag = G.Pair == 1 - Pick && (Other.bSwing || G.SinceLand < DRAG_WINDOW);
		G.Pair = bDrag ? -1 : Pick;
		const int32 Why = O.Stretch[Pick] > STRETCH_STEP ? 0 : (Pick == Fol ? 1 : (FMath::Abs(WrapAngle(Goals[Pick].Yaw - G.Feet[Pick].Yaw)) >= YAW_STEP_TH ? 3 : (O.bAhead ? 4 : 2)));
		++G.Why[Why];
		StartSwing(G, G.Feet[Pick], Goals[Pick], false, bDrag);
		G.Follow = Pick == G.Follow ? -1 : G.Follow;
		G.Last = Pick;
	}
}
