#ifndef MIYUKI_INPUT_H
#define MIYUKI_INPUT_H
#include <miyuki.h>

namespace Miyuki {
	namespace GUI {
		std::optional<bool> GetBool(std::string&, bool initial);
		std::optional<int> GetInt(std::string&, int initial);
		std::optional<Float> GetFloat(std::string&, Float initial);
		std::optional<Vec3f> GetVec3f(std::string&, Vec3f initial);
		std::optional<Spectrum> GetSpectrum(std::string&, Spectrum initial);
		std::optional<Float> GetFloatClamped(const std::string&, Float initial, Float minVal, Float maxVal);
		std::optional<std::string> GetString(const std::string&, const std::string&);
	}
}
#endif