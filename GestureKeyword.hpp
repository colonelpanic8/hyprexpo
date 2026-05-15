#pragma once

#include <hyprlang.hpp>

#include <string>

extern const std::string KEYWORD_EXPO_GESTURE;

Hyprlang::CParseResult   expoGestureKeyword(const char* LHS, const char* RHS);
void                     setGestureKeywordUnloading(bool unloading);
