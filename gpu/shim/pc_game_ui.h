// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
union SDL_Event;
struct ImFont;
namespace BbGameUi {
void SetMenuFont(ImFont* font);
bool HandleEvent(const SDL_Event& event);
bool Open();
bool CapturesInput();
bool ChoosingControls();
void Close();
void Draw(bool other_menu);
}
