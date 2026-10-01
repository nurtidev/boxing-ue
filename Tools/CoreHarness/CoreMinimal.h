// Заглушка CoreMinimal для проверки ядра вне UE (без std-заголовков: в системе нет STL/SDK).
#pragma once
#include <cmath>
typedef signed char int8;
typedef unsigned char uint8;
typedef int int32;
typedef unsigned int uint32;
typedef long long int64;
#ifndef BOXINGUE_API
#define BOXINGUE_API
#endif

struct FMath
{
	template <typename T> static T Max(const T A, const T B) { return A >= B ? A : B; }
	template <typename T> static T Min(const T A, const T B) { return A <= B ? A : B; }
	static double Abs(double A) { return A < 0 ? -A : A; }
	static int32 Abs(int32 A) { return A < 0 ? -A : A; }
	static double Sqrt(double A) { return std::sqrt(A); }
	static double Pow(double A, double B) { return std::pow(A, B); }
	static double Sin(double A) { return std::sin(A); }
	static double Atan2(double Y, double X) { return std::atan2(Y, X); }
	static double RoundToDouble(double A) { return std::floor(A + 0.5); }
};

template <typename T>
class TArray
{
public:
	TArray() {}
	TArray(const TArray& O) { CopyFrom(O); }
	TArray& operator=(const TArray& O) { if (this != &O) { delete[] Data; Data = nullptr; N = Cap = 0; CopyFrom(O); } return *this; }
	~TArray() { delete[] Data; }
	int32 Num() const { return N; }
	int32 Add(const T& V) { Grow(N + 1); Data[N] = V; return N++; }
	T& operator[](int32 I) { return Data[I]; }
	const T& operator[](int32 I) const { return Data[I]; }
	void RemoveAt(int32 I) { for (int32 K = I; K + 1 < N; ++K) Data[K] = Data[K + 1]; --N; }
	void Reset() { N = 0; }
	void Empty() { N = 0; }
	template <typename P> int32 RemoveAll(const P& Pred)
	{
		int32 W = 0;
		for (int32 R = 0; R < N; ++R) if (!Pred(Data[R])) Data[W++] = Data[R];
		const int32 Removed = N - W; N = W; return Removed;
	}
	T* begin() { return Data; }
	T* end() { return Data + N; }
private:
	void Grow(int32 Need)
	{
		if (Need <= Cap) return;
		int32 NewCap = Cap ? Cap * 2 : 8; while (NewCap < Need) NewCap *= 2;
		T* NewData = new T[NewCap];
		for (int32 K = 0; K < N; ++K) NewData[K] = Data[K];
		delete[] Data; Data = NewData; Cap = NewCap;
	}
	void CopyFrom(const TArray& O) { Grow(O.N); for (int32 K = 0; K < O.N; ++K) Data[K] = O.Data[K]; N = O.N; }
	T* Data = nullptr;
	int32 N = 0;
	int32 Cap = 0;
};
