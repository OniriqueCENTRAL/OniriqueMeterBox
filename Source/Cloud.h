// Onirique MeterBox — the ONIRIQUE account: logging in, and the tracks of its CLOUD
#pragma once

#include <juce_core/juce_core.h>

namespace onirique::cloud
{

using namespace juce;

/** The site: its API, and the signed addresses of the CLOUD's sounds. */
inline const char* const kSite = "https://onirique.store";

/** A track of the CLOUD, at its version in front. */
struct Track
{
    String title, project, filename, url, coverUrl;
    int seconds = 0;
};

/** Whether this computer holds a session for the account. */
bool isLoggedIn();

/** What a login says. `needsCode`: the account has two-factor authentication
    and the code was missing or wrong. */
struct LoginResult
{
    bool ok = false, needsCode = false;
    String error, name;
};

/** Logs in as the site does, and keeps the session on this computer. Blocks:
    never on the message or audio thread. */
LoginResult login (const String& email, const String& password, const String& code);

/** Ends the session, here and on the site. Blocks. */
void logout();

/** The tracks of the CLOUD, project by project, at their version in front.
    Blocks. `expired`: the session is no longer valid (it has been forgotten). */
Array<Track> tracks (String& error, bool& expired);

} // namespace onirique::cloud
