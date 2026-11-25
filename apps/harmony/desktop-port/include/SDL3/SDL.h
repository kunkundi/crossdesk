/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-14
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SDL_H_
#define _SDL_H_

// The desktop's small SDL service surface, implemented with Harmony APIs.
// This header is private to the Harmony target; desktop SDL builds are unchanged.
#include <cstdint>
#include <cstdlib>
using Uint8 = uint8_t;
using SDL_DisplayID = uint32_t;
struct SDL_Point { int x, y; };
struct SDL_Rect { int x, y, w, h; };
struct SDL_DisplayMode { int w, h; };
struct SDL_Event { uint32_t type; };
struct SDL_AudioSpec { int format, channels, freq; };
struct SDL_AudioStream;
constexpr uint32_t SDL_INIT_VIDEO = 1, SDL_INIT_AUDIO = 2;
constexpr uint32_t SDL_EVENT_FIRST = 0, SDL_EVENT_LAST = UINT32_MAX;
constexpr uint32_t SDL_EVENT_QUIT = 0x100, SDL_EVENT_CLIPBOARD_UPDATE = 0x900;
constexpr int SDL_GETEVENT = 2, SDL_AUDIO_S16 = 0x8010;
constexpr uint32_t SDL_BUTTON_LMASK = 1, SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK = 0;
bool SDL_Init(uint32_t);
bool SDL_InitSubSystem(uint32_t);
void SDL_Quit();
void SDL_QuitSubSystem(uint32_t);
const char* SDL_GetError();
uint64_t SDL_GetTicks();
void SDL_Delay(uint32_t);
bool SDL_PollEvent(SDL_Event*);
int SDL_PeepEvents(SDL_Event*, int, int, uint32_t, uint32_t);
bool SDL_PushEvent(SDL_Event*);
char* SDL_GetClipboardText();
bool SDL_SetClipboardText(const char*);
bool SDL_HasClipboardText();
inline void SDL_free(void* p) { std::free(p); }
const char* SDL_GetCurrentVideoDriver();
const SDL_DisplayMode* SDL_GetCurrentDisplayMode(SDL_DisplayID);
SDL_DisplayID SDL_GetPrimaryDisplay();
float SDL_GetDisplayContentScale(SDL_DisplayID);
bool SDL_EnableScreenSaver();
bool SDL_DisableScreenSaver();
SDL_DisplayID SDL_GetDisplayForPoint(const SDL_Point*);
SDL_DisplayID* SDL_GetDisplays(int*);
bool SDL_GetDisplayUsableBounds(SDL_DisplayID, SDL_Rect*);
uint32_t SDL_GetGlobalMouseState(float*, float*);
bool SDL_OpenURL(const char*);
uint32_t SDL_OpenAudioDevice(uint32_t, const SDL_AudioSpec*);
bool SDL_ResumeAudioDevice(uint32_t);
void SDL_CloseAudioDevice(uint32_t);
SDL_AudioStream* SDL_CreateAudioStream(const SDL_AudioSpec*, const SDL_AudioSpec*);
void SDL_DestroyAudioStream(SDL_AudioStream*);
bool SDL_BindAudioStream(uint32_t, SDL_AudioStream*);
bool SDL_PutAudioStreamData(SDL_AudioStream*, const void*, int);

#endif
