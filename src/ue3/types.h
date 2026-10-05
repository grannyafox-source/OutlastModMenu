// Unreal Engine 3 value types and flag constants as laid out in memory by
// Outlast (UE3, 2013). Pointer-sized members follow the build's bitness.
#pragma once

#include <cmath>
#include <cstdint>

namespace omm::ue3 {

// Engine objects are treated as opaque memory. The aliases only document
// which kind of object a pointer is expected to reference.
struct UObject;
using UClass = UObject;
using UStruct = UObject;
using UField = UObject;
using UProperty = UObject;
using UFunction = UObject;

struct FName {
    int32_t Index = 0;
    int32_t Number = 0;  // stored as number + 1; 0 means "no suffix"
    bool operator==(const FName& o) const { return Index == o.Index && Number == o.Number; }
    bool operator!=(const FName& o) const { return !(*this == o); }
};

template <typename T>
struct TArray {
    T* Data = nullptr;
    int32_t Num = 0;
    int32_t Max = 0;
};

// FString is a TArray<TCHAR> with a trailing NUL counted in Num. TCHAR is
// UTF-16 on Windows.
using FString = TArray<char16_t>;

struct FVector {
    float X = 0, Y = 0, Z = 0;
    FVector() = default;
    FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}
    FVector operator+(const FVector& o) const { return {X + o.X, Y + o.Y, Z + o.Z}; }
    FVector operator-(const FVector& o) const { return {X - o.X, Y - o.Y, Z - o.Z}; }
    FVector operator*(float s) const { return {X * s, Y * s, Z * s}; }
    float Dot(const FVector& o) const { return X * o.X + Y * o.Y + Z * o.Z; }
    float Size() const { return std::sqrt(X * X + Y * Y + Z * Z); }
    float Size2D() const { return std::sqrt(X * X + Y * Y); }
};

// Rotators are 16.16 fixed point: 65536 units = 360 degrees.
struct FRotator {
    int32_t Pitch = 0, Yaw = 0, Roll = 0;
};

struct FVector2D {
    float X = 0, Y = 0;
};

struct FColor {  // UE3 stores BGRA on PC
    uint8_t B = 0, G = 0, R = 0, A = 255;
};

struct FLinearColor {
    float R = 0, G = 0, B = 0, A = 1;
};

struct FScriptDelegate {
    UObject* Object = nullptr;
    FName FunctionName;
};

constexpr float kRotToRad = 3.14159265358979f / 32768.0f;
constexpr float kRadToRot = 32768.0f / 3.14159265358979f;

inline FVector RotatorToVector(const FRotator& r) {
    float p = r.Pitch * kRotToRad, y = r.Yaw * kRotToRad;
    return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
}

// Property flags (CPF_*).
constexpr uint64_t CPF_Edit = 0x1;
constexpr uint64_t CPF_Const = 0x2;
constexpr uint64_t CPF_OptionalParm = 0x10;
constexpr uint64_t CPF_Net = 0x20;
constexpr uint64_t CPF_Parm = 0x80;
constexpr uint64_t CPF_OutParm = 0x100;
constexpr uint64_t CPF_ReturnParm = 0x400;
constexpr uint64_t CPF_Config = 0x4000;

// Function flags (FUNC_*).
constexpr uint32_t FUNC_Final = 0x1;
constexpr uint32_t FUNC_Defined = 0x2;
constexpr uint32_t FUNC_Net = 0x40;
constexpr uint32_t FUNC_Simulated = 0x100;
constexpr uint32_t FUNC_Exec = 0x200;
constexpr uint32_t FUNC_Native = 0x400;
constexpr uint32_t FUNC_Event = 0x800;
constexpr uint32_t FUNC_Static = 0x2000;

// EPhysics values used by the mod.
enum EPhysics : uint8_t {
    PHYS_None = 0,
    PHYS_Walking = 1,
    PHYS_Falling = 2,
    PHYS_Swimming = 3,
    PHYS_Flying = 4,
    PHYS_Rotating = 5,
    PHYS_Projectile = 6,
    PHYS_Interpolating = 7,
    PHYS_Spider = 8,
    PHYS_Ladder = 9,
    PHYS_RigidBody = 10,
    PHYS_SoftBody = 11,
    PHYS_NavMeshWalking = 12,
    PHYS_Unused = 13,
    PHYS_Custom = 14,
};

}  // namespace omm::ue3
