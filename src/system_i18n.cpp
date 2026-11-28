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

const Language& chinese()
{
    static const Language l = {
        {
#include "system_i18n_zh.inc"
        },
        {
        { std::regex(R"(^Driver: (.*)$)"), "驱动：$1" },
        { std::regex(R"(^Receiving, (\d+) designer\(s\)$)"), "接收中，设计器 $1 个" },
        { std::regex(R"(^(\d+) accepted designer\(s\)$)"), "已接受的设计器：$1 个" },
        { std::regex(R"(^(\d+) key\(s\)$)"), "密钥 $1 个" },
        { std::regex(R"(^(\d+) registered key\(s\) in (.*)$)"), "已登记的密钥 $1 个（$2）" },
        { std::regex(R"(^\.\.\. and (\d+) more \(not shown\)$)"), "… 另有 $1 个（未显示）" },
        { std::regex(R"(^On for (\d+):(\d+) more: deploy from the new designer now\.$)"), "还将开启 $1:$2：请现在从新的设计器部署。" },
        { std::regex(R"(^On, port (\d+) at ([^\n]*)\nThis device: (.*)$)"), "开，端口 $1，地址 $2\n本机：$3" },
        { std::regex(R"(^(\d+) user\(s\), edited on this device$)"), "用户 $1 个（在本机编辑）" },
        { std::regex(R"(^(\d+) user\(s\), as defined in the project$)"), "用户 $1 个（按项目定义）" },
        { std::regex(R"(^Port (\d+): other machines can connect(.*)$)"), "端口 $1：其他设备可以连接$2" },
        { std::regex(R"(^Port (\d+): this device only \(no password set\)(.*)$)"), "端口 $1：仅限本机（未设置密码）$2" },
        { std::regex(R"(^(\d+) s$)"), "$1 秒" },
        { std::regex(R"(^Next (.*)$)"), "下次 $1" },
        { std::regex(R"(^Saved: (.*)  \(off\)$)"), "已保存：$1（关）" },
        { std::regex(R"(^Saved: (.*)$)"), "已保存：$1" },
        { std::regex(R"(^(\d\d:\d\d)  weekdays$)"), "$1  工作日" },
        { std::regex(R"(^(\d\d:\d\d)  daily$)"), "$1  每天" },
        { std::regex(R"(^(\d\d:\d\d)  weekends$)"), "$1  周末" },
        { std::regex(R"(^'(.*)' changed\.$)"), "已更改“$1”。" },
        { std::regex(R"(^'(.*)' added\.$)"), "已添加“$1”。" },
        { std::regex(R"(^'(.*)' deleted\.$)"), "已删除“$1”。" },
        { std::regex(R"(^'(.*)' is the last user who can open the system settings$)"), "“$1”是最后一个可以打开系统设置的用户" },
        { std::regex(R"(^(.*) = (.*)  \(kept inside the limits of the tag\)$)"), "$1 = $2（输入的值超出变量范围，已限制在范围内）" },
        { std::regex(R"re(^"(.*)" is not a value for a tag of type (.*)\.$)re"), "“$1”不是 $2 类型变量的值。" },
        // the login screen
        { std::regex(R"(^Logged in, but this needs (.*) or higher\.$)"), "已登录，但需要 $1 或更高级别。" },
        { std::regex(R"(^(.*) or higher is needed\.$)"), "需要 $1 或更高级别。" },
        { std::regex(R"(^Locked\. Try again in (\d+) s\.$)"), "已锁定。请在 $1 秒后重试。" },
        { std::regex(R"(^Wrong PIN \((\d+) more tries\)\.$)"), "PIN 错误（还可尝试 $1 次）。" },
        // what went wrong, with the reason as it came
        { std::regex(R"(^Not installed: ([\s\S]*)$)"), "未安装：$1" },
        { std::regex(R"(^Not changed: ([\s\S]*)$)"), "未更改：$1" },
        { std::regex(R"(^Not removed: ([\s\S]*)$)"), "未移除：$1" },
        { std::regex(R"(^Not written: ([\s\S]*)$)"), "未写入：$1" },
        { std::regex(R"(^Backup failed: ([\s\S]*)$)"), "备份失败：$1" },
        { std::regex(R"(^Backup written to ([\s\S]*)$)"), "备份已写入 $1" },
        { std::regex(R"(^A project was received but not installed: ([\s\S]*)$)"), "已接收项目，但未安装：$1" },
        },
    };
    return l;
}

const Language* language(const std::string& code)
{
    if(code == "ja") return &japanese();
    if(code == "de") return &german();
    if(code == "zh") return &chinese();
    return nullptr;                                  // English: the texts as they are
}

} // namespace

const std::vector<std::string>& system_languages()
{
    static const std::vector<std::string> codes = { "en", "ja", "de", "zh" };
    return codes;
}

void set_system_language(const std::string& lang) { g_lang = language(lang) ? lang : "en"; }
const std::string& system_language() { return g_lang; }

std::string system_language_sample(const std::string& lang)
{
    // letters the font of the project must have for the language to be used (else: English)
    if(lang == "ja") return "システム設定を開く適用戻る";
    if(lang == "de") return "äöüÄÖÜß";
    if(lang == "zh") return "系统设置应用关闭确定";
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
