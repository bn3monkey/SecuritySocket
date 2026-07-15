// HttpRouter::registerStatic — serve a directory of static files.
//
// A concrete convenience layered on top of the router's existing get() /
// fallback() virtuals: it registers a GET catch-all (and, for SPA mode, a
// fallback) whose handler resolves each request path against a root directory
// and streams the file back. Kept out of the header because it pulls in file
// I/O and a MIME table that the public interface should not carry.
//
// Security note: the ONLY thing standing between a request and the host
// filesystem is safeRelative() below. It rejects every "..", drive letter, and
// backslash segment, so a normalized request path can never climb above the
// configured root. This is the classic static-file vulnerability; the rejection
// is deliberately strict (reject, not collapse) because a static server has no
// legitimate need to resolve parent references.

// std::fopen is portable and correct here; silence MSVC's C4996 "use fopen_s"
// nag for this translation unit only (must precede any include).
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "../../SecuritySocket.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <stdlib.h>   // _fullpath, _MAX_PATH
#else
#include <climits>    // PATH_MAX
#include <cstdlib>    // realpath
#endif

namespace Bn3Monkey
{
namespace
{
    // Extension -> Content-Type. Covers what a built frontend actually ships;
    // anything unlisted is served as opaque bytes so the browser downloads
    // rather than misinterprets it.
    const char* mimeForPath(const std::string& path)
    {
        const size_t dot   = path.find_last_of('.');
        const size_t slash = path.find_last_of("/\\");
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
            return "application/octet-stream";

        std::string ext = path.substr(dot + 1);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (ext == "html" || ext == "htm")  return "text/html; charset=utf-8";
        if (ext == "css")                   return "text/css; charset=utf-8";
        if (ext == "js"   || ext == "mjs")  return "text/javascript; charset=utf-8";
        if (ext == "json")                  return "application/json; charset=utf-8";
        if (ext == "map")                   return "application/json; charset=utf-8";
        if (ext == "svg")                   return "image/svg+xml";
        if (ext == "png")                   return "image/png";
        if (ext == "jpg"  || ext == "jpeg") return "image/jpeg";
        if (ext == "gif")                   return "image/gif";
        if (ext == "webp")                  return "image/webp";
        if (ext == "avif")                  return "image/avif";
        if (ext == "ico")                   return "image/x-icon";
        if (ext == "woff2")                 return "font/woff2";
        if (ext == "woff")                  return "font/woff";
        if (ext == "ttf")                   return "font/ttf";
        if (ext == "otf")                   return "font/otf";
        if (ext == "eot")                   return "application/vnd.ms-fontobject";
        if (ext == "txt")                   return "text/plain; charset=utf-8";
        if (ext == "xml")                   return "application/xml; charset=utf-8";
        if (ext == "pdf")                   return "application/pdf";
        if (ext == "wasm")                  return "application/wasm";
        if (ext == "mp4")                   return "video/mp4";
        if (ext == "webmanifest")           return "application/manifest+json";
        return "application/octet-stream";
    }

    // A Windows reserved device name (with or without an extension) — fopen()ing
    // "root/CON" opens the console device rather than a file and can hang. Refuse
    // these everywhere (harmless on POSIX, and keeps behaviour identical).
    bool isReservedDeviceName(const std::string& seg)
    {
        std::string base = seg.substr(0, seg.find('.'));
        for (char& c : base) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL")
            return true;
        if (base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0)
            && base[3] >= '1' && base[3] <= '9')
            return true;
        return false;
    }

    // Normalize a (percent-decoded) request path into a relative path that
    // provably cannot escape the root. Returns false — caller must refuse the
    // request — on any segment that could climb out or reach something it
    // shouldn't. This is the cheap, filesystem-free first line of defence; the
    // canonical-path check below is the second.
    bool safeRelative(const char* rel, std::string& out)
    {
        out.clear();
        if (rel == nullptr) return true;   // root request; caller substitutes index

        std::vector<std::string> parts;
        std::string seg;

        auto flush = [&]() -> bool {
            if (seg.empty() || seg == ".") { seg.clear(); return true; }
            // Reject any dot-prefixed segment. This covers ".." (traversal) and,
            // deliberately, dotfiles/dotdirs like ".env", ".git", ".htpasswd" —
            // a very common real-world leak. (A site that genuinely needs to
            // expose e.g. "/.well-known/..." registers an explicit route for it.)
            if (seg[0] == '.') return false;
            if (isReservedDeviceName(seg)) return false;
            parts.push_back(seg);
            seg.clear();
            return true;
        };

        for (const char* p = rel; *p != '\0'; ++p) {
            const char c = *p;
            if (c == '/') { if (!flush()) return false; continue; }
            if (c == '\\' || c == ':') return false;  // backslash / drive letter
            seg.push_back(c);
        }
        if (!flush()) return false;

        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) out.push_back('/');
            out += parts[i];
        }
        return true;
    }

    // Resolve a path to its real, absolute form (following symlinks on POSIX).
    // Empty string on failure — which, for a request path, means the file does
    // not exist and should 404. On Windows separators are normalized to '/' and
    // the string is lowercased so containment can be compared case-insensitively.
    std::string canonical(const std::string& path)
    {
#if defined(_WIN32)
        char buf[_MAX_PATH];
        if (_fullpath(buf, path.c_str(), _MAX_PATH) == nullptr) return std::string();
        std::string s(buf);
        for (char& c : s) {
            if (c == '\\') c = '/';
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
#else
        char buf[PATH_MAX];
        if (::realpath(path.c_str(), buf) == nullptr) return std::string();
        return std::string(buf);
#endif
    }

    // True iff canonical `full` lies inside canonical `root` (or is root itself).
    // Compared on already-canonicalized strings, so a symlink that resolves
    // outside root — an escape with no ".." anywhere in the URL — is caught here.
    bool contains(const std::string& root, const std::string& full)
    {
        if (root.empty() || full.empty() || full.size() < root.size()) return false;
        if (full.compare(0, root.size(), root) != 0) return false;
        return full.size() == root.size() || full[root.size()] == '/';
    }

    bool readFile(const std::string& path, std::vector<char>& out)
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (f == nullptr) return false;

        if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
        const long n = std::ftell(f);
        if (n < 0) { std::fclose(f); return false; }
        std::rewind(f);

        out.resize(static_cast<size_t>(n));
        const size_t got = (n > 0)
            ? std::fread(out.data(), 1, static_cast<size_t>(n), f)
            : 0;
        std::fclose(f);

        // A directory opened as a file typically yields a short/failed read; a
        // size mismatch means "not a plain readable file", so report not-found.
        return got == static_cast<size_t>(n);
    }

    // The wildcard segment name the catch-all binds the remaining path to.
    // Deliberately obscure so it cannot collide with a real :param the caller
    // might also register elsewhere.
    constexpr const char* kWildcardName = "__ss_static_path";
}

void HttpRouter::registerStatic(const char* directory,
                                bool spa_fallback,
                                const char* index_file)
{
    std::string root  = (directory && *directory) ? directory : ".";
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\'))
        root.pop_back();
    const std::string index = (index_file && *index_file) ? index_file : "index.html";

    auto serve = [root, index, spa_fallback](ClientConnection&,
                                             HttpRequest&  req,
                                             HttpResponse& res)
    {
        // Null for the "/" route and the SPA fallback (no wildcard matched);
        // the remaining path (e.g. "css/app.css") for the catch-all.
        const char* rel = req.pathParam(kWildcardName);

        std::string relative;
        std::vector<char> bytes;

        if (safeRelative(rel, relative)) {
            const std::string target = relative.empty() ? index : relative;
            const std::string full   = root + "/" + target;
            // Second line of defence: the resolved real path must still be inside
            // the resolved real root. canonical() follows symlinks, so a link
            // inside the root that points elsewhere is refused here even though
            // the request contained no "..". A missing file canonicalizes to ""
            // and simply falls through to the 404 / SPA path below.
            const std::string canon_full = canonical(full);
            if (contains(canonical(root), canon_full) && readFile(full, bytes)) {
                // bodyCopy, not body(): `bytes` is a local that dies when this
                // handler returns, and serialization runs after that.
                res.status(200)
                   .header("Content-Type", mimeForPath(full))
                   .bodyCopy(bytes.data(), bytes.size());
                return;
            }
        }

        // No such file. For a single-page app, hand back index.html so the
        // client-side router can resolve the deep link; otherwise it is a 404.
        if (spa_fallback) {
            const std::string full = root + "/" + index;
            if (readFile(full, bytes)) {
                res.status(200)
                   .header("Content-Type", "text/html; charset=utf-8")
                   .bodyCopy(bytes.data(), bytes.size());
                return;
            }
        }

        static const char kNotFound[] = "404 Not Found";
        res.status(404)
           .header("Content-Type", "text/plain; charset=utf-8")
           .body(kNotFound, sizeof(kNotFound) - 1);
    };

    // Disk I/O -> SLOW so the read runs on a worker thread, never the event loop.
    //
    // Three registrations:
    //   "/"                 — the root request; the wildcard does NOT match "/"
    //                         (the trie returns the root node directly), so the
    //                         index needs its own route.
    //   "/*<wildcard>"      — every file under the root, interior '/' included.
    //   fallback (SPA only) — trailing-slash paths and anything the catch-all
    //                         structurally missed still reach index.html.
    const std::string wildcard = std::string("/*") + kWildcardName;

    get(wildcard.c_str(), serve, RequestProcessingMode::SLOW);
    get("/",              serve, RequestProcessingMode::SLOW);
    if (spa_fallback)
        fallback(serve, RequestProcessingMode::SLOW);
}

}  // namespace Bn3Monkey
