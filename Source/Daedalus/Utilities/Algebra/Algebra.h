#pragma once

#include "Constants.h"

namespace utils {
}

namespace std {
	template <typename T>
	void hashCombine(int64 & seed, const T & v) {
		std::hash<T> hasher;
		seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}
}