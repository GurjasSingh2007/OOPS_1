// Pro79 "Stellar App" - C++ port of the React Native (Expo) app.
//
// Mapping from the original project:
//   App.js (Stack.Navigator)  -> Navigator class (a stack of screens)
//   screens/Home.js           -> HomeScreen
//   screens/StarMap.js        -> StarMapScreen   (WebView -> opens URL in your browser)
//   screens/DailyPic.js       -> DailyPicScreen  (axios  -> `curl`, Linking -> browser)
//   screens/SpaceCraft.js     -> SpaceCraftScreen
//
// Console app, standard C++17, no external libraries.
// Internet features use the `curl` command (built into Windows 10+, macOS, most Linux).

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <windows.h>
  #define POPEN  _popen
  #define PCLOSE _pclose
#else
  #define POPEN  popen
  #define PCLOSE pclose
#endif

using namespace std;

// ===================== NASA API settings =====================
// Key taken from the original DailyPic.js. It was public on GitHub, so you should
// get a fresh free key at https://api.nasa.gov and paste it here (or "DEMO_KEY").
static const string NASA_API_KEY = "COGdtQeIWk1mbziFVQgpfK3JfP4dBSAlQg8imDGw";

// ===================== Helpers =====================
static void clearScreen() { cout << "\033[2J\033[H" << flush; }

static void setupConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | 0x0004); // ANSI escapes
#endif
}

static string wrap(const string& text, size_t width, const string& indent) {
    istringstream words(text);
    string word, line = indent, out;
    bool lineHasWord = false;
    while (words >> word) {
        if (lineHasWord && line.size() + 1 + word.size() > width) {
            out += line + "\n";
            line = indent;
            lineHasWord = false;
        }
        if (lineHasWord) line += " ";
        line += word;
        lineHasWord = true;
    }
    return out + line + "\n";
}

static string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Only allow URLs that are safe to hand to the OS shell.
static bool safeUrl(const string& url) {
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return false;
    for (char c : url)
        if (string("\"'`$\\^|<>; \r\n%").find(c) != string::npos) return false;
    return true;
}

static bool openInBrowser(const string& url) {
    if (!safeUrl(url)) return false;
#ifdef _WIN32
    string cmd = "start \"\" \"" + url + "\"";
#elif __APPLE__
    string cmd = "open '" + url + "' >/dev/null 2>&1";
#else
    string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
#endif
    return system(cmd.c_str()) == 0;
}

// HTTP GET using curl (replacement for axios.get). Returns false on failure.
static bool httpGet(const string& url, string& body, string& error) {
    if (!safeUrl(url)) { error = "Unsafe URL"; return false; }
    string cmd = "curl -s -S -L --max-time 20 \"" + url + "\"";
#ifndef _WIN32
    cmd += " 2>&1";
#endif
    FILE* pipe = POPEN(cmd.c_str(), "r");
    if (!pipe) { error = "Could not run curl. Is curl installed?"; return false; }
    char buf[4096];
    body.clear();
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, pipe)) > 0) body.append(buf, n);
    int rc = PCLOSE(pipe);
    if (body.empty() || rc != 0) {
        error = body.empty() ? "No response (check your internet connection / curl)" : trim(body);
        return false;
    }
    return true;
}

// ===================== Tiny JSON reader =====================
// Reads the top-level keys of a JSON object. String values are unescaped;
// numbers / nested objects are stored as raw text. Enough for the APOD response.
static void appendUtf8(string& out, unsigned cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

static bool readJsonString(const string& s, size_t& i, string& out) {
    out.clear();
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char e = s[++i];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': break;
                case 'u':
                    if (i + 4 < s.size()) {
                        appendUtf8(out, (unsigned)strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16));
                        i += 4;
                    }
                    break;
                default: out += e;     // \" \\ \/ etc.
            }
        } else out += s[i];
        ++i;
    }
    if (i >= s.size()) return false;
    ++i; // closing quote
    return true;
}

static map<string, string> parseJsonObject(const string& s) {
    map<string, string> result;
    size_t i = s.find('{');
    if (i == string::npos) return result;
    ++i;
    while (i < s.size()) {
        while (i < s.size() && (isspace((unsigned char)s[i]) || s[i] == ',')) ++i;
        if (i >= s.size() || s[i] == '}') break;
        string key, val;
        if (!readJsonString(s, i, key)) break;
        while (i < s.size() && (isspace((unsigned char)s[i]) || s[i] == ':')) ++i;
        if (i >= s.size()) break;
        if (s[i] == '"') {
            if (!readJsonString(s, i, val)) break;
        } else {
            size_t start = i;
            int depth = 0;
            bool inStr = false;
            while (i < s.size()) {
                char c = s[i];
                if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; }
                else if (c == '"') inStr = true;
                else if (c == '{' || c == '[') ++depth;
                else if (c == '}' || c == ']') { if (depth == 0) break; --depth; }
                else if (c == ',' && depth == 0) break;
                ++i;
            }
            val = trim(s.substr(start, i - start));
        }
        result[key] = val;
    }
    return result;
}

// ===================== Screen framework =====================
class Navigator;

class Screen {
public:
    virtual ~Screen() = default;
    virtual void render() const = 0;
    virtual void handleInput(const string& in, Navigator& nav) = 0;
};

// Equivalent of createStackNavigator(): navigate() pushes, goBack() pops.
class Navigator {
public:
    void navigate(unique_ptr<Screen> s) { stack_.push_back(move(s)); }
    void goBack() { if (stack_.size() > 1) stack_.pop_back(); }
    void quit() { stack_.clear(); }
    bool empty() const { return stack_.empty(); }
    Screen& current() { return *stack_.back(); }
private:
    vector<unique_ptr<Screen>> stack_;
};

static string prompt(const string& label) {
    cout << label << flush;
    string line;
    getline(cin, line);
    return trim(line);
}

// ===================== SpaceCraft (placeholder, as in the original) =====================
class SpaceCraftScreen : public Screen {
public:
    void render() const override {
        cout << "\n\n\n        ==============================\n"
                  << "          SpaceCraft Screen!\n"
                  << "        ==============================\n\n  [b] Back\n";
    }
    void handleInput(const string& in, Navigator& nav) override { if (in == "b") nav.goBack(); }
};

// ===================== Star Map =====================
class StarMapScreen : public Screen {
public:
    void render() const override {
        cout << "\n        ***  S T A R   M A P  ***\n\n";
        cout << "   Longitude: " << (longitude_.empty() ? "(not set)" : longitude_) << "\n";
        cout << "   Latitude : " << (latitude_.empty()  ? "(not set)" : latitude_)  << "\n\n";
        cout << "   Sky map link:\n" << wrap(url(), 78, "   ") << "\n";
        if (!message_.empty()) cout << "   " << message_ << "\n\n";
        cout << "   [1] Enter longitude\n"
                  << "   [2] Enter latitude\n"
                  << "   [o] Open live sky map in browser\n"
                  << "   [b] Back\n";
    }
    void handleInput(const string& in, Navigator& nav) override {
        message_.clear();
        if (in == "b") nav.goBack();
        else if (in == "1") setCoord("Enter your longitude: ", longitude_);
        else if (in == "2") setCoord("Enter your latitude: ", latitude_);
        else if (in == "o") message_ = openInBrowser(url()) ? "Opening in your browser..." : "Could not open the browser.";
    }
private:
    string longitude_, latitude_, message_;

    void setCoord(const string& label, string& target) {
        string v = prompt(label);
        for (char c : v)
            if (!(isdigit((unsigned char)c) || c == '-' || c == '+' || c == '.')) {
                message_ = "Invalid number. Use digits, '-' and '.' only (e.g. -73.98).";
                return;
            }
        target = v;
    }
    string url() const {
        return "https://virtualsky.lco.global/embed/index.html?longitude=" + longitude_ +
               "&latitude=" + latitude_ +
               "&constellations=true&constellationlabels=true&showstarlabels=true"
               "&gridlines_az=true&live=true&projection=stereo&showdate=false&showposition=false";
    }
};

// ===================== Daily Picture (NASA APOD) =====================
class DailyPicScreen : public Screen {
public:
    DailyPicScreen() { load(); }

    void render() const override {
        cout << "\n        ***  D A I L Y   P I C  ***\n\n";
        if (!error_.empty()) {
            cout << wrap("Error: " + error_, 78, "   ") << "\n";
        } else {
            cout << "   " << title_ << "\n";
            cout << "   " << string(title_.size() > 74 ? 74 : title_.size(), '-') << "\n";
            if (!date_.empty()) cout << "   Date: " << date_ << "\n";
            if (!mediaType_.empty()) cout << "   Type: " << mediaType_ << "\n";
            cout << "\n" << wrap(explanation_, 78, "   ") << "\n";
            cout << "   Link: " << url_ << "\n\n";
        }
        if (!message_.empty()) cout << "   " << message_ << "\n\n";
        cout << "   [o] Open picture/video in browser\n"
                  << "   [r] Reload\n"
                  << "   [b] Back\n";
    }
    void handleInput(const string& in, Navigator& nav) override {
        message_.clear();
        if (in == "b") nav.goBack();
        else if (in == "r") load();
        else if (in == "o") {
            if (url_.empty()) message_ = "Nothing to open yet.";
            else message_ = openInBrowser(url_) ? "Opening in your browser..." : "Could not open the browser.";
        }
    }
private:
    string title_, explanation_, url_, date_, mediaType_, error_, message_;

    void load() {
        cout << "\n   Loading..." << endl;   // the original showed "Loading"
        error_.clear(); title_.clear(); explanation_.clear(); url_.clear();
        string body, err;
        if (!httpGet("https://api.nasa.gov/planetary/apod?api_key=" + NASA_API_KEY, body, err)) {
            error_ = err;
            return;
        }
        auto j = parseJsonObject(body);
        if (!j.count("title") || !j.count("url")) {
            error_ = "NASA returned an unexpected response: " + trim(body).substr(0, 300);
            return;
        }
        title_ = j["title"];
        explanation_ = j.count("explanation") ? j["explanation"] : "";
        url_ = j["url"];
        date_ = j.count("date") ? j["date"] : "";
        mediaType_ = j.count("media_type") ? j["media_type"] : "";
    }
};

// ===================== Home =====================
class HomeScreen : public Screen {
public:
    void render() const override {
        cout << "\n              *  .  *   .   *\n"
                  << "            .    S T E L L A R   .\n"
                  << "              *     A P P    *  .\n\n"
                  << "   +----------------------------------+\n"
                  << "   |  [1]  Spacecrafts                |\n"
                  << "   +----------------------------------+\n"
                  << "   |  [2]  Star Map                   |\n"
                  << "   +----------------------------------+\n"
                  << "   |  [3]  Daily Pictures             |\n"
                  << "   +----------------------------------+\n\n"
                  << "   [q] Quit\n";
    }
    void handleInput(const string& in, Navigator& nav) override {
        if (in == "1")      nav.navigate(make_unique<SpaceCraftScreen>());
        else if (in == "2") nav.navigate(make_unique<StarMapScreen>());
        else if (in == "3") nav.navigate(make_unique<DailyPicScreen>());
        else if (in == "q") nav.quit();
    }
};

#ifndef STELLAR_NO_MAIN
int main() {
    setupConsole();
    Navigator nav;
    nav.navigate(make_unique<HomeScreen>());   // initialRouteName="Home"

    while (!nav.empty()) {
        clearScreen();
        nav.current().render();
        cout << "\n> " << flush;
        string input;
        if (!getline(cin, input)) break;
        nav.current().handleInput(trim(input), nav);
    }
    clearScreen();
    cout << "Goodbye!\n";
    return 0;
}
#endif
