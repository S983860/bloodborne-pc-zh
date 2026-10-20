// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the in-game menu's text in the languages other than English and Russian
// (bbport_menu_i18n.cpp), for MenuText (bbport_settings.h).
#pragma once

#include "bbport_settings.h"

namespace BbSettings {

/// The translation of a menu text, looked up by its English text; the English text itself when the
/// table has none (or for English and Russian, which MenuText answers itself). Static storage.
const char* MenuTranslation(MenuLanguage language, const char* english);

} // namespace BbSettings
