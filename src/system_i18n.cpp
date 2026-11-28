#include "system_i18n.hpp"

#include <map>
#include <regex>
#include <vector>

// The texts of the system screens are written in English in the code. Each other language is a
// table (whole texts, system_i18n_<code>.inc) and a list of patterns (texts with values in
// them). To add a language: a table, a list, a line in language() below, and its code in
// system_languages() - tests/test_system_i18n.cpp then lists what has no entry yet.
namespace hml {
namespace {

std::string g_lang = "en";

struct Pattern { std::regex re; std::string to; };
struct Language {
    std::map<std::string, std::string> table;      // English -> the language, whole texts
    std::vector<Pattern>               patterns;   // texts with values in them
};

const Language& japanese()
{
    static const Language l = {
        {
#include "system_i18n_ja.inc"
        },
        {
        { std::regex(R"(^Driver: (.*)$)"), "ドライバー: $1" },
        { std::regex(R"(^Receiving, (\d+) designer\(s\)$)"), "受け取り中、デザイナー $1 件" },
        { std::regex(R"(^(\d+) accepted designer\(s\)$)"), "承認済みのデザイナー: $1 件" },
        { std::regex(R"(^(\d+) key\(s\)$)"), "鍵 $1 個" },
        { std::regex(R"(^(\d+) registered key\(s\) in (.*)$)"), "登録済みの鍵 $1 個（$2）" },
        { std::regex(R"(^\.\.\. and (\d+) more \(not shown\)$)"), "… ほか $1 件（表示していません）" },
        { std::regex(R"(^On for (\d+):(\d+) more: deploy from the new designer now\.$)"), "あと $1:$2 の間オン: 新しいデザイナーからデプロイしてください。" },
        { std::regex(R"(^On, port (\d+) at ([^\n]*)\nThis device: (.*)$)"), "オン（ポート $1、$2）\nこの装置: $3" },
        { std::regex(R"(^(\d+) user\(s\), edited on this device$)"), "ユーザー $1 人（この装置で編集）" },
        { std::regex(R"(^(\d+) user\(s\), as defined in the project$)"), "ユーザー $1 人（プロジェクトの定義）" },
        { std::regex(R"(^Port (\d+): other machines can connect(.*)$)"), "ポート $1: ほかの機器からつなげます$2" },
        { std::regex(R"(^Port (\d+): this device only \(no password set\)(.*)$)"), "ポート $1: この装置からだけ（パスワード未設定）$2" },
        { std::regex(R"(^(\d+) s$)"), "$1 秒" },
        { std::regex(R"(^Next (.*)$)"), "次回 $1" },
        { std::regex(R"(^Saved: (.*)  \(off\)$)"), "保存しました: $1（オフ）" },
        { std::regex(R"(^Saved: (.*)$)"), "保存しました: $1" },
        { std::regex(R"(^(\d\d:\d\d)  weekdays$)"), "$1  平日" },
        { std::regex(R"(^(\d\d:\d\d)  daily$)"), "$1  毎日" },
        { std::regex(R"(^(\d\d:\d\d)  weekends$)"), "$1  土日" },
        { std::regex(R"(^'(.*)' changed\.$)"), "「$1」を変更しました。" },
        { std::regex(R"(^'(.*)' added\.$)"), "「$1」を追加しました。" },
        { std::regex(R"(^'(.*)' deleted\.$)"), "「$1」を削除しました。" },
        { std::regex(R"(^'(.*)' is the last user who can open the system settings$)"), "「$1」は、システム設定を開ける最後のユーザーです" },
        { std::regex(R"(^(.*) = (.*)  \(kept inside the limits of the tag\)$)"), "$1 = $2（入れた値はタグの範囲の外でした。範囲内の値にしました）" },
        { std::regex(R"re(^"(.*)" is not a value for a tag of type (.*)\.$)re"), "「$1」は、$2 型のタグの値ではありません。" },
        // the login screen
        { std::regex(R"(^Logged in, but this needs (.*) or higher\.$)"), "ログインしましたが、$1 以上のレベルが必要です。" },
        { std::regex(R"(^(.*) or higher is needed\.$)"), "$1 以上のレベルが必要です。" },
        { std::regex(R"(^Locked\. Try again in (\d+) s\.$)"), "ロック中です。$1 秒後にもう一度試してください。" },
        { std::regex(R"(^Wrong PIN \((\d+) more tries\)\.$)"), "PIN が違います（あと $1 回）。" },
        // what went wrong, with the reason as it came
        { std::regex(R"(^Not installed: ([\s\S]*)$)"), "入れ替えられませんでした: $1" },
        { std::regex(R"(^Not changed: ([\s\S]*)$)"), "変更できませんでした: $1" },
        { std::regex(R"(^Not removed: ([\s\S]*)$)"), "削除できませんでした: $1" },
        { std::regex(R"(^Not written: ([\s\S]*)$)"), "書き込めませんでした: $1" },
        { std::regex(R"(^Backup failed: ([\s\S]*)$)"), "バックアップに失敗しました: $1" },
        { std::regex(R"(^Backup written to ([\s\S]*)$)"), "バックアップを書き出しました: $1" },
        { std::regex(R"(^A project was received but not installed: ([\s\S]*)$)"), "プロジェクトを受け取りましたが、入れ替えられませんでした: $1" },
        },
    };
    return l;
}

const Language& german()
{
    static const Language l = {
        {
#include "system_i18n_de.inc"
        },
        {
        { std::regex(R"(^Driver: (.*)$)"), "Treiber: $1" },
        { std::regex(R"(^Receiving, (\d+) designer\(s\)$)"), "Empfang ein, $1 Designer" },
        { std::regex(R"(^(\d+) accepted designer\(s\)$)"), "Angenommene Designer: $1" },
        { std::regex(R"(^(\d+) key\(s\)$)"), "$1 Schlüssel" },
        { std::regex(R"(^(\d+) registered key\(s\) in (.*)$)"), "$1 eingetragene Schlüssel in $2" },
        { std::regex(R"(^\.\.\. and (\d+) more \(not shown\)$)"), "... und $1 weitere (nicht angezeigt)" },
        { std::regex(R"(^On for (\d+):(\d+) more: deploy from the new designer now\.$)"), "Noch $1:$2 ein: Jetzt vom neuen Designer übertragen." },
        { std::regex(R"(^On, port (\d+) at ([^\n]*)\nThis device: (.*)$)"), "Ein, Port $1 auf $2\nDieses Gerät: $3" },
        { std::regex(R"(^(\d+) user\(s\), edited on this device$)"), "$1 Benutzer, auf diesem Gerät bearbeitet" },
        { std::regex(R"(^(\d+) user\(s\), as defined in the project$)"), "$1 Benutzer, wie im Projekt festgelegt" },
        { std::regex(R"(^Port (\d+): other machines can connect(.*)$)"), "Port $1: Andere Geräte können sich verbinden$2" },
        { std::regex(R"(^Port (\d+): this device only \(no password set\)(.*)$)"), "Port $1: Nur dieses Gerät (kein Passwort gesetzt)$2" },
        { std::regex(R"(^(\d+) s$)"), "$1 s" },
        { std::regex(R"(^Next (.*)$)"), "Nächster $1" },
        { std::regex(R"(^Saved: (.*)  \(off\)$)"), "Gespeichert: $1 (aus)" },
        { std::regex(R"(^Saved: (.*)$)"), "Gespeichert: $1" },
        { std::regex(R"(^(\d\d:\d\d)  weekdays$)"), "$1  werktags" },
        { std::regex(R"(^(\d\d:\d\d)  daily$)"), "$1  täglich" },
        { std::regex(R"(^(\d\d:\d\d)  weekends$)"), "$1  Wochenende" },
        { std::regex(R"(^'(.*)' changed\.$)"), "„$1“ geändert." },
        { std::regex(R"(^'(.*)' added\.$)"), "„$1“ hinzugefügt." },
        { std::regex(R"(^'(.*)' deleted\.$)"), "„$1“ gelöscht." },
        { std::regex(R"(^'(.*)' is the last user who can open the system settings$)"), "„$1“ ist der letzte Benutzer, der die Systemeinstellungen öffnen kann" },
        { std::regex(R"(^(.*) = (.*)  \(kept inside the limits of the tag\)$)"), "$1 = $2 (der eingegebene Wert lag außerhalb der Grenzen und wurde begrenzt)" },
        { std::regex(R"re(^"(.*)" is not a value for a tag of type (.*)\.$)re"), "„$1“ ist kein Wert für eine Variable vom Typ $2." },
        // the login screen
        { std::regex(R"(^Logged in, but this needs (.*) or higher\.$)"), "Angemeldet, aber hierfür ist $1 oder höher nötig." },
        { std::regex(R"(^(.*) or higher is needed\.$)"), "$1 oder höher ist nötig." },
        { std::regex(R"(^Locked\. Try again in (\d+) s\.$)"), "Gesperrt. In $1 s erneut versuchen." },
        { std::regex(R"(^Wrong PIN \((\d+) more tries\)\.$)"), "Falsche PIN (noch $1 Versuche)." },
        // what went wrong, with the reason as it came
        { std::regex(R"(^Not installed: ([\s\S]*)$)"), "Nicht installiert: $1" },
        { std::regex(R"(^Not changed: ([\s\S]*)$)"), "Nicht geändert: $1" },
        { std::regex(R"(^Not removed: ([\s\S]*)$)"), "Nicht entfernt: $1" },
        { std::regex(R"(^Not written: ([\s\S]*)$)"), "Nicht geschrieben: $1" },
        { std::regex(R"(^Backup failed: ([\s\S]*)$)"), "Sicherung fehlgeschlagen: $1" },
        { std::regex(R"(^Backup written to ([\s\S]*)$)"), "Sicherung geschrieben nach $1" },
        { std::regex(R"(^A project was received but not installed: ([\s\S]*)$)"), "Ein Projekt wurde empfangen, aber nicht installiert: $1" },
        },
    };
    return l;
}

const Language* language(const std::string& code)
{
    if(code == "ja") return &japanese();
    if(code == "de") return &german();
    return nullptr;                                  // English: the texts as they are
}

} // namespace

const std::vector<std::string>& system_languages()
{
    static const std::vector<std::string> codes = { "en", "ja", "de" };
    return codes;
}

void set_system_language(const std::string& lang) { g_lang = language(lang) ? lang : "en"; }
const std::string& system_language() { return g_lang; }

std::string system_language_sample(const std::string& lang)
{
    // letters the font of the project must have for the language to be used (else: English)
    if(lang == "ja") return "システム設定を開く適用戻る";
    if(lang == "de") return "äöüÄÖÜß";
    return "";
}

bool sys_text_known(const std::string& english, const std::string& lang)
{
    const Language* l = language(lang);
    if(!l) return true;
    if(l->table.count(english)) return true;
    for(const Pattern& p : l->patterns) if(std::regex_match(english, p.re)) return true;
    return false;
}

std::string sys_text(const std::string& english)
{
    const Language* l = language(g_lang);
    if(!l || english.empty()) return english;
    auto it = l->table.find(english);
    if(it != l->table.end()) return it->second;
    for(const Pattern& p : l->patterns) if(std::regex_match(english, p.re)) return std::regex_replace(english, p.re, p.to);
    return english;
}

} // namespace hml
