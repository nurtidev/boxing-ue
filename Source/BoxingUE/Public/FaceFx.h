// S-74 (game feel): мимика и повреждения лица бойца — только визуал, ядро и его ГСЧ не трогаются.
//
//  * FBoxerFaceDamage — повреждения лица по ходу боя (порт web/src/ui/three/damage.ts 1:1): копятся от пропущенных
//    ударов, не заживают; перерыв чуть подчищает кровь/красноту (катмен), отёк и рассечения остаются.
//  * FBoxerFaceFx — мимика по событиям и состоянию (без ГСЧ, детерминирована событиями): гримаса на попадании по силе
//    (в корпус — сжатые зубы, в голову — зажмурился), сжатые зубы и выдох на своём силовом, усталость (рот приоткрыт,
//    тяжёлое дыхание) по стамине, «поплыл» после тяжёлого/нокдауна (веки тяжёлые, челюсть отвисла, взгляд мутный),
//    отёк — глаз заплывает (веко сверху), моргание по таймеру. Выход — веса каналов 0..1 (FBoxerFaceFrame).
//  * Каналы мапятся на кривые RigLogic лица MetaHuman (CTRL_expressions_*): видимый меш тела отдаёт их кривыми своей
//    позы, лицо (Face_AnimBP: копия позы тела вместе с кривыми) → пост-процесс лица (RigLogic) двигает кости лица.
#pragma once

#include "CoreMinimal.h"

// Повреждения (стороны L/R — СОБСТВЕННЫЕ левая/правая бойца), 0..1.
struct FBoxerFaceDamage
{
	float Head = 0.f;  // общий «побитый» вид (покраснение)
	float EyeL = 0.f;  // отёк под глазом
	float EyeR = 0.f;
	float CutL = 0.f;  // рассечение брови
	float CutR = 0.f;
	float Nose = 0.f;  // кровь из носа
	float Mouth = 0.f; // разбитая губа
	float CheekL = 0.f;
	float CheekR = 0.f;
	float BodyL = 0.f;
	float BodyR = 0.f;

	enum class EPunch : uint8 { Jab, Cross, Hook, Uppercut };
	// Попадание (или пробитый блок, bBlocked — лёгкое покраснение). Mag — урон события ядра (0.5..3); bRear — задняя рука.
	void ApplyHit(float Mag, EPunch Punch, bool bRear, bool bBody, bool bBlocked);
	void ApplyKnockdown();
	void BetweenRounds();
	float Max() const;
};

// Каналы мимики.
enum class EBoxFaceCh : uint8
{
	JawOpen,      // рот открыт (дыхание, отвисшая челюсть)
	Clench,       // сжатые зубы (челюсть сжата, губы прижаты)
	Stretch,      // рот растянут (гримаса боли)
	BrowDown,     // брови сведены вниз
	BrowRaise,    // брови домиком (поплыл)
	SquintL,      // прищур (щёки вверх, веко снизу)
	SquintR,
	BlinkL,       // веко сверху: моргание, зажмурился, тяжёлые веки, отёк
	BlinkR,
	NoseWrinkle,  // морщит нос (боль, усилие)
	Funnel,       // губы трубочкой (выдох «пф»)
	Num
};
constexpr int32 BOX_FACE_NUM = static_cast<int32>(EBoxFaceCh::Num);

struct FBoxerFaceFrame
{
	float V[BOX_FACE_NUM] = {};
	bool bOn = false;
	float& operator[](EBoxFaceCh C) { return V[static_cast<int32>(C)]; }
	float operator[](EBoxFaceCh C) const { return V[static_cast<int32>(C)]; }
};

// Вход на кадр (игровой поток).
struct FBoxerFaceInput
{
	float Stamina = 1.f;       // 0..1
	bool bDown = false;        // лежит (нокдаун/нокаут)
	bool bKO = false;
	bool bPunching = false;    // идёт свой удар
	float PunchPhase = 0.f;    // фаза удара (контакт = ContactFrac)
	float ContactFrac = 0.5f;
	bool bPowerPunch = false;  // кросс / хук / апперкот (силовой)
	bool bStunned = false;     // встряхнут (ядро)
	bool bVictory = false;     // победил (финал)
};

class FBoxerFaceFx
{
public:
	// Пропустил удар (Mag — урон события ядра), bBlocked — в блок. Гримаса по силе.
	void OnHit(float Mag, bool bBody, bool bBlocked);
	void OnKnockdown();
	void Update(float Dt, const FBoxerFaceInput& In);
	const FBoxerFaceFrame& Frame() const { return Out; }
	void Reset() { *this = FBoxerFaceFx(); }

	FBoxerFaceDamage Damage;

	// Константы (тест читает).
	static constexpr float GRIMACE_MIN_MAG = 0.45f; // слабее — без гримасы (не спамим лицом на джебах в блок)
	static constexpr float DAZE_MAG = 1.6f;          // тяжелее — «поплыл»
	static constexpr float DAZE_S = 2.6f;            // столько держится «поплыл» после тяжёлого
	static constexpr float DAZE_KD_S = 5.f;          // после нокдауна
	static constexpr float TIRED_FROM = 0.55f;       // стамина ниже — рот приоткрыт, дыхание
	static constexpr float BLINK_EVERY = 3.1f;       // моргание раз в N с (детерминированно, без ГСЧ)

	// Отладка/тест.
	float Grimace() const { return GrimaceA; }
	float Daze() const { return DazeW; }
	float Tired() const { return TiredW; }

private:
	FBoxerFaceFrame Out;
	float T = 0.f;
	float GrimaceA = 0.f;   // текущая сила гримасы 0..1 (спад)
	float GrimaceT = 0.f;   // атака
	bool bGrimaceBody = false;
	float DazeLeft = 0.f;   // с «поплыл»
	float DazeW = 0.f;      // сглаженный вес
	float TiredW = 0.f;
	float ExhaleT = -1.f;   // с с контакта своего силового (выдох), −1 — нет
	bool bWasPunching = false;
	bool bPrevContact = false;
	float BlinkT = 0.f;
	float ClenchW = 0.f;
};

// Кривые лица на кадр (игровой поток → анимпоток видимого меша): уникальные имена, значения 0..1.
struct FBoxerFaceCurves
{
	static constexpr int32 MAX = 48;
	FName Names[MAX];
	float Values[MAX] = {};
	int32 Num = 0;
};

// Каналы → кривые RigLogic. Build: Known — кривые, которые есть у лица (метаданные меша/скелета); пусто — брать все.
class FBoxerFaceMap
{
public:
	void Build(const TSet<FName>& Known);
	void Fill(const FBoxerFaceFrame& F, FBoxerFaceCurves& Out) const;
	bool IsBuilt() const { return bBuilt; }
	TArray<FName> Missing; // имена из CurvesFor, которых у лица нет (отладка)
	int32 NumCurves() const { return Unique.Num(); }

private:
	struct FEntry
	{
		int32 Curve = 0; // индекс в Unique
		int32 Ch = 0;
		float W = 1.f;
	};
	TArray<FName> Unique;
	TArray<FEntry> Entries;
	bool bBuilt = false;
};

namespace BoxFace
{
	// Кривые RigLogic лица MetaHuman для канала (CTRL_expressions_*), без суффикса стороны: L/R (или пусто) — по каналу.
	// OutNames — имена кривых, OutWeights — доля канала на кривую.
	void CurvesFor(EBoxFaceCh Ch, TArray<FName>& OutNames, TArray<float>& OutWeights);
}
