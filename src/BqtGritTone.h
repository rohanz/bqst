#pragma once

namespace bqt::gritTone
{
enum class Kind
{
    lowShelf,
    peak,
    highShelf
};

struct FilterSpec
{
    Kind kind;
    float hz, q, gainDb;
};
inline constexpr FilterSpec lowGuardPre{Kind::lowShelf, 95.f, 0.55f, -2.2f};
inline constexpr FilterSpec lowGuardPost{Kind::lowShelf, 95.f, 0.55f, 2.2f};
inline constexpr FilterSpec lowDrive{Kind::lowShelf, 165.f, 0.62f, 1.10f};
inline constexpr FilterSpec lowRestore{Kind::lowShelf, 165.f, 0.62f, -0.70f};
inline constexpr FilterSpec weight{Kind::peak, 245.f, 0.72f, 0.55f};
inline constexpr FilterSpec top{Kind::highShelf, 7800.f, 0.50f, -0.75f};

} // namespace bqt::gritTone
