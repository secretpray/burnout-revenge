// Burnout Revenge. GPL-3.0-or-later (see COPYING).
#include <update/check.h>
#include <cassert>
#include <string>
int main()
{
    using namespace update;
    Fetched fetched;
    fetched.transport = Transport::Ok;
    fetched.response.status = 200;
    auto release = [](const char* version, const char* asset, bool prerelease = false, bool draft = false) {
        return std::string("{\"tag_name\":\"") + version + "\",\"html_url\":\"https://github.com/secretpray/burnout-revenge/releases/tag/" + version +
            "\",\"prerelease\":" + (prerelease ? "true" : "false") + ",\"draft\":" + (draft ? "true" : "false") +
            ",\"assets\":[{\"name\":\"" + asset + "\",\"browser_download_url\":\"https://example.invalid/ignored\"}]}";
    };
    fetched.response.body = "[" + release("v9.0.0", "Mac.dmg") + "," +
        release("v0.1.27-linux.1", "BurnoutRevenge-0.1.27-linux.1-linux-x86_64.tar.xz", true) + "," +
        release("v0.1.28-linux.1", "BurnoutRevenge-0.1.28-linux.1-linux-arm64.tar.xz", true) + "," +
        release("v0.1.29", "BurnoutRevenge-0.1.29-linux-x86_64.tar.xz", false, true) + "]";
    auto x86 = EvaluateLinux(fetched, "0.1.26", 0, false);
    assert(x86.outcome == Outcome::Available && x86.latest == "0.1.27-linux.1");
    assert(x86.url.ends_with("/v0.1.27-linux.1"));
    auto arm = EvaluateLinux(fetched, "0.1.26", 0, true);
    assert(arm.outcome == Outcome::Available && arm.latest == "0.1.28-linux.1");
    assert(EvaluateLinux(fetched, "0.1.27-linux.1", 0, false).outcome == Outcome::UpToDate);
    // The stable Mac channel retains its policy of ignoring prereleases.
    fetched.response.body = release("v0.1.27-linux.1", "BurnoutRevenge-0.1.27-linux.1-linux-x86_64.tar.xz", true);
    assert(Evaluate(fetched, "0.1.26", 0).outcome == Outcome::UpToDate);
    fetched.response.body = "[" + release("v9.0.0", "Mac.dmg") + "]";
    auto absent = EvaluateLinux(fetched, "0.1.26", 0, false);
    assert(absent.outcome == Outcome::Unavailable && absent.url == kLinuxReleasesPage);
    fetched.response.body = "[]";
    assert(EvaluateLinux(fetched, "0.1.26", 0, false).outcome == Outcome::Unavailable);
    for (const char* bad : {"{}", "[{}", "[{},]", "[]garbage", "[{\"assets\":{}}]", "[{\"assets\":[{\"name\":5}]}]"})
    {
        fetched.response.body = bad;
        assert(EvaluateLinux(fetched, "0.1.26", 0, false).outcome == Outcome::Unreadable);
    }
    fetched.response.status = 429;
    assert(EvaluateLinux(fetched, "0.1.26", 0, false).outcome == Outcome::RateLimited);
    fetched.transport = Transport::Secure;
    assert(EvaluateLinux(fetched, "0.1.26", 0, false).outcome == Outcome::Offline);
}
