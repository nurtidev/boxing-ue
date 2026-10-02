// Постановка раунда — порт web/src/engine/interactive/corners.ts (+ отдых в углу из rounds.ts, S-43). S-53.
//
// Где стоят бойцы до гонга, выход к точке встречи, перерыв в своих углах, нейтральный угол на нокдауне и
// возврат к бою после счёта. Постановка — чистая «ходьба»: без ГСЧ, без стамины и без боевого времени T
// (на Out/Resume/Rest оно стоит, на Neutral идёт счёт рефери), поэтому решения ИИ и броски ГСЧ после выхода —
// те же, что без постановки: бой без нокдаунов бит-в-бит совпадает с боем bCorners = false (Tools/CoreHarness).
//
// Часы раунда честно: идут только в бою (Phase == Fighting). По гонгу бойцы идут из углов, а время раунда
// пускается командой «Бокс!», когда пара сошлась; на счёте нокдауна и возврате к бою часы стоят.
//
// Имена хелперов — свои (Stg*), всё квалифицировано: в unity-сборке UE этот файл может попасть в одну
// единицу трансляции с BoxingFightCore.cpp (там `using namespace BoxingFightConst` и анонимные ClampD/Hypot).
#include "FightStaging.h"
#include "BoxingFightCore.h"

namespace BoxingStagingImpl
{
	constexpr double ARRIVE_EPS = 1e-6;
	constexpr double CORNER_STAM = 0.28;      // rounds.ts: доля максимума стамины, что возвращает угол
	constexpr double CORNER_WEAR = 0.72;      // доля износа, что остаётся после угла
	constexpr double CORNER_RECOVER_S = 3.0;  // за сколько секунд перерыва отдых набирается видимо

	double StgClamp(double V, double Lo, double Hi) { return FMath::Max(Lo, FMath::Min(Hi, V)); }
	double StgHypot(double X, double Z) { return FMath::Sqrt(X * X + Z * Z); }
	double StgClampRing(double V) { return StgClamp(V, -BoxingStaging::RING_HALF, BoxingStaging::RING_HALF); }
	double StgSign(double V) { return V > 0 ? 1.0 : (V < 0 ? -1.0 : 0.0); }

	// Расстояние от точки P до отрезка A→B.
	double StgSegDist(BoxingStaging::FRingPoint A, BoxingStaging::FRingPoint B, BoxingStaging::FRingPoint P)
	{
		const double Vx = B.X - A.X;
		const double Vz = B.Z - A.Z;
		double L2 = Vx * Vx + Vz * Vz;
		if (L2 == 0) L2 = 1;
		const double U = StgClamp(((P.X - A.X) * Vx + (P.Z - A.Z) * Vz) / L2, 0, 1);
		return StgHypot(A.X + Vx * U - P.X, A.Z + Vz * U - P.Z);
	}
}

// ======================================================================
// Геометрия (FightStaging.h) — чистые функции
// ======================================================================
namespace BoxingStaging
{
	FRingPoint CornerOf(int32 Fighter)
	{
		// CORNER_SIGNS: red (−1, −1), blue (+1, +1).
		const double S = Fighter == 0 ? -1.0 : 1.0;
		return {S * CORNER_SPOT, S * CORNER_SPOT};
	}

	FRingPoint NeutralCorner(int32 K)
	{
		// CORNER_SIGNS.neutral: [(+1, −1), (−1, +1)].
		return K == 0 ? FRingPoint{CORNER_SPOT, -CORNER_SPOT} : FRingPoint{-CORNER_SPOT, CORNER_SPOT};
	}

	// Не на диагонали намеренно (corners.ts meetPoint): с диагонали до угла за спиной дальше (√2), пару реже
	// прижимало к канатам — досрочки автопилота уплывали от simulate.
	FRingPoint MeetPoint(int32 Fighter)
	{
		return {(Fighter == 0 ? -1.0 : 1.0) * (DIST_START / 2), 0};
	}

	FRingPoint FarNeutral(FRingPoint P)
	{
		const FRingPoint A = NeutralCorner(0);
		const FRingPoint B = NeutralCorner(1);
		return BoxingStagingImpl::StgHypot(A.X - P.X, A.Z - P.Z) >= BoxingStagingImpl::StgHypot(B.X - P.X, B.Z - P.Z) ? A : B;
	}

	FRingPoint LyingBody(FRingPoint Down, FRingPoint Stand)
	{
		const double Dx = Down.X - Stand.X;
		const double Dz = Down.Z - Stand.Z;
		double L = BoxingStagingImpl::StgHypot(Dx, Dz);
		if (L == 0) L = 1;
		return {Down.X + (Dx / L) * BODY_BACK, Down.Z + (Dz / L) * BODY_BACK};
	}

	double NeutralPathClear(FRingPoint Down, FRingPoint Stand, FRingPoint C)
	{
		const FRingPoint Body = LyingBody(Down, Stand);
		return FMath::Min(BoxingStagingImpl::StgSegDist(Stand, C, Body) - PASS_CLEAR,
			BoxingStagingImpl::StgSegDist(Stand, C, Down) - PASS_CLEAR * 0.8);
	}

	// Правило: в дальний от лежащего. Если путь туда через лежащего (упал назад — как раз в ту сторону) —
	// в другой: через тело не шагают. Оба пути перекрыты — тот, что даёт больше простора.
	FRingPoint NeutralFor(FRingPoint Down, FRingPoint Stand)
	{
		const FRingPoint Far = FarNeutral(Down);
		const FRingPoint N0 = NeutralCorner(0);
		const bool bFarIs0 = Far.X == N0.X && Far.Z == N0.Z;
		const FRingPoint Near = bFarIs0 ? NeutralCorner(1) : N0;
		const double ClearFar = NeutralPathClear(Down, Stand, Far);
		if (ClearFar >= 0) return Far;
		const double ClearNear = NeutralPathClear(Down, Stand, Near);
		if (ClearNear >= 0) return Near;
		return ClearFar >= ClearNear ? Far : Near;
	}
}

// ======================================================================
// Стадии (corners.ts) — над состоянием ядра
// ======================================================================
namespace
{
	BoxingStaging::FRingPoint StgPt(double X, double Z) { return {X, Z}; }
}

void FBoxingFightCore::BeginStage(ERingStageKind Kind, const FVec2* Target0, const FVec2* Target1)
{
	Stage = FStageState();
	Stage.Kind = Kind;
	const FVec2* Tg[2] = {Target0, Target1};
	for (int32 I = 0; I < 2; ++I)
	{
		Stage.bHasTarget[I] = Tg[I] != nullptr;
		if (Tg[I]) Stage.Target[I] = *Tg[I];
		Stage.bArrived[I] = Tg[I] == nullptr;
		FRuntime& R = Rt[I];
		R.WalkSpeed = 0;
		R.WalkVX = 0;
		R.WalkVZ = 0;
		R.bHasFaceYaw = false;
		if (Tg[I])
		{
			R.Step = EStepKind::None;
			R.StepUntil = 0;
		}
	}
}

// Старт боя: оба в своих углах.
void FBoxingFightCore::PlaceInCorners()
{
	for (int32 I = 0; I < 2; ++I)
	{
		const BoxingStaging::FRingPoint C = BoxingStaging::CornerOf(I);
		Rt[I].X = C.X;
		Rt[I].Z = C.Z;
	}
}

// Гонг: из углов (или откуда стоят) — к точкам встречи; бой начнётся, когда сойдутся.
void FBoxingFightCore::BeginWalkout()
{
	Phase = EFightPhase::Walkout;
	const BoxingStaging::FRingPoint M0 = BoxingStaging::MeetPoint(0);
	const BoxingStaging::FRingPoint M1 = BoxingStaging::MeetPoint(1);
	const FVec2 T0{M0.X, M0.Z};
	const FVec2 T1{M1.X, M1.Z};
	BeginStage(ERingStageKind::Out, &T0, &T1);
}

// Гонг конца раунда: оба уходят в свои углы (Phase == Between).
void FBoxingFightCore::BeginRest()
{
	const BoxingStaging::FRingPoint C0 = BoxingStaging::CornerOf(0);
	const BoxingStaging::FRingPoint C1 = BoxingStaging::CornerOf(1);
	const FVec2 T0{C0.X, C0.Z};
	const FVec2 T1{C1.X, C1.Z};
	BeginStage(ERingStageKind::Rest, &T0, &T1);
}

// Нокдаун: стоящий уходит в дальний от лежащего нейтральный угол, лежащий — на месте.
void FBoxingFightCore::BeginNeutral(int32 DownIdx)
{
	const int32 Up = 1 - DownIdx;
	// После счёта бой продолжится с той же дистанции, на которой упал (не дальше стартовой).
	ResumeGap = FMath::Max(BoxingStaging::DIST_MIN, FMath::Min(BoxingStaging::DIST_START, Distance()));
	const BoxingStaging::FRingPoint DownP = StgPt(Rt[DownIdx].X, Rt[DownIdx].Z);
	const BoxingStaging::FRingPoint UpP = StgPt(Rt[Up].X, Rt[Up].Z);
	const BoxingStaging::FRingPoint N = BoxingStaging::NeutralFor(DownP, UpP);
	const BoxingStaging::FRingPoint Body = BoxingStaging::LyingBody(DownP, UpP);
	Lying = FVec2{Body.X, Body.Z};
	bHasLying = true;
	const FVec2 Tg{N.X, N.Z};
	BeginStage(ERingStageKind::Neutral, Up == 0 ? &Tg : nullptr, Up == 1 ? &Tg : nullptr);
}

// Счёт окончен, встал: стоящий идёт из нейтрального угла к вставшему — на дистанцию, с которой тот упал
// (ResumeGap), по линии «вставший → он»; вставший ждёт (Phase == Walkout).
void FBoxingFightCore::BeginResume(int32 RoseIdx)
{
	using namespace BoxingStagingImpl;
	const int32 Up = 1 - RoseIdx;
	const FRuntime& A = Rt[RoseIdx];
	const FRuntime& B = Rt[Up];
	const double Dx = B.X - A.X;
	const double Dz = B.Z - A.Z;
	const double L = StgHypot(Dx, Dz);
	// Вставший у самых канатов — точку зажимает в ринг, дистанция может выйти меньше.
	const double Gap = ResumeGap;
	const double SignX = StgSign(A.X != 0 ? A.X : 1.0);
	double Tx = StgClampRing(L > 1e-6 ? A.X + (Dx / L) * Gap : A.X - SignX * Gap);
	double Tz = StgClampRing(L > 1e-6 ? A.Z + (Dz / L) * Gap : A.Z);
	if (StgHypot(Tx - A.X, Tz - A.Z) < BoxingStaging::DIST_MIN)
	{
		// Прижат в угол так, что по линии не выйти, — на ту же дистанцию ближе к центру.
		double Cl = StgHypot(A.X, A.Z);
		if (Cl == 0) Cl = 1;
		Tx = StgClampRing(A.X - (A.X / Cl) * Gap);
		Tz = StgClampRing(A.Z - (A.Z / Cl) * Gap);
	}
	const FVec2 Tg{Tx, Tz};
	Phase = EFightPhase::Walkout;
	BeginStage(ERingStageKind::Resume, Up == 0 ? &Tg : nullptr, Up == 1 ? &Tg : nullptr);
}

// Конец постановки: бой («Бокс!»). Часы раунда пойдут со следующего тика.
void FBoxingFightCore::StartFighting()
{
	EndStage();
	Phase = EFightPhase::Fighting;
}

// Снять постановку (бой идёт / бой окончен): бойцы стоят, лицом к сопернику.
void FBoxingFightCore::EndStage()
{
	Stage = FStageState();
	for (int32 I = 0; I < 2; ++I)
	{
		FRuntime& R = Rt[I];
		R.WalkSpeed = 0;
		R.WalkVX = 0;
		R.WalkVZ = 0;
		R.bHasFaceYaw = false;
	}
}

bool FBoxingFightCore::StageAllArrived() const
{
	return Stage.Kind == ERingStageKind::None || (Stage.bArrived[0] && Stage.bArrived[1]);
}

// Ходьба на тике: к цели со скоростью WALK_SPEED (после паузы гонга), лицом — по ходу в угол (Rest/Neutral)
// или на соперника (Out/Resume); дошли оба — бой (Out/Resume).
void FBoxingFightCore::UpdateStage(double Dt)
{
	using namespace BoxingStagingImpl;
	if (Stage.Kind == ERingStageKind::None) return;
	Stage.T += Dt;
	const bool bMoving = Stage.T >= BoxingStaging::GONG_DELAY;
	const bool bNeutral = Stage.Kind == ERingStageKind::Neutral;
	for (int32 I = 0; I < 2; ++I)
	{
		if (!Stage.bHasTarget[I] || Stage.bArrived[I]) continue;
		if (!bMoving) continue;
		FRuntime& R = Rt[I];
		const FRuntime& Op = Rt[1 - I];
		const FVec2 Tg = Stage.Target[I];
		double Dx = Tg.X - R.X;
		double Dz = Tg.Z - R.Z;
		const double Left = StgHypot(Dx, Dz);
		if (Left <= ARRIVE_EPS)
		{
			Arrive(I);
			continue;
		}
		Dx /= Left;
		Dz /= Left;
		bool bSteered = false;
		// Обход: у лежащего (тело) или у соперника под носом (пути в углы/к центру пересекаются) — убрать
		// составляющую «на него» и идти вдоль.
		double ObX = Op.X, ObZ = Op.Z;
		if (bNeutral && bHasLying && StgHypot(Lying.X - R.X, Lying.Z - R.Z) < StgHypot(Op.X - R.X, Op.Z - R.Z))
		{
			ObX = Lying.X;
			ObZ = Lying.Z;
		}
		const double Ox = ObX - R.X;
		const double Oz = ObZ - R.Z;
		const double Od = StgHypot(Ox, Oz);
		if (Od < (bNeutral ? BoxingStaging::PASS_CLEAR : BoxingStaging::WALK_CLEAR) && Od > 1e-6)
		{
			const double Dot = (Dx * Ox + Dz * Oz) / Od;
			if (Dot > 0)
			{
				bSteered = true;
				Dx -= (Ox / Od) * Dot;
				Dz -= (Oz / Od) * Dot;
				const double L = StgHypot(Dx, Dz);
				if (L < 1e-3)
				{
					Dx = -Oz / Od; // ровно «сквозь» — в обход по касательной
					Dz = Ox / Od;
				}
				else
				{
					Dx /= L;
					Dz /= L;
				}
			}
		}
		const double S = FMath::Min(Left, BoxingStaging::WALK_SPEED * Dt);
		if (S >= Left - 1e-9 && !bSteered)
		{
			Arrive(I); // последний шаг — ровно в цель
			continue;
		}
		R.X = StgClampRing(R.X + Dx * S);
		R.Z = StgClampRing(R.Z + Dz * S);
		R.WalkSpeed = BoxingStaging::WALK_SPEED;
		R.WalkVX = Dx * BoxingStaging::WALK_SPEED;
		R.WalkVZ = Dz * BoxingStaging::WALK_SPEED;
		// В угол — лицом по ходу (развернулся и пошёл); к бою — лицом на соперника.
		R.bHasFaceYaw = Stage.Kind == ERingStageKind::Rest || bNeutral;
		R.FaceYaw = R.bHasFaceYaw ? FMath::Atan2(-Dz, Dx) : 0;
	}
	if ((Stage.Kind == ERingStageKind::Out || Stage.Kind == ERingStageKind::Resume) && Stage.bArrived[0] && Stage.bArrived[1])
	{
		StartFighting();
	}
}

void FBoxingFightCore::Arrive(int32 I)
{
	FRuntime& R = Rt[I];
	R.X = Stage.Target[I].X;
	R.Z = Stage.Target[I].Z;
	Stage.bArrived[I] = true;
	R.WalkSpeed = 0;
	R.WalkVX = 0;
	R.WalkVZ = 0;
	R.bHasFaceYaw = false; // дошёл — развернулся к сопернику (в углу — лицом в ринг)
}

// ======================================================================
// Отдых в углу (rounds.ts beginCornerRest / cornerRecover, S-43)
// ======================================================================
// Итог считается по гонгу, а набирается ВИДИМО за перерыв — плавно за CORNER_RECOVER_S; Proceed доводит до
// того же итога, сколько бы ни шёл перерыв, — исход боя бит-в-бит прежний (ГСЧ перерыв не трогает).
void FBoxingFightCore::BeginCornerRest()
{
	using namespace BoxingStagingImpl;
	RestT = 0;
	for (int32 I = 0; I < 2; ++I)
	{
		FRuntime& R = Rt[I];
		R.bHasRest = true;
		R.RestStam0 = R.Stamina;
		R.RestStam1 = FMath::Min(R.StamCap(), R.Stamina + R.MaxStam * CORNER_STAM);
		R.RestWear0 = R.Accumulated;
		R.RestWear1 = R.Accumulated * CORNER_WEAR;
	}
}

void FBoxingFightCore::CornerRecover(double Dt)
{
	using namespace BoxingStagingImpl;
	RestT += Dt;
	const double X = FMath::Min(1.0, RestT / CORNER_RECOVER_S);
	const double U = X * X * (3 - 2 * X); // плавно: быстрее в середине, без рывка на старте и в конце
	for (int32 I = 0; I < 2; ++I)
	{
		FRuntime& R = Rt[I];
		if (!R.bHasRest) continue;
		R.Stamina = X >= 1 ? R.RestStam1 : R.RestStam0 + (R.RestStam1 - R.RestStam0) * U;
		R.Accumulated = X >= 1 ? R.RestWear1 : R.RestWear0 + (R.RestWear1 - R.RestWear0) * U;
	}
}
