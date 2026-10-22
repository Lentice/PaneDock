#include "managed_list/icon_source.h"
#include "unit/test_util.h"

#include <objbase.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace panedock;
namespace ml = panedock::managed_list;

namespace {
ml::PropertyValue type_text(ml::IconSource& source, const std::wstring& dir, const std::wstring& name,
                            bool directory) {
    ml::IconRequest request;
    request.key.location = request.request.location = {dir, {}, dir};
    request.key.identity = {name, false};
    request.key.directory = directory;
    request.property = "System.ItemTypeText";
    EXPECT(source.replace({request}));
    EXPECT(WaitForSingleObject(source.event(), 5000) == WAIT_OBJECT_0);
    auto result = source.take();
    EXPECT(result.has_value());
    return result ? result->property : ml::PropertyValue{};
}
std::wstring registered_type(const wchar_t* name, DWORD attributes) {
    SHFILEINFOW info{};
    SHGetFileInfoW(name, attributes, &info, sizeof(info), SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES);
    return info.szTypeName;
}
} // namespace

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto dir = std::filesystem::temp_directory_path() / L"panedock_pd_type_text";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / L"folder");
    { std::ofstream(dir / L"note.txt", std::ios::binary) << "x"; }
    auto source = ml::make_icon_source();
    EXPECT(source != nullptr);
    if (source) {
        const auto file = type_text(*source, dir.wstring(), L"note.txt", false);
        EXPECT(file.state == ml::PropertyState::value);
        EXPECT(file.text == registered_type(L"note.txt", FILE_ATTRIBUTE_NORMAL));
        EXPECT(!file.text.empty());
        const auto folder = type_text(*source, dir.wstring(), L"folder", true);
        EXPECT(folder.state == ml::PropertyState::value);
        EXPECT(folder.text == registered_type(L"folder", FILE_ATTRIBUTE_DIRECTORY));
        // The lookup is by extension only, so it never touches the file system.
        const auto missing = type_text(*source, dir.wstring(), L"not-there.txt", false);
        EXPECT(missing.state == ml::PropertyState::value);
        EXPECT(missing.text == file.text);
        // Duration only exists for audio/video: other types are ruled out by type, so
        // the (missing) file is never opened and the row shows an empty cell.
        ml::IconRequest duration;
        duration.key.location = duration.request.location = {dir.wstring(), {}, dir.wstring()};
        duration.key.identity = {L"not-there.txt", false};
        duration.property = "System.Media.Duration";
        EXPECT(source->replace({duration}));
        EXPECT(WaitForSingleObject(source->event(), 5000) == WAIT_OBJECT_0);
        const auto result = source->take();
        EXPECT(result.has_value() && result->property.state == ml::PropertyState::missing);
        source->cancel();
    }
    source.reset();
    std::filesystem::remove_all(dir);
    return test::summary("managed_list_type_text");
}
