#include "Cloud.h"

#if ! JUCE_WINDOWS
 #include <sys/stat.h>
#endif

namespace onirique::cloud
{

namespace
{
    /** Where the session is kept: beside the other ONIRIQUE data, readable by this user only. */
    File sessionFile()
    {
        return File::getSpecialLocation (File::userApplicationDataDirectory)
                   .getChildFile ("Onirique").getChildFile ("meterbox-cloud-session");
    }

    String readToken()
    {
        const auto f = sessionFile();
        return f.existsAsFile() ? f.loadFileAsString().trim() : String();
    }

    void writeToken (const String& token)
    {
        const auto f = sessionFile();
        f.getParentDirectory().createDirectory();
        f.replaceWithText (token);

       #if ! JUCE_WINDOWS
        ::chmod (f.getFullPathName().toRawUTF8(), 0600);
       #endif
    }

    String unreachable()
    {
        return String::fromUTF8 ("Impossible de joindre le site ONIRIQUE : vérifie ta connexion.");
    }

    String absolute (const String& path)
    {
        return path.startsWith ("/") ? String (kSite) + path : path;
    }
}

bool isLoggedIn()
{
    return readToken().isNotEmpty();
}

LoginResult login (const String& email, const String& password, const String& code)
{
    LoginResult result;

    DynamicObject::Ptr body (new DynamicObject());
    body->setProperty ("email", email.trim());
    body->setProperty ("password", password);

    if (code.trim().isNotEmpty())
        body->setProperty ("code", code.trim());

    int status = 0;
    auto stream = URL (String (kSite) + "/api/auth/login").withPOSTData (JSON::toString (var (body.get()), true))
                      .createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inPostData)
                                              .withExtraHeaders ("Content-Type: application/json")
                                              .withConnectionTimeoutMs (20000)
                                              .withStatusCode (&status));

    if (stream == nullptr)
    {
        result.error = unreachable();
        return result;
    }

    const auto answer = JSON::parse (stream->readEntireStreamAsString());
    const auto token = answer.getProperty ("token", var()).toString();

    if (status == 200 && token.isNotEmpty())
    {
        writeToken (token);
        result.ok = true;
        result.name = answer.getProperty ("user", var()).getProperty ("name", var()).toString();
        return result;
    }

    result.needsCode = (bool) answer.getProperty ("twoFactor", false);
    result.error = answer.getProperty ("error", var()).toString();

    if (result.error.isEmpty())
        result.error = String::fromUTF8 ("La connexion a échoué (") + String (status) + ").";

    return result;
}

void logout()
{
    const auto token = readToken();
    sessionFile().deleteFile();

    if (token.isNotEmpty())
        URL (String (kSite) + "/api/auth/logout").withPOSTData ("{}")
            .createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inPostData)
                                    .withExtraHeaders ("Content-Type: application/json\r\nAuthorization: Bearer " + token)
                                    .withConnectionTimeoutMs (8000));
}

Array<Track> tracks (String& error, bool& expired)
{
    Array<Track> result;
    expired = false;

    const auto token = readToken();

    if (token.isEmpty())
    {
        expired = true;
        return result;
    }

    int status = 0;
    auto stream = URL (String (kSite) + "/api/me/sounds")
                      .createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                                              .withExtraHeaders ("Authorization: Bearer " + token)
                                              .withConnectionTimeoutMs (20000)
                                              .withStatusCode (&status));

    if (stream == nullptr)
    {
        error = unreachable();
        return result;
    }

    const auto answer = JSON::parse (stream->readEntireStreamAsString());

    if (status == 401 || status == 403)
    {
        // The session has ended (logged out elsewhere, expired): it is forgotten here too.
        sessionFile().deleteFile();
        expired = true;
        error = String::fromUTF8 ("Ta session a expiré : reconnecte-toi.");
        return result;
    }

    if (status != 200)
    {
        error = answer.getProperty ("error", var()).toString();

        if (error.isEmpty())
            error = String::fromUTF8 ("Ton CLOUD n'a pas pu être lu.");

        return result;
    }

    // As the page of the site lists them: each track at its version in front.
    if (const auto* projects = answer.getProperty ("projects", var()).getArray())
        for (const auto& p : *projects)
        {
            const auto projectTitle = p.getProperty ("title", var()).toString();
            const auto cover = p.getProperty ("cover", var()).toString();

            if (const auto* list = p.getProperty ("tracks", var()).getArray())
                for (const auto& t : *list)
                {
                    const auto current = t.getProperty ("currentId", var());

                    if (const auto* versions = t.getProperty ("versions", var()).getArray())
                        for (const auto& v : *versions)
                        {
                            const auto url = v.getProperty ("url", var()).toString();

                            if (v.getProperty ("id", var()) != current || url.isEmpty())
                                continue;

                            Track track;
                            track.title = t.getProperty ("title", var()).toString();
                            track.project = projectTitle;
                            track.filename = v.getProperty ("filename", var()).toString();
                            track.url = absolute (url);
                            track.coverUrl = cover.isNotEmpty() ? absolute (cover) : String();
                            track.seconds = roundToInt ((double) v.getProperty ("duration", 0.0));
                            result.add (track);
                        }
                }
        }

    if (result.isEmpty())
        error = String::fromUTF8 ("Ton CLOUD ne contient pas encore de son.");

    return result;
}

} // namespace onirique::cloud
