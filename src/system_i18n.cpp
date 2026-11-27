#include "system_i18n.hpp"

#include <map>
#include <regex>
#include <vector>

namespace hml {
namespace {

std::string g_lang = "en";

// English -> Japanese, whole texts
const std::map<std::string, std::string>& table()
{
    static const std::map<std::string, std::string> t = {
#include "system_i18n_ja.inc"
    };
    return t;
}

// texts with values in them
struct Pattern { std::regex re; std::string to; };
const std::vector<Pattern>& patterns()
{
    static const std::vector<Pattern> p = {
        { std::regex(R"(^Driver: (.*)$)"), "ドライバ: $1" },
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
    };
    return p;
}

} // namespace

void set_system_language(const std::string& lang) { g_lang = lang == "ja" ? "ja" : "en"; }
const std::string& system_language() { return g_lang; }

bool sys_text_known(const std::string& english)
{
    if(table().count(english)) return true;
    for(const Pattern& p : patterns()) if(std::regex_match(english, p.re)) return true;
    return false;
}

std::string sys_text(const std::string& english)
{
    if(g_lang != "ja" || english.empty()) return english;
    auto it = table().find(english);
    if(it != table().end()) return it->second;
    for(const Pattern& p : patterns()) if(std::regex_match(english, p.re)) return std::regex_replace(english, p.re, p.to);
    return english;
}

} // namespace hml
