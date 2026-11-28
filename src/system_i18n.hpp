// The language of the built-in system screens (System > ...). Their texts are written in
// English in the code; here is what they are in Japanese. A text without an entry is shown as
// it is, so a missing entry is never worse than English.
//
// Which language: "system_language" of project.json ("ja" / "en"), else the language the
// project is shown in ("texts": its current language), else - a project without languages -
// Japanese when its title is written in Japanese. Japanese is only used when the project's
// font has the characters (a project without a Japanese font would show empty boxes).
#pragma once
#include <string>
#include <vector>

namespace hml {

void set_system_language(const std::string& lang);   // a code of system_languages(); anything else = English
// The languages the system screens have: "en", "ja", "de".
const std::vector<std::string>& system_languages();
// Letters the project's font must have for this language to be used ("" = none: English).
std::string system_language_sample(const std::string& lang);
const std::string& system_language();

// The text in the language of the system screens: an entry of the table, a pattern with values
// in it ("Driver: plc" -> "ドライバー: plc"), or the text itself.
std::string sys_text(const std::string& english);
// Is there a translation for this text (the table or a pattern)? For the test that looks for
// texts that were forgotten.
bool sys_text_known(const std::string& english, const std::string& lang = "ja");

} // namespace hml
