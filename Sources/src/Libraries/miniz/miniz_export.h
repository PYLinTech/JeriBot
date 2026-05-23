#pragma once

#ifndef MINIZ_EXPORT
#ifdef MINIZ_SHARED
#ifdef _WIN32
#ifdef MINIZ_BUILD
#define MINIZ_EXPORT __declspec(dllexport)
#else
#define MINIZ_EXPORT __declspec(dllimport)
#endif
#else
#define MINIZ_EXPORT __attribute__((visibility("default")))
#endif
#else
#define MINIZ_EXPORT
#endif
#endif