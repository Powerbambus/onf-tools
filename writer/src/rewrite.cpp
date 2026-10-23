#include "rewrite.h"

#include <set>
#include "onf/read/asset_check.h"
#include <utility>

namespace onf_writer {

using namespace onf;

Result<WriteDocument, RewriteError> collect_document(const Document& document, DiagnosticsSink* sink, std::vector<std::string>& notes) {
    WriteDocument out;
    const Manifest& manifest = document.manifest();
    out.title = manifest.title;
    out.created_at = manifest.created_at;
    out.modified_at = manifest.modified_at;
    // An application that modifies a file replaces the value with its own (spec 6.1.2).
    out.generator = "onf-tools onf-writer";

    std::set<std::string> loaded;
    for (const PageEntry& entry : document.pages()) {
        auto page = document.page(entry.id, sink);
        if (!page) {
            return unexpected(RewriteError{page.error().code, "page " + entry.id + " cannot be read: " + page.error().message});
        }

        WritePage copy;
        copy.meta = page.value().meta;
        copy.ink = page.value().ink;

        for (const AssetRef& placement : copy.meta.assets) {
            const std::string name = placement.hash + "." + placement.extension;
            if (!loaded.insert(name).second) continue;

            auto content = document.asset(placement, entry.id, sink);
            if (!content) return unexpected(RewriteError{content.error().code, "asset " + name + " cannot be read: " + content.error().message});
            if (content.value().check.status != AssetStatus::Ok) {
                return unexpected(RewriteError{ErrorCode::WriteInvalidDocument,
                                               "page " + entry.id + ": asset " + name + " cannot be copied: " + content.value().check.detail});
            }
            out.assets.push_back({placement.extension, std::move(content.value().bytes)});
        }
        out.pages.push_back(std::move(copy));
    }

    auto thumbnail = document.thumbnail_bytes();
    if (!thumbnail) return unexpected(RewriteError{thumbnail.error().code, thumbnail.error().message});
    if (thumbnail.value()) {
        const AssetCheck check = check_asset_content("png", *thumbnail.value());
        if (check.status == AssetStatus::Ok && *check.width <= 256 && *check.height <= 256) {
            out.thumbnail = std::move(thumbnail).value();
        } else {
            notes.push_back("thumbnail.png is left out: " + (check.status != AssetStatus::Ok ? check.detail : std::string("it is larger than 256 pixels")));
        }
    }
    return out;
}

}  // namespace onf_writer
