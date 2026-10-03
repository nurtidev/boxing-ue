// S-71 (game feel): боец садится на стул в своём углу в перерыве — порт web/src/ui/three/sitPose.ts.
// Клипа «сидеть» нет (ни у Mixamo-набора, ни у GASP) — поза ПРОЦЕДУРНАЯ поверх ретаргета видимого меша
// (FBoxerVisualRootNode, после слоя «ощущения»): бёдра вперёд, голени вниз, колени врозь, корпус чуть откинут
// на подушку угла, руки разведены назад на канаты, перчатки свисают; таз опускается так, чтобы ступни стояли
// на настиле (стул ставит ABoxingCornerCrew под таз). W — вес позы 0..1 (садится/встаёт плавно).
// Только визуал: ядро и его ГСЧ не трогаются; IK ног сидя не нужен — ноги задаёт поза целиком (вес W).
#pragma once

#include "CoreMinimal.h"
#include "BoneIndices.h"

struct FCompactPose;
struct FBoneContainer;
struct FFightSnapshot;

namespace BoxerSit
{
	// web sitPose.ts (рад).
	constexpr float THIGH = 1.42f;      // бедро вперёд от вертикали
	constexpr float SHIN_BACK = 0.08f;  // голень чуть назад от вертикали (KNEE 1.5 − THIGH)
	constexpr float SPREAD = 0.2f;      // колени врозь
	constexpr float BACK = 0.1f;        // корпус откинут на подушку угла
	constexpr float ARM_BACK = 0.75f;   // руки назад, на канаты
	constexpr float ARM_DOWN = 0.32f;   // и чуть вниз от горизонтали
	constexpr float FORE_DROOP = 0.45f; // перчатки свисают с каната
	constexpr float HEAD_DOWN = 0.12f;  // взгляд чуть вниз — слушает тренера
	// Сесть — после разворота в ринг (ядро доворачивает курс в углу), встать — по гонгу сразу.
	constexpr float SIT_DELAY = 0.35f;  // с после прихода в угол
	constexpr float SIT_RATE = 1.f / 0.55f;   // доля позы в секунду (садится ~0.55 с)
	constexpr float STAND_RATE = 1.f / 0.35f; // встаёт быстрее (выход по гонгу уже идёт)
	constexpr float SEATED_AT = 0.95f;  // «сел» (флаг для UI: панель перерыва, «Продолжить»)

	// Канаты угла (ringSize веба): верхний — 1.32 м, третий — 1.02 м; сидя плечи ≈ 1.05 м — перчатки на третьем.
	constexpr float ROPE_HALF_CM = 305.f;
	constexpr float ROPE_Z_CM = 102.f;
	constexpr float HAND_OVER_CM = 6.f;   // кисть чуть за осью каната (рука «перекинута» через канат)
	constexpr float HAND_ALONG_CM = 24.f; // вдоль каната — от проекции плеча (дальше от столба)

	// Нужен ли боец на стуле: перерыв (стадия Rest), дошёл до своего угла, не лежит.
	BOXINGUE_API bool WantsSit(const FFightSnapshot& S, int32 Fighter);
	// Куда положить кисти сидя (мир, см): на ближайший к каждой руке канат своего угла. Floor — центр настила ринга,
	// Corner 0 — красный (−X −Y), 1 — синий; At — место бойца, Fwd/Left — его курс. [0] — левая, [1] — правая.
	BOXINGUE_API void RopeHands(const FVector& Floor, int32 Corner, const FVector& At, const FVector& Left, FVector Out[2]);
}

// Состояние посадки одного бойца (игровой поток, ABoxerCharacter::ApplyFightState).
struct BOXINGUE_API FBoxerSitState
{
	float W = 0.f;          // вес позы 0..1 (линейный; в позе — smoothstep)
	float ArrivedFor = 0.f; // с с прихода в угол в перерыве
	bool bWant = false;

	void Update(bool bWantSit, float Dt);
	void Reset() { W = 0.f; ArrivedFor = 0.f; bWant = false; }
	bool IsSeated() const { return bWant && W >= BoxerSit::SEATED_AT; }
};

// Кадр позы для анимпотока (копируется в прокси видимого меша).
struct FBoxerSitFrame
{
	float W = 0.f;                          // smoothstep веса; 0 — поза не трогается
	FVector Fwd = FVector::ForwardVector;   // курс бойца (мир)
	FVector Left = -FVector::RightVector;
	float FloorZ = 0.f;                     // настил под бойцом (мир, см)
	bool bHands = false;                    // кисти — IK на канаты (HandTarget)
	FVector HandTarget[2] = {FVector::ZeroVector, FVector::ZeroVector};
	bool bMirror = false;                   // левша — та же поза (симметрична), флаг на будущее
};

// Применение позы (компонентное пространство видимого меша, после ретаргета и слоя «ощущения»).
class BOXINGUE_API FBoxerSitFx
{
public:
	void Apply(FCompactPose& Pose, const FBoxerSitFrame& Frame, const FTransform& CompToWorld);
	// Отладка: на сколько опущен таз (см) в последнем кадре.
	float LastDropCm = 0.f;

private:
	void Resolve(const FBoneContainer& Bones);
	uint16 Serial = MAX_uint16;
	const void* ContainerPtr = nullptr;
	FCompactPoseBoneIndex Pelvis = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Spine[3] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Neck = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Head = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Upper[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Lower[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Hand[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Thigh[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Calf[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Foot[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Ball[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
};
