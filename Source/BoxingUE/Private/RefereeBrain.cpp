// Порт web/src/ui/three/refereeBrain.ts (S-58) — числа и порядок вычислений те же, что в вебе.
#include "RefereeBrain.h"

#include "FightStaging.h"
#include "FightTypes.h"

namespace BoxRef
{
	namespace
	{
		constexpr double SIDE_PREF = 1.5;
		constexpr double GAP_R = 0.55;
		constexpr double GOAL_SLACK = 0.3;
		constexpr double OUT_CENTER_GAP = 4.2;
		constexpr double STAND_CLEAR = 1.0;
		constexpr double DOWN_CLEAR = 0.9;
		constexpr double BODY_CLEAR = 0.95;
		constexpr double LOOK_T = 0.7;
		constexpr double LOOK_MIN_SPEED = 0.5;
		constexpr double FVEL_RATE = 12;
		constexpr double FVEL_MAX = 6;
		constexpr double ACCEL = 5.5;
		constexpr double ARRIVE_K = 2.4;
		constexpr double STRIDE = 0.62;
		constexpr double YAW_RATE = 6;
		constexpr double ARM_RATE = 9;
		constexpr double COUNT_RATE = 22;
		constexpr double DEFAULT_BEAT = 0.75;
		constexpr double POINT_MAX = 2.2;
		constexpr double ANNOUNCE_ARRIVE = 0.22;
		constexpr double ANNOUNCE_STILL = 0.35;
		constexpr double ANNOUNCE_MAX = 3.2;
		constexpr double GRAB_T = 0.55;
		constexpr double SHOULDER_LAT = 0.19;
		constexpr double SHOULDER_Y = 1.42;
		constexpr double WRIST_HIGH = 2.05;
		constexpr double WRIST_LOW = 1.0;
		constexpr double WRIST_NEAR = 0.12;
		constexpr double ANNOUNCE_GAP = 0.66;
		constexpr double DETOUR_PAIR_MAX = 2.4;
		// UE: сглаживание доли жеста в руке (1/с).
		constexpr double ARMW_RATE = 8;

		double Hyp2(double X, double Z) { return FMath::Sqrt(X * X + Z * Z); }
		double Or1(double V) { return V != 0 ? V : 1; }
		double ClampRing(double V) { return FMath::Max(-REF_RING, FMath::Min(REF_RING, V)); }
		double SmoothK(double Rate, double Dt) { return 1 - FMath::Exp(-Rate * Dt); }
		double Sign(double V) { return V > 0 ? 1 : (V < 0 ? -1 : 0); }
		double WrapAngle(double A) { return FMath::Atan2(FMath::Sin(A), FMath::Cos(A)); }

		FV3 Norm(const FV3& V)
		{
			const double L = Or1(FMath::Sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z));
			return FV3(V.X / L, V.Y / L, V.Z / L);
		}
		FV3 MixV(const FV3& A, const FV3& B, double K)
		{
			return Norm(FV3(A.X + (B.X - A.X) * K, A.Y + (B.Y - A.Y) * K, A.Z + (B.Z - A.Z) * K));
		}
		FArm MkArm(double S, const FV3& U, const FV3& F)
		{
			FArm A;
			A.Upper = Norm(FV3(U.X * S, U.Y, U.Z));
			A.Fore = Norm(FV3(F.X * S, F.Y, F.Z));
			return A;
		}
		FArm ArmCountUp(double S) { return MkArm(S, FV3(0.25, 0.55, 0.8), FV3(0.12, 0.88, 0.45)); }
		FArm ArmCountDown(double S) { return MkArm(S, FV3(0.18, -0.3, 0.93), FV3(0.1, -0.72, 0.68)); }
		FArm ArmKnee(double S) { return MkArm(S, FV3(0.15, -0.88, 0.45), FV3(0.05, -0.55, 0.83)); }
		FArm ArmCross(double S) { return MkArm(S, FV3(-0.32, 0.12, 0.94), FV3(-0.45, 0.1, 0.89)); }
		FArm ArmSpread(double S) { return MkArm(S, FV3(0.95, 0.1, 0.3), FV3(0.97, 0.05, 0.22)); }
		FArm MixArm(const FArm& A, const FArm& B, double K)
		{
			FArm R;
			R.Upper = MixV(A.Upper, B.Upper, K);
			R.Fore = MixV(A.Fore, B.Fore, K);
			return R;
		}

		double YawTo(const FV& From, const FV& To) { return FMath::Atan2(-(To.Z - From.Z), To.X - From.X); }
	}

	double Hyp(const FV& A, const FV& B) { return Hyp2(A.X - B.X, A.Z - B.Z); }

	FV ClosestOnSeg(const FV& A, const FV& B, const FV& P)
	{
		const double Vx = B.X - A.X;
		const double Vz = B.Z - A.Z;
		const double L2 = Or1(Vx * Vx + Vz * Vz);
		const double U = FMath::Max(0.0, FMath::Min(1.0, ((P.X - A.X) * Vx + (P.Z - A.Z) * Vz) / L2));
		return FV(A.X + Vx * U, A.Z + Vz * U);
	}

	FV FarPerp(const FV& F0, const FV& F1, const FV& Cam)
	{
		const double Ux = F1.X - F0.X;
		const double Uz = F1.Z - F0.Z;
		const double L = Or1(Hyp2(Ux, Uz));
		double Px = -Uz / L;
		double Pz = Ux / L;
		const FV M((F0.X + F1.X) / 2, (F0.Z + F1.Z) / 2);
		if (Px * (Cam.X - M.X) + Pz * (Cam.Z - M.Z) > 0)
		{
			Px = -Px;
			Pz = -Pz;
		}
		return FV(Px, Pz);
	}

	bool BlocksView(const FV& P, const FV& Cam, const FV& F)
	{
		const double Dp = Hyp(P, Cam);
		const double Df = Hyp(F, Cam);
		if (Dp > Df - 0.25)
		{
			return false;
		}
		const double Ap = FMath::Atan2(P.Z - Cam.Z, P.X - Cam.X);
		const double Af = FMath::Atan2(F.Z - Cam.Z, F.X - Cam.X);
		const double Da = FMath::Abs(WrapAngle(Ap - Af));
		return Da < FMath::Atan(0.4 / Df) + FMath::Atan(0.3 / FMath::Max(0.3, Dp));
	}

	double SideCost(const FV& P, const FV& F0, const FV& F1, const FV& Cam, const FV* Current)
	{
		const FV M((F0.X + F1.X) / 2, (F0.Z + F1.Z) / 2);
		double C = 0;
		const double Sd = Hyp(P, ClosestOnSeg(F0, F1, P));
		if (Sd < SIDE_MIN) C += (SIDE_MIN - Sd) * 8;
		if (Sd > SIDE_MAX) C += (Sd - SIDE_MAX) * 4;
		C += FMath::Abs(Sd - SIDE_PREF) * 0.6;
		const double Near = FMath::Min(Hyp(P, F0), Hyp(P, F1));
		if (Near < FIGHTER_CLEAR) C += (FIGHTER_CLEAR - Near) * 10 + 3;
		// сторона: дальняя от камеры — 0, ближняя — 2
		const double Cl = Or1(Hyp(Cam, M));
		const double Pl = Or1(Hyp(P, M));
		const double SideK = ((P.X - M.X) * (Cam.X - M.X) + (P.Z - M.Z) * (Cam.Z - M.Z)) / (Cl * Pl);
		C += (SideK + 1) * 1.1;
		if (BlocksView(P, Cam, F0) || BlocksView(P, Cam, F1)) C += 6;
		// Просвет пары: за ними на линии взгляда камеры — третья фигура «между» бойцами.
		const double Ag = FMath::Atan2(M.Z - Cam.Z, M.X - Cam.X);
		const double Ap = FMath::Atan2(P.Z - Cam.Z, P.X - Cam.X);
		const double Half = FMath::Max(0.05, FMath::Atan((Hyp(F0, F1) / 2) / Cl));
		const double Off = FMath::Abs(WrapAngle(Ap - Ag));
		if (Off < Half * 0.7) C += (1 - Off / (Half * 0.7)) * 0.8;
		if (Current) C += Hyp(P, *Current) * 0.35;
		return C;
	}

	FV SidePlacement(const FV& F0, const FV& F1, const FV& Cam, const FV* Current)
	{
		const FV M((F0.X + F1.X) / 2, (F0.Z + F1.Z) / 2);
		FV Best = M;
		double BestC = TNumericLimits<double>::Max();
		auto Consider = [&](const FV& P)
		{
			const double C = SideCost(P, F0, F1, Cam, Current);
			if (C < BestC)
			{
				BestC = C;
				Best = P;
			}
		};
		static const double Ds[3] = {1.35, 1.6, 1.85};
		for (int32 A = 0; A < 24; ++A)
		{
			const double Ang = (A / 24.0) * UE_DOUBLE_PI * 2;
			for (const double D : Ds)
			{
				Consider(FV(ClampRing(M.X + FMath::Cos(Ang) * D), ClampRing(M.Z + FMath::Sin(Ang) * D)));
			}
		}
		// Нынешнее место — тоже кандидат (стоять лучше, чем переходить ради сотых).
		if (Current)
		{
			Consider(FV(ClampRing(Current->X), ClampRing(Current->Z)));
		}
		return Best;
	}

	void MoveToward(FV& Pos, FV& Vel, const FV& Target, const TArray<FObstacle>& Obs, double Dt, int32& Side)
	{
		if (Dt <= 0)
		{
			return;
		}
		double Dx = Target.X - Pos.X;
		double Dz = Target.Z - Pos.Z;
		const double Dist = Hyp2(Dx, Dz);
		double Speed = FMath::Min(MAX_SPEED, Dist * ARRIVE_K);
		if (Dist < 0.02) Speed = 0;
		if (Dist > 1e-6)
		{
			Dx /= Dist;
			Dz /= Dist;
		}
		for (const FObstacle& O : Obs)
		{
			const double Ox = O.P.X - Pos.X;
			const double Oz = O.P.Z - Pos.Z;
			const double Od = Hyp2(Ox, Oz);
			const double Zone = O.R + 0.45;
			if (Od > Zone || Od < 1e-6) continue;
			const double Nx = Ox / Od;
			const double Nz = Oz / Od;
			const double Dot = Dx * Nx + Dz * Nz;
			if (Dot <= 0) continue;
			// Цель за препятствием — идём по касательной (сторону выбирает цель, потом держим).
			const double W = FMath::Min(1.0, (Zone - Od) / 0.45);
			const double Tx = -Nz;
			const double Tz = Nx;
			const double Want = Dx * Tx + Dz * Tz;
			if (FMath::Abs(Want) > 0.15) Side = Want > 0 ? 1 : -1;
			Dx -= Nx * Dot * W;
			Dz -= Nz * Dot * W;
			const double L = Hyp2(Dx, Dz);
			if (L < 0.3)
			{
				Dx = Tx * Side;
				Dz = Tz * Side;
			}
			else
			{
				Dx /= L;
				Dz /= L;
			}
		}
		const double Wx = Dx * Speed - Vel.X;
		const double Wz = Dz * Speed - Vel.Z;
		const double Wl = Hyp2(Wx, Wz);
		const double Acc = FMath::Min(Wl, ACCEL * Dt);
		if (Wl > 1e-9)
		{
			Vel.X += (Wx / Wl) * Acc;
			Vel.Z += (Wz / Wl) * Acc;
		}
		const double X0 = Pos.X;
		const double Z0 = Pos.Z;
		Pos.X += Vel.X * Dt;
		Pos.Z += Vel.Z * Dt;
		// Сквозь бойцов не проходит: оказался внутри — выводить к границе, гасить ход «внутрь».
		for (const FObstacle& O : Obs)
		{
			if (O.bSoft) continue;
			const double Ox = Pos.X - O.P.X;
			const double Oz = Pos.Z - O.P.Z;
			const double Od = Hyp2(Ox, Oz);
			if (Od >= O.R) continue;
			double Nx = 1;
			double Nz = 0;
			if (Od > 1e-6)
			{
				Nx = Ox / Od;
				Nz = Oz / Od;
			}
			else
			{
				const double Gx = Target.X - O.P.X;
				const double Gz = Target.Z - O.P.Z;
				const double Gl = Hyp2(Gx, Gz);
				if (Gl > 1e-6)
				{
					Nx = Gx / Gl;
					Nz = Gz / Gl;
				}
			}
			const double Push = FMath::Min(O.R - Od, MAX_SPEED * Dt);
			Pos.X += Nx * Push;
			Pos.Z += Nz * Push;
			const double Vin = Vel.X * Nx + Vel.Z * Nz;
			if (Vin < 0)
			{
				Vel.X -= Nx * Vin;
				Vel.Z -= Nz * Vin;
			}
		}
		// Ход за кадр — не быстрее MAX_SPEED (ход + выталкивание вместе).
		const double Mx = Pos.X - X0;
		const double Mz = Pos.Z - Z0;
		const double Ml = Hyp2(Mx, Mz);
		const double Lim = MAX_SPEED * Dt;
		if (Ml > Lim)
		{
			Pos.X = X0 + (Mx / Ml) * Lim;
			Pos.Z = Z0 + (Mz / Ml) * Lim;
		}
		const double Cx = ClampRing(Pos.X);
		const double Cz = ClampRing(Pos.Z);
		if (Cx != Pos.X) Vel.X = 0;
		if (Cz != Pos.Z) Vel.Z = 0;
		Pos.X = Cx;
		Pos.Z = Cz;
	}

	double SegDist(const FV& A, const FV& B, const FV& C, const FV& D)
	{
		auto Cross = [](const FV& O, const FV& P, const FV& Q) { return (P.X - O.X) * (Q.Z - O.Z) - (P.Z - O.Z) * (Q.X - O.X); };
		const double D1 = Cross(A, B, C);
		const double D2 = Cross(A, B, D);
		const double D3 = Cross(C, D, A);
		const double D4 = Cross(C, D, B);
		if (((D1 > 0 && D2 < 0) || (D1 < 0 && D2 > 0)) && ((D3 > 0 && D4 < 0) || (D3 < 0 && D4 > 0)))
		{
			return 0;
		}
		return FMath::Min(FMath::Min(Hyp(A, ClosestOnSeg(C, D, A)), Hyp(B, ClosestOnSeg(C, D, B))),
			FMath::Min(Hyp(C, ClosestOnSeg(A, B, C)), Hyp(D, ClosestOnSeg(A, B, D))));
	}

	bool PairDetour(const FV& Pos, const FV& Goal, const FV& F0, const FV& F1, int32 Prefer, FV& OutP, int32& OutEnd)
	{
		const double Half = Hyp(F0, F1) / 2;
		// Пара разошлась (углы, постановка) — между ними свободно: не стена, обход не нужен.
		if (Half < 1e-3 || Half * 2 > DETOUR_PAIR_MAX) return false;
		const double Ux = (F1.X - F0.X) / (2 * Half);
		const double Uz = (F1.Z - F0.Z) / (2 * Half);
		const FV M((F0.X + F1.X) / 2, (F0.Z + F1.Z) / 2);
		auto Perp = [&](const FV& P) { return -(P.X - M.X) * Uz + (P.Z - M.Z) * Ux; };
		const double Pp = Perp(Pos);
		if (FMath::Abs(Pp) < 0.05 || Sign(Pp) == Sign(Perp(Goal))) return false;
		if (Hyp(Goal, ClosestOnSeg(F0, F1, Goal)) < DETOUR_R) return false; // цель у самой пары
		if (SegDist(Pos, Goal, F0, F1) >= DETOUR_R) return false;             // путь пару не режет
		double BestC = TNumericLimits<double>::Max();
		bool bAny = false;
		for (int32 End = 0; End < 2; ++End)
		{
			const FV& E = End == 0 ? F0 : F1;
			const double S = End == 0 ? -1 : 1;
			const FV Raw(E.X + Ux * S * (DETOUR_R + 0.2), E.Z + Uz * S * (DETOUR_R + 0.2));
			const FV P(ClampRing(Raw.X), ClampRing(Raw.Z));
			double C = Hyp(Pos, P) + Hyp(P, Goal);
			if (Hyp(P, Raw) > 0.05) C += 4 + Hyp(P, Raw) * 6; // торец у канатов — обход там тесный
			if (End == Prefer) C -= 0.6;
			if (C < BestC)
			{
				BestC = C;
				OutP = P;
				OutEnd = End;
				bAny = true;
			}
		}
		return bAny;
	}

	FV RestSpot(const FV& Cam)
	{
		const BoxingStaging::FRingPoint A0 = BoxingStaging::NeutralCorner(0);
		const BoxingStaging::FRingPoint B0 = BoxingStaging::NeutralCorner(1);
		const FV A(A0.X, A0.Z);
		const FV B(B0.X, B0.Z);
		const FV C = Hyp(A, Cam) >= Hyp(B, Cam) ? A : B;
		return FV(C.X * 0.86, C.Z * 0.86);
	}

	FV CountSpot(const FV& Down, const FV& Toward, const FV& Cam)
	{
		const double L = Or1(Hyp(Toward, Down));
		const double Ux = (Toward.X - Down.X) / L;
		const double Uz = (Toward.Z - Down.Z) / L;
		const FV Pp = FarPerp(Down, Toward, Cam);
		return FV(ClampRing(Down.X + Ux * 1.0 + Pp.X * 0.55), ClampRing(Down.Z + Uz * 1.0 + Pp.Z * 0.55));
	}

	double RouteDist(const TArray<FV>& R, const FV& P)
	{
		if (R.Num() == 0) return TNumericLimits<double>::Max();
		if (R.Num() == 1) return Hyp(R[0], P);
		double D = TNumericLimits<double>::Max();
		for (int32 I = 1; I < R.Num(); ++I)
		{
			D = FMath::Min(D, Hyp(P, ClosestOnSeg(R[I - 1], R[I], P)));
		}
		return D;
	}

	TArray<FV> RouteAhead(const TArray<FV>& R, const FV& P)
	{
		TArray<FV> Out;
		Out.Add(P);
		if (R.Num() < 2)
		{
			Out.Append(R);
			return Out;
		}
		int32 Seg = 1;
		double Best = TNumericLimits<double>::Max();
		for (int32 I = 1; I < R.Num(); ++I)
		{
			const double D = Hyp(P, ClosestOnSeg(R[I - 1], R[I], P));
			if (D < Best - 1e-9)
			{
				Best = D;
				Seg = I;
			}
		}
		for (int32 I = Seg; I < R.Num(); ++I)
		{
			Out.Add(R[I]);
		}
		return Out;
	}

	double LyingDist(const FInput& In, const FV& P)
	{
		if (!In.bHasLying)
		{
			return 1e9;
		}
		const FV& Feet = In.bHasLyingFeet ? In.LyingFeet : In.LyingPelvis;
		double D = FMath::Min(Hyp(P, ClosestOnSeg(Feet, In.LyingPelvis, P)), Hyp(P, ClosestOnSeg(In.LyingPelvis, In.LyingHead, P)));
		for (const FV& L : In.LyingLimbs)
		{
			D = FMath::Min(D, Hyp(P, L) + (LYING_R - LIMB_R));
		}
		return D;
	}

	bool LyingDetour(const FInput& In, const FV& Pos, const FV& Goal, int32 Prefer, FV& OutP, int32& OutEnd)
	{
		if (!In.bHasLying)
		{
			return false;
		}
		const FV& Feet = In.bHasLyingFeet ? In.LyingFeet : In.LyingPelvis;
		const FV& Pel = In.LyingPelvis;
		const FV& Head = In.LyingHead;
		// Сторона точки от оси тела — по ближайшему отрезку.
		auto SideOf = [&](const FV& P)
		{
			const bool bLegs = Hyp(P, ClosestOnSeg(Feet, Pel, P)) <= Hyp(P, ClosestOnSeg(Pel, Head, P));
			const FV& A = bLegs ? Feet : Pel;
			const FV& B = bLegs ? Pel : Head;
			return Sign((B.X - A.X) * (P.Z - A.Z) - (B.Z - A.Z) * (P.X - A.X));
		};
		const double Near = FMath::Min(SegDist(Pos, Goal, Feet, Pel), SegDist(Pos, Goal, Pel, Head));
		if (Near >= LYING_R || SideOf(Pos) == SideOf(Goal))
		{
			return false;
		}
		// Торцы: за стопами и за головой по оси тела, с запасом на радиус обхода.
		auto EndAt = [&](const FV& Tip, const FV& From)
		{
			const double L = Or1(Hyp(Tip, From));
			const double K = LYING_R + 0.35;
			return FV(Tip.X + (Tip.X - From.X) / L * K, Tip.Z + (Tip.Z - From.Z) / L * K);
		};
		const FV Ends[2] = {EndAt(Feet, Pel), EndAt(Head, Pel)};
		double Best = 1e18;
		OutEnd = -1;
		for (int32 E = 0; E < 2; ++E)
		{
			const FV P(ClampRing(Ends[E].X), ClampRing(Ends[E].Z));
			if (LyingDist(In, P) < LYING_R + 0.12)
			{
				continue; // торец у канатов — не пройти
			}
			const double C = Hyp(Pos, P) + Hyp(P, Goal) - (E == Prefer ? 0.3 : 0.0);
			if (C < Best)
			{
				Best = C;
				OutP = P;
				OutEnd = E;
			}
		}
		return OutEnd >= 0;
	}

	FV DownSpot(const FV& Down, const FV& Body, const FV& Toward, const FV& Cam, const TArray<FV>* Route, const FV* Stand, const FV* Current, const FV* Head,
		const FInput* Lying, const FV* From)
	{
		const FV Ideal = CountSpot(Down, Toward, Cam);
		const bool bBody = Lying && Lying->bHasLying;
		auto Viol = [](double D, double Need) { return D < Need ? (Need - D) * 20 + 5 : 0.0; };
		auto Cost = [&](const FV& P)
		{
			double C = Hyp(P, Ideal);
			C += Viol(Hyp(P, Down), DOWN_CLEAR) + Viol(Hyp(P, Body), BODY_CLEAR);
			if (Head) C += Viol(Hyp(P, *Head), BODY_CLEAR);
			// S-66: всё тело (стопы → таз → голова, кисти, колени), а не только его точки.
			if (bBody) C += Viol(LyingDist(*Lying, P), LYING_CLEAR);
			if (bBody && From)
			{
				// За телом — дороже на длину обхода (иначе рефери метался у тела, пытаясь пройти к месту за ним насквозь).
				FV Dp;
				int32 De = -1;
				if (LyingDetour(*Lying, *From, P, -1, Dp, De)) C += Hyp(*From, Dp) + Hyp(Dp, P) - Hyp(*From, P);
				else if (FMath::Min(SegDist(*From, P, Lying->bHasLyingFeet ? Lying->LyingFeet : Lying->LyingPelvis, Lying->LyingPelvis),
					SegDist(*From, P, Lying->LyingPelvis, Lying->LyingHead)) < LYING_R * 0.5) C += 3;
			}
			if (Route) C += Viol(RouteDist(*Route, P), ROUTE_CLEAR);
			if (Stand) C += Viol(Hyp(P, *Stand), STAND_CLEAR);
			if (BlocksView(P, Cam, Down) || BlocksView(P, Cam, Body)) C += 2;
			if (bBody && (BlocksView(P, Cam, Lying->LyingHead) || BlocksView(P, Cam, Lying->LyingPelvis))) C += 2;
			if (Current) C += Hyp(P, *Current) * 0.35;
			return C;
		};
		FV Best = Ideal;
		double BestC = Cost(Ideal);
		auto Consider = [&](const FV& P)
		{
			const double C = Cost(P);
			if (C < BestC)
			{
				BestC = C;
				Best = P;
			}
		};
		if (Current) Consider(FV(ClampRing(Current->X), ClampRing(Current->Z)));
		static const double Rs[4] = {0.95, 1.1, 1.3, 1.55};
		for (int32 A = 0; A < 32; ++A)
		{
			const double Ang = (A / 32.0) * UE_DOUBLE_PI * 2;
			for (const double R : Rs)
			{
				Consider(FV(ClampRing(Down.X + FMath::Cos(Ang) * R), ClampRing(Down.Z + FMath::Sin(Ang) * R)));
				// S-66: тело длинное (до 1.9 м от точки падения) — места и вокруг его середины.
				if (bBody) Consider(FV(ClampRing(Body.X + FMath::Cos(Ang) * R), ClampRing(Body.Z + FMath::Sin(Ang) * R)));
			}
		}
		return Best;
	}

	FV AnnounceSpot(const FV& F0, const FV& F1, const FV& Cam)
	{
		const FV Pp = FarPerp(F0, F1, Cam);
		const double Half = Hyp(F0, F1) / 2;
		const double Off = FMath::Max(0.25, FMath::Sqrt(FMath::Max(0.0, ANNOUNCE_GAP * ANNOUNCE_GAP - Half * Half)));
		return FV(ClampRing((F0.X + F1.X) / 2 + Pp.X * Off), ClampRing((F0.Z + F1.Z) / 2 + Pp.Z * Off));
	}

	FArm ArmIdle(double S) { return MkArm(S, FV3(0.17, -0.98, 0.05), FV3(0.1, -0.9, 0.42)); }
	FArm ArmReady(double S) { return MkArm(S, FV3(0.2, -0.88, 0.42), FV3(-0.02, -0.22, 0.97)); }

	void ToLocal(double Dx, double Dz, double Yaw, double& OutLat, double& OutFwd)
	{
		const double C = FMath::Cos(Yaw);
		const double S = FMath::Sin(Yaw);
		OutFwd = Dx * C - Dz * S;
		OutLat = -Dx * S - Dz * C;
	}

	FArm WristArm(double S, const FV& Ref, double Yaw, const FV& F, double H, bool bHigh)
	{
		const double L = Or1(Hyp(F, Ref));
		const double Near = FMath::Min(WRIST_NEAR, L * 0.5);
		double Lat, Fwd;
		ToLocal(F.X + ((Ref.X - F.X) / L) * Near - Ref.X, F.Z + ((Ref.Z - F.Z) / L) * Near - Ref.Z, Yaw, Lat, Fwd);
		// Рука к своему боку: боец «за спиной» на другой стороне — рука в сторону, не поперёк груди.
		const double LatW = S * FMath::Max(0.08, S * (Lat - S * SHOULDER_LAT));
		FV3 V = Norm(FV3(LatW, H - SHOULDER_Y, Fwd));
		if (bHigh && V.Y < 0.78)
		{
			const double K = FMath::Sqrt((1 - 0.78 * 0.78) / FMath::Max(1e-6, V.X * V.X + V.Z * V.Z));
			V = FV3(V.X * K, 0.78, V.Z * K);
		}
		if (!bHigh) V = Norm(FV3(V.X, FMath::Min(V.Y, -0.35), V.Z));
		FArm A;
		A.Upper = Norm(FV3(V.X, V.Y - 0.12, V.Z));
		A.Fore = V;
		return A;
	}

	double CountLift(double Age, double Beat)
	{
		const double U = Age / FMath::Max(0.2, Beat);
		if (U < 0.12) return 0;
		if (U < 0.55)
		{
			const double K = (U - 0.12) / 0.43;
			return K * K * (3 - 2 * K);
		}
		if (U < 0.8) return 1;
		if (U < 1)
		{
			const double K = (U - 0.8) / 0.2;
			return 1 - K * K;
		}
		return 0; // такт затянулся — рука внизу, ждёт счёта
	}

	FV LyingBody(const FV& Down, const FV& Stand)
	{
		const BoxingStaging::FRingPoint B = BoxingStaging::LyingBody({Down.X, Down.Z}, {Stand.X, Stand.Z});
		return FV(B.X, B.Z);
	}

	FInput InputFromSnapshot(const FFightSnapshot& S, const FFightResult* Result, const FV At[2], const FV& Cam, bool bStanding)
	{
		FInput In;
		const ERingStageKind K = S.Stage.Kind;
		In.Phase = EPhase::Fight;
		if (S.Phase == EFightPhase::Over) In.Phase = EPhase::Over;
		else if (K == ERingStageKind::Out) In.Phase = EPhase::Out;
		else if (K == ERingStageKind::Rest || S.Phase == EFightPhase::Between) In.Phase = EPhase::Rest;
		else if (K == ERingStageKind::Neutral || S.Phase == EFightPhase::Down) In.Phase = EPhase::Down;
		else if (K == ERingStageKind::Resume) In.Phase = EPhase::Resume;
		In.Fighters[0] = At[0];
		In.Fighters[1] = At[1];
		In.Camera = Cam;
		if (S.Phase == EFightPhase::Down && S.DownWho >= 0 && S.DownWho <= 1)
		{
			const int32 Up = 1 - S.DownWho;
			In.Down.bValid = true;
			In.Down.Who = S.DownWho;
			In.Down.Count = S.DownCount;
			In.Down.bHasNeutral = K == ERingStageKind::Neutral && S.Stage.bHasTarget[Up];
			if (In.Down.bHasNeutral)
			{
				In.Down.Neutral = FV(S.Stage.TargetX[Up], S.Stage.TargetZ[Up]);
			}
			In.Down.bArrived = K == ERingStageKind::Neutral ? S.Stage.bArrived[Up] : true;
		}
		if (S.Phase == EFightPhase::Over && Result)
		{
			In.Over.bValid = true;
			In.Over.Winner = Result->WinnerIndex;
			In.Over.bStoppage = Result->Method == EFightMethod::KO || Result->Method == EFightMethod::RSC;
			In.Over.bStanding = bStanding;
		}
		return In;
	}

	// ---------------------------------------------------------------------------------------------

	void FBrain::Place(const FV& P, double InYaw)
	{
		Pos = P;
		Vel = FV();
		Yaw = InYaw;
		bInit = true;
	}

	FBrain::EMode FBrain::PickMode(const FInput& In)
	{
		if (In.Phase != EPhase::Over) bStopDone = false;
		switch (In.Phase)
		{
		case EPhase::Out:
			return Hyp(In.Fighters[0], In.Fighters[1]) > OUT_CENTER_GAP ? EMode::Center : EMode::Side;
		case EPhase::Rest:
			return EMode::Corner;
		case EPhase::Down:
			return In.Down.bValid ? EMode::Count : EMode::Side;
		case EPhase::Over:
			if (!In.Over.bValid) return EMode::Side;
			if (!(In.Over.bStoppage && In.Over.Winner >= 0)) return EMode::Announce;
			// Остановили стоящего: развёл руками — и объявляет победителя (лежащий — стоит над ним).
			if (In.Over.bStanding && Mode == EMode::Stop && ModeT > STOP_DELAY + WAVE_TIME + 0.2) bStopDone = true;
			return bStopDone && In.Over.bStanding ? EMode::Announce : EMode::Stop;
		default:
			return EMode::Side;
		}
	}

	FRefFrame FBrain::Update(const FInput& In, double Dt)
	{
		const FV& F0 = In.Fighters[0];
		const FV& F1 = In.Fighters[1];
		if (!bInit)
		{
			bInit = true;
			Yaw = YawTo(Pos, In.Camera);
		}
		const EMode NewMode = PickMode(In);
		if (NewMode != Mode)
		{
			Mode = NewMode;
			ModeT = 0;
			bHasGoal = false;
		}
		else
		{
			ModeT += Dt;
		}

		// Скорости бойцов (по смещению мест): идущего по постановке рефери обходит с упреждением.
		if (bHasPrevF && Dt > 0)
		{
			for (int32 F = 0; F < 2; ++F)
			{
				double Vx = (In.Fighters[F].X - PrevF[F].X) / Dt;
				double Vz = (In.Fighters[F].Z - PrevF[F].Z) / Dt;
				if (Hyp2(Vx, Vz) > FVEL_MAX) Vx = Vz = 0; // разрыв (перемотка) — не ход
				const double K = SmoothK(FVEL_RATE, Dt);
				FVel[F].X += (Vx - FVel[F].X) * K;
				FVel[F].Z += (Vz - FVel[F].Z) * K;
			}
		}
		PrevF[0] = In.Fighters[0];
		PrevF[1] = In.Fighters[1];
		bHasPrevF = true;

		// Нокдаун/досрочка: где лежит и откуда ушёл соперник — один раз на падение.
		const FDown& Dn = In.Down;
		int32 DownIdx = -1;
		if (Dn.bValid) DownIdx = Dn.Who;
		else if (In.Over.bValid && In.Over.bStoppage && In.Over.Winner >= 0) DownIdx = 1 - In.Over.Winner;
		if (DownIdx < 0)
		{
			bHasDownAt = false;
			bHasToward = false;
			bHasRoute = false;
			Route.Reset();
		}
		else if (!bHasDownAt)
		{
			bHasDownAt = true;
			DownAt = In.Fighters[DownIdx];
			StandAt = In.Fighters[1 - DownIdx];
		}
		if (DownIdx >= 0 && Dn.bValid && Dn.bHasNeutral)
		{
			bHasToward = true;
			TowardAt = Dn.Neutral;
			// Путь стоящего: от места на нокдауне — по точкам сцены (обход тела) или прямо в угол.
			if (!bHasRoute || Dn.Path.Num() > 0)
			{
				Route.Reset();
				Route.Add(StandAt);
				if (Dn.Path.Num() > 0) Route.Append(Dn.Path);
				else Route.Add(Dn.Neutral);
				bHasRoute = true;
			}
		}

		// --- цель и препятствия ---
		FV Target;
		TArray<FObstacle> Obs;
		auto PushFighters = [&](double R)
		{
			for (int32 F = 0; F < 2; ++F)
			{
				if (DownIdx < 0 || F != DownIdx) Obs.Add({In.Fighters[F], R * FMath::Max(1.0, In.FighterScale[F]), false});
			}
		};
		if (Mode == EMode::Center)
		{
			Target = FV(0, 0);
			PushFighters(PASS_R);
		}
		else if (Mode == EMode::Corner)
		{
			Target = RestSpot(In.bHasFightCam ? In.FightCam : In.Camera);
			PushFighters(PASS_R);
			// Камера перерыва идёт за игроком — рефери обходит её, а не проходит вплотную перед объективом.
			Obs.Add({In.Camera, CAM_CLEAR, false});
		}
		else if (Mode == EMode::Count || Mode == EMode::Stop)
		{
			const FV Down = DownAt;
			const FV Toward = bHasToward ? TowardAt : StandAt;
			const FV Body = In.bHasLying ? FV((In.LyingHead.X + In.LyingPelvis.X) / 2, (In.LyingHead.Z + In.LyingPelvis.Z) / 2) : LyingBody(Down, StandAt);
			const FV* Cur = bHasGoal ? &Goal : nullptr;
			Target = DownSpot(Down, Body, Toward, In.Camera, bHasRoute ? &Route : nullptr, &In.Fighters[1 - DownIdx], Cur,
				In.bHasLying ? &In.LyingHead : nullptr, &In, &Pos);
			PushFighters(PASS_R);
			if (In.bHasLying)
			{
				// UE (S-62/S-66): падающее тело целиком — капсула «стопы → таз → голова» и конечности (клип, масштаб облика,
				// доворот/сдвиг от канатов). Ближайшая точка оси — без «швов» между кругами (S-62: голова, таз и точка
				// падения отдельными кругами — рефери застревал на стыке над бёдрами, 18 см до тела).
				const FV& Feet = In.bHasLyingFeet ? In.LyingFeet : Down;
				Obs.Add({ClosestOnSeg(Feet, In.LyingPelvis, Pos), LYING_R, false});
				Obs.Add({ClosestOnSeg(In.LyingPelvis, In.LyingHead, Pos), LYING_R, false});
				for (const FV& L : In.LyingLimbs)
				{
					Obs.Add({L, LIMB_R, false});
				}
				if (!In.bHasLyingFeet) Obs.Add({Down, 0.75, false});
			}
			else
			{
				Obs.Add({Body, 0.7, false});
				Obs.Add({Down, 0.75, false});
			}
			// Стоящий ещё идёт в угол — его путь впереди него — стена.
			if (Mode == EMode::Count && bHasRoute && Dn.bValid && !Dn.bArrived)
			{
				const TArray<FV> Ahead = RouteAhead(Route, In.Fighters[1 - DownIdx]);
				for (int32 I = 1; I < Ahead.Num(); ++I)
				{
					Obs.Add({ClosestOnSeg(Ahead[I - 1], Ahead[I], Pos), PASS_R, false});
				}
			}
		}
		else if (Mode == EMode::Announce)
		{
			Target = AnnounceSpot(F0, F1, In.Camera);
			PushFighters(0.6);
		}
		else
		{
			// бой: сбоку от пары; цель держится, пока лучшая не ушла дальше GOAL_SLACK
			Target = SidePlacement(F0, F1, In.Camera, bHasGoal ? &Goal : nullptr);
			PushFighters(PASS_R);
			Obs.Add({ClosestOnSeg(F0, F1, Pos), GAP_R, true});
		}
		// Вне боя идущий боец — препятствие с упреждением (в центре на выходе из углов — стоит).
		if (In.Phase != EPhase::Fight && Mode != EMode::Center)
		{
			for (int32 F = 0; F < 2; ++F)
			{
				if (F == DownIdx) continue;
				const FV& V = FVel[F];
				const double Sp = Hyp2(V.X, V.Z);
				if (Sp < LOOK_MIN_SPEED) continue;
				const FV& P = In.Fighters[F];
				const FV AheadP(P.X + V.X * LOOK_T, P.Z + V.Z * LOOK_T);
				Obs.Add({ClosestOnSeg(P, AheadP, Pos), PASS_R, false});
			}
		}
		if (!bHasGoal || Hyp(Goal, Target) > GOAL_SLACK || Mode == EMode::Count || Mode == EMode::Stop || Mode == EMode::Announce)
		{
			Goal = Target;
			bHasGoal = true;
		}
		// Цель за парой — через торец пары, а не в лоб.
		FV DetP;
		int32 DetEnd = -1;
		bool bDet = DownIdx < 0 && PairDetour(Pos, Goal, F0, F1, DetourEnd, DetP, DetEnd);
		DetourEnd = bDet ? DetEnd : -1;
		// S-66: цель за лежащим — в обход через торец тела (стопы/голова), а не сквозь него.
		if (DownIdx >= 0 && (Mode == EMode::Count || Mode == EMode::Stop))
		{
			int32 Le = -1;
			const bool bLd = LyingDetour(In, Pos, Goal, LyingEnd, DetP, Le);
			LyingEnd = bLd ? Le : -1;
			bDet = bLd;
		}
		else
		{
			LyingEnd = -1;
		}
		MoveToward(Pos, Vel, bDet ? DetP : Goal, Obs, Dt, Side);

		// --- курс: идёт далеко — лицом по ходу, иначе на действие ---
		FV Look((F0.X + F1.X) / 2, (F0.Z + F1.Z) / 2);
		if (Mode == EMode::Center || Mode == EMode::Announce)
		{
			Look = In.Camera;
		}
		else if (Mode == EMode::Corner)
		{
			Look = FV(0, 0);
		}
		else if (Mode == EMode::Count || Mode == EMode::Stop)
		{
			const FV Body = In.bHasLying ? In.LyingPelvis : LyingBody(DownAt, StandAt);
			Look = FV((DownAt.X + Body.X) / 2, (DownAt.Z + Body.Z) / 2);
		}
		const double Speed = Hyp2(Vel.X, Vel.Z);
		const double Left = Hyp(Goal, Pos);
		double Want = Hyp(Look, Pos) > 0.05 ? YawTo(Pos, Look) : Yaw;
		if (Left > 1.4 && Speed > 0.9) Want = FMath::Atan2(-Vel.Z, Vel.X);
		const double Dy = WrapAngle(Want - Yaw);
		Yaw = WrapAngle(Yaw + Dy * SmoothK(YAW_RATE, Dt));

		// --- шаг ---
		WalkK += (FMath::Min(1.0, Speed / 1.1) - WalkK) * SmoothK(8, Dt);
		StepPhase = FMath::Fmod(StepPhase + (Speed * Dt * UE_DOUBLE_PI) / STRIDE, UE_DOUBLE_PI * 2);
		double MvLat = 0, MvFwd = 1;
		if (Speed > 1e-3) ToLocal(Vel.X / Speed, Vel.Z / Speed, Yaw, MvLat, MvFwd);

		// --- руки ---
		const FGest G = Gestures(In, Dt);
		for (int32 I = 0; I < 2; ++I)
		{
			const double K = SmoothK(G.Rates[I], Dt);
			ArmsNow[I] = MixArm(ArmsNow[I], G.Arms[I], K);
		}
		FRefFrame Out;
		Out.Arms[0] = ArmsNow[0];
		Out.Arms[1] = ArmsNow[1];
		// Мах руками на ходу (только свободные руки), противофазой ногам.
		for (int32 I = 0; I < 2; ++I)
		{
			if (!G.Free[I] || WalkK < 0.02) continue;
			const double Sw = (I == 0 ? -1 : 1) * FMath::Sin(StepPhase) * 0.4 * WalkK;
			const FArm& A = ArmsNow[I];
			Out.Arms[I].Upper = Norm(FV3(A.Upper.X + MvLat * Sw, A.Upper.Y, A.Upper.Z + MvFwd * Sw));
			Out.Arms[I].Fore = Norm(FV3(A.Fore.X + MvLat * Sw * 1.3, A.Fore.Y, A.Fore.Z + MvFwd * Sw * 1.3));
		}
		// «Боевая» стойка — в бою, когда стоит или переступает (на быстром ходу — обычная походка).
		const double ReadyWant = In.Phase == EPhase::Fight && Mode == EMode::Side ? 1 - FMath::Min(1.0, FMath::Max(0.0, (Speed - 0.9) / 0.6)) : 0;
		ReadyK += (ReadyWant - ReadyK) * SmoothK(3, Dt);
		// UE: доля жеста в руке — свободная рука отдаёт руки локомоции (в бою — по стойке), жест — целиком.
		for (int32 I = 0; I < 2; ++I)
		{
			const double WantW = G.Free[I] ? (In.Phase == EPhase::Fight && Mode == EMode::Side ? ReadyK : 0.0) : 1.0;
			ArmWNow[I] += (WantW - ArmWNow[I]) * SmoothK(ARMW_RATE, Dt);
			Out.ArmW[I] = ArmWNow[I];
		}
		Out.X = Pos.X;
		Out.Z = Pos.Z;
		Out.Yaw = Yaw;
		Out.Walk = WalkK;
		Out.Phase = StepPhase;
		Out.MoveLat = MvLat;
		Out.MoveFwd = MvFwd;
		Out.Lean = G.Lean;
		Out.Hands[0] = G.Hands[0];
		Out.Hands[1] = G.Hands[1];
		Out.Ready = ReadyK;
		Out.Raised = RaisedT;
		return Out;
	}

	FBrain::FGest FBrain::Gestures(const FInput& In, double Dt)
	{
		FGest G;
		// Бой: руки перед собой; иначе (ходьба, перерыв) — опущены.
		const bool bFight = In.Phase == EPhase::Fight && Mode == EMode::Side;
		G.Arms[0] = bFight ? ArmReady(1) : ArmIdle(1);
		G.Arms[1] = bFight ? ArmReady(-1) : ArmIdle(-1);
		G.Hands[0] = G.Hands[1] = EHand::Relax;
		G.Rates[0] = G.Rates[1] = ARM_RATE;
		G.Free[0] = G.Free[1] = true;
		G.Lean = 0;
		const FDown& Dn = In.Down;
		if (Mode == EMode::Count && Dn.bValid)
		{
			// Такт счёта: новый счёт — взмах; длина такта — по замеру между счётами.
			const bool bPointing = Dn.bHasNeutral && !Dn.bArrived && ModeT < POINT_MAX;
			if (Dn.Count != LastCount)
			{
				if (LastCount > 0 && BeatAge > 0.25 && BeatAge < 2) Beat = BeatAge;
				if (LastCount == 0)
				{
					// Начало счёта: рука к углу — та, с чьей стороны угол; считает другая — до конца счёта.
					double Ps = 1;
					if (bPointing)
					{
						double Lat, Fwd;
						ToLocal(Dn.Neutral.X - Pos.X, Dn.Neutral.Z - Pos.Z, Yaw, Lat, Fwd);
						Ps = Lat >= 0 ? 1 : -1;
					}
					CountSide = bPointing ? -Ps : -1;
				}
				LastCount = Dn.Count;
				BeatAge = 0;
			}
			else
			{
				BeatAge += Dt;
			}
			const double CS = CountSide;
			bool bPoint = false;
			FArm Point;
			if (bPointing)
			{
				// Показывает угол свободной рукой; угол «ушёл» за спину — рука показывает вперёд.
				const double Ps = -CS;
				double Lat, Fwd;
				ToLocal(Dn.Neutral.X - Pos.X, Dn.Neutral.Z - Pos.Z, Yaw, Lat, Fwd);
				const double Dl = Or1(Hyp2(Lat, Fwd));
				const FV3 Dir = Norm(FV3(Ps * FMath::Max(0.05, (Ps * Lat) / Dl), 0.18, FMath::Max(-0.4, Fwd / Dl)));
				Point.Upper = Dir;
				Point.Fore = Dir;
				bPoint = true;
			}
			const double E = CountLift(BeatAge, Beat);
			const FArm C = MixArm(ArmCountDown(CS), ArmCountUp(CS), E);
			const FArm Other = bPoint ? Point : ArmKnee(-CS);
			if (CS > 0)
			{
				G.Arms[0] = C;
				G.Arms[1] = Other;
			}
			else
			{
				G.Arms[1] = C;
				G.Arms[0] = Other;
			}
			G.Rates[0] = CS > 0 ? COUNT_RATE : ARM_RATE;
			G.Rates[1] = CS > 0 ? ARM_RATE : COUNT_RATE;
			G.Free[0] = G.Free[1] = false;
			G.Lean = bPoint ? 0.12 : 0.3;
			// Счёт пальцами (1..5 на руке счёта; дальше — по кругу), свободная — указывает в угол.
			const int32 Shown = ((((Dn.Count - 1) % 5) + 5) % 5) + 1;
			const int32 Ci = CS > 0 ? 0 : 1;
			G.Hands[Ci] = static_cast<EHand>(Shown);
			G.Hands[1 - Ci] = bPoint ? EHand::Point : EHand::Relax;
		}
		else
		{
			LastCount = 0;
			BeatAge = 99;
		}
		if (Mode == EMode::Stop)
		{
			const double Wt = ModeT - STOP_DELAY; // сперва — падение, потом жест
			if (Wt >= 0 && Wt < WAVE_TIME)
			{
				// «Бой окончен»: руки скрещиваются перед собой и разводятся.
				const double W = 0.5 + 0.5 * FMath::Cos(Wt * UE_DOUBLE_PI * 2 * 1.6);
				G.Arms[0] = MixArm(ArmSpread(1), ArmCross(1), W);
				G.Arms[1] = MixArm(ArmSpread(-1), ArmCross(-1), W);
				G.Rates[0] = G.Rates[1] = 16;
				G.Hands[0] = G.Hands[1] = EHand::Open;
			}
			else if (Wt >= WAVE_TIME)
			{
				G.Arms[0] = ArmKnee(1);
				G.Arms[1] = ArmKnee(-1);
				G.Lean = 0.3;
			}
			G.Free[0] = G.Free[1] = false;
		}
		if (Mode == EMode::Announce)
		{
			// Не на ходу: сперва встаёт между бойцами (или крайний срок), берёт обоих за запястья внизу
			// и лишь потом поднимает руку победителя (ничья — обе).
			const FV GoalP = bHasGoal ? Goal : Pos;
			const bool bStill = Hyp(GoalP, Pos) < ANNOUNCE_ARRIVE && Hyp2(Vel.X, Vel.Z) < ANNOUNCE_STILL;
			if (AnnounceT >= 0) AnnounceT += Dt;
			else if (bStill || ModeT > ANNOUNCE_MAX) AnnounceT = 0;
			if (AnnounceT >= 0)
			{
				const int32 Win = In.Over.bValid ? In.Over.Winner : -1;
				const FV& A0 = In.Fighters[0];
				const FV& A1 = In.Fighters[1];
				double Lat0, Lat1, Fw;
				ToLocal(A0.X - Pos.X, A0.Z - Pos.Z, Yaw, Lat0, Fw);
				ToLocal(A1.X - Pos.X, A1.Z - Pos.Z, Yaw, Lat1, Fw);
				const int32 Li = Lat0 >= Lat1 ? 0 : 1;
				const int32 Ri = 1 - Li;
				const bool bLift = AnnounceT >= GRAB_T;
				auto High = [&](int32 I) { return bLift && (Win < 0 || Win == I); };
				G.Arms[0] = WristArm(1, Pos, Yaw, In.Fighters[Li], High(Li) ? WRIST_HIGH : WRIST_LOW, High(Li));
				G.Arms[1] = WristArm(-1, Pos, Yaw, In.Fighters[Ri], High(Ri) ? WRIST_HIGH : WRIST_LOW, High(Ri));
				if (bLift) RaisedT += Dt;
				G.Rates[0] = G.Rates[1] = bLift ? 7 : 10;
				G.Free[0] = G.Free[1] = false;
				G.Hands[0] = G.Hands[1] = EHand::Open;
			}
		}
		else
		{
			AnnounceT = -1;
			RaisedT = 0;
		}
		return G;
	}
}
