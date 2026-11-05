#pragma once
// Css.hpp -- the window's colours, in one place. The overlay colours (Cairo)
// and the stylesheet's (CSS) are the same palette; change both together.
namespace optik::css {

struct Rgba { double r, g, b, a; };

// Area kinds, FineReader's convention: text green, picture red, heading blue.
inline constexpr Rgba kText    {0.30, 0.69, 0.31, 1.0};
inline constexpr Rgba kHeading {0.26, 0.52, 0.96, 1.0};
inline constexpr Rgba kPicture {0.90, 0.30, 0.30, 1.0};
inline constexpr Rgba kChrome  {0.62, 0.52, 0.82, 1.0};   // running heads, folios
inline constexpr Rgba kIgnore  {0.55, 0.55, 0.55, 1.0};

// "Look here": the uncertain word, amber.
inline constexpr Rgba kUncertain {0.949, 0.757, 0.306, 0.38};
inline constexpr Rgba kCurrent   {0.949, 0.757, 0.306, 1.00};

void init();   // install the stylesheet on the default display

}  // namespace optik::css
